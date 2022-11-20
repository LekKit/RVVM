/*
riscv_cpu.c - RISC-V CPU Emulation
Copyright (C) 2021  LekKit <github.com/LekKit>
                    Mr0maks <mr.maks0443@gmail.com>

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

#include "riscv_cpu.h"
#include "riscv_mmu.h"
#include <stdio.h>

void riscv_illegal_insn(rvvm_hart_t* vm, const uint32_t instruction)
{
    riscv_trap(vm, TRAP_ILL_INSTR, instruction);
}

void riscv_c_illegal_insn(rvvm_hart_t* vm, const uint16_t instruction)
{
    riscv_trap(vm, TRAP_ILL_INSTR, instruction);
}

static inline uint32_t riscv_funcid(const uint32_t instr)
{
    return (((instr >> 17) & 0x100) | ((instr >> 7) & 0xE0) | ((instr >> 2) & 0x1F));
}

static inline uint32_t riscv_c_funcid(const uint16_t instr)
{
    return (((instr >> 13) << 2) | (instr & 3));
}

#define RV_OPCODE_MASK 0x3

// Install instruction implementations to the jumptable
void riscv_install_opcode_R(rvvm_hart_t* vm, uint32_t opcode, riscv_inst_t func)
{
    vm->decoder.opcodes[opcode] = func;
}

void riscv_install_opcode_UJ(rvvm_hart_t* vm, uint32_t opcode, riscv_inst_t func)
{
    for (uint32_t f3=0; f3<0x10; ++f3) {
        vm->decoder.opcodes[opcode | (f3 << 5)] = func;
    }
}

void riscv_install_opcode_ISB(rvvm_hart_t* vm, uint32_t opcode, riscv_inst_t func)
{
    vm->decoder.opcodes[opcode] = func;
    vm->decoder.opcodes[opcode | 0x100] = func;
}

void riscv_install_opcode_C(rvvm_hart_t* vm, uint32_t opcode, riscv_inst_c_t func)
{
    vm->decoder.opcodes_c[opcode] = func;
}

#ifdef USE_JIT

/*
 * When returning from recompiled blocks, hart state
 * stays consistent, thus allowing to switch between
 * trace-interpret-compile and trace-execute states
 */

static inline void riscv_jit_tlb_put(rvvm_hart_t* vm, vaddr_t vaddr, rvjit_func_t block)
{
    vaddr_t entry = (vaddr >> 1) & TLB_MASK;
    vm->jtlb[entry].pc = vaddr;
    vm->jtlb[entry].block = block;
}

NOINLINE bool riscv_jit_lookup(rvvm_hart_t* vm)
{
    /*
     * Translate virtual address into physical.
     * We are tracing address already fetched from,
     * thus a pagefault isn't possible
     */
    vaddr_t virt_pc = vm->registers[REGISTER_PC];
    vmptr_t ptr = riscv_vma_translate_e(vm, virt_pc);
    // Lookup in the hashmap, cache in JTLB
    if (ptr) {
        paddr_t phys_pc = (size_t)(ptr - vm->mem.data) + vm->mem.begin;
        rvjit_func_t block = rvjit_block_lookup(&vm->jit, phys_pc);
        if (block) {
            riscv_jit_tlb_put(vm, virt_pc, block);
            block(vm);
            return true;
        }

        /*
         * No valid block compiled for this location,
         * make a new one and enable compiler
         */
        rvjit_block_init(&vm->jit);
        vm->jit.pc_off = 0;
        vm->jit.virt_pc = virt_pc;
        vm->jit.phys_pc = phys_pc;

        // Flush JTLB upon hiting a dirty block
        riscv_jit_tlb_flush(vm);

        vm->jit_compiling = true;
        vm->block_ends = false;
    }
    return false;
}

void riscv_jit_mark_dirty_mem(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size)
{
    vector_foreach(machine->harts, i) {
        rvjit_mark_dirty_mem(&vector_at(machine->harts, i)->jit, addr, size);
    }
}

static void riscv_jit_finalize(rvvm_hart_t* vm)
{
    if (rvjit_block_nonempty(&vm->jit)) {
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

static inline void riscv_emulate(rvvm_hart_t *vm, uint32_t instruction)
{
#ifdef USE_JIT
    if (unlikely(vm->jit_compiling)) {
        /*
         * If we hit non-compilable instruction or cross page boundaries,
         * the block is finalized.
         */
        if (vm->block_ends
        || (vm->jit.virt_pc >> PAGE_SHIFT) != (vm->registers[REGISTER_PC] >> PAGE_SHIFT)) {
            riscv_jit_finalize(vm);
        }
        vm->block_ends = true;
    }
#endif
    if ((instruction & RV_OPCODE_MASK) != RV_OPCODE_MASK) {
        vm->decoder.opcodes_c[riscv_c_funcid(instruction)](vm, instruction);
        // FYI: Any jump instruction implementation should take care of PC increment
        vm->registers[REGISTER_PC] += 2;
    } else {
        vm->decoder.opcodes[riscv_funcid(instruction)](vm, instruction);
        vm->registers[REGISTER_PC] += 4;
    }
}

#ifdef USE_RV64
void riscv_decoder_init_rv64(rvvm_hart_t* vm)
{
    riscv64i_init(vm);
    riscv64c_init(vm);
    riscv64m_init(vm);
    riscv64a_init(vm);
#ifdef USE_FPU
    if (fpu_is_enabled(vm)) {
        riscv64f_enable(vm, true);
        riscv64d_enable(vm, true);
    }
#endif
}
#endif

void riscv_decoder_init_rv32(rvvm_hart_t* vm)
{
    riscv32i_init(vm);
    riscv32c_init(vm);
    riscv32m_init(vm);
    riscv32a_init(vm);
#ifdef USE_FPU
    if (fpu_is_enabled(vm)) {
        riscv32f_enable(vm, true);
        riscv32d_enable(vm, true);
    }
#endif
}

void riscv_decoder_enable_fpu(rvvm_hart_t* vm, bool enable)
{
#ifdef USE_RV64
    if (vm->rv64) {
        riscv64f_enable(vm, enable);
        riscv64d_enable(vm, enable);
    } else {
#endif
        riscv32f_enable(vm, enable);
        riscv32d_enable(vm, enable);
#ifdef USE_RV64
    }
#endif
}

/*
 * Optimized dispatch loop that does not fetch each instruction,
 * and invokes MMU on page change instead.
 * This gains us about 40-60% more performance depending on workload.
 * Attention: Any TLB flush must clear vm->wait_event to
 * restart dispatch loop, otherwise it will continue executing current page
 */
TSAN_SUPPRESS void riscv_run_till_event(rvvm_hart_t* vm)
{
    size_t inst_ptr = 0;  // Updated before any read
    uint32_t instruction;
    // page_addr should always mismatch pc by at least 1 page before execution
    vaddr_t inst_addr, page_addr = vm->registers[REGISTER_PC] + 0x1000;

    // Execute instructions loop until some event occurs (interrupt, trap)
    while (likely(vm->wait_event)) {
        vm->registers[REGISTER_ZERO] = 0;
        inst_addr = vm->registers[REGISTER_PC];
        if (likely(inst_addr - page_addr < 0xFFD)) {
            riscv_emulate(vm, read_uint32_le_m((vmptr_t)(size_t)(inst_ptr + TLB_VADDR(inst_addr))));
#ifndef __llvm__
            vm->registers[REGISTER_ZERO] = 0;
            inst_addr = vm->registers[REGISTER_PC];
            if (likely(vm->wait_event)) {
                if (likely(inst_addr - page_addr < 0xFFD)) {
                    riscv_emulate(vm, read_uint32_le_m((vmptr_t)(size_t)(inst_ptr + TLB_VADDR(inst_addr))));
                }
            }
            continue;
#endif
        }
#ifdef __llvm__
        else
#endif
        if (likely(riscv_fetch_inst(vm, inst_addr, &instruction))) {
            // Update pointer to the current page in real memory
            inst_ptr = vm->tlb[(inst_addr >> PAGE_SHIFT) & TLB_MASK].ptr;
            // If we are executing code from MMIO, direct memory fetch fails
            page_addr = vm->tlb[(inst_addr >> PAGE_SHIFT) & TLB_MASK].e << PAGE_SHIFT;
            riscv_emulate(vm, instruction);
        } else break;
    }
}
