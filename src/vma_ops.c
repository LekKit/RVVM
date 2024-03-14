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
#include "utils.h"
#include <string.h>

#ifdef _WIN32
#define VMA_WIN32_IMPL
#include <windows.h>

static inline DWORD vma_native_flags(uint32_t flags)
{
    switch (flags & VMA_RWX) {
        case VMA_EXEC: return PAGE_EXECUTE;
        case VMA_READ: return PAGE_READONLY;
        case VMA_RDEX: return PAGE_EXECUTE_READ;
        case VMA_RDWR: return PAGE_READWRITE;
        case VMA_RWX:  return PAGE_EXECUTE_READWRITE;
    }
    return PAGE_NOACCESS;
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

static inline int vma_native_flags(uint32_t flags)
{
    int mmap_flags = 0;
    if (flags & VMA_EXEC)  mmap_flags |= PROT_EXEC;
    if (flags & VMA_READ)  mmap_flags |= PROT_READ;
    if (flags & VMA_WRITE) mmap_flags |= PROT_WRITE;
    return mmap_flags ? mmap_flags : PROT_NONE;
}

// TODO: CreateFileMapping
static int vma_anon_memfd(size_t size)
{
    int memfd = -1;
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
    return memfd;
}

#else
#include <stdlib.h>
#warning No native VMA support!

#endif

static size_t host_pagesize = 0;

size_t vma_page_size()
{
    if (!host_pagesize) {
#if defined(VMA_WIN32_IMPL)
        SYSTEM_INFO info = { .dwPageSize = 0x1000, };
        GetSystemInfo(&info);
        host_pagesize = info.dwPageSize;
#elif defined(VMA_MMAP_IMPL)
        host_pagesize = sysconf(_SC_PAGESIZE);
#else
        host_pagesize = 0x1000;
#endif
    }
    return host_pagesize;
}

static inline size_t vma_page_mask()
{
    return vma_page_size() - 1;
}

// Align VMA size/address to page boundaries
static inline size_t size_to_page(size_t size)
{
    return (size + vma_page_mask()) & (~vma_page_mask());
}

static inline void* ptr_to_page(void* ptr)
{
    return (void*)(size_t)(((size_t)ptr) & (~vma_page_mask()));
}

static inline size_t ptrsize_to_page(void* ptr, size_t size)
{
    return size_to_page(size + (((size_t)ptr) & vma_page_mask()));
}

void* vma_alloc(void* addr, size_t size, uint32_t flags)
{
    size_t ptr_diff = ((size_t)addr) & vma_page_mask();
    size = ptrsize_to_page(addr, size);
    addr = ptr_to_page(addr);
#if defined(VMA_WIN32_IMPL)
    void* ret = VirtualAlloc(addr, size, MEM_COMMIT | MEM_RESERVE, vma_native_flags(flags));
#elif defined(VMA_MMAP_IMPL)
    int mmap_flags = (flags & VMA_EXEC) ? MAP_VMA_JIT : MAP_VMA_ANON;
#ifdef MAP_FIXED
    if (flags & VMA_FIXED) mmap_flags |= MAP_FIXED;
#endif
    void* ret = mmap(addr, size, vma_native_flags(flags), mmap_flags, -1, 0);
    if (ret == MAP_FAILED) {
        ret = NULL;
    } else if ((flags & VMA_FIXED) && ret != addr) {
        vma_free(ret, size);
        ret = NULL;
    } else {
#if defined(__linux__) && defined(MADV_MERGEABLE)
        if (flags & VMA_KSM) madvise(ret, size, MADV_MERGEABLE);
#endif
#if defined(__linux__) && defined(MADV_HUGEPAGE)
        if (flags & VMA_THP) madvise(ret, size, MADV_HUGEPAGE);
#endif
    }
#else
    if (addr || (flags & (VMA_EXEC | VMA_FIXED))) return NULL;
    void* ret = calloc(size, 1);
#endif
    if (ret == NULL) return NULL;
    return ((uint8_t*)ret) + ptr_diff;
}

bool vma_multi_mmap(void** rw, void** exec, size_t size)
{
    size = size_to_page(size);
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

// Resize VMA
void* vma_remap(void* addr, size_t old_size, size_t new_size, uint32_t flags)
{
    void* ret = addr;
    old_size = ptrsize_to_page(addr, old_size);
    new_size = ptrsize_to_page(addr, new_size);
    addr = ptr_to_page(addr);
#if defined(VMA_WIN32_IMPL)
    if (flags & VMA_FIXED) {
        // TODO
        ret = NULL;
    } else if (new_size != old_size) {
        // Just copy the data into a completely new mapping
        ret = vma_alloc(NULL, new_size, flags);
        if (ret) {
            memcpy(ret, addr, old_size);
            vma_free(addr, old_size);
        }
    }
#elif defined(VMA_MMAP_IMPL)
#if defined(__linux__) && defined(MREMAP_MAYMOVE)
    ret = mremap(addr, old_size, new_size, (flags & VMA_FIXED) ? 0 : MREMAP_MAYMOVE);
    if (ret == MAP_FAILED) ret = NULL;
#else
    void* region_end = ((uint8_t*)addr) + new_size;
    if (new_size < old_size) {
        // Shrink the mapping by unmapping at the end
        if (!vma_free(region_end, old_size - new_size)) ret = NULL;
    } else if (new_size > old_size) {
        // Grow the mapping by mapping additional pages at the end
        void* tmp = vma_alloc(region_end, new_size - old_size, flags & ~VMA_FIXED);
        if (tmp != region_end) {
            ret = NULL;
            if (tmp) vma_free(tmp, new_size - old_size);
        }
    }
    if (ret == NULL && !(flags & VMA_FIXED)) {
        // Just copy the data into a completely new mapping
        ret = vma_alloc(NULL, new_size, flags);
        if (ret) {
            memcpy(ret, addr, old_size);
            vma_free(addr, old_size);
        }
    }
#endif
#else
    if (flags & VMA_FIXED) {
        ret = NULL;
    } else if (new_size != old_size) {
        ret = realloc(addr, new_size);
    }
#endif
    return ret;
}

bool vma_protect(void* addr, size_t size, uint32_t flags)
{
    size = ptrsize_to_page(addr, size);
    addr = ptr_to_page(addr);
#if defined(VMA_WIN32_IMPL)
    DWORD old;
    return VirtualProtect(addr, size, vma_native_flags(flags), &old);
#elif defined(VMA_MMAP_IMPL)
    return mprotect(addr, size, vma_native_flags(flags)) == 0;
#else
    UNUSED(addr);
    UNUSED(size);
    return flags == VMA_RDWR;
#endif
}

bool vma_clean(void* addr, size_t size, bool lazy)
{
    size = ptrsize_to_page(addr, size);
    addr = ptr_to_page(addr);
#if defined(VMA_WIN32_IMPL)
    if (lazy) {
        return VirtualAlloc(addr, size, MEM_RESET, PAGE_NOACCESS);
    } else {
        MEMORY_BASIC_INFORMATION mbi = {0};
        if (!VirtualQuery(addr, &mbi, sizeof(mbi))) return false;
        if (!VirtualFree(addr, size, MEM_DECOMMIT)) return false;
        if (!VirtualAlloc(addr, size, MEM_COMMIT, mbi.Protect)) {
            rvvm_fatal("VirtualAlloc() failed on decommited segment");
        }
        return true;
    }
#elif defined(VMA_MMAP_IMPL)
#ifdef MADV_FREE
    if (lazy) return madvise(addr, size, MADV_FREE) == 0;
#endif
#if defined(__linux__) && defined(MADV_DONTNEED)
    return madvise(addr, size, MADV_DONTNEED) == 0;
#endif
#endif
    return addr && size && lazy;
}

bool vma_free(void* addr, size_t size)
{
    size = ptrsize_to_page(addr, size);
    addr = ptr_to_page(addr);
#if defined(VMA_WIN32_IMPL)
    //VirtualFree(addr, size, MEM_DECOMMIT);
    UNUSED(size);
    return VirtualFree(addr, 0, MEM_RELEASE);
#elif defined(VMA_MMAP_IMPL)
    return munmap(addr, size) == 0;
#else
    if (size) free(addr);
    return addr && size;
#endif
}
