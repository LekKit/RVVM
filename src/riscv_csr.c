/*
riscv_csr.c - RISC-V Control and Status Registers
Copyright (C) 2021  LekKit <github.com/LekKit>
                    cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#include "riscv_csr.h"
#include "riscv_hart.h"
#include "riscv_mmu.h"
#include "riscv_cpu.h"

#ifdef USE_FPU
// For host FPU exception manipulation
#include <fenv.h>
#endif

riscv_csr_handler_t riscv_csr_list[4096];

#define CSR_MARCHID 0x5256564D // 'RVVM'

// no N extension, U_x bits are hardwired to 0
#define CSR_MSTATUS_MASK 0x7E79CC
#define CSR_SSTATUS_MASK 0x0C6122

#define CSR_STATUS_FS_MASK 0x6000

#define CSR_MEIP_MASK    0xAAA
#define CSR_SEIP_MASK    0x222

static inline void csr_helper(maxlen_t* csr, maxlen_t* dest, uint8_t op)
{
    maxlen_t tmp = *csr;
    switch (op) {
    case CSR_SWAP:
        *csr = *dest;
        break;
    case CSR_SETBITS:
        *csr |= *dest;
        break;
    case CSR_CLEARBITS:
        *csr &= (~*dest);
        break;
    }
    *dest = tmp;
}

static inline void csr_helper_masked(maxlen_t* csr, maxlen_t* dest, maxlen_t mask, uint8_t op)
{
    maxlen_t tmp = *csr;
    switch (op) {
    case CSR_SWAP:
        *csr &= (~mask);
        *csr |= (*dest & mask);
        break;
    case CSR_SETBITS:
        *csr |= (*dest & mask);
        break;
    case CSR_CLEARBITS:
        *csr &= (~(*dest & mask));
        break;
    }
    *dest = tmp & mask;
}

static inline void csr_status_helper(rvvm_hart_t* vm, maxlen_t* dest, maxlen_t mask, uint8_t op)
{
#ifdef USE_FPU
    bool fpu_was_enabled = bit_cut(vm->csr.status, 13, 2) != FS_OFF;
#else
    mask = bit_replace(mask, 13, 2, 0);
#endif
#ifdef USE_RV64
    if (vm->rv64 && unlikely(bit_cut(*dest, 32, 6) ^ (bit_cut(*dest, 32, 6) >> 1))) {
        // Changed XLEN somewhere
        if (bit_cut(*dest, 32, 2) ^ (bit_cut(*dest, 32, 2) >> 1)) {
            mask |= 0x300000000ULL;
        }
        if (bit_cut(*dest, 34, 2) ^ (bit_cut(*dest, 34, 2) >> 1)) {
            mask |= 0xC00000000ULL;
        }
        if (bit_cut(*dest, 36, 2) ^ (bit_cut(*dest, 36, 2) >> 1)) {
            mask |= 0x3000000000ULL;
        }
    }
#endif
    csr_helper_masked(&vm->csr.status, dest, mask, op);
#ifdef USE_RV64
    if (vm->rv64) *dest |= vm->csr.status & 0x3F00000000ULL;
#endif
#ifdef USE_FPU
    uint8_t new_fs = bit_cut(vm->csr.status, 13, 2);
    bool fpu_enabled = new_fs != FS_OFF;
    if (fpu_was_enabled != fpu_enabled) {
        riscv_decoder_enable_fpu(vm, fpu_enabled);
    #ifndef USE_PRECISE_FS
        if (fpu_enabled) {
            vm->csr.status = bit_replace(vm->csr.status, 13, 2, FS_DIRTY);
        }
    #endif
    }
    // Set SD bit
    if (bit_cut(vm->csr.status, 13, 2) == FS_DIRTY) {
    #ifdef USE_RV64
        if (vm->rv64) {
            *dest |= 0x8000000000000000ULL;
        } else {
            *dest |= 0x80000000U;
        }
    #else
        *dest |= 0x80000000U;
    #endif
    }
#endif
#ifdef USE_RV64
    riscv_update_xlen(vm);
#endif
}

static bool riscv_csr_illegal(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    UNUSED(vm);
    UNUSED(dest);
    UNUSED(op);
    return false;
}

// For zero-hardwired registers we did not yet implement
static bool riscv_csr_zero(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    UNUSED(vm);
    UNUSED(op);
    *dest = 0;
    return true;
}


/*
 * Machine CSRs
 */

static uint32_t riscv_mkmisa(const char* str)
{
    uint32_t ret = 0;
    while (*str) {
        ret |= (1 << (*str - 'A'));
        str++;
    }
    return ret;
}

static bool riscv_csr_marchid(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    UNUSED(vm);
    UNUSED(op);
    *dest = CSR_MARCHID;
    return true;
}

static bool riscv_csr_mhartid(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    UNUSED(vm);
    UNUSED(op);
    *dest = vm->csr.hartid;
    return true;
}

static bool riscv_csr_mstatus(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_status_helper(vm, dest, CSR_MSTATUS_MASK, op);
    return true;
}

static bool riscv_csr_misa(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    UNUSED(op);
#ifdef USE_RV64
    if (vm->rv64 && (*dest & CSR_MISA_RV32)) {
        vm->csr.isa &= (~CSR_MISA_RV64);
        vm->csr.isa |= CSR_MISA_RV32;
        riscv_update_xlen(vm);
    } else if (!vm->rv64 && (*dest & (CSR_MISA_RV64 >> 32))) {
        vm->csr.isa &= (~CSR_MISA_RV32);
        vm->csr.isa |= CSR_MISA_RV64;
        riscv_update_xlen(vm);
    }
#endif
#ifdef USE_FPU
    *dest = vm->csr.isa | riscv_mkmisa("IMAFDCSU");
#else
    *dest = vm->csr.isa | riscv_mkmisa("IMACSU");
#endif
    return true;
}

static bool riscv_csr_medeleg(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper(&vm->csr.edeleg[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv_csr_mideleg(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper(&vm->csr.ideleg[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv_csr_mie(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper_masked(&vm->csr.ie, dest, CSR_MEIP_MASK, op);
    // handle possible interrupts?
    riscv_restart_dispatch(vm);
    return true;
}

static bool riscv_csr_mtvec(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper(&vm->csr.tvec[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv_csr_mscratch(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper(&vm->csr.scratch[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv_csr_mepc(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper(&vm->csr.epc[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv_csr_mcause(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper(&vm->csr.cause[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv_csr_mtval(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper(&vm->csr.tval[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv_csr_mip(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper_masked(&vm->csr.ip, dest, CSR_MEIP_MASK, op);
    // handle possible interrupts?
    riscv_restart_dispatch(vm);
    return true;
}

/*
 * Supervisor CSRs
 */

static bool riscv_csr_sstatus(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_status_helper(vm, dest, CSR_SSTATUS_MASK, op);
    return true;
}

static bool riscv_csr_sie(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper_masked(&vm->csr.ie, dest, CSR_SEIP_MASK, op);
    // handle possible interrupts?
    riscv_restart_dispatch(vm);
    return true;
}

static bool riscv_csr_stvec(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper(&vm->csr.tvec[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv_csr_sscratch(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper(&vm->csr.scratch[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv_csr_sepc(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper(&vm->csr.epc[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv_csr_scause(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper(&vm->csr.cause[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv_csr_stval(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper(&vm->csr.tval[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv_csr_sip(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    csr_helper_masked(&vm->csr.ip, dest, CSR_SEIP_MASK, op);
    // handle possible interrupts?
    riscv_restart_dispatch(vm);
    return true;
}

static bool riscv_csr_satp(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    uint8_t prev_mmu = vm->mmu_mode;
#ifdef USE_RV64
    if (vm->rv64) {
        maxlen_t satp = (((maxlen_t)vm->mmu_mode) << 60) | (vm->root_page_table >> PAGE_SHIFT);
        csr_helper(&satp, dest, op);
        vm->mmu_mode = satp >> 60;
        vm->root_page_table = (satp & bit_mask(44)) << PAGE_SHIFT;
    } else {
#endif
        maxlen_t satp = (((maxlen_t)vm->mmu_mode) << 31) | (vm->root_page_table >> PAGE_SHIFT);
        csr_helper(&satp, dest, op);
        vm->mmu_mode = satp >> 31;
        vm->root_page_table = (satp & bit_mask(22)) << PAGE_SHIFT;
#ifdef USE_RV64
    }
#endif
    /*
    * We currently cache physical addresses in TLB as well, so switching
    * between bare/virtual modes will pollute the address space with illegal entries
    * Hence, a TLB flush is required on switch
    */
    if (!!vm->mmu_mode != !!prev_mmu) riscv_tlb_flush(vm);
    return true;
}

/*
 * Userspace CSRs
 */

#ifdef USE_FPU

static int fpu_get_exceptions()
{
    int ret = 0;
    int exc = fetestexcept(FE_ALL_EXCEPT);
    if (exc & FE_INEXACT)   ret |= FFLAG_NX;
    if (exc & FE_UNDERFLOW) ret |= FFLAG_UF;
    if (exc & FE_OVERFLOW)  ret |= FFLAG_OF;
    if (exc & FE_DIVBYZERO) ret |= FFLAG_DZ;
    if (exc & FE_INVALID)   ret |= FFLAG_NV;
    return ret;
}

static void fpu_set_exceptions(uint32_t flags)
{
    int exc = 0;
    feclearexcept(FE_ALL_EXCEPT);
    if (flags & FFLAG_NX) exc |= FE_INEXACT;
    if (flags & FFLAG_UF) exc |= FE_UNDERFLOW;
    if (flags & FFLAG_OF) exc |= FE_OVERFLOW;
    if (flags & FFLAG_DZ) exc |= FE_DIVBYZERO;
    if (flags & FFLAG_NV) exc |= FE_INVALID;
    if (exc != 0) feraiseexcept(exc);
}

uint8_t fpu_set_rm(rvvm_hart_t* vm, uint8_t newrm)
{
    if (likely(newrm == RM_DYN)) {
        /* do nothing - rounding mode should be already set with csr */
        return RM_DYN;
    }

    switch (newrm) {
        case RM_RNE:
            fesetround(FE_TONEAREST);
            break;
        case RM_RTZ:
            fesetround(FE_TOWARDZERO);
            break;
        case RM_RDN:
            fesetround(FE_DOWNWARD);
            break;
        case RM_RUP:
            fesetround(FE_UPWARD);
            break;
        case RM_RMM:
            /* TODO: handle this somehow? */
            fesetround(FE_TONEAREST);
            break;
        default:
            return RM_INVALID;
    }

    uint8_t oldrm = bit_cut(vm->csr.fcsr, 5, 3);
    if (unlikely(oldrm > RM_RMM)) {
        return RM_INVALID;
    }
    return oldrm;
}

static bool riscv_csr_fflags(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    if (!fpu_is_enabled(vm)) {
        return false;
    }
    maxlen_t val = fpu_get_exceptions();
    maxlen_t oldval = val;
    csr_helper(&val, dest, op);
    if (val != oldval) fpu_set_fs(vm, FS_DIRTY);
    fpu_set_exceptions(val);
    vm->csr.fcsr &= ~((1 << 5) - 1);
    vm->csr.fcsr |= val;
    vm->csr.fcsr &= 0xff;
    *dest &= 0x1f;
    return true;
}

static bool riscv_csr_frm(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    if (!fpu_is_enabled(vm)) {
        return false;
    }
    maxlen_t val = vm->csr.fcsr >> 5;
    maxlen_t oldval = val;
    csr_helper(&val, dest, op);
    if (val != oldval) fpu_set_fs(vm, FS_DIRTY);
    fpu_set_rm(vm, val & ((1 << 3) - 1));
    vm->csr.fcsr = (vm->csr.fcsr & ((1 << 5) - 1)) | (val << 5);
    vm->csr.fcsr &= 0xff;
    *dest &= 0x7;
    return true;
}

static bool riscv_csr_fcsr(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    if (!fpu_is_enabled(vm)) {
        return false;
    }
    maxlen_t val = vm->csr.fcsr | fpu_get_exceptions();
    maxlen_t oldval = val;
    csr_helper(&val, dest, op);
    if (val != oldval) fpu_set_fs(vm, FS_DIRTY);
    fpu_set_rm(vm, bit_cut(val, 5, 3));
    fpu_set_exceptions(val);
    vm->csr.fcsr = val;
    vm->csr.fcsr &= 0xff;
    *dest &= 0xff;
    return true;
}

#endif

static bool riscv_csr_time(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    UNUSED(op);
    rvtimer_update(&vm->timer);
    *dest = vm->timer.time;
    return true;
}

static bool riscv_csr_timeh(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    UNUSED(op);
    if (vm->rv64) return false;
    *dest = vm->timer.time >> 32;
    return true;
}

static bool riscv_csr_debug(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    UNUSED(op);
    printf("Terminated with code: %d\n", (*dest) >> 1);
    exit(0);
    return true;
}

void riscv_csr_global_init()
{
    for (size_t i=0; i<4096; ++i) riscv_csr_list[i] = riscv_csr_illegal;
riscv_csr_list[0x800] = riscv_csr_debug;
    // Machine Information Registers
    riscv_csr_list[0xF11] = riscv_csr_zero;     // mvendorid
    riscv_csr_list[0xF12] = riscv_csr_marchid;  // marchid
    riscv_csr_list[0xF13] = riscv_csr_marchid;  // mimpid
    riscv_csr_list[0xF14] = riscv_csr_mhartid;  // mhartid

    // Machine Trap Setup
    riscv_csr_list[0x300] = riscv_csr_mstatus;  // mstatus
    riscv_csr_list[0x301] = riscv_csr_misa;     // misa
    riscv_csr_list[0x302] = riscv_csr_medeleg;  // medeleg
    riscv_csr_list[0x303] = riscv_csr_mideleg;  // mideleg
    riscv_csr_list[0x304] = riscv_csr_mie;      // mie
    riscv_csr_list[0x305] = riscv_csr_mtvec;    // mtvec
    riscv_csr_list[0x306] = riscv_csr_zero;     // mcounteren

    // Machine Trap Handling
    riscv_csr_list[0x340] = riscv_csr_mscratch; // mscratch
    riscv_csr_list[0x341] = riscv_csr_mepc;     // mepc
    riscv_csr_list[0x342] = riscv_csr_mcause;   // mcause
    riscv_csr_list[0x343] = riscv_csr_mtval;    // mtval
    riscv_csr_list[0x344] = riscv_csr_mip;      // mip

    // Machine Memory Protection
    for (size_t i=0x3A0; i<0x3A4; ++i)
        riscv_csr_list[i] = riscv_csr_zero;     // pmpcfg
    for (size_t i=0x3B0; i<0x3C0; ++i)
        riscv_csr_list[i] = riscv_csr_zero;     // pmpaddr

    // Machine Counter/Timers
    riscv_csr_list[0xB00] = riscv_csr_zero;     // mcycle
    riscv_csr_list[0xB02] = riscv_csr_zero;     // minstret
    riscv_csr_list[0xB80] = riscv_csr_zero;     // mcycleh
    riscv_csr_list[0xB82] = riscv_csr_zero;     // minstreth
    for (size_t i=0xB03; i<0xB20; ++i)
        riscv_csr_list[i] = riscv_csr_zero;     // mhpmcounter
    for (size_t i=0xB83; i<0xBA0; ++i)
        riscv_csr_list[i] = riscv_csr_zero;     // mhpmcounterh

    // Machine Counter Setup
    riscv_csr_list[0x320] = riscv_csr_zero;     // mcountinhibit
    for (size_t i=0x323; i<0x340; ++i)
        riscv_csr_list[i] = riscv_csr_zero;     // mhpmevent



    // Supervisor Trap Setup
    riscv_csr_list[0x100] = riscv_csr_sstatus;  // sstatus
    riscv_csr_list[0x102] = riscv_csr_illegal;  // sedeleg
    riscv_csr_list[0x103] = riscv_csr_illegal;  // sideleg
    riscv_csr_list[0x104] = riscv_csr_sie;      // sie
    riscv_csr_list[0x105] = riscv_csr_stvec;    // stvec
    riscv_csr_list[0x106] = riscv_csr_zero;     // scounteren

    // Supervisor Trap Handling
    riscv_csr_list[0x140] = riscv_csr_sscratch; // sscratch
    riscv_csr_list[0x141] = riscv_csr_sepc;     // sepc
    riscv_csr_list[0x142] = riscv_csr_scause;   // scause
    riscv_csr_list[0x143] = riscv_csr_stval;    // stval
    riscv_csr_list[0x144] = riscv_csr_sip;      // sip

    // Supervisor Protection and Translation
    riscv_csr_list[0x180] = riscv_csr_satp;     // satp



    // User Trap Setup
    riscv_csr_list[0x000] = riscv_csr_illegal;  // ustatus
    riscv_csr_list[0x004] = riscv_csr_illegal;  // uie
    riscv_csr_list[0x005] = riscv_csr_illegal;  // utvec

    // User Trap Handling
    riscv_csr_list[0x040] = riscv_csr_illegal;  // uscratch
    riscv_csr_list[0x041] = riscv_csr_illegal;  // uepc
    riscv_csr_list[0x042] = riscv_csr_illegal;  // ucause
    riscv_csr_list[0x043] = riscv_csr_illegal;  // utval
    riscv_csr_list[0x044] = riscv_csr_illegal;  // uip

#ifdef USE_FPU
    // User Floating-Point CSRs
    riscv_csr_list[0x001] = riscv_csr_fflags;   // fflags
    riscv_csr_list[0x002] = riscv_csr_frm;      // frm
    riscv_csr_list[0x003] = riscv_csr_fcsr;     // fcsr
#endif

    // User Counter/Timers
    //riscv_csr_list[0xC00] = riscv_csr_zero;     // cycle
    riscv_csr_list[0xC01] = riscv_csr_time;     // time
    //riscv_csr_list[0xC02] = riscv_csr_zero;     // instret
    //riscv_csr_list[0xC80] = riscv_csr_zero;     // cycleh
    riscv_csr_list[0xC81] = riscv_csr_timeh;    // timeh
    //riscv_csr_list[0xC82] = riscv_csr_zero;     // instreth

    for (size_t i=0xC03; i<0xC20; ++i)
        riscv_csr_list[i] = riscv_csr_zero;     // hpmcounter
    for (size_t i=0xC83; i<0xCA0; ++i)
        riscv_csr_list[i] = riscv_csr_zero;     // hpmcounterh
}
