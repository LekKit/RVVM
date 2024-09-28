/*
vma_ops.c - Virtual memory area operations
Copyright (C) 2023  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.

Alternatively, the contents of this file may be used under the terms
of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or any later version.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// Needed for syscall() when not passing -std=gnu..
#define _GNU_SOURCE
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include "vma_ops.h"

#ifdef _WIN32
#define VMA_WIN32_IMPL
#include <windows.h>

#ifndef FILE_MAP_EXECUTE
#define FILE_MAP_EXECUTE 0x20
#endif

static inline DWORD vma_native_prot(uint32_t flags)
{
    switch (flags & VMA_RWX) {
        case VMA_EXEC: return PAGE_EXECUTE;
        case VMA_READ: return PAGE_READONLY;
        case VMA_RDEX: return PAGE_EXECUTE_READ;
        case VMA_WRITE:
        case VMA_RDWR: return PAGE_READWRITE;
        case VMA_EXEC | VMA_WRITE:
        case VMA_RWX:  return PAGE_EXECUTE_READWRITE;
    }
    return PAGE_NOACCESS;
}

static inline DWORD vma_native_view_prot(uint32_t flags)
{
    DWORD ret = 0;
    if (!(flags & VMA_SHARED) && (flags & VMA_WRITE)) {
        // Set FILE_MAP_COPY on private writable mappings
        return FILE_MAP_COPY;
    }
    if (flags & VMA_READ)      ret |= FILE_MAP_READ;
    if (flags & VMA_WRITE)     ret |= FILE_MAP_WRITE;
    if (flags & VMA_EXEC)      ret |= FILE_MAP_EXECUTE;
    return ret;
}

#elif defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)
#define VMA_MMAP_IMPL
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef __linux__
// For memfd_create()
#include <sys/syscall.h>
#include <signal.h>
#endif
#ifdef __serenity__
// For anon_create()
#include <serenity.h>
#endif
#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#define MAP_VMA_ANON (MAP_PRIVATE | MAP_ANON)

#if defined(MAP_JIT) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101400
#define MAP_VMA_JIT (MAP_VMA_ANON | MAP_JIT)
#else
#define MAP_VMA_JIT MAP_VMA_ANON
#endif

static inline int vma_native_prot(uint32_t flags)
{
    int mmap_flags = 0;
    if (flags & VMA_EXEC)  mmap_flags |= PROT_EXEC;
    if (flags & VMA_READ)  mmap_flags |= PROT_READ;
    if (flags & VMA_WRITE) mmap_flags |= PROT_WRITE;
    return mmap_flags ? mmap_flags : PROT_NONE;
}

#else
#include <stdlib.h>
#warning No native VMA support!

#endif

// RVVM internal headers come after system headers because of safe_free()
#include "mem_ops.h"
#include "utils.h"
#include "blk_io.h"

static size_t host_pagesize = 0;
static size_t host_granularity = 0; // Allocation granularity, may be > pagesize

static void vma_page_size_init_once(void)
{
#if defined(VMA_WIN32_IMPL)
    SYSTEM_INFO info = { .dwPageSize = 0x1000, .dwAllocationGranularity = 0x10000, };
    GetSystemInfo(&info);
    host_pagesize = info.dwPageSize;
    host_granularity = info.dwAllocationGranularity;
#elif defined(VMA_MMAP_IMPL)
    host_pagesize = sysconf(_SC_PAGESIZE);
    host_granularity = host_pagesize;
#else
    // Non-paging fallback via malloc/free, disable alignment
    host_pagesize = 1;
    host_granularity = 1;
#endif
}

static void vma_page_size_init(void)
{
    DO_ONCE(vma_page_size_init_once());
}

size_t vma_page_size(void)
{
    vma_page_size_init();
    return host_pagesize;
}

static size_t vma_granularity(void)
{
    vma_page_size_init();
    return host_granularity;
}

static inline void* align_ptr_down(void* ptr, size_t align)
{
    return (void*)align_size_down((size_t)ptr, align);
}

int vma_anon_memfd(size_t size)
{
    int memfd = -1;
    size = align_size_up(size, vma_granularity());
#if defined(VMA_MMAP_IMPL)
#if defined(__NR_memfd_create)
    // If we are running on older kernel, should return -ENOSYS
    signal(SIGSYS, SIG_IGN);
    memfd = syscall(__NR_memfd_create, "vma_anon", 1);
#elif defined(__FreeBSD__)
    memfd = shm_open(SHM_ANON, O_RDWR | O_CLOEXEC, 0);
#elif defined(__OpenBSD__)
    char shm_temp_file[] = "/tmp/tmpXXXXXXXXXX_vma_anon";
    memfd = shm_mkstemp(shm_temp_file);
    if (shm_unlink(shm_temp_file) < 0) {
        close(memfd);
        memfd = -1;
    }
#elif defined(__serenity__)
    memfd = anon_create(size, O_CLOEXEC);
    if (memfd >= 0) return memfd;
#else
    rvvm_info("No VMA memfd support for this platform");
#endif

#if !defined(ANDROID) && !defined(__ANDROID__) && !defined(__serenity__)
    if (memfd < 0) {
        char shm_file[] = "/shm-vma-anon-XXXXXXXX";
        rvvm_randomserial(shm_file + 14, 8);
        rvvm_info("Falling back to VMA shmem");
        memfd = shm_open(shm_file, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (memfd >= 0 && shm_unlink(shm_file) < 0) {
            close(memfd);
            memfd = -1;
        }
    }
#endif

    if (memfd < 0) {
        char path[256] = {0};
        const char* xdg = getenv("XDG_RUNTIME_DIR");
        rvvm_info("Falling back to VMA file mapping, may lower perf");
        if (xdg) {
            size_t off = rvvm_strlcpy(path, xdg, sizeof(path));
            off += rvvm_strlcpy(path + off, "/vma-anon-XXXXXXXX", sizeof(path) - off);
            rvvm_randomserial(path + off - 8, 8);
            if (off < 250) {
                memfd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            } else rvvm_warn("XDG_RUNTIME_DIR path too long!");
        }
        if (memfd < 0) {
            size_t off = rvvm_strlcpy(path, "/var/tmp/vma-anon-XXXXXXXX", sizeof(path));
            rvvm_randomserial(path + off - 8, 8);
            memfd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        }
        if (memfd < 0) {
            size_t off = rvvm_strlcpy(path, "/tmp/vma-anon-XXXXXXXX", sizeof(path));
            rvvm_randomserial(path + off - 8, 8);
            memfd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        }
        if (memfd >= 0 && unlink(path) < 0) {
            close(memfd);
            memfd = -1;
        }
    }
    // Resize anon FD
    if (memfd >= 0 && ftruncate(memfd, size) < 0) {
        close(memfd);
        memfd = -1;
    }
#else
    UNUSED(size);
    rvvm_warn("Anonymous memfd is not supported!");
#endif
    return memfd;
}

/*
 * TODO: Better mmap() emulation on Win32?
 * - Proper vma_remap()
 * - Partial unmapping
 * - Handle non-granular fixed-address file mappings
 */

static void* vma_mmap_aligned_internal(void* addr, size_t size, uint32_t flags, rvfile_t* file, uint64_t offset)
{
    void* ret = NULL;
#if defined(VMA_WIN32_IMPL)
    // Win32 implementation
    if (file) {
        HANDLE file_handle = rvfile_get_win32_handle(file);
        if ((flags & VMA_WRITE) && (flags & VMA_EXEC)) {
            // WX protection is prohibited
            return NULL;
        }
        if (file_handle) {
            HANDLE file_map = CreateFileMappingW(file_handle, NULL, vma_native_prot(flags), 0, 0, NULL);
            if (!file_map) {
                return NULL;
            }
            if (flags & VMA_FIXED) {
#ifdef UNDER_CE
                // No MapViewOfFileEx() on Windows CE
                ret = NULL;
#else
                ret = MapViewOfFileEx(file_map, vma_native_view_prot(flags), offset >> 32, (uint32_t)offset, size, addr);
#endif
            } else {
                ret = MapViewOfFile(file_map, vma_native_view_prot(flags), offset >> 32, (uint32_t)offset, size);
            }
            CloseHandle(file_map);
        } else {
            // File doesn't have a native Win32 HANDLE
            return NULL;
        }
    } else {
        ret = VirtualAlloc(addr, size, MEM_COMMIT | MEM_RESERVE, vma_native_prot(flags));
    }

#elif defined(VMA_MMAP_IMPL)
    // POSIX mmap() implementation
    int mmap_flags = (flags & VMA_EXEC) ? MAP_VMA_JIT : MAP_VMA_ANON;
    int mmap_fd = file ? rvfile_get_posix_fd(file) : -1;
    if (file) {
        mmap_flags = (flags & VMA_SHARED) ? MAP_SHARED : MAP_PRIVATE;
        if (mmap_fd == -1) {
            // File doesn't have a native POSIX fd
            return NULL;
        }
    }
    // Use MAP_FIXED_NOREPLACE on Linux for non-destructive behavior
#if defined(MAP_FIXED_NOREPLACE)
    if (flags & VMA_FIXED) mmap_flags |= MAP_FIXED_NOREPLACE;
#elif defined(MAP_FIXED) && !defined(__linux__)
    if (flags & VMA_FIXED) mmap_flags |= MAP_FIXED;
#endif
    ret = mmap(addr, size, vma_native_prot(flags), mmap_flags, mmap_fd, offset);
    if (ret == MAP_FAILED) ret = NULL;
    // Apply madvise() flags
#if defined(__linux__) && defined(MADV_MERGEABLE)
    if (ret && (flags & VMA_KSM)) madvise(ret, size, MADV_MERGEABLE);
#endif
#if defined(__linux__) && defined(MADV_HUGEPAGE)
    if (ret && (flags & VMA_THP)) madvise(ret, size, MADV_HUGEPAGE);
#endif

#else
    // Generic libc implementationn
    UNUSED(addr);
    if (flags & (VMA_SHARED | VMA_EXEC | VMA_FIXED)) {
        // No support for VMA_SHARED, VMA_EXEC, VMA_FIXED
        DO_ONCE(rvvm_warn("Unsupported VMA flags %x on fallback implementation", flags));
        return NULL;
    }
    ret = calloc(size, 1);
    if (ret && file) {
        // Emulate private file mappings by reading the file into memory
        rvread(file, ret, size, offset);
    }
#endif
    return ret;
}

void* vma_alloc(void* addr, size_t size, uint32_t flags)
{
    return vma_mmap(addr, size, flags, NULL, 0);
}

void* vma_mmap(void* addr, size_t size, uint32_t flags, rvfile_t* file, uint64_t offset)
{
    size_t ptr_diff = ((size_t)addr) & (vma_granularity() - 1);
    if (file) {
        // File VMA mapping
        size_t off_diff = offset & (vma_granularity() - 1);
        offset -= off_diff;
        if (flags & VMA_FIXED) {
            if (ptr_diff != off_diff) {
                // Misaligned address / offset
                return NULL;
            }
        } else {
            // Fixup file offset misalign
            ptr_diff = off_diff;
        }
    } else {
        // Anonymous VMA allocation
        offset = 0;
        if (flags & VMA_SHARED) {
            // Mapping shared anonymous memory doesn't make a lot of sense
            return NULL;
        }
    }

    addr = align_ptr_down(addr, vma_granularity());
    size = align_size_up(size + ptr_diff, vma_page_size());

    if (file && offset + size > rvfilesize(file)) {
        // Grow the file to fit the mapping for same semantics across systems
        if (!rvfallocate(file, offset + size)) return NULL;
    }

    uint8_t* ret = vma_mmap_aligned_internal(addr, size, flags, file, offset);

    if ((flags & VMA_FIXED) && ret && ret != addr) {
        vma_free(ret, size);
        ret = NULL;
    }

    return ret ? (ret + ptr_diff) : NULL;
}

bool vma_multi_mmap(void** rw, void** exec, size_t size)
{
    size = align_size_up(size, vma_granularity());
#ifdef VMA_MMAP_IMPL
    int memfd = vma_anon_memfd(size);
    if (memfd < 0) {
        rvvm_warn("VMA memfd creation failed");
        return false;
    }
    *rw = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (*rw != MAP_FAILED) {
        *exec = mmap(NULL, size, PROT_READ | PROT_EXEC, MAP_SHARED, memfd, 0);
        if (*exec == MAP_FAILED) {
            munmap(*rw, size);
            *exec = NULL;
        }
    } else {
        *rw = NULL;
        *exec = NULL;
    }
    close(memfd);

    return *exec != NULL;
#else
    UNUSED(rw);
    UNUSED(exec);
    UNUSED(size);
    return false;
#endif
}

void* vma_remap(void* addr, size_t old_size, size_t new_size, uint32_t flags)
{
    size_t ptr_diff = ((size_t)addr) & (vma_granularity() - 1);
    uint8_t* ret = NULL;
    addr = align_ptr_down(addr, vma_granularity());
    old_size = align_size_up(old_size + ptr_diff, vma_page_size());
    new_size = align_size_up(new_size + ptr_diff, vma_page_size());

    if (new_size == old_size) return addr;

#if defined(VMA_MMAP_IMPL) && defined(__linux__) && defined(MREMAP_MAYMOVE)
    ret = mremap(addr, old_size, new_size, (flags & VMA_FIXED) ? 0 : MREMAP_MAYMOVE);
    if (ret == MAP_FAILED) ret = NULL;
#elif defined(VMA_MMAP_IMPL) || defined(VMA_WIN32_IMPL)
#ifdef VMA_MMAP_IMPL
    if (new_size < old_size) {
        // Shrink the mapping by unmapping at the end
        if (!vma_free(((uint8_t*)addr) + new_size, old_size - new_size)) {
            ret = NULL;
        }
    } else if (new_size > old_size) {
        // Grow the mapping by mapping additional pages at the end
        if (!vma_alloc(((uint8_t*)addr) + old_size, new_size - old_size, flags | VMA_FIXED)) {
            ret = NULL;
        }
    }
#endif
    if (ret == NULL && !(flags & VMA_FIXED)) {
        // Just copy the data into a completely new mapping
        ret = vma_alloc(NULL, new_size, flags);
        if (ret) {
            memcpy(ret, addr, EVAL_MIN(old_size, new_size));
            vma_free(addr, old_size);
        }
    }
#else
    if (flags & VMA_FIXED) {
        if (new_size > old_size) ret = NULL;
    } else {
        ret = realloc(addr, new_size);
    }
#endif

    return ret ? (ret + ptr_diff) : NULL;
}

bool vma_protect(void* addr, size_t size, uint32_t flags)
{
    size_t ptr_diff = ((size_t)addr) & (vma_page_size() - 1);
    addr = align_ptr_down(addr, vma_page_size());
    size = align_size_up(size + ptr_diff, vma_page_size());

#if defined(VMA_WIN32_IMPL)
    DWORD old;
    return VirtualProtect(addr, size, vma_native_prot(flags), &old);
#elif defined(VMA_MMAP_IMPL)
    return mprotect(addr, size, vma_native_prot(flags)) == 0;
#else
    UNUSED(addr);
    UNUSED(size);
    return flags == VMA_RDWR;
#endif
}

bool vma_sync(void* addr, size_t size, bool lazy)
{
    size_t ptr_diff = ((size_t)addr) & (vma_page_size() - 1);
    addr = align_ptr_down(addr, vma_page_size());
    size = align_size_up(size + ptr_diff, vma_page_size());
    UNUSED(lazy);

#if defined(VMA_WIN32_IMPL)
    return FlushViewOfFile(addr, size);
#elif defined(VMA_MMAP_IMPL) && defined(MS_ASYNC) && defined(MS_SYNC)
    return msync(addr, size, lazy ? MS_ASYNC : MS_SYNC) == 0;
#else
    return false;
#endif
}

bool vma_clean(void* addr, size_t size, bool lazy)
{
    size_t ptr_diff = ((size_t)addr) & (vma_page_size() - 1);
    addr = align_ptr_down(addr, vma_page_size());
    size = align_size_up(size + ptr_diff, vma_page_size());

#if defined(VMA_WIN32_IMPL)
    if (lazy) {
        return VirtualAlloc(addr, size, MEM_RESET, PAGE_NOACCESS);
    } else {
        // NOTE: There is a tiny timeslice when other thread may SEGV
        // upon trying to access the cleaned region, consider such
        // behavior as a race condition anyways and use a lock
        MEMORY_BASIC_INFORMATION mbi = {0};
        if (!VirtualQuery(addr, &mbi, sizeof(mbi))) return false;
        if (!VirtualFree(addr, size, MEM_DECOMMIT)) return false;
        if (!VirtualAlloc(addr, size, MEM_COMMIT, mbi.Protect)) {
            rvvm_fatal("VirtualAlloc() failed on decommited segment");
        }
        return true;
    }
#elif defined(VMA_MMAP_IMPL)
#if defined(__linux__) && defined(MADV_DONTNEED)
    return madvise(addr, size, MADV_DONTNEED) == 0;
#endif
#ifdef MADV_FREE
    return madvise(addr, size, MADV_FREE) == 0 && lazy;
#endif
#endif

    return addr && size && lazy;
}

bool vma_pageout(void* addr, size_t size, bool lazy)
{
    size_t ptr_diff = ((size_t)addr) & (vma_page_size() - 1);
    addr = align_ptr_down(addr, vma_page_size());
    size = align_size_up(size + ptr_diff, vma_page_size());

    if (!lazy) {
#if defined(VMA_WIN32_IMPL) && !defined(UNDER_CE)
        return VirtualUnlock(addr, size) || GetLastError() == ERROR_NOT_LOCKED;
#elif defined(VMA_MMAP_IMPL) && defined(__linux__) && defined(MADV_PAGEOUT)
        return madvise(addr, size, MADV_PAGEOUT) == 0;
#endif
    }

#if defined(VMA_MMAP_IMPL) && defined(__linux__) && defined(MADV_COLD)
    madvise(addr, size, MADV_COLD);
#elif defined(VMA_MMAP_IMPL) && defined(__FreeBSD__) && defined(MADV_DONTNEED)
    madvise(addr, size, MADV_DONTNEED);
#endif

    return lazy;
}

bool vma_free(void* addr, size_t size)
{
    size_t ptr_diff = ((size_t)addr) & (vma_granularity() - 1);
    addr = align_ptr_down(addr, vma_granularity());
    size = align_size_up(size + ptr_diff, vma_page_size());
    if (!addr || !size) return false;

#if defined(VMA_WIN32_IMPL)
    MEMORY_BASIC_INFORMATION mbi = {0};
    if (!VirtualQuery(addr, &mbi, sizeof(mbi))) {
        rvvm_warn("vma_free(): VirtualQuery() failed!");
        return false;
    }
    if (mbi.RegionSize != size) {
        rvvm_warn("vma_free(): Invalid VMA size!");
    }
    if (mbi.AllocationBase != addr) {
        rvvm_warn("vma_free(): Invalid VMA address!");
    }
    if (mbi.Type == MEM_MAPPED) {
        return UnmapViewOfFile(addr);
    } else if (mbi.Type == MEM_PRIVATE) {
        return VirtualFree(addr, 0, MEM_RELEASE);
    } else {
        rvvm_fatal("vma_free(): Invalid win32 page type %x!", (uint32_t)mbi.Type);
        return false;
    }
#elif defined(VMA_MMAP_IMPL)
    return munmap(addr, size) == 0;
#else
    free(addr);
    return true;
#endif
}
