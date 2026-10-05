/*
riscv_cpu.c - RISC-V CPU Interfaces
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "riscv_cpu.h"
#include "riscv_hart.h"

void riscv_run_till_event(rvvm_hart_t* vm)
{
    if (vm->rv64) {
#ifdef USE_RV64
        riscv64_run_interpreter(vm);
#endif
    } else {
#ifdef USE_RV32
        riscv32_run_interpreter(vm);
#endif
    }
}

slow_path void riscv_illegal_insn(rvvm_hart_t* vm, const uint32_t insn)
{
    riscv_trap(vm, RISCV_TRAP_ILL_INSN, insn);
}

void riscv_jit_flush_cache(rvvm_hart_t* vm)
{
#ifdef USE_JIT
    if (vm->jit_enabled) {
        riscv_jit_discard(vm);
        riscv_jit_tlb_flush(vm);
        rvjit_flush_cache(&vm->jit);
    }
#else
    UNUSED(vm);
#endif
}

#ifdef USE_JIT

void riscv_jit_mark_dirty_mem(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size)
{
    vector_foreach(machine->harts, i) {
        rvjit_mark_dirty_mem(&vector_at(machine->harts, i)->jit, addr, size);
    }
}

/*
 * When returning from recompiled blocks, hart state
 * stays consistent, thus allowing to switch between
 * interpret-trace-compile and trace-execute states
 */

static inline void riscv_jit_tlb_put(rvvm_hart_t* vm, rvvm_addr_t vaddr, rvjit_func_t block)
{
    rvvm_addr_t entry = (vaddr >> 1) & RVVM_TLB_MASK;
    vm->jtlb[entry].pc = vaddr;
    vm->jtlb[entry].block = block;
}

static bool riscv_jit_lookup(rvvm_hart_t* vm)
{
    // Translate virtual PC into physical, JIT operates on phys_pc
    rvvm_addr_t virt_pc = vm->registers[RISCV_REG_PC];
    rvvm_addr_t phys_pc = 0;
    // Lookup in the hashmap, cache virt_pc->block in JTLB
    if (riscv_virt_translate_e(vm, virt_pc, &phys_pc)) {
        rvjit_func_t block = rvjit_block_lookup(&vm->jit, phys_pc);
        if (block) {
            riscv_jit_tlb_put(vm, virt_pc, block);
            block(vm);
            return true;
        }

        // No valid block compiled for this location,
        // init a new one and enable JIT compiler
        rvjit_block_init(&vm->jit);
        vm->jit.pc_off = 0;
        vm->jit.virt_pc = virt_pc;
        vm->jit.phys_pc = phys_pc;

        // Von Neumann icache: Flush JTLB upon hiting a dirty block
        riscv_jit_tlb_flush(vm);

        vm->jit_compiling = true;
        vm->jit_block_ends = false;
    }
    return false;
}

static forceinline bool riscv_jtlb_lookup(rvvm_hart_t* vm)
{
    // Try to find & execute a block
    rvvm_addr_t pc = vm->registers[RISCV_REG_PC];
    rvvm_addr_t entry = (pc >> 1) & RVVM_TLB_MASK;
    rvvm_addr_t tpc = vm->jtlb[entry].pc;
    if (likely(pc == tpc)) {
        vm->jtlb[entry].block(vm);
        return true;
    } else {
        return false;
    }
}

slow_path bool riscv_jit_tlb_lookup(rvvm_hart_t* vm)
{
    if (likely(vm->jit_enabled)) {
        if (likely(riscv_jtlb_lookup(vm))) {
#ifndef RVJIT_NATIVE_LINKER
            // Try to execute more blocks if they aren't linked
            for (size_t i=0; i<10 && riscv_jtlb_lookup(vm); ++i);
#endif
            return true;
        } else {
            return riscv_jit_lookup(vm);
        }
    }
    return false;
}

slow_path void riscv_jit_finalize(rvvm_hart_t* vm)
{
    // A cross-page jump to x0 may emit no instructions until the PC-update epilogue.
    if (rvjit_block_nonempty(&vm->jit) || vm->jit.pc_off != 0) {
        rvjit_func_t block = rvjit_block_finalize(&vm->jit);

        if (block) {
            riscv_jit_tlb_put(vm, vm->jit.virt_pc, block);
        } else {
            // Our cache is full, flush it
            riscv_jit_tlb_flush(vm);
            rvjit_flush_cache(&vm->jit);
        }
    }

    vm->jit_compiling = false;
}

#endif
