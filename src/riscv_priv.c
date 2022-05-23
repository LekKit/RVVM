/*
riscv_priv.c - RISC-V Privileged
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

#include "riscv_priv.h"
#include "riscv_csr.h"
#include "riscv_hart.h"
#include "riscv_mmu.h"
#include "riscv_cpu.h"
#include "bit_ops.h"
#include "atomics.h"

static void riscv_priv_system(rvvm_hart_t* vm, const uint32_t instruction)
{
    switch (instruction) {
        case RV_PRIV_S_ECALL:
            riscv_trap(vm, TRAP_ENVCALL_UMODE + vm->priv_mode, 0);
            return;
        case RV_PRIV_S_EBREAK:
            riscv_trap(vm, TRAP_BREAKPOINT, 0);
            return;
        case RV_PRIV_S_URET:
            // No N extension
            riscv_trap(vm, TRAP_ILL_INSTR, instruction);
            return;
        case RV_PRIV_S_SRET:
            if (vm->priv_mode >= PRIVILEGE_SUPERVISOR) {
                uint8_t next_priv = bit_cut(vm->csr.status, 8, 1);
                // Set SPP to U
                vm->csr.status = bit_replace(vm->csr.status, 8, 1, PRIVILEGE_USER);
                // Set SIE to SPIE
                vm->csr.status = bit_replace(vm->csr.status, 1, 1, bit_cut(vm->csr.status, 5, 1));
                // Set PC to csr.sepc
                vm->registers[REGISTER_PC] = vm->csr.epc[PRIVILEGE_SUPERVISOR];
                // Set privilege mode to SPP
                if (vm->csr.ip & vm->csr.ie) riscv_restart_dispatch(vm);
                riscv_switch_priv(vm, next_priv);
                // If we aren't unwinded to dispatch decrement PC by instruction size
                vm->registers[REGISTER_PC] -= 4;
            } else {
                riscv_trap(vm, TRAP_ILL_INSTR, instruction);
            }
            return;
        case RV_PRIV_S_MRET:
            if (vm->priv_mode >= PRIVILEGE_MACHINE) {
                uint8_t next_priv = bit_cut(vm->csr.status, 11, 2);
                // Set MPP to U
                vm->csr.status = bit_replace(vm->csr.status, 11, 2, PRIVILEGE_USER);
                // Set MIE to MPIE
                vm->csr.status = bit_replace(vm->csr.status, 3, 1, bit_cut(vm->csr.status, 7, 1));
                // Set PC to csr.mepc
                vm->registers[REGISTER_PC] = vm->csr.epc[PRIVILEGE_MACHINE];
                // Set privilege mode to MPP
                if (vm->csr.ip & vm->csr.ie) riscv_restart_dispatch(vm);
                riscv_switch_priv(vm, next_priv);
                // If we aren't unwinded to dispatch decrement PC by instruction size
                vm->registers[REGISTER_PC] -= 4;
            } else {
                riscv_trap(vm, TRAP_ILL_INSTR, instruction);
            }
            return;
        case RV_PRIV_S_WFI:
            // Resume execution for locally enabled interrupts pending at any privilege level
            if (vm->csr.ip & vm->csr.ie) {
                riscv_restart_dispatch(vm);
                return;
            }
            // Stall the hart until an interrupt might need servicing
            while (atomic_load_uint32(&vm->wait_event)) {
                uint64_t timestamp = rvtimer_get(&vm->timer);
                if (timestamp >= vm->timer.timecmp) vm->csr.ip |= (1 << INTERRUPT_MTIMER);
                // Calculate sleep period
                if (vm->timer.timecmp > timestamp) {
                    timestamp = (vm->timer.timecmp - timestamp) * 1000 / vm->timer.freq;
                    if (timestamp == 0) timestamp = 1;
                    if (timestamp > 100) timestamp = 100;
                    sleep_ms(timestamp);
                } else {
                    /*
                     * Timer interrupt is pending, but we are still here.
                     * This most likely means that timer IRQs are disabled,
                     * so we should sleep and wait for devices / IPI
                     */
                    sleep_ms(10);
                }
            }
            return;
    }

    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    UNUSED(rs1);
    UNUSED(rs2);
    switch (instruction & RV_PRIV_S_FENCE_MASK) {
    case RV_PRIV_S_SFENCE_VMA:
        if (vm->priv_mode >= PRIVILEGE_SUPERVISOR) {
            riscv_tlb_flush(vm);
        } else {
            riscv_trap(vm, TRAP_ILL_INSTR, instruction);
        }
        return;
    // The extension is not ratified yet, no reason to implement these now
    case RV_PRIV_S_HFENCE_BVMA:
        riscv_trap(vm, TRAP_ILL_INSTR, instruction);
        return;
    case RV_PRIV_S_HFENCE_GVMA:
        riscv_trap(vm, TRAP_ILL_INSTR, instruction);
        return;
    }

    riscv_trap(vm, TRAP_ILL_INSTR, instruction);
}

static void riscv_i_fence(rvvm_hart_t* vm, const uint32_t instruction)
{
    UNUSED(vm);
    UNUSED(instruction);
    atomic_fence();
}

static void riscv_i_zifence(rvvm_hart_t* vm, const uint32_t instruction)
{
    UNUSED(instruction);
    riscv_jit_flush_cache(vm);
}

static void riscv_zicsr_csrrw(rvvm_hart_t* vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    uint32_t csr = bit_cut(instruction, 20, 12);
    maxlen_t val = vm->registers[rs1];

    if (riscv_csr_op(vm, csr, &val, CSR_SWAP)) {
        vm->registers[rds] = val;
    } else {
        riscv_trap(vm, TRAP_ILL_INSTR, instruction);
    }
}

static void riscv_zicsr_csrrs(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    uint32_t csr = bit_cut(instruction, 20, 12);
    maxlen_t val = vm->registers[rs1];

    if (riscv_csr_op(vm, csr, &val, CSR_SETBITS)) {
        vm->registers[rds] = val;
    } else {
        riscv_trap(vm, TRAP_ILL_INSTR, instruction);
    }
}

static void riscv_zicsr_csrrc(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    uint32_t csr = bit_cut(instruction, 20, 12);
    maxlen_t val = vm->registers[rs1];

    if (riscv_csr_op(vm, csr, &val, CSR_CLEARBITS)) {
        vm->registers[rds] = val;
    } else {
        riscv_trap(vm, TRAP_ILL_INSTR, instruction);
    }
}

static void riscv_zicsr_csrrwi(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t csr = bit_cut(instruction, 20, 12);
    maxlen_t val = bit_cut(instruction, 15, 5);

    if (riscv_csr_op(vm, csr, &val, CSR_SWAP)) {
        vm->registers[rds] = val;
    } else {
        riscv_trap(vm, TRAP_ILL_INSTR, instruction);
    }
}

static void riscv_zicsr_csrrsi(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t csr = bit_cut(instruction, 20, 12);
    maxlen_t val = bit_cut(instruction, 15, 5);

    if (riscv_csr_op(vm, csr, &val, CSR_SETBITS)) {
        vm->registers[rds] = val;
    } else {
        riscv_trap(vm, TRAP_ILL_INSTR, instruction);
    }
}

static void riscv_zicsr_csrrci(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t csr = bit_cut(instruction, 20, 12);
    maxlen_t val = bit_cut(instruction, 15, 5);

    if (riscv_csr_op(vm, csr, &val, CSR_CLEARBITS)) {
        vm->registers[rds] = val;
    } else {
        riscv_trap(vm, TRAP_ILL_INSTR, instruction);
    }
}

static uint32_t csr_global_init = 0;

void riscv_priv_init(rvvm_hart_t* vm)
{
    // Thread-safe csr initialization
    if (atomic_cas_uint32(&csr_global_init, 0, 1)) {
        riscv_csr_global_init();
    }
    
    riscv_install_opcode_ISB(vm, RV_PRIV_SYSTEM, riscv_priv_system);
    riscv_install_opcode_ISB(vm, RV_PRIV_FENCE, riscv_i_fence);
    riscv_install_opcode_ISB(vm, RV_PRIV_ZIFENCE_I, riscv_i_zifence);
    riscv_install_opcode_ISB(vm, RV_PRIV_ZICSR_CSRRW, riscv_zicsr_csrrw);
    riscv_install_opcode_ISB(vm, RV_PRIV_ZICSR_CSRRS, riscv_zicsr_csrrs);
    riscv_install_opcode_ISB(vm, RV_PRIV_ZICSR_CSRRC, riscv_zicsr_csrrc);
    riscv_install_opcode_ISB(vm, RV_PRIV_ZICSR_CSRRWI, riscv_zicsr_csrrwi);
    riscv_install_opcode_ISB(vm, RV_PRIV_ZICSR_CSRRSI, riscv_zicsr_csrrsi);
    riscv_install_opcode_ISB(vm, RV_PRIV_ZICSR_CSRRCI, riscv_zicsr_csrrci);
}
