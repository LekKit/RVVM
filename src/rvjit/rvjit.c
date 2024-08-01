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
#include "atomics.h"
#include "bit_ops.h"
#include "vma_ops.h"

#if defined(_WIN32) && !defined(RVJIT_X86) && !defined(GNU_EXTS)
#include <windows.h>
#endif

#if defined(__APPLE__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 110000
void sys_icache_invalidate(void* start, size_t len);
#include <pthread.h>
#define RVJIT_APPLE_SILICON
#endif

#if defined(RVJIT_RISCV) && defined(__linux__)
/*
 * Clang doesn't seem to implement __builtin___clear_cache properly
 * on RISC-V, wreaking havok on hosts with non-coherent icache
 * (RVVM is also affected, heh), hence we make a direct syscall
 */
#include <sys/syscall.h>
#include <unistd.h>
#ifndef __NR_riscv_flush_icache
#define __NR_riscv_flush_icache 259
#endif
#endif

static void rvjit_flush_icache(const void* addr, size_t size)
{
#ifdef RVJIT_X86
    // x86 has coherent instruction caches
    UNUSED(addr);
    UNUSED(size);
#elif defined(RVJIT_APPLE_SILICON)
    sys_icache_invalidate((void*)addr, size);
#elif defined(RVJIT_RISCV) && defined(__linux__)
    syscall(__NR_riscv_flush_icache, addr, ((char*)addr) + size, 0);
#elif GCC_CHECK_VER(4, 7) || CLANG_CHECK_VER(3, 5)
    // Use __builtin___clear_cache on modern toolchains
    __builtin___clear_cache((char*)addr, ((char*)addr) + size);
#elif defined(GNU_EXTS)
    __clear_cache((char*)addr, ((char*)addr) + size);
#elif defined(_WIN32)
    FlushInstructionCache(GetCurrentProcess(), addr, size);
#else
    #error No rvjit_flush_icache() support!
#endif
}

bool rvjit_ctx_init(rvjit_block_t* block, size_t size)
{
    // Assume it's already inited
    if (block->heap.data) return true;

    if (rvvm_has_arg("rvjit_disable_rwx")) {
        rvvm_info("RWX disabled, allocating W^X multi-mmap RVJIT heap");
    } else {
        block->heap.data = vma_alloc(NULL, size, VMA_RWX);
        block->heap.code = block->heap.data;

        // Possible on Linux PaX (hardened) or OpenBSD
        if (block->heap.data == NULL) {
            rvvm_info("Failed to allocate RWX RVJIT heap, falling back to W^X multi-mmap");
        }
    }

    if (block->heap.data == NULL) {
        void* rw = NULL;
        void* exec = NULL;
        if (!vma_multi_mmap(&rw, &exec, size)) {
            rvvm_warn("Failed to allocate W^X RVJIT heap!");
            return false;
        }
        block->heap.data = rw;
        block->heap.code = exec;
    }

    rvjit_flush_icache(block->heap.code, block->heap.size);

    block->space = 1024;
    block->code = safe_malloc(block->space);

    block->heap.size = size;
    block->heap.curr = 0;

    block->rv64 = false;

    hashmap_init(&block->heap.blocks, 64);
    hashmap_init(&block->heap.block_links, 64);
    vector_init(block->links);
    return true;
}

void rvjit_init_memtracking(rvjit_block_t* block, size_t size)
{
    // Each dirty page is marked in atomic bitmask
    free(block->heap.dirty_pages);
    free(block->heap.jited_pages);
    block->heap.dirty_mask = bit_next_pow2((size + 0x1FFFF) >> 17) - 1;
    block->heap.dirty_pages = safe_new_arr(uint32_t, block->heap.dirty_mask + 1);
    block->heap.jited_pages = safe_new_arr(uint32_t, block->heap.dirty_mask + 1);
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
    vma_free(block->heap.data, block->heap.size);
    if (block->heap.code != block->heap.data) {
        vma_free((void*)block->heap.code, block->heap.size);
    }
    rvjit_linker_cleanup(block);
    hashmap_destroy(&block->heap.blocks);
    hashmap_destroy(&block->heap.block_links);
    vector_free(block->links);
    free(block->code);
    free(block->heap.dirty_pages);
    free(block->heap.jited_pages);
}

static inline void rvjit_mark_jited_page(rvjit_block_t* block, phys_addr_t addr)
{
    if (block->heap.jited_pages == NULL) return;
    size_t offset = (addr >> 17) & block->heap.dirty_mask;
    uint32_t mask = 1U << ((addr >> 12) & 0x1F);
    atomic_or_uint32_ex(block->heap.jited_pages + offset, mask, ATOMIC_RELAXED);
}

static inline void rvjit_mark_dirty_page(rvjit_block_t* block, phys_addr_t addr)
{
    size_t offset = (addr >> 17) & block->heap.dirty_mask;
    uint32_t mask = 1U << ((addr >> 12) & 0x1F);
    if (atomic_load_uint32_ex(block->heap.jited_pages + offset, ATOMIC_RELAXED) & mask) {
        atomic_or_uint32_ex(block->heap.dirty_pages + offset, mask, ATOMIC_RELAXED);
        atomic_and_uint32_ex(block->heap.jited_pages + offset, ~mask, ATOMIC_RELAXED);
    }
}

void rvjit_mark_dirty_mem(rvjit_block_t* block, phys_addr_t addr, size_t size)
{
    if (block->heap.dirty_pages == NULL) return;
    for (size_t i=0; i<size; i += 4096) {
        rvjit_mark_dirty_page(block, addr + i);
    }
}

static inline bool rvjit_page_needs_flush(rvjit_block_t* block, phys_addr_t addr)
{
    size_t offset = (addr >> 17) & block->heap.dirty_mask;
    uint32_t mask = 1U << ((addr >> 12) & 0x1F);
    if (block->heap.dirty_pages == NULL) return false;
    return (atomic_load_uint32_ex(block->heap.dirty_pages + offset, ATOMIC_RELAXED) & mask)
        && (atomic_and_uint32(block->heap.dirty_pages + offset, ~mask) & mask);
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
    void* dest = block->heap.data + block->heap.curr;
    const void* code = block->heap.code + block->heap.curr;

    rvjit_emit_end(block, block->linkage);

    if (block->heap.curr + block->size > block->heap.size) {
        // The cache is full
        return NULL;
    }

#ifdef RVJIT_APPLE_SILICON
    pthread_jit_write_protect_np(false);
#endif

    memcpy(dest, block->code, block->size);
    rvjit_flush_icache(code, block->size);
    //block->heap.curr = (block->heap.curr + block->size + 31) & ~31ULL;
    block->heap.curr += block->size;

    hashmap_put(&block->heap.blocks, block->phys_pc, (size_t)code);

#ifdef RVJIT_NATIVE_LINKER
    vector_t(uint8_t*)* linked_blocks;
    phys_addr_t k;
    size_t v;
    vector_foreach(block->links, i) {
        k = vector_at(block->links, i).dest;
        v = vector_at(block->links, i).ptr;
        linked_blocks = (void*)hashmap_get(&block->heap.block_links, k);
        if (!linked_blocks) {
            linked_blocks = safe_calloc(1, sizeof(vector_t(uint8_t*)));
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

#ifdef RVJIT_APPLE_SILICON
    pthread_jit_write_protect_np(true);
#endif

    rvjit_mark_jited_page(block, block->phys_pc);

    return (rvjit_func_t)code;
}

rvjit_func_t rvjit_block_lookup(rvjit_block_t* block, phys_addr_t phys_pc)
{
    if (unlikely(rvjit_page_needs_flush(block, phys_pc))) {
        vector_t(uint8_t*)* linked_blocks;
        phys_pc &= ~0xFFFULL;

        for (size_t i=0; i<4096; ++i) {
            hashmap_remove(&block->heap.blocks, phys_pc + i);
            linked_blocks = (void*)hashmap_get(&block->heap.block_links, phys_pc + i);
            if (linked_blocks) {
                vector_free(*linked_blocks);
                free(linked_blocks);
                hashmap_remove(&block->heap.block_links, phys_pc + i);
            }
        }
        return NULL;
    }
    return (rvjit_func_t)hashmap_get(&block->heap.blocks, phys_pc);
}

void rvjit_flush_cache(rvjit_block_t* block)
{
    if (block->heap.curr > 0x10000) {
        // Deallocate the physical memory used for RWX JIT cache
        // This reduces average memory usage since the cache is never full
        vma_clean(block->heap.data, block->heap.size, true);
    }
    rvjit_flush_icache(block->heap.code, block->heap.curr);

    hashmap_clear(&block->heap.blocks);
    block->heap.curr = 0;

    rvjit_linker_cleanup(block);

    if (block->heap.dirty_pages) {
        for (size_t i=0; i<=block->heap.dirty_mask; ++i) {
            atomic_store_uint32_ex(block->heap.dirty_pages + i, 0, ATOMIC_RELAXED);
        }
    }

    rvjit_block_init(block);
}
