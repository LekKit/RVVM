/*
riscv32_priv.c - RISC-V privileged mode emulation
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

#include <assert.h>

#include "riscv.h"
#include "riscv32.h"
#include "riscv32_mmu.h"
#include "riscv32_priv.h"
#include "riscv32_csr.h"
#include "bit_ops.h"

extern uint64_t clint_mtimecmp;
extern uint64_t clint_mtime;

static void riscv32i_system(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    switch (instruction) {
    case RV32_S_ECALL:
        riscv32_debug(vm, "RV32I: ecall");
        riscv32_trap(vm, TRAP_ENVCALL_UMODE + vm->priv_mode, 0);
        return;
    case RV32_S_EBREAK:
        riscv32_debug(vm, "RV32I: ebreak");
        riscv32_trap(vm, TRAP_BREAKPOINT, 0);
        return;
    case RV32_S_URET:
        riscv32_debug_always(vm, "RV32I: uret");
        // No N extension
        riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
        return;
    case RV32_S_SRET:
        riscv32_debug(vm, "RV32I: sret");
        if (vm->priv_mode >= PRIVILEGE_SUPERVISOR) {
            // Set privilege mode to SPP
            vm->priv_mode = cut_bits(vm->csr.status, CSR_STATUS_SPP, 1);
            // Set SIE to SPIE
            vm->csr.status = replace_bits(vm->csr.status, CSR_STATUS_SIE, 1, cut_bits(vm->csr.status, CSR_STATUS_SPIE, 1));
            // Set PC to csr.sepc
            riscv32i_write_register_u(vm, REGISTER_PC, vm->csr.epc[PRIVILEGE_SUPERVISOR] - 4);
        } else {
            riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
        }
        return;
    case RV32_S_MRET:
        riscv32_debug(vm, "RV32I: mret");
        if (vm->priv_mode >= PRIVILEGE_MACHINE) {
            // Set privilege mode to MPP
            vm->priv_mode = cut_bits(vm->csr.status, CSR_STATUS_MPP_START, CSR_STATUS_MPP_SIZE);
            // Set MIE to MPIE
            vm->csr.status = replace_bits(vm->csr.status, CSR_STATUS_MIE, 1, cut_bits(vm->csr.status, CSR_STATUS_MPIE, 1));
            // Set PC to csr.mepc
            riscv32i_write_register_u(vm, REGISTER_PC, vm->csr.epc[PRIVILEGE_MACHINE] - 4);
        } else {
            riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
        }
        return;
    case RV32_S_WFI:
        riscv32_debug(vm, "RV32I: wfi");

        // Clear timer interrupt if needed
        if (!rvtimer_pending(&vm->timer)) vm->csr.ip &= ~(1 << INTERRUPT_MTIMER);
        // Check for external interrupts
        if (riscv32_handle_ip(vm, true)) return;
        // Sleep before timer interrupt
        while (!rvtimer_pending(&vm->timer)) {
            sleep_ms(1);
        }
        vm->csr.ip |= (1 << INTERRUPT_MTIMER);
        riscv32_handle_ip(vm, true);
        return;
    }

    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    UNUSED(rs1);
    UNUSED(rs2);
    switch (instruction & RV32_S_FENCE_MASK) {
    case RV32_S_SFENCE_VMA:
        riscv32_debug(vm, "RV32I: sfence.vma %r, %r", rs1, rs1);
        if (vm->priv_mode >= PRIVILEGE_SUPERVISOR) {
            riscv32_tlb_flush(vm);
        } else {
            riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
        }
        return;
    // The extension is not ratified yet, no reason to implement these now
    case RV32_S_HFENCE_BVMA:
        riscv32_debug_always(vm, "RV32I: unimplemented hfence.bvma %h", instruction);
        riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
        return;
    case RV32_S_HFENCE_GVMA:
        riscv32_debug_always(vm, "RV32I: unimplemented hfence.gvma %h", instruction);
        riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
        return;
    }

    riscv32_illegal_insn(vm, instruction);
}

static void riscv32i_fence(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    UNUSED(vm);
    UNUSED(instruction);
    riscv32_debug(vm, "RV32I: unimplemented fence %h", instruction);
}

static void riscv32zifence_i(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    UNUSED(vm);
    UNUSED(instruction);
    riscv32_debug(vm, "RV32I: unimplemented zifence.i %h", instruction);
}

static void riscv32zicsr_csrrw(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t csr = cut_bits(instruction, 20, 12);
    reg_t val = riscv32i_read_register_u(vm, rs1);

    if (riscv32_csr_op(vm, csr, &val, CSR_SWAP)) {
        riscv32i_write_register_u(vm, rds, val);
    } else {
        riscv32_debug_always(vm, "RV32I: bad csr %h", csr);
        riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
    }
    riscv32_debug(vm, "RV32I: csrrw %r, %c, %r", rds, csr, rs1);
}

static void riscv32zicsr_csrrs(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t csr = cut_bits(instruction, 20, 12);
    reg_t val = riscv32i_read_register_u(vm, rs1);

    if (riscv32_csr_op(vm, csr, &val, CSR_SETBITS)) {
        riscv32i_write_register_u(vm, rds, val);
    } else {
        riscv32_debug_always(vm, "RV32I: bad csr %h", csr);
        riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
    }
    riscv32_debug(vm, "RV32I: csrrs %r, %c, %r", rds, csr, rs1);
}

static void riscv32zicsr_csrrc(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t csr = cut_bits(instruction, 20, 12);
    reg_t val = riscv32i_read_register_u(vm, rs1);

    if (riscv32_csr_op(vm, csr, &val, CSR_CLEARBITS)) {
        riscv32i_write_register_u(vm, rds, val);
    } else {
        riscv32_debug_always(vm, "RV32I: bad csr %h\n", csr);
        riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
    }
    riscv32_debug(vm, "RV32I: csrrc %r, %c, %r", rds, csr, rs1);
}

static void riscv32zicsr_csrrwi(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 5);
    reg_t val = cut_bits(instruction, 15, 5);
    uint32_t csr = cut_bits(instruction, 20, 12);

    if (riscv32_csr_op(vm, csr, &val, CSR_SWAP)) {
        riscv32i_write_register_u(vm, rds, val);
    } else {
        riscv32_debug_always(vm, "RV32priv: bad csr %h", csr);
        riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
    }
    riscv32_debug(vm, "RV32I: csrrwi %r, %c, %h", rds, csr, cut_bits(instruction, 15, 5));
}

static void riscv32zicsr_csrrsi(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 5);
    reg_t val = cut_bits(instruction, 15, 5);
    uint32_t csr = cut_bits(instruction, 20, 12);

    if (riscv32_csr_op(vm, csr, &val, CSR_SETBITS)) {
        riscv32i_write_register_u(vm, rds, val);
    } else {
        riscv32_debug_always(vm, "RV32priv: bad csr %h", csr);
        riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
    }
    riscv32_debug(vm, "RV32I: csrrsi %r, %c, %h", rds, csr, cut_bits(instruction, 15, 5));
}

static void riscv32zicsr_csrrci(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 5);
    reg_t val = cut_bits(instruction, 15, 5);
    uint32_t csr = cut_bits(instruction, 20, 12);

    if (riscv32_csr_op(vm, csr, &val, CSR_CLEARBITS)) {
        riscv32i_write_register_u(vm, rds, val);
    } else {
        riscv32_debug_always(vm, "RV32priv: bad csr %h", csr);
        riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
    }
    riscv32_debug(vm, "RV32I: csrrci %r, %c, %h", rds, csr, cut_bits(instruction, 15, 5));
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
