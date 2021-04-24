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

#include "rvjit_x86.h"

#ifdef _WIN32
#include <windows.h>
#include <memoryapi.h>
#else
#include <sys/mman.h>
#endif

void rvjit_heap_init(rvjit_heap_t* heap, size_t size)
{
    size = (size + 0xFFF) & ~0xFFF;
#ifdef _WIN32
#ifdef RVJIT_PARANOID
    heap->data = VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
#else
    heap->data = VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#endif
#else
#ifdef RVJIT_PARANOID
    heap->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#else
    heap->data = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
#endif
    heap->size = size;
    heap->curr = 0;
}

void rvjit_heap_free(rvjit_heap_t* heap)
{
#ifdef _WIN32
    VirtualFree(heap->data, 0, MEM_RELEASE);
#else
    munmap(heap->data, heap->size);
#endif
}

static void rvjit_heap_protect(rvjit_heap_t* heap)
{
#ifdef RVJIT_PARANOID
#ifdef _WIN32
    DWORD old;
    VirtualProtect(heap->data, heap->size, PAGE_EXECUTE_READ, &old);
#else
    mprotect(heap->data, heap->size, PROT_READ | PROT_EXEC);
#endif
#endif
}

static void rvjit_heap_unprotect(rvjit_heap_t* heap)
{
#ifdef RVJIT_PARANOID
#ifdef _WIN32
    DWORD old;
    VirtualProtect(heap->data, heap->size, PAGE_READWRITE, &old);
#else
    mprotect(heap->data, heap->size, PROT_READ | PROT_WRITE);
#endif
#endif
}

void rvjit_block_init(rvjit_block_t* block, rvjit_heap_t* heap)
{
    block->heap = heap;
    block->code = ((code_ptr_t)heap->data) + heap->curr;
    block->size = 0;
    block->hreg_mask = rvjit_native_default_hregmask();
    block->abireclaim_mask = 0;
    for (regid_t i=0; i<REGISTERS_MAX; ++i) {
        block->regs[i].hreg = REG_ILL;
        block->regs[i].last_used = 0;
        block->regs[i].flags = 0;
    }
    rvjit_heap_unprotect(block->heap);
}

code_ptr_t rvjit_block_finish(rvjit_block_t* block)
{
    rvjit_emit_end(block);
    rvjit_heap_protect(block->heap);
    // Flush instruction cache
    #ifndef _WIN32
    __builtin___clear_cache(block->code, block->code + block->size);
    #endif
    block->heap->curr += (block->size + 7) & ~7;
    return block->code;
}
