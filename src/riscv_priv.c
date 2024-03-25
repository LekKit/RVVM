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

// Precise instruction values for SYSTEM opcode decoding
#define RV_PRIV_S_ECALL       0x73
#define RV_PRIV_S_EBREAK      0x100073
#define RV_PRIV_S_SRET        0x10200073
#define RV_PRIV_S_MRET        0x30200073
#define RV_PRIV_S_WFI         0x10500073

// Privileged FENCE instructions mask and decoding
#define RV_PRIV_S_FENCE_MASK  0xFE007FFF
#define RV_PRIV_S_SFENCE_VMA  0x12000073
#define RV_PRIV_S_HFENCE_BVMA 0x22000073
#define RV_PRIV_S_HFENCE_GVMA 0xA2000073

void riscv_emulate_opc_system(rvvm_hart_t* vm, const uint32_t insn)
{
    switch (insn) {
        case RV_PRIV_S_ECALL:
            riscv_trap(vm, TRAP_ENVCALL_UMODE + vm->priv_mode, 0);
            return;
        case RV_PRIV_S_EBREAK:
            riscv_trap(vm, TRAP_BREAKPOINT, 0);
            return;
        case RV_PRIV_S_SRET:
            // Allow sret only when in S-mode or more privileged, and TSR isn't enabled
            if (vm->priv_mode >= PRIVILEGE_SUPERVISOR && !(vm->csr.status & CSR_STATUS_TSR)) {
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
                return;
            }
            break;
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
                return;
            }
            break;
        case RV_PRIV_S_WFI:
            // Resume execution for locally enabled interrupts pending at any privilege level
            if (!(vm->csr.ip & vm->csr.ie)) {
                // Stall the hart until an interrupt might need servicing
                while (atomic_load_uint32(&vm->wait_event)) {
                    if (vm->csr.ie & (1 << INTERRUPT_MTIMER)) {
                        // Calculate sleep period
                        uint64_t timestamp = rvtimer_get(&vm->timer);
                        if (vm->timer.timecmp > timestamp) {
                            timestamp = (vm->timer.timecmp - timestamp) * 1000000000 / vm->timer.freq;
                            condvar_wait_ns(vm->wfi_cond, timestamp);
                        }
                        // Hint interrupt dispatcher to check actual timer expiration
                        vm->csr.ip |= (1 << INTERRUPT_MTIMER);
                        break;
                    } else {
                        // Timer IRQs disabled, wait for external IRQs
                        condvar_wait(vm->wfi_cond, CONDVAR_INFINITE);
                    }
                }
            }
            riscv_restart_dispatch(vm);
            return;
    }

    const regid_t rds = bit_cut(insn, 7, 5);
    const uint32_t funct3 = bit_cut(insn, 12, 3);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const uint32_t csr = insn >> 20;

    switch (funct3) {
        case 0x0:
            switch (insn & RV_PRIV_S_FENCE_MASK) {
                case RV_PRIV_S_SFENCE_VMA:
                    // Allow sfence.vma only when in S-mode or more privileged, and TVM isn't enabled
                    if (vm->priv_mode >= PRIVILEGE_SUPERVISOR && !(vm->csr.status & CSR_STATUS_TVM)) {
                        if (rs1) {
                            riscv_tlb_flush_page(vm, vm->registers[rs1]);
                        } else {
                            riscv_tlb_flush(vm);
                        }
                        return;
                    }
                    break;
            }
            break;
        case 0x1: { // csrrw
            maxlen_t val = vm->registers[rs1];
            if (riscv_csr_op(vm, csr, &val, CSR_SWAP)) {
                vm->registers[rds] = val;
                return;
            }
            break;
        }
        case 0x2: { // csrrs
            maxlen_t val = vm->registers[rs1];
            if (riscv_csr_op(vm, csr, &val, CSR_SETBITS)) {
                vm->registers[rds] = val;
                return;
            }
            break;
        }
        case 0x3: { // csrrc
            maxlen_t val = vm->registers[rs1];
            if (riscv_csr_op(vm, csr, &val, CSR_CLEARBITS)) {
                vm->registers[rds] = val;
                return;
            }
            break;
        }
        case 0x5: { // csrrwi
            maxlen_t val = bit_cut(insn, 15, 5);
            if (riscv_csr_op(vm, csr, &val, CSR_SWAP)) {
                vm->registers[rds] = val;
                return;
            }
            break;
        }
        case 0x6: { // csrrsi
            maxlen_t val = bit_cut(insn, 15, 5);
            if (riscv_csr_op(vm, csr, &val, CSR_SETBITS)) {
                vm->registers[rds] = val;
                return;
            }
            break;
        }
        case 0x7: { // csrrci
            maxlen_t val = bit_cut(insn, 15, 5);
            if (riscv_csr_op(vm, csr, &val, CSR_CLEARBITS)) {
                vm->registers[rds] = val;
                return;
            }
            break;
        }
    }

    riscv_illegal_insn(vm, insn);
}

void riscv_emulate_opc_misc_mem(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t funct3 = bit_cut(insn, 12, 3);
    switch (funct3) {
        case 0x0: // fence
            atomic_fence();
            return;
        case 0x1: // fence.i
#ifdef USE_JIT
            if (rvvm_get_opt(vm->machine, RVVM_OPT_JIT_HARWARD)) {
                riscv_jit_flush_cache(vm);
            } else {
                // This eliminates possible dangling dirty blocks in JTLB
                riscv_jit_tlb_flush(vm);
            }
#endif
            return;
    }
    riscv_illegal_insn(vm, insn);
}
