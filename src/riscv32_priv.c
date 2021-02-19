/*
riscv32_priv.c - RISC-V privileged mode emulation
Copyright (C) 2021  Mr0maks <mr.maks0443@gmail.com>
                    LekKit <github.com/LekKit>

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

#include <assert.h>

#include "riscv.h"
#include "riscv32.h"
#include "riscv32_mmu.h"
#include "riscv32_priv.h"
#include "riscv32_csr.h"
#include "bit_ops.h"

static void riscv32i_system(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    switch (instruction) {
    case RV32_S_ECALL:
        riscv32_debug(vm, "RV32I: ecall");
        return;
    case RV32_S_EBREAK:
        riscv32_debug(vm, "RV32I: ebreak");
        return;
    case RV32_S_URET:
        riscv32_debug(vm, "RV32I: uret");
        return;
    case RV32_S_SRET:
        riscv32_debug(vm, "RV32I: sret");
        return;
    case RV32_S_MRET:
        riscv32_debug(vm, "RV32I: mret");
        return;
    case RV32_S_WFI:
        riscv32_debug(vm, "RV32I: wfi");
        return;
    }

    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    UNUSED(rs1);
    UNUSED(rs2);
    switch (instruction & RV32_S_FENCE_MASK) {
    case RV32_S_SFENCE_VMA:
        riscv32_tlb_flush(vm);
        riscv32_debug(vm, "RV32I: sfence.vma %r, %r", rs1, rs1);
        return;
    // The extension is not ratified yet, no reason to implement these now
    case RV32_S_HFENCE_BVMA:
        riscv32_debug_always(vm, "RV32I: unimplemented hfence.bvma %h", instruction);
        return;
    case RV32_S_HFENCE_GVMA:
        riscv32_debug_always(vm, "RV32I: unimplemented hfence.gvma %h", instruction);
        return;
    }

    riscv32_illegal_insn(vm, instruction);
}

static void riscv32i_fence(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    riscv32_debug_always(vm, "RV32I: unimplemented fence %h", instruction);
}

static void riscv32zifence_i(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    riscv32_debug_always(vm, "RV32I: unimplemented zifence.i %h", instruction);
}

static void riscv32zicsr_csrrw(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t csr = cut_bits(instruction, 20, 12);

    if(rds == REGISTER_ZERO)
        return;

    if(!riscv32_csr_swap(vm, csr, rs1, rds)) {
        //TODO: error here
        riscv32_debug_always(vm, "RV32priv: bad csr %h\n", csr);
    }
    riscv32_debug(vm, "RV32priv: csrrw %r, %h, %r\n", rds, csr, rs1);
}

static void riscv32zicsr_csrrs(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    riscv32_debug_always(vm, "RV32I: unimplemented csrrs %h", instruction);
}

static void riscv32zicsr_csrrc(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    riscv32_debug_always(vm, "RV32I: unimplemented csrrc %h", instruction);
}

static void riscv32zicsr_csrrwi(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    riscv32_debug_always(vm, "RV32I: unimplemented csrrwi %h", instruction);
}

static void riscv32zicsr_csrrsi(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    riscv32_debug_always(vm, "RV32I: unimplemented csrrsi %h", instruction);
}

static void riscv32zicsr_csrrci(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    riscv32_debug_always(vm, "RV32I: unimplemented csrrci %h", instruction);
}

void riscv32_priv_init()
{
    smudge_opcode_ISB(RV32I_SYSTEM, riscv32i_system);
    smudge_opcode_ISB(RV32I_FENCE, riscv32i_fence);
    smudge_opcode_ISB(RV32_ZIFENCE_I, riscv32zifence_i);
    smudge_opcode_ISB(RV32_ZICSR_CSRRW, riscv32zicsr_csrrw);
    smudge_opcode_ISB(RV32_ZICSR_CSRRS, riscv32zicsr_csrrs);
    smudge_opcode_ISB(RV32_ZICSR_CSRRC, riscv32zicsr_csrrc);
    smudge_opcode_ISB(RV32_ZICSR_CSRRWI, riscv32zicsr_csrrwi);
    smudge_opcode_ISB(RV32_ZICSR_CSRRSI, riscv32zicsr_csrrsi);
    smudge_opcode_ISB(RV32_ZICSR_CSRRCI, riscv32zicsr_csrrci);
}
