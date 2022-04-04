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
#include "vector.h"

#define RVJIT_MEM_EXEC 0x1
#define RVJIT_MEM_RDWR 0x2
#define RVJIT_MEM_RWX  0x3

static size_t page_mask();

// Align block size/address to page boundaries for mmap/mprotect
static inline size_t size_to_page(size_t size)
{
    return (size + page_mask()) & (~page_mask());
}

static inline void* ptr_to_page(void* ptr)
{
    return (void*)(size_t)(((size_t)ptr) & (~page_mask()));
}

static inline size_t ptrsize_to_page(void* ptr, size_t size)
{
    return (size + (((size_t)ptr) & page_mask()) + page_mask()) & (~page_mask());
}

#ifdef _WIN32
#include <windows.h>
#include <memoryapi.h>

static size_t page_mask()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize - 1;
}

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
#endif

#ifndef MAP_JIT
#define MAP_JIT 0
#elif defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#define RVJIT_APPLE
#endif

static size_t page_mask()
{
    return sysconf(_SC_PAGESIZE) - 1;
}

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
    void* tmp = mmap(NULL, size_to_page(size), rvjit_virt_flags(flags), MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    if (tmp == MAP_FAILED) tmp = NULL;
    return tmp;
}

static int rvjit_anon_memfd()
{
#if defined(__NR_memfd_create)
    // If we are running on older kernel, should return -ENOSYS
    int memfd = syscall(__NR_memfd_create, "rvjit_heap", 1);
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
#if defined(ANDROID) || defined(__ANDROID__)
    rvvm_warn("No RVJIT shmem support on Android");
#else
    if (memfd < 0) {
        rvvm_info("Falling back to RVJIT shmem");
        char shm_file[] = "/shm-rvjit";
        memfd = shm_open(shm_file, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (shm_unlink(shm_file) == -1) {
            close(memfd);
            memfd = -1;
        }
    }
#endif
    return memfd;
}

bool rvjit_multi_mmap(void** rw, void** exec, size_t size)
{
    // Try creating anonymous memfd and mapping onto different virtual mappings
    int memfd = rvjit_anon_memfd();
    if (memfd == -1 || ftruncate(memfd, size_to_page(size))) {
        rvvm_warn("RVJIT memfd creation failed");
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

#if defined(RVJIT_RISCV) && defined(__linux__)
/*
 * Clang doesn't seem to implement __builtin___clear_cache properly
 * on RISC-V, wreaking havok on hosts with non-coherent icache
 * (RVVM is also affected, heh), hence we make a direct syscall
 */
#include <sys/syscall.h>
#ifndef __NR_riscv_flush_icache
#define __NR_riscv_flush_icache 259
#endif
static void flush_icache(const void* addr, size_t size)
{
    syscall(__NR_riscv_flush_icache, addr, ((char*)addr) + size, 0);
}
#elif defined(RVJIT_APPLE)
static void flush_icache(const void* addr, size_t size)
{
    sys_icache_invalidate((void*)addr, size);
}
#elif defined(GNU_EXTS)
static void flush_icache(const void* addr, size_t size)
{
    __builtin___clear_cache((char*)addr, ((char*)addr) + size);
}
#elif defined(_WIN32)
static void flush_icache(const void* addr, size_t size)
{
    FlushInstructionCache(GetCurrentProcess(), addr, size);
}
#else
#ifndef RVJIT_X86
#error No flush_icache support!
#endif
static void flush_icache(const void* addr, size_t size)
{
    UNUSED(addr);
    UNUSED(size);
}
#endif

bool rvjit_ctx_init(rvjit_block_t* block, size_t size)
{
    block->heap.data = NULL;
    block->heap.code = NULL;

    if (rvvm_has_arg("rvjit_disable_rwx")) {
        rvvm_info("RWX disabled, allocating W^X multi-mmap RVJIT heap");
    } else {
        block->heap.data = rvjit_mmap(size, RVJIT_MEM_RWX);

        // Possible on Linux PaX (hardened) or OpenBSD
        if (block->heap.data == NULL) rvvm_info("Failed to allocate RWX RVJIT heap, falling back to W^X multi-mmap");
    }

    if (block->heap.data == NULL) {
        if (!rvjit_multi_mmap((void**)&block->heap.data, (void**)&block->heap.code, size)) {
            rvvm_warn("RVJIT heap allocation failure!");
            return false;
        }
        flush_icache(block->heap.code, block->heap.size);
    }

    flush_icache(block->heap.data, block->heap.size);

    block->space = 1024;
    block->code = safe_malloc(block->space);

    block->heap.size = size_to_page(size);
    block->heap.curr = 0;

    block->rv64 = false;

    hashmap_init(&block->heap.blocks, 64);
    hashmap_init(&block->heap.block_links, 64);
    vector_init(block->links);
    return true;
}

static void rvjit_linker_cleanup(rvjit_block_t* block)
{
    vector_t(void*)* linked_blocks;
    hashmap_foreach(&block->heap.block_links, k, v) {
        UNUSED(k);
        linked_blocks = (void*)v;
        vector_free(*linked_blocks);
        free(linked_blocks);
    }
    hashmap_clear(&block->heap.block_links);
}

void rvjit_ctx_free(rvjit_block_t* block)
{
    rvjit_munmap(block->heap.data, block->heap.size);
    rvjit_linker_cleanup(block);
    hashmap_destroy(&block->heap.blocks);
    hashmap_destroy(&block->heap.block_links);
    vector_free(block->links);
    free(block->code);
}

void rvjit_block_init(rvjit_block_t* block)
{
    block->size = 0;
    block->linkage = LINKAGE_JMP;
    vector_clear(block->links);
    rvjit_emit_init(block);
}

rvjit_func_t rvjit_block_finalize(rvjit_block_t* block)
{
    uint8_t* dest = block->heap.data + block->heap.curr;
    const uint8_t* code;
    if (block->heap.code == NULL) {
        code = dest;
    } else {
        code = block->heap.code + block->heap.curr;
    }

    rvjit_emit_end(block, block->linkage);

    if (block->heap.curr + block->size > block->heap.size) {
        // The cache is full
        return NULL;
    }

#ifdef RVJIT_APPLE
    pthread_jit_write_protect_np(false);
#endif

    memcpy(dest, block->code, block->size);
    flush_icache(code, block->size);
    //block->heap.curr = (block->heap.curr + block->size + 31) & ~31ULL;
    block->heap.curr += block->size;

    hashmap_put(&block->heap.blocks, block->phys_pc, (size_t)code);

#ifdef RVJIT_NATIVE_LINKER
    vector_t(uint8_t*)* linked_blocks;
    paddr_t k;
    size_t v;
    vector_foreach(block->links, i) {
        k = vector_at(block->links, i).dest;
        v = vector_at(block->links, i).ptr;
        linked_blocks = (void*)hashmap_get(&block->heap.block_links, k);
        if (!linked_blocks) {
            linked_blocks = calloc(sizeof(vector_t(uint8_t*)), 1);
            vector_init(*linked_blocks);
            hashmap_put(&block->heap.block_links, k, (size_t)linked_blocks);
        }
        vector_push_back(*linked_blocks, (uint8_t*)v);
    }

    linked_blocks = (void*)hashmap_get(&block->heap.block_links, block->phys_pc);
    if (linked_blocks) {
        vector_foreach(*linked_blocks, i) {
            uint8_t* jptr = vector_at(*linked_blocks, i);
            rvjit_linker_patch_jmp(jptr, ((size_t)dest) - ((size_t)jptr));
        }
        vector_free(*linked_blocks);
        free(linked_blocks);
        hashmap_remove(&block->heap.block_links, block->phys_pc);
    }
#endif

#ifdef RVJIT_APPLE
    pthread_jit_write_protect_np(true);
#endif

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
    }
    flush_icache(block->heap.data, block->heap.curr);

    hashmap_clear(&block->heap.blocks);
    block->heap.curr = 0;

    rvjit_linker_cleanup(block);

    rvjit_block_init(block);
}
