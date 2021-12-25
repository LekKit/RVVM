/*
rvjit.c - Retargetable Versatile JIT Compiler
Copyright (C) 2021  LekKit <github.com/LekKit>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "rvjit.h"
#include "rvjit_emit.h"
#include "utils.h"

#define RVJIT_MEM_EXEC 0x1
#define RVJIT_MEM_RDWR 0x2
#define RVJIT_MEM_RWX  0x3

// Align block size/address to page boundaries for mmap/mprotect
static inline size_t size_to_page(size_t size)
{
    return (size + 0xFFFULL) & (~0xFFFULL);
}

static inline void* ptr_to_page(void* ptr)
{
    return (void*)(size_t)(((size_t)ptr) & (~0xFFFULL));
}

static inline size_t ptrsize_to_page(void* ptr, size_t size)
{
    return (size + (((size_t)ptr) & 0xFFF) + 0xFFF) & (~0xFFFULL);
}

#ifdef _WIN32
#include <windows.h>
#include <memoryapi.h>

static inline DWORD rvjit_virt_flags(uint8_t flags)
{
    switch (flags) {
        case RVJIT_MEM_EXEC: return PAGE_EXECUTE_READ;
        case RVJIT_MEM_RDWR: return PAGE_READWRITE;
        case RVJIT_MEM_RWX:  return PAGE_EXECUTE_READWRITE;
    }
    return PAGE_NOACCESS;
}

void* rvjit_mmap(size_t size, uint8_t flags)
{
    return VirtualAlloc(NULL, size_to_page(size), MEM_COMMIT, rvjit_virt_flags(flags));
}

bool rvjit_multi_mmap(void** rw, void** ex, size_t size)
{
    // No multi-mmap support for Win32
    UNUSED(rw);
    UNUSED(ex);
    UNUSED(size);
    return false;
}

void rvjit_munmap(void* addr, size_t size)
{
    VirtualFree(addr, size_to_page(size), MEM_DECOMMIT);
}

void rvjit_memprotect(void* addr, size_t size, uint8_t flags)
{
    DWORD old;
    VirtualProtect(ptr_to_page(addr), ptrsize_to_page(addr, size), rvjit_virt_flags(flags), &old);
}

#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __linux__
#include <sys/syscall.h>
#include <linux/memfd.h>
#endif

static inline int rvjit_virt_flags(uint8_t flags)
{
    switch (flags) {
        case RVJIT_MEM_EXEC: return PROT_READ | PROT_EXEC;
        case RVJIT_MEM_RDWR: return PROT_READ | PROT_WRITE;
        case RVJIT_MEM_RWX:  return PROT_READ | PROT_WRITE | PROT_EXEC;
    }
    return PROT_NONE;
}

void* rvjit_mmap(size_t size, uint8_t flags)
{
    return mmap(NULL, size_to_page(size), rvjit_virt_flags(flags), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

static int rvjit_anon_memfd()
{
#if defined(__NR_memfd_create)
    // If we are running on older kernel, should return -ENOSYS
    int memfd = syscall(__NR_memfd_create, "rvjit_heap", MFD_CLOEXEC);
#elif defined(__FreeBSD__)
    int memfd = shm_open(SHM_ANON, O_RDWR, 0);
#elif defined(__OpenBSD__)
    char shm_temp_file[] = "/tmp/tmpXXXXXXXXXX_rvjit";
    int memfd = shm_mkstemp(shm_temp_file);
    if (shm_unlink(shm_temp_file) == -1) {
        close(memfd);
        memfd = -1;
    }
#else
    int memfd = -1;
    rvvm_info("No RVJIT anon memfd support for this platform");
#endif
    if (memfd == -1) {
        rvvm_info("Falling back to RVJIT shmem");
        char shm_file[] = "/shm-rvjit";
        memfd = shm_open(shm_file, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (shm_unlink(shm_file) == -1) {
            close(memfd);
            memfd = -1;
        }
    }
    return memfd;
}

bool rvjit_multi_mmap(void** rw, void** ex, size_t size)
{
    // Try creating anonymous memfd and mapping onto different virtual mappings
    int memfd = rvjit_anon_memfd();
    if (memfd == -1) return false;
    if (ftruncate(memfd, size_to_page(size))) {
        close(memfd);
        return false;
    }
    *rw = mmap(NULL, size_to_page(size), PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    *ex = *rw != NULL ? mmap(NULL, size_to_page(size), PROT_READ | PROT_EXEC, MAP_SHARED, memfd, 0) : NULL;
    close(memfd);
    return *ex != NULL;
}

void rvjit_munmap(void* addr, size_t size)
{
    munmap(addr, size_to_page(size));
}

void rvjit_memprotect(void* addr, size_t size, uint8_t flags)
{
    mprotect(ptr_to_page(addr), ptrsize_to_page(addr, size), rvjit_virt_flags(flags));
}

#endif

// Instruction cache is usually non-coherent on non-x86 hosts
#if defined(GNU_EXTS) && !defined(RVJIT_X86)
void flush_icache(void* addr, size_t size)
{
    __builtin___clear_cache(addr, ((char*)addr) + size);
}
#else
void flush_icache(void* addr, size_t size)
{
    UNUSED(addr);
    UNUSED(size);
}
#endif

void rvjit_ctx_init(rvjit_block_t* block, size_t size)
{
    block->space = 1024;
    block->code = safe_malloc(block->space);

    block->heap.data = rvjit_mmap(size, RVJIT_MEM_RWX);
    block->heap.code = NULL;
    if (block->heap.data == NULL) {
        // Possible on Linux PaX (hardened) or OpenBSD
        rvvm_info("Failed to allocate RWX mapping, falling back to multi-mmap");
        if (!rvjit_multi_mmap((void**)&block->heap.data, (void**)&block->heap.code, size)) {
            rvvm_fatal("RVJIT heap allocation failure!");
        }
    }

    block->heap.size = size_to_page(size);
    block->heap.curr = 0;

    block->rv64 = false;

    hashmap_init(&block->heap.blocks, 64);
}

void rvjit_ctx_free(rvjit_block_t* block)
{
    rvjit_munmap(block->heap.data, block->heap.size);
    hashmap_destroy(&block->heap.blocks);
    free(block->code);
}

void rvjit_block_init(rvjit_block_t* block)
{
    block->size = 0;
    rvjit_emit_init(block);
}

rvjit_func_t rvjit_block_finalize(rvjit_block_t* block)
{
    uint8_t* dest = block->heap.data + block->heap.curr;
    uint8_t* code;
    if (block->heap.code == NULL) {
        code = dest;
    } else {
        code = block->heap.code + block->heap.curr;
    }

    rvjit_emit_end(block);

    if (block->heap.curr + block->size > block->heap.size) {
        // The cache is full
        return NULL;
    }

    memcpy(dest, block->code, block->size);
    block->heap.curr += block->size;
    hashmap_put(&block->heap.blocks, block->phys_pc, (size_t)code);

    return (rvjit_func_t)code;
}

rvjit_func_t rvjit_block_lookup(rvjit_block_t* block, paddr_t phys_pc)
{
    return (rvjit_func_t)hashmap_get(&block->heap.blocks, phys_pc);
}

void rvjit_flush_cache(rvjit_block_t* block)
{
    if (block->heap.code) {
        flush_icache(block->heap.code, block->heap.curr);
    } else {
        flush_icache(block->heap.data, block->heap.curr);
    }

    hashmap_clear(&block->heap.blocks);
    block->heap.curr = 0;
}
