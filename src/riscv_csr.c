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
#include "fpu_ops.h"
#endif

// Get RVVM commit in a mimpid hex form
static uint32_t rvvm_mimpid(void)
{
    const char* version_string = RVVM_VERSION;
    const char* dash = rvvm_strfind(version_string, "-");
    if (dash) {
        uint32_t commit_hex = str_to_uint_base(dash + 1, NULL, 16) << 4;
        if (rvvm_strfind(version_string, "dirty")) {
            commit_hex |= 0xD;
        }
        return commit_hex;
    }
    return 0;
}

// Make a misa CSR value from ISA string
static uint64_t riscv_mkmisa(const char* str)
{
    uint64_t ret = 0;
    if (rvvm_strfind(str, "rv64")) {
        ret |= CSR_MISA_RV64;
        str += 4;
    } else if (rvvm_strfind(str, "rv32")) {
        ret |= CSR_MISA_RV32;
        str += 4;
    }
    while (*str && *str != '_') {
        if (*str >= 'a' && *str <= 'z') {
            ret |= (1 << (*str - 'a'));
        }
        str++;
    }
    return ret;
}

static inline bool riscv_csr_helper_masked(maxlen_t* csr, maxlen_t* dest, maxlen_t mask, uint8_t op)
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
    return true;
}

static inline bool riscv_csr_helper(maxlen_t* csr, maxlen_t* dest, uint8_t op)
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
            *csr &= ~(*dest);
            break;
    }
    *dest = tmp;
    return true;
}

static inline bool riscv_csr_helper_l(rvvm_hart_t* vm, uint64_t* csr, maxlen_t* dest, uint64_t mask, uint8_t op)
{
    maxlen_t tmp = *csr;
    riscv_csr_helper_masked(&tmp, dest, mask, op);
    if (vm->rv64) {
        *csr = tmp;
    } else {
        *csr = bit_replace(*csr, 0, 32, tmp);
    }
    return true;
}

static inline bool riscv_csr_helper_h(rvvm_hart_t* vm, uint64_t* csr, maxlen_t* dest, uint64_t mask, uint8_t op)
{
    if (!vm->rv64) {
        maxlen_t tmp = (*csr >> 32);
        riscv_csr_helper_masked(&tmp, dest, mask >> 32, op);
        *csr = bit_replace(*csr, 32, 32, tmp);
        return true;
    }
    return false;
}

static inline bool riscv_csr_const(maxlen_t* dest, maxlen_t val)
{
    *dest = val;
    return true;
}

static inline bool riscv_csr_zero(maxlen_t* dest)
{
    return riscv_csr_const(dest, 0);
}

static inline bool riscv_csr_zero_h(rvvm_hart_t* vm, maxlen_t* dest)
{
    if (!vm->rv64) {
        return riscv_csr_const(dest, 0);
    }
    return false;
}

static inline bool riscv_csr_time(rvvm_hart_t* vm, maxlen_t* dest)
{
    if (riscv_csr_timer_enabled(vm)) {
        return riscv_csr_const(dest, rvtimer_get(&vm->machine->timer));
    }
    return false;
}

static inline bool riscv_csr_timeh(rvvm_hart_t* vm, maxlen_t* dest)
{
    if (!vm->rv64 && riscv_csr_timer_enabled(vm)) {
        return riscv_csr_const(dest, rvtimer_get(&vm->machine->timer) >> 32);
    }
    return false;
}

static inline bool riscv_csr_seed(rvvm_hart_t* vm, maxlen_t* dest)
{
    if (riscv_csr_seed_enabled(vm)) {
        uint16_t seed = 0;
        rvvm_randombytes(&seed, sizeof(seed));
        return riscv_csr_const(dest, seed);
    }
    return false;
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
    *dest = vm->csr.isa | riscv_mkmisa("imafdcbsu");
#else
    *dest = vm->csr.isa | riscv_mkmisa("imacbsu");
#endif
    return true;
}

static bool riscv_csr_status(rvvm_hart_t* vm, maxlen_t* dest, maxlen_t mask, uint8_t op)
{
    maxlen_t new_status = *dest;
#ifdef USE_FPU
#ifndef USE_PRECISE_FS
    bool fpu_was_enabled = bit_cut(vm->csr.status, 13, 2) != FS_OFF;
    if (fpu_was_enabled) {
        vm->csr.status = bit_replace(vm->csr.status, 13, 2, FS_DIRTY);
    }
#endif
    maxlen_t sd_mask = 0x80000000U;
#ifdef USE_RV64
    if (vm->rv64) {
        sd_mask = 0x8000000000000000ULL;
    }
#endif
    mask |= sd_mask;

    // Set SD bit
    if (bit_cut(vm->csr.status, 13, 2) == FS_DIRTY) {
        vm->csr.status |= sd_mask;
    } else {
        vm->csr.status &= ~sd_mask;
    }
#else
    mask = bit_replace(mask, 13, 2, 0);
#endif

#ifdef USE_RV64
    if (vm->rv64 && unlikely(bit_cut(new_status, 32, 6) ^ (bit_cut(new_status, 32, 6) >> 1))) {
        // Changed XLEN somewhere
        if (bit_cut(new_status, 32, 2) ^ (bit_cut(new_status, 32, 2) >> 1)) {
            mask |= 0x300000000ULL;
        }
        if (bit_cut(new_status, 34, 2) ^ (bit_cut(new_status, 34, 2) >> 1)) {
            mask |= 0xC00000000ULL;
        }
        if (bit_cut(new_status, 36, 2) ^ (bit_cut(new_status, 36, 2) >> 1)) {
            mask |= 0x3000000000ULL;
        }
        riscv_update_xlen(vm);
    }
#endif
    riscv_csr_helper_masked(&vm->csr.status, dest, mask, op);
    maxlen_t old_status = *dest;
#ifdef USE_RV64
    if (vm->rv64) *dest |= vm->csr.status & 0x3F00000000ULL;
#endif
    if (bit_cut(new_status, 0, 4) & ~bit_cut(old_status, 0, 4)) {
        // IRQ enable bits were set
        riscv_hart_check_interrupts(vm);
    }
    return true;
}

static inline bool riscv_csr_ie(rvvm_hart_t* vm, maxlen_t* dest, maxlen_t mask, uint8_t op)
{
    riscv_csr_helper_masked(&vm->csr.ie, dest, mask, op);
    riscv_hart_check_interrupts(vm);
    return true;
}

static inline bool riscv_csr_ip(rvvm_hart_t* vm, maxlen_t* dest, maxlen_t mask, uint8_t op)
{
    riscv_csr_helper_masked(&vm->csr.ip, dest, mask, op);
    *dest |= (riscv_interrupts_raised(vm) & mask);
    riscv_hart_check_interrupts(vm);
    return true;
}

static void riscv_csr_stimecmp_set(rvvm_hart_t* vm, uint64_t stimecmp)
{
    rvtimecmp_set(&vm->stimecmp, stimecmp);
    if (rvtimecmp_pending(&vm->stimecmp)) {
        riscv_interrupt(vm, INTERRUPT_STIMER);
    } else {
        riscv_interrupt_clear(vm, INTERRUPT_STIMER);
    }
}

static inline bool riscv_csr_stimecmp(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    if (riscv_csr_sstc_enabled(vm)) {
        uint64_t stimecmp = rvtimecmp_get(&vm->stimecmp);
        riscv_csr_helper_l(vm, &stimecmp, dest, -1ULL, op);
        riscv_csr_stimecmp_set(vm, stimecmp);
        return true;
    }
    return false;
}

static inline bool riscv_csr_stimecmph(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    if (!vm->rv64 && riscv_csr_sstc_enabled(vm)) {
        uint64_t stimecmp = rvtimecmp_get(&vm->stimecmp);
        riscv_csr_helper_h(vm, &stimecmp, dest, -1ULL, op);
        riscv_csr_stimecmp_set(vm, stimecmp);
        return true;
    }
    return false;
}

static bool riscv_csr_satp(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op)
{
    uint8_t prev_mmu = vm->mmu_mode;
    if (vm->csr.status & CSR_STATUS_TVM) return false; // TVM should trap on acces to satp
#ifdef USE_RV64
    if (vm->rv64) {
        maxlen_t satp = (((maxlen_t)vm->mmu_mode) << 60) | (vm->root_page_table >> MMU_PAGE_SHIFT);
        riscv_csr_helper(&satp, dest, op);
        vm->mmu_mode = satp >> 60;
        if (vm->mmu_mode < CSR_SATP_MODE_SV39
         || vm->mmu_mode > CSR_SATP_MODE_SV57
         || (vm->mmu_mode == CSR_SATP_MODE_SV48 && !rvvm_has_arg("sv48"))
         || (vm->mmu_mode == CSR_SATP_MODE_SV57 && !rvvm_has_arg("sv57"))) {
            vm->mmu_mode = CSR_SATP_MODE_PHYS;
        }
        vm->root_page_table = (satp & bit_mask(44)) << MMU_PAGE_SHIFT;
    } else {
#endif
        maxlen_t satp = (((maxlen_t)vm->mmu_mode) << 31) | (vm->root_page_table >> MMU_PAGE_SHIFT);
        riscv_csr_helper(&satp, dest, op);
        vm->mmu_mode = satp >> 31;
        vm->root_page_table = (satp & bit_mask(22)) << MMU_PAGE_SHIFT;
#ifdef USE_RV64
    }
#endif
    /*
    * We currently cache physical addresses in TLB as well, so switching
    * between bare/virtual modes will pollute the address space with illegal entries
    * Hence, a TLB flush is required on MMU switch
    */
    if (!!vm->mmu_mode != !!prev_mmu) riscv_tlb_flush(vm);
    return true;
}

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
    riscv_csr_helper(&val, dest, op);
    if (val != oldval) {
        fpu_set_fs(vm, FS_DIRTY);
        fpu_set_exceptions(val);
    }
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
    riscv_csr_helper(&val, dest, op);
    if (val != oldval) {
        fpu_set_fs(vm, FS_DIRTY);
        fpu_set_rm(vm, val & ((1 << 3) - 1));
    }
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
    riscv_csr_helper(&val, dest, op);
    if (val != oldval) {
        fpu_set_fs(vm, FS_DIRTY);
        fpu_set_rm(vm, bit_cut(val, 5, 3));
        fpu_set_exceptions(val);
    }
    vm->csr.fcsr = val;
    vm->csr.fcsr &= 0xff;
    *dest &= 0xff;
    return true;
}

#endif

static forceinline bool riscv_csr_op_internal(rvvm_hart_t* vm, uint32_t csr_id, maxlen_t* dest, uint8_t op)
{
    switch (csr_id) {
#ifdef USE_FPU
        // User Floating-Point CSRs
        case CSR_FFLAGS:
            return riscv_csr_fflags(vm, dest, op);
        case CSR_FRM:
            return riscv_csr_frm(vm, dest, op);
        case CSR_FCSR:
            return riscv_csr_fcsr(vm, dest, op);
#endif

        // Unprivileged Entropy Source CSR
        case CSR_SEED:
            return riscv_csr_seed(vm, dest);

        // User Counters / Timers
        case CSR_CYCLE:
            return riscv_csr_zero(dest);
        case CSR_CYCLEH:
            return riscv_csr_zero_h(vm, dest);
        case CSR_TIME:
            return riscv_csr_time(vm, dest);
        case CSR_TIMEH:
            return riscv_csr_timeh(vm, dest);
        case CSR_INSTRET:
            return riscv_csr_zero(dest);
        case CSR_INSTRETH:
            return riscv_csr_zero_h(vm, dest);

        // Supervisor Trap Setup
        case CSR_SSTATUS:
            return riscv_csr_status(vm, dest, CSR_SSTATUS_MASK, op);
        case CSR_SIE:
            return riscv_csr_ie(vm, dest, CSR_SEIP_MASK, op);
        case CSR_STVEC:
            return riscv_csr_helper(&vm->csr.tvec[PRIVILEGE_SUPERVISOR], dest, op);
        case CSR_SCOUNTEREN:
            return riscv_csr_helper_masked(&vm->csr.counteren[PRIVILEGE_SUPERVISOR], dest, CSR_COUNTEREN_MASK, op);

        // Supervisor Configuration
        case CSR_SENVCFG:
            return riscv_csr_helper_l(vm, &vm->csr.envcfg[PRIVILEGE_SUPERVISOR], dest, CSR_SENVCFG_MASK, op);

        // Supervisor Trap Handling
        case CSR_SSCRATCH:
            return riscv_csr_helper(&vm->csr.scratch[PRIVILEGE_SUPERVISOR], dest, op);
        case CSR_SEPC:
            return riscv_csr_helper(&vm->csr.epc[PRIVILEGE_SUPERVISOR], dest, op);
        case CSR_SCAUSE:
            return riscv_csr_helper(&vm->csr.cause[PRIVILEGE_SUPERVISOR], dest, op);
        case CSR_STVAL:
            return riscv_csr_helper(&vm->csr.tval[PRIVILEGE_SUPERVISOR], dest, op);
        case CSR_SIP:
            return riscv_csr_ip(vm, dest, CSR_SEIP_MASK, op);
        case CSR_STIMECMP:
            return riscv_csr_stimecmp(vm, dest, op);
        case CSR_STIMECMPH:
            return riscv_csr_stimecmph(vm, dest, op);

        // Supervisor Protection and Translation
        case CSR_SATP:
            return riscv_csr_satp(vm, dest, op);

        // Machine Information Registers
        case CSR_MVENDORID:
            return riscv_csr_zero(dest); // Not a commercial implementation
        case CSR_MARCHID:
            return riscv_csr_const(dest, 0x5256564D); // 'RVVM' in hex
        case CSR_MIMPID:
            return riscv_csr_const(dest, rvvm_mimpid());
        case CSR_MHARTID:
            return riscv_csr_const(dest, vm->csr.hartid);

        // Machine Trap Setup
        case CSR_MSTATUS:
            return riscv_csr_status(vm, dest, CSR_MSTATUS_MASK, op);
        case CSR_MSTATUSH:
            return riscv_csr_zero(dest); // We don't need upper half on rv32 for now
        case CSR_MISA:
            return riscv_csr_misa(vm, dest, op);
        case CSR_MEDELEG:
            return riscv_csr_helper_masked(&vm->csr.edeleg[PRIVILEGE_MACHINE], dest, CSR_MEDELEG_MASK, op);
        case CSR_MIDELEG:
            return riscv_csr_helper_masked(&vm->csr.ideleg[PRIVILEGE_MACHINE], dest, CSR_MIDELEG_MASK, op);
        case CSR_MIE:
            return riscv_csr_ie(vm, dest, CSR_MEIP_MASK, op);
        case CSR_MTVEC:
            return riscv_csr_helper(&vm->csr.tvec[PRIVILEGE_MACHINE], dest, op);
        case CSR_MCOUNTEREN:
            return riscv_csr_helper_masked(&vm->csr.counteren[PRIVILEGE_MACHINE], dest, CSR_COUNTEREN_MASK, op);

        // Machine Trap Handling
        case CSR_MSCRATCH:
            return riscv_csr_helper(&vm->csr.scratch[PRIVILEGE_MACHINE], dest, op);
        case CSR_MEPC:
            return riscv_csr_helper(&vm->csr.epc[PRIVILEGE_MACHINE], dest, op);
        case CSR_MCAUSE:
            return riscv_csr_helper(&vm->csr.cause[PRIVILEGE_MACHINE], dest, op);
        case CSR_MTVAL:
            return riscv_csr_helper(&vm->csr.tval[PRIVILEGE_MACHINE], dest, op);
        case CSR_MIP:
            return riscv_csr_ip(vm, dest, CSR_MEIP_MASK, op);

        // Machine Configuration
        case CSR_MENVCFG:
            return riscv_csr_helper_l(vm, &vm->csr.envcfg[PRIVILEGE_MACHINE], dest, CSR_MENVCFG_MASK, op);
        case CSR_MENVCFGH:
            return riscv_csr_helper_h(vm, &vm->csr.envcfg[PRIVILEGE_MACHINE], dest, CSR_MENVCFG_MASK, op);
        case CSR_MSECCFG:
            return riscv_csr_helper_l(vm, &vm->csr.mseccfg, dest, CSR_MSECCFG_MASK, op);
        case CSR_MSECCFGH:
            return riscv_csr_helper_h(vm, &vm->csr.mseccfg, dest, CSR_MSECCFG_MASK, op);

        // Machine Memory Protection
        case 0x3A0: // pmpcfg0
        case 0x3A1: // pmpcfg1
        case 0x3A2: // pmpcfg2
        case 0x3A3: // pmpcfg3
            return riscv_csr_zero(dest); // TODO: PMP
        case 0x3B0: // pmpaddr0
        case 0x3B1: // pmpaddr1
        case 0x3B2: // pmpaddr2
        case 0x3B3: // pmpaddr3
        case 0x3B4: // pmpaddr4
        case 0x3B5: // pmpaddr5
        case 0x3B6: // pmpaddr6
        case 0x3B7: // pmpaddr7
        case 0x3B8: // pmpaddr8
        case 0x3B9: // pmpaddr9
        case 0x3BA: // pmpaddr10
        case 0x3BB: // pmpaddr11
        case 0x3BC: // pmpaddr12
        case 0x3BD: // pmpaddr13
        case 0x3BE: // pmpaddr14
        case 0x3BF: // pmpaddr15
            return riscv_csr_zero(dest); // TODO: PMP

        // Machine Counters/Timers
        case CSR_MCYCLE:
        case CSR_MINSTRET:
        case 0xB03: // mhpmcounter3
        case 0xB04: // mhpmcounter4
        case 0xB05: // mhpmcounter5
        case 0xB06: // mhpmcounter6
        case 0xB07: // mhpmcounter7
        case 0xB08: // mhpmcounter8
        case 0xB09: // mhpmcounter9
        case 0xB0A: // mhpmcounter10
        case 0xB0B: // mhpmcounter11
        case 0xB0C: // mhpmcounter12
        case 0xB0D: // mhpmcounter13
        case 0xB0E: // mhpmcounter14
        case 0xB0F: // mhpmcounter15
        case 0xB10: // mhpmcounter16
        case 0xB11: // mhpmcounter17
        case 0xB12: // mhpmcounter18
        case 0xB13: // mhpmcounter19
        case 0xB14: // mhpmcounter20
        case 0xB15: // mhpmcounter21
        case 0xB16: // mhpmcounter22
        case 0xB17: // mhpmcounter23
        case 0xB18: // mhpmcounter24
        case 0xB19: // mhpmcounter25
        case 0xB1A: // mhpmcounter26
        case 0xB1B: // mhpmcounter27
        case 0xB1C: // mhpmcounter28
        case 0xB1D: // mhpmcounter29
        case 0xB1E: // mhpmcounter30
        case 0xB1F: // mhpmcounter31
            return riscv_csr_zero(dest);
        case CSR_MCYCLEH:
        case CSR_MINSTRETH:
        case 0xB83: // mhpmcounter3h
        case 0xB84: // mhpmcounter4h
        case 0xB85: // mhpmcounter5h
        case 0xB86: // mhpmcounter6h
        case 0xB87: // mhpmcounter7h
        case 0xB88: // mhpmcounter8h
        case 0xB89: // mhpmcounter9h
        case 0xB8A: // mhpmcounter10h
        case 0xB8B: // mhpmcounter11h
        case 0xB8C: // mhpmcounter12h
        case 0xB8D: // mhpmcounter13h
        case 0xB8E: // mhpmcounter14h
        case 0xB8F: // mhpmcounter15h
        case 0xB90: // mhpmcounter16h
        case 0xB91: // mhpmcounter17h
        case 0xB92: // mhpmcounter18h
        case 0xB93: // mhpmcounter19h
        case 0xB94: // mhpmcounter20h
        case 0xB95: // mhpmcounter21h
        case 0xB96: // mhpmcounter22h
        case 0xB97: // mhpmcounter23h
        case 0xB98: // mhpmcounter24h
        case 0xB99: // mhpmcounter25h
        case 0xB9A: // mhpmcounter26h
        case 0xB9B: // mhpmcounter27h
        case 0xB9C: // mhpmcounter28h
        case 0xB9D: // mhpmcounter29h
        case 0xB9E: // mhpmcounter30h
        case 0xB9F: // mhpmcounter31h
            return riscv_csr_zero_h(vm, dest);

        // Machine Counter Setup
        case CSR_MCOUNTINHIBIT:
            return riscv_csr_zero(dest);
    }

    return false;
}

bool riscv_csr_op(rvvm_hart_t* vm, uint32_t csr_id, maxlen_t* dest, uint8_t op)
{
    if (riscv_csr_readonly(csr_id)) {
        // This is a readonly CSR, only set/clear zero bits is allowed
        if (unlikely(op == CSR_SWAP || *dest != 0)) return false;
    }

    if (unlikely(riscv_csr_privilege(csr_id) > vm->priv_mode)) {
        // Not privileged enough to access this CSR
        return false;
    }

    if (!vm->rv64) {
        // Zero upper input bits on CSR access
        // TODO: Make this better somehow
        *dest = (uint32_t)*dest;
    }
    bool ret = riscv_csr_op_internal(vm, csr_id, dest, op);
    if (!vm->rv64) {
        // Sign-extend the result into the register
        *dest = (int32_t)*dest;
    }
    return ret;
}

