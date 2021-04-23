/*
rvjit.h - Retargetable Versatile JIT Compiler
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

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>

#ifndef RVJIT_H
#define RVJIT_H

// Explicitly protect/unprotect memory on each JIT invocation
#define RVJIT_PARANOID

#define REG_ILL 0xFF // Register is not allocated

#define REGISTERS_MAX 32
#define REGISTER_ZERO_ENABLED 1

#define REG_SRC    0x1
#define REG_DST    0x2

#define REG_LOADED REG_SRC
#define REG_DIRTY  REG_DST

#ifdef __x86_64__
    #ifdef _WIN32
        #ifdef __MINGW64__
            #define RVJIT_CALL __attribute__((sysv_abi))
            #define RVJIT_ABI_SYSV 1
        #else
            #define RVJIT_ABI_WIN64 1
        #endif
    #else
        #define RVJIT_CALL __attribute__((sysv_abi))
        #define RVJIT_ABI_SYSV 1
    #endif
    #define RVJIT_NATIVE_64BIT 1
    #define RVJIT_X86 1
#elif __i386__
    #ifdef _WIN32
        #ifdef __MINGW32__
            #define RVJIT_CALL __attribute__((fastcall))
        #else
            #define RVJIT_CALL __fastcall
        #endif
    #else
        #define RVJIT_CALL __attribute__((fastcall))
    #endif
    #define RVJIT_ABI_FASTCALL 1
    #define RVJIT_X86 1
#else
    #error No JIT support for the target platform!!!
#endif

// No specific calling convention requirements
#ifndef RVJIT_CALL
#define RVJIT_CALL
#endif

typedef RVJIT_CALL void (*code_ptr_t)(void* vm);
typedef uint8_t regid_t;
typedef uint8_t regflags_t;

typedef struct {
    void* data;
    //uint8_t* bitmask;
    //uint32_t* block_sizes;
    size_t curr;
    size_t size;
} rvjit_heap_t;

typedef struct {
    size_t last_used;   // Last usage of register for LRU reclaim
    regid_t hreg;       // Claimed host register, REG_ILL if not mapped
    regflags_t flags;   // Register allocation details
} rvjit_reginfo_t;

typedef struct {
    rvjit_heap_t* heap;
    code_ptr_t code;
    size_t size;
    size_t hreg_mask;        // Bitmask of available non-clobbered host registers
    size_t abireclaim_mask;  // Bitmask of reclaimed abi-clobbered host registers to restore
    rvjit_reginfo_t regs[REGISTERS_MAX];
} rvjit_block_t;

void rvjit_heap_init(rvjit_heap_t* heap, size_t size);
void rvjit_heap_free(rvjit_heap_t* heap);
void rvjit_block_init(rvjit_block_t* block, rvjit_heap_t* heap);
code_ptr_t rvjit_block_finish(rvjit_block_t* block);

static inline size_t rvjit_hreg_mask(regid_t hreg)
{
    return (1ULL << hreg);
}

static inline void rvjit_put_code(rvjit_block_t* block, const void* inst, size_t size)
{
    memcpy(block->code + block->size, inst, size);
    block->size += size;
}

static inline void rvjit_exec(void* vm, rvjit_block_t* block)
{
    block->code(vm);
}

#endif
