/*
riscv_priv.c - RISC-V Privileged
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "atomics.h"
#include "bit_ops.h"

#include "riscv_cpu.h"
#include "riscv_csr.h"
#include "riscv_hart.h"
#include "riscv_mmu.h"
#include "riscv_priv.h"

// Precise instruction values for SYSTEM opcode decoding
#define RISCV_INSN_ECALL  0x00000073UL
#define RISCV_INSN_EBREAK 0x00100073UL
#define RISCV_INSN_SRET   0x10200073UL
#define RISCV_INSN_MRET   0x30200073UL
#define RISCV_INSN_WFI    0x10500073UL
#define RISCV_INSN_WRS_NT 0x00D00073UL
#define RISCV_INSN_WRS_ST 0x01D00073UL

// Privileged FENCE instructions mask and decoding
#define RISCV_SFENCE_MASK 0xFE007FFFUL
#define RISCV_SFENCE_VMA  0x12000073UL

// Precise instruction values for MISC_MEM opcode decoding
#define RISCV_INSN_PAUSE  0x0100000FUL

static forceinline bool riscv_mstatus_allowed(rvvm_hart_t* vm, const uint32_t flag)
{
    return (vm->priv_mode >= RISCV_PRIV_SUPERVISOR && !(vm->csr.status & flag)) //
        || vm->priv_mode == RISCV_PRIV_MACHINE;
}

slow_path void riscv_emulate_opc_system(rvvm_hart_t* vm, const uint32_t insn)
{
    switch (insn) {
        case RISCV_INSN_ECALL:
            riscv_trap(vm, RISCV_TRAP_ECALL_UMODE + vm->priv_mode, 0);
            return;
        case RISCV_INSN_EBREAK:
            riscv_breakpoint(vm);
            return;
        case RISCV_INSN_SRET:
            // Executing sret should trap in S-mode when mstatus.TSR is enabled
            if (likely(riscv_mstatus_allowed(vm, CSR_STATUS_TSR))) {
                uint8_t next_priv = bit_ext_u32(vm->csr.status, 8, 1);
                // Set SPP to U
                vm->csr.status = bit_replace(vm->csr.status, 8, 1, RISCV_PRIV_USER);
                // Set SIE to SPIE
                vm->csr.status = bit_replace(vm->csr.status, 1, 1, bit_ext_u32(vm->csr.status, 5, 1));
                // Set SPIE to 1
                vm->csr.status = bit_replace(vm->csr.status, 5, 1, 1);
                // Set PC to csr.sepc, counteract PC increment done by interpreter
                vm->registers[RISCV_REG_PC] = vm->csr.epc[RISCV_PRIV_SUPERVISOR] - 4;
                // Set privilege mode to SPP
                riscv_switch_priv(vm, next_priv);
                riscv_hart_check_interrupts(vm);
                return;
            }
            break;
        case RISCV_INSN_MRET:
            if (likely(vm->priv_mode == RISCV_PRIV_MACHINE)) {
                uint8_t next_priv = bit_ext_u32(vm->csr.status, 11, 2);
                if (next_priv < RISCV_PRIV_MACHINE) {
                    // Clear MPRV when returning to less privileged mode
                    vm->csr.status &= ~CSR_STATUS_MPRV;
                }
                // Set MPP to U
                vm->csr.status = bit_replace(vm->csr.status, 11, 2, RISCV_PRIV_USER);
                // Set MIE to MPIE
                vm->csr.status = bit_replace(vm->csr.status, 3, 1, bit_ext_u32(vm->csr.status, 7, 1));
                // Set MPIE to 1
                vm->csr.status = bit_replace(vm->csr.status, 7, 1, 1);
                // Set PC to csr.mepc, counteract PC increment done by interpreter
                vm->registers[RISCV_REG_PC] = vm->csr.epc[RISCV_PRIV_MACHINE] - 4;
                // Set privilege mode to MPP
                riscv_switch_priv(vm, next_priv);
                riscv_hart_check_interrupts(vm);
                return;
            }
            break;
        case RISCV_INSN_WFI:
            // Executing wfi should trap in S-mode when mstatus.TSR is enabled, and in U-mode
            if (likely(riscv_mstatus_allowed(vm, CSR_STATUS_TW))) {
                // Resume execution for locally enabled interrupts pending at any privilege level
#if defined(USE_THREAD_EMU)
                riscv_hart_check_timer(vm);
                if (!riscv_interrupts_pending(vm)) {
                    riscv_hart_pause(vm);
                }
#else
                if (!riscv_interrupts_pending(vm)) {
                    while (atomic_load_uint32_ex(&vm->running, ATOMIC_RELAXED)) {
                        // Stall the hart until an interrupt might need servicing
                        uint64_t delay = CONDVAR_INFINITE;
                        if (vm->csr.ie & (1U << RISCV_INTERRUPT_MTIMER)) {
                            delay = rvtimecmp_delay_ns(&vm->mtimecmp);
                        }
                        if (vm->csr.ie & (1U << RISCV_INTERRUPT_STIMER)) {
                            delay = EVAL_MIN(delay, rvtimecmp_delay_ns(&vm->stimecmp));
                        }
                        condvar_wait_ns(vm->wfi_cond, delay);

                        // Check timer expiration
                        riscv_hart_check_timer(vm);
                    }
                }
#endif
                return;
            }
            break;
        case RISCV_INSN_WRS_NT: // wrs.nto (Zawrs)
        case RISCV_INSN_WRS_ST: // wrs.sto (Zawrs)
            thread_sched_yield();
            return;
    }

    const size_t   rds    = bit_ext_u32(insn, 7, 5);
    const uint32_t funct3 = bit_ext_u32(insn, 12, 3);
    const size_t   rs1    = bit_ext_u32(insn, 15, 5);
    const uint32_t csr    = insn >> 20;

    switch (funct3) {
        case 0x00:
            switch (insn & RISCV_SFENCE_MASK) {
                case RISCV_SFENCE_VMA:
                    // Allow sfence.vma only when in S-mode and TVM isn't enabled, or more privileged
                    if (likely(riscv_mstatus_allowed(vm, CSR_STATUS_TVM))) {
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
        case 0x01: { // csrrw
            rvvm_uxlen_t val = vm->registers[rs1];
            if (riscv_csr_op(vm, csr, &val, CSR_SWAP, true)) {
                vm->registers[rds] = val;
                return;
            }
            break;
        }
        case 0x02: { // csrrs
            rvvm_uxlen_t val = vm->registers[rs1];
            if (riscv_csr_op(vm, csr, &val, CSR_SETBITS, rs1 != 0)) {
                vm->registers[rds] = val;
                return;
            }
            break;
        }
        case 0x03: { // csrrc
            rvvm_uxlen_t val = vm->registers[rs1];
            if (riscv_csr_op(vm, csr, &val, CSR_CLEARBITS, rs1 != 0)) {
                vm->registers[rds] = val;
                return;
            }
            break;
        }
        case 0x04:
            if ((insn & 0xB0000000UL) == 0x80000000UL) { // mop.r.{0,31}, mop.rr.{0,7} (Zimop)
                return;
            }
            break;
        case 0x05: { // csrrwi
            rvvm_uxlen_t val = bit_ext_u32(insn, 15, 5);
            if (riscv_csr_op(vm, csr, &val, CSR_SWAP, true)) {
                vm->registers[rds] = val;
                return;
            }
            break;
        }
        case 0x06: { // csrrsi
            rvvm_uxlen_t val = bit_ext_u32(insn, 15, 5);
            if (riscv_csr_op(vm, csr, &val, CSR_SETBITS, rs1 != 0)) {
                vm->registers[rds] = val;
                return;
            }
            break;
        }
        case 0x07: { // csrrci
            rvvm_uxlen_t val = bit_ext_u32(insn, 15, 5);
            if (riscv_csr_op(vm, csr, &val, CSR_CLEARBITS, rs1 != 0)) {
                vm->registers[rds] = val;
                return;
            }
            break;
        }
    }

    riscv_illegal_insn(vm, insn);
}

TSAN_SUPPRESS static forceinline void riscv_cbo_zero(rvvm_hart_t* vm, rvvm_addr_t vaddr)
{
    // ThreadSanitizer doesn't recognize fences between non-atomic memset and AMO
    void* ptr = riscv_rmw_translate(vm, vaddr & ~63ULL, NULL, 64);
    if (ptr) {
        memset(assume_aligned_ptr(ptr, 64), 0, 64);
    }
}

slow_path void riscv_emulate_opc_misc_mem(rvvm_hart_t* vm, const uint32_t insn)
{
    switch (bit_ext_u32(insn, 12, 3)) {
        case 0x00: // fence
            if (unlikely(insn == RISCV_INSN_PAUSE)) {
                // Pause hint, yield the vCPU thread
                thread_sched_yield();
            } else if (unlikely((insn & 0x05000000UL) && (insn & 0x00A00000UL))) {
                // StoreLoad fence needed (SEQ_CST)
                atomic_fence_ex(ATOMIC_SEQ_CST);
            } else {
                // LoadLoad, LoadStore, StoreStore fence (ACQ_REL)
                atomic_fence_ex(ATOMIC_ACQ_REL);
            }
            return;
        case 0x01: // fence.i
#ifdef USE_JIT
            if (rvvm_get_opt(vm->machine, RVVM_OPT_JIT_HARVARD)) {
                riscv_jit_flush_cache(vm);
            } else {
                // This eliminates possible dangling dirty blocks in JTLB
                riscv_jit_tlb_flush(vm);
            }
#endif
            return;
        case 0x02: {
            const size_t rds = bit_ext_u32(insn, 7, 5);
            const size_t rs1 = bit_ext_u32(insn, 15, 5);
            switch (insn >> 20) {
                case 0x00: // cbo.inval (Zicbom)
                    if (likely(!rds && riscv_csr_cbi_enabled(vm))) {
                        // Simply use a fence, all emulated devices are coherent
                        atomic_fence();
                        return;
                    }
                    break;
                case 0x01: // cbo.clean (Zicbom)
                case 0x02: // cbo.flush (Zicbom)
                    if (likely(!rds && riscv_csr_cbcf_enabled(vm))) {
                        // Simply use a fence, all emulated devices are coherent
                        atomic_fence();
                        return;
                    }
                    break;
                case 0x04: // cbo.zero (Zicboz)
                    if (likely(!rds && riscv_csr_cbz_enabled(vm))) {
                        riscv_cbo_zero(vm, vm->registers[rs1]);
                        return;
                    }
                    break;
            }
            break;
        }
    }

    riscv_illegal_insn(vm, insn);
}
