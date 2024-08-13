/*
riscv_csr.h - RISC-V Control and Status Registers
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

#ifndef RISCV_CSR_H
#define RISCV_CSR_H

#include "rvvm.h"
#include "bit_ops.h"

/*
 * CSR Listing
 */

// Unprivileged Floating-Point CSRs
#define CSR_FFLAGS 0x001
#define CSR_FRM    0x002
#define CSR_FCSR   0x003

// Unprivileged Entropy Source CSR
#define CSR_SEED 0x015

// Unprivileged Counters/Timers
#define CSR_CYCLE    0xC00
#define CSR_CYCLEH   0xC80
#define CSR_TIME     0xC01
#define CSR_TIMEH    0xC81
#define CSR_INSTRET  0xC02
#define CSR_INSTRETH 0xC82

// Supervisor Trap Setup
#define CSR_SSTATUS    0x100
#define CSR_SIE        0x104
#define CSR_STVEC      0x105
#define CSR_SCOUNTEREN 0x106
#define CSR_STIMECMP   0x14D
#define CSR_STIMECMPH  0x15D

// Supervisor Configuration
#define CSR_SENVCFG 0x10A

// Supervisor Trap Handling
#define CSR_SSCRATCH 0x140
#define CSR_SEPC     0x141
#define CSR_SCAUSE   0x142
#define CSR_STVAL    0x143
#define CSR_SIP      0x144

// Supervisor Protection and Translation
#define CSR_SATP 0x180

// Debug/Trace Registers (Debug extension)
#define CSR_SCONTEXT 0x5A8

// Hypervisor Trap Setup
#define CSR_HSTATUS    0x600
#define CSR_HEDELEG    0x602
#define CSR_HIDELEG    0x603
#define CSR_HIE        0x604
#define CSR_HCOUNTEREN 0x606
#define CSR_HGEIE      0x607
#define CSR_HEDELEGH   0x612

// Hypervisor Trap Handling
#define CSR_HTVAL  0x643
#define CSR_HIP    0x644
#define CSR_HVIP   0x645
#define CSR_HTINST 0x64A
#define CSR_HGEIP  0xE12

// Hypervisor Configuration
#define CSR_HENVCFG  0x60A
#define CSR_HENVCFGH 0x61A

// Debug/Trace Registers (Debug extension)
#define CSR_HCONTEXT 0x6A8

// Hypervisor Counter/Timer Virtualization Registers
#define CSR_HTIMEDELTA  0x605
#define CSR_HTIMEDELTAH 0x615

// Virtual Supervisor Registers (Swapped on HS<->VS)
#define CSR_VSSTATUS  0x200
#define CSR_VSIE      0x204
#define CSR_VSTVEC    0x205
#define CSR_VSSCRATCH 0x240
#define CSR_VSEPC     0x241
#define CSR_VSCAUSE   0x242
#define CSR_VSTVAL    0x243
#define CSR_VSIP      0x244
#define CSR_VSATP     0x280

// Machine Information Registers
#define CSR_MVENDORID  0xF11
#define CSR_MARCHID    0xF12
#define CSR_MIMPID     0xF13
#define CSR_MHARTID    0xF14
#define CSR_MCONFIGPTR 0xF15

// Machine Trap Setup
#define CSR_MSTATUS    0x300
#define CSR_MSTATUSH   0x310
#define CSR_MISA       0x301
#define CSR_MEDELEG    0x302
#define CSR_MEDELEGH   0x312
#define CSR_MIDELEG    0x303
#define CSR_MIE        0x304
#define CSR_MTVEC      0x305
#define CSR_MCOUNTEREN 0x306

// Machine Trap Handling
#define CSR_MSCRATCH 0x340
#define CSR_MEPC     0x341
#define CSR_MCAUSE   0x342
#define CSR_MTVAL    0x343
#define CSR_MIP      0x344
#define CSR_MTINST   0x34A // Machine trap instruction (transformed)
#define CSR_MTVAL2   0x34B // Machine bad guest physical address

// Machine Configuration
#define CSR_MENVCFG  0x30A
#define CSR_MENVCFGH 0x31A
#define CSR_MSECCFG  0x747 // THE BOEING 747???
#define CSR_MSECCFGH 0x757

// Machine Memory Protection
// 0x3A0 - 0x3A3 pmpcfg0 - pmpcfg3
// 0x3B0 - 0x3BF pmpaddr0 - pmpaddr15

// Machine Non-Maskable Interrupt Handling (But no NMIs in RVVM)
#define CSR_MNSCRATCH 0x740
#define CSR_MNEPC     0x741
#define CSR_MNCAUSE   0x742
#define CSR_MNSTATUS  0x744

// Machine Counters/Timers
#define CSR_MCYCLE    0xB00
#define CSR_MCYCLEH   0xB80
#define CSR_MINSTRET  0xB02
#define CSR_MINSTRETH 0xB82
// 0xB03 - 0xB1F mhpmcounter3 - mhpmcounter31
// 0xB83 - 0xB9F mhpmcounter3h - mhpmcounter31h

// Machine Counter Setup
#define CSR_MCOUNTINHIBIT 0x320
// 0x323 - 0x33F mhpmevent3 - mhpmevent31

// Debug/Trace Registers (Shared with Debug Mode)
#define CSR_TSELECT  0x7A0
#define CSR_TDATA1   0x7A1
#define CSR_TDATA2   0x7A2
#define CSR_TDATA3   0x7A3
#define CSR_MCONTEXT 0x7A8

// Debug Mode Registers
#define CSR_DCSR      0x7B0
#define CSR_DPC       0x7B1
#define CSR_DSCRATCH0 0x7B2
#define CSR_DSCRATCH1 0x7B3

/*
 * CSR Operations
 */

#define CSR_SWAP       0x1
#define CSR_SETBITS    0x2
#define CSR_CLEARBITS  0x3

/*
 * CSR Values / Bitfields
 */

#define CSR_STATUS_MPRV (1ULL << 17)
#define CSR_STATUS_SUM  (1ULL << 18)
#define CSR_STATUS_MXR  (1ULL << 19)
#define CSR_STATUS_TVM  (1ULL << 20)
#define CSR_STATUS_TW   (1ULL << 21)
#define CSR_STATUS_TSR  (1ULL << 22)

#define CSR_ENVCFG_CBIE  (1ULL << 4)
#define CSR_ENVCFG_CBCFE (1ULL << 6)
#define CSR_ENVCFG_CBZE  (1ULL << 7)
#define CSR_ENVCFG_STCE  (1ULL << 63)

#define CSR_MSECCFG_USEED (1ULL << 8)
#define CSR_MSECCFG_SSEED (1ULL << 9)

#define CSR_SATP_MODE_PHYS   0
#define CSR_SATP_MODE_SV32   1
#define CSR_SATP_MODE_SV39   8
#define CSR_SATP_MODE_SV48   9
#define CSR_SATP_MODE_SV57   10

#define CSR_MISA_RV32  0x40000000U
#define CSR_MISA_RV64  0x8000000000000000ULL

#define CSR_COUNTEREN_TM 0x2U

/*
 * CSR Masks (For WARL behavior of CSRs)
 */

#define CSR_MSTATUS_MASK 0x7E79AA
#define CSR_SSTATUS_MASK 0x0C6122
#define CSR_STATUS_FS_MASK 0x6000

#define CSR_MEDELEG_MASK 0xB109
#define CSR_MIDELEG_MASK 0x0222

#define CSR_MEIP_MASK    0xAAA
#define CSR_SEIP_MASK    0x222

#define CSR_COUNTEREN_MASK CSR_COUNTEREN_TM

#define CSR_MENVCFG_MASK 0x80000000000000D0ULL
#define CSR_SENVCFG_MASK 0xD0ULL

#define CSR_MSECCFG_MASK 0x300ULL

/*
 * FPU control stuff
 */

#define FS_OFF      0
#define FS_INITIAL  1
#define FS_CLEAN    2
#define FS_DIRTY    3

#define FFLAG_NX (1 << 0) // Inexact
#define FFLAG_UF (1 << 1) // Undeflow
#define FFLAG_OF (1 << 2) // Overflow
#define FFLAG_DZ (1 << 3) // Divide by zero
#define FFLAG_NV (1 << 4) // Invalid operation

#define RM_RNE 0 // Round to nearest, ties to even
#define RM_RTZ 1 // Round to zero
#define RM_RDN 2 // Round down - towards -inf
#define RM_RUP 3 // Round up - towards +inf
#define RM_RMM 4 // Round to nearest, ties to max magnitude
#define RM_DYN 7 // Round to instruction's rm field
#define RM_INVALID 255 // Invalid rounding mode was specified - should cause a trap

uint8_t fpu_set_rm(rvvm_hart_t* vm, uint8_t newrm);

static forceinline bool fpu_is_enabled(rvvm_hart_t* vm)
{
    return bit_cut(vm->csr.status, 13, 2) != FS_OFF;
}

static forceinline void fpu_set_fs(rvvm_hart_t* vm, uint8_t value)
{
#ifdef USE_PRECISE_FS
    vm->csr.status = bit_replace(vm->csr.status, 13, 2, value);
#else
    UNUSED(vm);
    UNUSED(value);
#endif
}

/*
 * CSR interface
 */

// Get minimal privilege mode to access CSR
static inline uint8_t riscv_csr_privilege(uint32_t csr_id)
{
    return bit_cut(csr_id, 8, 2);
}

// Check whether writes are illegal for this CSR
static inline bool riscv_csr_readonly(uint32_t csr_id)
{
    return bit_cut(csr_id, 10, 2) == 0x3;
}

// Perform a CSR operation, set *dest to original CSR value
// Returns false on failure (To raise exception afterwards)
bool riscv_csr_op(rvvm_hart_t* vm, uint32_t csr_id, maxlen_t* dest, uint8_t op);

/*
 * Feature enablement checks
 */

static inline bool riscv_csr_envcfg_enabled(rvvm_hart_t* vm, uint64_t mask)
{
    if (vm->priv_mode < PRIVILEGE_MACHINE) mask &= vm->csr.envcfg[PRIVILEGE_MACHINE];
    // TODO: henvcfg delegation?
    if (vm->priv_mode < PRIVILEGE_SUPERVISOR) mask &= vm->csr.envcfg[PRIVILEGE_SUPERVISOR];
    return !!mask;
}

static inline bool riscv_csr_counter_enabled(rvvm_hart_t* vm, uint32_t mask)
{
    if (vm->priv_mode < PRIVILEGE_MACHINE) mask &= vm->csr.counteren[PRIVILEGE_MACHINE];
    // TODO: hcounteren delegation?
    if (vm->priv_mode < PRIVILEGE_SUPERVISOR) mask &= vm->csr.counteren[PRIVILEGE_SUPERVISOR];
    return !!mask;
}

static inline bool riscv_csr_timer_enabled(rvvm_hart_t* vm)
{
    return riscv_csr_counter_enabled(vm, CSR_COUNTEREN_TM);
}

static inline bool riscv_csr_seed_enabled(rvvm_hart_t* vm)
{
    if (vm->priv_mode == PRIVILEGE_USER) {
        return !!(vm->csr.mseccfg & CSR_MSECCFG_USEED);
    } else if (vm->priv_mode < PRIVILEGE_MACHINE) {
        return !!(vm->csr.mseccfg & CSR_MSECCFG_SSEED);
    } else return true;
}

static inline bool riscv_csr_cbi_enabled(rvvm_hart_t* vm)
{
    return riscv_csr_envcfg_enabled(vm, CSR_ENVCFG_CBIE);
}

static inline bool riscv_csr_cbcf_enabled(rvvm_hart_t* vm)
{
    return riscv_csr_envcfg_enabled(vm, CSR_ENVCFG_CBCFE);
}

static inline bool riscv_csr_cbz_enabled(rvvm_hart_t* vm)
{
    return riscv_csr_envcfg_enabled(vm, CSR_ENVCFG_CBZE);
}

static inline bool riscv_csr_sstc_enabled(rvvm_hart_t* vm)
{
    return riscv_csr_envcfg_enabled(vm, CSR_ENVCFG_STCE);
}

#endif
