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

#include "vma_ops.h"
#include "utils.h"

#ifdef _WIN32
#define VMA_WIN32_IMPL
#include <windows.h>

static size_t vma_page_size()
{
    static SYSTEM_INFO info = {0};
    if (!info.dwPageSize) GetSystemInfo(&info);
    return info.dwPageSize;
}

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
// For memfd()
#include <sys/syscall.h>
#include <signal.h>
#endif
#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#define MAP_VMA_ANON (MAP_PRIVATE | MAP_ANON)

#if defined(MAP_JIT) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101400
#define MAP_VMA_JIT (MAP_VMA_ANON | MAP_JIT)
#else
#define MAP_VMA_JIT MAP_VMA_ANON
#endif

static size_t vma_page_size()
{
    static size_t pagesize = 0;
    if (!pagesize) pagesize = sysconf(_SC_PAGESIZE);
    return pagesize;
}

static inline int vma_native_flags(uint32_t flags)
{
    int mmap_flags = 0;
    if (flags & VMA_EXEC)  mmap_flags |= PROT_EXEC;
    if (flags & VMA_READ)  mmap_flags |= PROT_READ;
    if (flags & VMA_WRITE) mmap_flags |= PROT_WRITE;
    return mmap_flags ? mmap_flags : PROT_NONE;
}

// TODO: CreateFileMapping
static int vma_anon_memfd()
{
    int memfd = -1;
#if defined(__NR_memfd_create)
    // If we are running on older kernel, should return -ENOSYS
    signal(SIGSYS, SIG_IGN);
    memfd = syscall(__NR_memfd_create, "vma_anon", 1);
#elif defined(__FreeBSD__)
    memfd = shm_open(SHM_ANON, O_RDWR, 0);
#elif defined(__OpenBSD__)
    char shm_temp_file[] = "/tmp/tmpXXXXXXXXXX_vma_anon";
    memfd = shm_mkstemp(shm_temp_file);
    if (shm_unlink(shm_temp_file) == -1) {
        close(memfd);
        memfd = -1;
    }
#else
    rvvm_info("No VMA memfd support for this platform");
#endif

#if !defined(ANDROID) && !defined(__ANDROID__) && !defined(__serenity__)
    if (memfd < 0) {
        rvvm_info("Falling back to VMA shmem");
        char shm_file[] = "/shm-vma-anon";
        memfd = shm_open(shm_file, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (shm_unlink(shm_file) < 0) {
            close(memfd);
            memfd = -1;
        }
    }
#endif

    if (memfd < 0) {
        rvvm_warn("Falling back to VMA file mapping");
        const char* filename = "/var/tmp/vma_anon";
        memfd = open(filename, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (memfd < 0) {
            filename = "/tmp/vma_anon";
            memfd = open(filename, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        }
        if (memfd > 0 && unlink(filename) < 0) {
            close(memfd);
            memfd = -1;
        }
    }
    return memfd;
}

#else
#include <stdlib.h>
#warning No native VMA support!

static inline size_t vma_page_size()
{
    return 1;
}

#endif

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
        if (flags & VMA_KSM) madvise(addr, size, MADV_MERGEABLE);
#endif
#if defined(__linux__) && defined(MADV_HUGEPAGE)
        if (flags & VMA_THP) madvise(addr, size, MADV_HUGEPAGE);
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
#ifdef VMA_MMAP_IMPL
    int memfd = vma_anon_memfd();
    if (memfd == -1 || ftruncate(memfd, size_to_page(size))) {
        rvvm_warn("VMA memfd creation failed");
        if (memfd != -1) close(memfd);
        return false;
    }
    *rw = mmap(NULL, size_to_page(size), PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (*rw != MAP_FAILED) {
        *exec = mmap(NULL, size_to_page(size), PROT_READ | PROT_EXEC, MAP_SHARED, memfd, 0);
        if (*exec == MAP_FAILED) {
            munmap(*rw, size_to_page(size));
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
    old_size = ptrsize_to_page(addr, old_size);
    new_size = ptrsize_to_page(addr, new_size);
    addr = ptr_to_page(addr);
#if defined(VMA_WIN32_IMPL) || defined(VMA_MMAP_IMPL)
    if (new_size < old_size) {
        vma_free(((uint8_t*)addr) + new_size, old_size - new_size);
    } else if (new_size > old_size) {
        if (!vma_alloc(((uint8_t*)addr) + new_size, new_size - old_size, flags)) {
            return NULL;
        }
    }
    return addr;
#else
    if (flags & VMA_FIXED) return NULL;
    return realloc(addr, new_size);
#endif
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
    UNUSED(flags);
    return false;
#endif
}

bool vma_clean(void* addr, size_t size, bool lazy)
{
    size = ptrsize_to_page(addr, size);
    addr = ptr_to_page(addr);
#if defined(VMA_WIN32_IMPL)
    if (VirtualAlloc(addr, size, MEM_RESET, PAGE_NOACCESS) && lazy) return true;
    VirtualUnlock(addr, size);
#elif defined(VMA_MMAP_IMPL)
#ifdef MADV_FREE
    if (lazy && madvise(addr, size, MADV_FREE) == 0) return true;
#endif
#ifdef MADV_DONTNEED
    if (madvise(addr, size, MADV_DONTNEED) == 0) return true;
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
