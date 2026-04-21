/*
riscv_csr.h - RISC-V Control and Status Registers
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef RVVM_RISCV_CSR_H
#define RVVM_RISCV_CSR_H

#include "bit_ops.h"
#include "rvvm.h"

/*
 * CSR Listing
 */

// Unprivileged Floating-Point CSRs
#define CSR_FFLAGS         0x001 // Floating-point accrued exceptions
#define CSR_FRM            0x002 // Floating-point dynamic rounding mode
#define CSR_FCSR           0x003 // Floating point control and status register (frm + fflags)
#define CSR_VSTART         0x008 // Vector start position
#define CSR_VXSAT          0x009 // Fixed-point saturate flag
#define CSR_VXRM           0x00A // Fixed-point rounding mode
#define CSR_VCSR           0x00F // Vector control and status register (vxrm + vsat)
#define CSR_VL             0xC20 // Vector length
#define CSR_VTYPE          0xC21 // Vector data type
#define CSR_VLENB          0xC22 // Vector register length in bytes

// Unprivileged Entropy Source CSR
#define CSR_SEED           0x015 // Seed for cryptographic RNGs

// Unprivileged Counters/Timers
#define CSR_CYCLE          0xC00 // CPU cycle counter
#define CSR_CYCLEH         0xC80 // CPU cycle counter high (RV32)
#define CSR_TIME           0xC01 // Timer
#define CSR_TIMEH          0xC81 // Timer high (RV32)
#define CSR_INSTRET        0xC02 // Instructions retired
#define CSR_INSTRETH       0xC82 // Instructions retired high (RV32)
// 0xC03 - 0xC1F hpmcounter
// 0xC83 - 0xC9F hpmcounterh (RV32)

// Supervisor Trap Setup
#define CSR_SSTATUS        0x100 // Supervisor status
#define CSR_SIE            0x104 // Supervisor interrupts enabled
#define CSR_SIEH           0x114 // Supervisor interrupts enabled high (AIA, RV32)
#define CSR_STVEC          0x105 // Supervisor trap vector
#define CSR_SCOUNTEREN     0x106 // Supervisor counters enabled
#define CSR_STIMECMP       0x14D // Supervisor timer compare (Sstc)
#define CSR_STIMECMPH      0x15D // Supervisor timer compare high (Sstc, RV32)

// Supervisor Configuration
#define CSR_SENVCFG        0x10A // Supervisor environment configuration

// Supervisor Counter Setup
#define CSR_SCOUNTINHIBIT  0x120 // Supervisor counter-inhibit

// Supervisor Indirect CSR Access (Sscsrind)
#define CSR_SISELECT       0x150 // Supervisor indirect register select (Sscsrind)
#define CSR_SIREG          0x151 // Supervisor indirect register alias (Sscsrind)
#define CSR_SIREG2         0x152 // Supervisor indirect register alias 2 (Sscsrind)
#define CSR_SIREG3         0x153 // Supervisor indirect register alias 3 (Sscsrind)
#define CSR_SIREG4         0x155 // Supervisor indirect register alias 4 (Sscsrind)
#define CSR_SIREG5         0x156 // Supervisor indirect register alias 5 (Sscsrind)
#define CSR_SIREG6         0x157 // Supervisor indirect register alias 6 (Sscsrind)

// Supervisor Trap Handling
#define CSR_SSCRATCH       0x140 // Supervisor scratch register
#define CSR_SEPC           0x141 // Supervisor exception PC
#define CSR_SCAUSE         0x142 // Supervisor trap cause
#define CSR_STVAL          0x143 // Supervisor trap value
#define CSR_SIP            0x144 // Supervisor interrupts pending
#define CSR_SIPH           0x154 // Supervisor interrupts pending high (AIA, RV32)
#define CSR_STOPEI         0x15C // Supervisor top external interrupt (AIA, IMSIC)
#define CSR_SCOUNTOVF      0xDA0 // Supervisor count overflow
#define CSR_STOPI          0xDB0 // Supervisor top interrupt (AIA)

// Supervisor Protection and Translation
#define CSR_SATP           0x180 // Supervisor address translation and protection

// Debug/Trace Registers (Debug extension)
#define CSR_SCONTEXT       0x5A8 // Supervisor-mode debug context

// Supervisor State Enable Registers
#define CSR_SSTATEEN0      0x10C // Supervisor state enable 0
#define CSR_SSTATEEN1      0x10D // Supervisor state enable 1
#define CSR_SSTATEEN2      0x10E // Supervisor state enable 2
#define CSR_SSTATEEN3      0x10F // Supervisor state enable 3

// Hypervisor Trap Setup
#define CSR_HSTATUS        0x600 // Hypervisor status register
#define CSR_HEDELEG        0x602 // Hypervisor exception delegation
#define CSR_HEDELEGH       0x612 // Hypervisor exception delegation high (RV32)
#define CSR_HIDELEG        0x603 // Hypervisor interrupt delegation
#define CSR_HIDELEGH       0x613 // Hypervisor interrupt delegation high (AIA, RV32)
#define CSR_HIE            0x604 // Hypervisor interrupts enabled
#define CSR_HCOUNTEREN     0x606 // Hypervisor counter enable
#define CSR_HGEIE          0x607 // Hypervisor guest external interrupts
#define CSR_HVIEN          0x608 // Hypervisor virtual interrupt enables
#define CSR_HVIENH         0x618 // Hypervisor virtual interrupt enables high (AIA, RV32)
#define CSR_HVICTL         0x609 // Hypervisor virtual interrupt control

// Hypervisor Trap Handling
#define CSR_HTVAL          0x643 // Hypervisor trap value
#define CSR_HIP            0x644 // Hypervisor interrupts pending
#define CSR_HVIP           0x645 // Hypervisor virtual interrupts pending
#define CSR_HVIPH          0x655 // Hypervisor virtual interrupts pending high (AIA, RV32)
#define CSR_HVIPRIO1       0x646 // Hypervisor virtual interrupts priorities
#define CSR_HVIPRIO1H      0x656 // Hypervisor virtual interrupts priorities high (AIA, RV32)
#define CSR_HVIPRIO2       0x647 // Hypervisor virtual interrupts priorities
#define CSR_HVIPRIO2H      0x657 // Hypervisor virtual interrupts priorities high (AIA, RV32)
#define CSR_HTINST         0x64A // Hypervisor trap instruction
#define CSR_HGEIP          0xE12 // Hypervisor guest external interrupts pending

// Hypervisor Configuration
#define CSR_HENVCFG        0x60A // Hypervisor environment configuration
#define CSR_HENVCFGH       0x61A // Hypervisor environment configuration high (RV32)

// Hypervisor Protection and Translation
#define CSR_HGATP          0x680 // Hypervisor guest address translation and protection

// Debug/Trace Registers (Debug extension)
#define CSR_HCONTEXT       0x6A8 // Hypervisor debug context

// Hypervisor Counter/Timer Virtualization Registers
#define CSR_HTIMEDELTA     0x605 // Delta for VS/VU mode timer
#define CSR_HTIMEDELTAH    0x615 // Delta for VS/VU mode timer high (RV32)

// Hypervisor State Enable Registers
#define CSR_HSTATEEN0      0x60C // Hypervisor state enable 0
#define CSR_HSTATEEN1      0x60D // Hypervisor state enable 1
#define CSR_HSTATEEN2      0x60E // Hypervisor state enable 2
#define CSR_HSTATEEN3      0x60F // Hypervisor state enable 3
#define CSR_HSTATEEN0H     0x61C // Hypervisor state enable 0 high (RV32)
#define CSR_HSTATEEN1H     0x61D // Hypervisor state enable 1 high (RV32)
#define CSR_HSTATEEN2H     0x61E // Hypervisor state enable 2 high (RV32)
#define CSR_HSTATEEN3H     0x61F // Hypervisor state enable 3 high (RV32)

// Virtual Supervisor Registers (Swapped on HS<->VS)
#define CSR_VSSTATUS       0x200 // Virtual supervisor status
#define CSR_VSIE           0x204 // Virtual supervisor interrupts enabled
#define CSR_VSTVEC         0x205 // Virtual supervisor trap vector
#define CSR_VSSCRATCH      0x240 // Virtual supervisor scratch register
#define CSR_VSEPC          0x241 // Virtual supervisor exception PC
#define CSR_VSCAUSE        0x242 // Virtual supervisor trap cause
#define CSR_VSTVAL         0x243 // Virtual supervisor trap value
#define CSR_VSIP           0x244 // Virtual supervisor interrupts pending
#define CSR_VSATP          0x280 // Virtual supervisor address translation and protection

// Virtual Supervisor Indirect CSR Access (Sscsrind)
#define CSR_VSISELECT      0x250 // Virtual supervisor indirect register select (Sscsrind)
#define CSR_VSIREG         0x251 // Virtual supervisor indirect register alias (Sscsrind)
#define CSR_VSIREG2        0x252 // Virtual supervisor indirect register alias 2 (Sscsrind)
#define CSR_VSIREG3        0x253 // Virtual supervisor indirect register alias 3 (Sscsrind)
#define CSR_VSIREG4        0x255 // Virtual supervisor indirect register alias 4 (Sscsrind)
#define CSR_VSIREG5        0x256 // Virtual supervisor indirect register alias 5 (Sscsrind)
#define CSR_VSIREG6        0x257 // Virtual supervisor indirect register alias 6 (Sscsrind)

// Machine Information Registers
#define CSR_MVENDORID      0xF11 // Machine Vendor ID
#define CSR_MARCHID        0xF12 // Machine Architecture ID
#define CSR_MIMPID         0xF13 // Machine Implementation ID
#define CSR_MHARTID        0xF14 // Machine Hart ID
#define CSR_MCONFIGPTR     0xF15 // Pointer to configuration data structure (FDT?)

// Machine Trap Setup
#define CSR_MSTATUS        0x300 // Machine status register
#define CSR_MSTATUSH       0x310 // Machine status register high (RV32)
#define CSR_MISA           0x301 // Machine ISA
#define CSR_MEDELEG        0x302 // Machine exception delegation
#define CSR_MEDELEGH       0x312 // Machine exception delegation high (RV32)
#define CSR_MIDELEG        0x303 // Machine interrupts delegation
#define CSR_MIDELEGH       0x313 // Machine interrupts delegation high (AIA, RV32)
#define CSR_MIE            0x304 // Machine interrupts enabled
#define CSR_MIEH           0x314 // Machine interrupts enabled high (AIA, RV32)
#define CSR_MTVEC          0x305 // Machine trap vector
#define CSR_MCOUNTEREN     0x306 // Machine counters enabled
#define CSR_MVIEN          0x308 // Machine virtual interrupt enables (AIA)
#define CSR_MVIENH         0x318 // Machine virtual interrupt enables high (AIA, RV32)
#define CSR_MVIP           0x309 // Machine virtual interrupt pending (AIA)
#define CSR_MVIPH          0x319 // Machine virtual interrupt pending high (AIA, RV32)

// Machine Trap Handling
#define CSR_MSCRATCH       0x340 // Machine scratch register
#define CSR_MEPC           0x341 // Machine exception PC
#define CSR_MCAUSE         0x342 // Machine trap cause
#define CSR_MTVAL          0x343 // Machine trap value
#define CSR_MIP            0x344 // Machine interrupts pending
#define CSR_MIPH           0x354 // Machine interrupts pending high (AIA, RV32)
#define CSR_MTOPEI         0x35C // Machine top external interrupt (AIA, IMSIC)
#define CSR_MTINST         0x34A // Machine trap instruction (transformed)
#define CSR_MTVAL2         0x34B // Machine bad guest physical address
#define CSR_MTOPI          0xFB0 // Machine top interrupt (AIA)

// Machine Configuration
#define CSR_MENVCFG        0x30A // Machine environment configuration
#define CSR_MENVCFGH       0x31A // Machine environment configuration high (RV32)
#define CSR_MSECCFG        0x747 // Machine security configuration
#define CSR_MSECCFGH       0x757 // Machine security configuration high (RV32)

// Machine Memory Protection
// 0x3A0 - 0x3A3 pmpcfg0 - pmpcfg3
// 0x3B0 - 0x3BF pmpaddr0 - pmpaddr15

// Machine Non-Maskable Interrupt Handling (But no NMIs in RVVM!)
#define CSR_MNSCRATCH      0x740 // Resumable NMI scratch register
#define CSR_MNEPC          0x741 // Resumable NMI program counter
#define CSR_MNCAUSE        0x742 // Resumable NMI cause
#define CSR_MNSTATUS       0x744 // Resumable NMI status

// Machine Counters/Timers
#define CSR_MCYCLE         0xB00 // Machine cycle counter
#define CSR_MCYCLEH        0xB80 // Machine cycle counter high (RV32)
#define CSR_MINSTRET       0xB02 // Machine instructions retired
#define CSR_MINSTRETH      0xB82 // Machine instructions retired high (RV32)
// 0xB03 - 0xB1F mhpmcounter3 - mhpmcounter31
// 0xB83 - 0xB9F mhpmcounter3h - mhpmcounter31h

// Machine Counter Setup
#define CSR_MCOUNTINHIBIT  0x320 // Machine counter inhibit
// 0x323 - 0x33F mhpmevent3 - mhpmevent31

// Machine Indirect CSR Access (Smcsrind)
#define CSR_MISELECT       0x350 // Machine indirect register select (Smcsrind)
#define CSR_MIREG          0x351 // Machine indirect register alias (Smcsrind)
#define CSR_MIREG2         0x352 // Machine indirect register alias 2 (Smcsrind)
#define CSR_MIREG3         0x353 // Machine indirect register alias 3 (Smcsrind)
#define CSR_MIREG4         0x355 // Machine indirect register alias 4 (Smcsrind)
#define CSR_MIREG5         0x356 // Machine indirect register alias 5 (Smcsrind)
#define CSR_MIREG6         0x357 // Machine indirect register alias 6 (Smcsrind)

// Debug/Trace Registers (Shared with Debug Mode)
#define CSR_TSELECT        0x7A0 // Debug/Trace trigger select register
#define CSR_TDATA1         0x7A1 // First Debug/Trace trigger data register
#define CSR_TDATA2         0x7A2 // Second Debug/Trace trigger data register
#define CSR_TDATA3         0x7A3 // Third Debug/Trace trigger data register
#define CSR_MCONTEXT       0x7A8 // Machine-mode context register

// Debug Mode Registers
#define CSR_DCSR           0x7B0 // Debug control and status register
#define CSR_DPC            0x7B1 // Debug program counter
#define CSR_DSCRATCH0      0x7B2 // Debug scratch register 0
#define CSR_DSCRATCH1      0x7B3 // Debug scratch register 1

/*
 * Indirect CSRs
 */

#define CSRI_MIPRIO_0      0x30 // Major interrupt priorities (0 - 15) (AIA)
#define CSRI_MIPRIO_15     0x3F // Major interrupt priorities (0 - 15) (AIA)
#define CSRI_EIDELIVERY    0x70 // External interrupt delivery (AIA)
#define CSRI_EITHRESHOLD   0x72 // External interrupt threshold (AIA)
#define CSRI_EIP0          0x80 // External interrupts pending (0 - 63) (AIA)
#define CSRI_EIP63         0xBF // External interrupts pending (0 - 63) (AIA)
#define CSRI_EIE0          0xC0 // External interrupts enabled (0 - 63) (AIA)
#define CSRI_EIE63         0xFF // External interrupts enabled (0 - 63) (AIA)

/*
 * CSR Operations
 */

#define CSR_SWAP           0x1
#define CSR_SETBITS        0x2
#define CSR_CLEARBITS      0x3

/*
 * CSR Values / Bitfields
 */

#define CSR_STATUS_MPRV    (1ULL << 17)
#define CSR_STATUS_SUM     (1ULL << 18)
#define CSR_STATUS_MXR     (1ULL << 19)
#define CSR_STATUS_TVM     (1ULL << 20)
#define CSR_STATUS_TW      (1ULL << 21)
#define CSR_STATUS_TSR     (1ULL << 22)

#define CSR_ENVCFG_CBIE    (1ULL << 4)
#define CSR_ENVCFG_CBCFE   (1ULL << 6)
#define CSR_ENVCFG_CBZE    (1ULL << 7)
#define CSR_ENVCFG_STCE    (1ULL << 63)

#define CSR_MSECCFG_USEED  (1ULL << 8)
#define CSR_MSECCFG_SSEED  (1ULL << 9)

#define CSR_SATP_MODE_BARE 0x0
#define CSR_SATP_MODE_SV32 0x1
#define CSR_SATP_MODE_SV39 0x8
#define CSR_SATP_MODE_SV48 0x9
#define CSR_SATP_MODE_SV57 0xA

#define CSR_MISA_RV32      0x40000000U
#define CSR_MISA_RV64      0x8000000000000000ULL

#define CSR_COUNTEREN_CY   0x1U
#define CSR_COUNTEREN_TM   0x2U
#define CSR_COUNTEREN_IR   0x4U

/*
 * CSR Masks (For WARL behavior of CSRs)
 */

#define CSR_FFLAGS_MASK    0x0000001FU
#define CSR_FRM_MASK       0x00000007U
#define CSR_FCSR_MASK      0x000000FFU

#define CSR_MSTATUS_MASK   0xF007FFFAAULL
#define CSR_SSTATUS_MASK   0x3000DE722ULL
#define CSR_STATUS_FS_MASK 0x000006000ULL

#define CSR_MEDELEG_MASK   0x0000B109U
#define CSR_MIDELEG_MASK   0x00000222U

#define CSR_MEIP_MASK      0x00000AAAU
#define CSR_SEIP_MASK      0x00000222U

#define CSR_COUNTEREN_MASK 0x00000007U

#define CSR_MENVCFG_MASK   0x80000000000000D0ULL
#define CSR_SENVCFG_MASK   0x00000000000000D0ULL

#define CSR_MSECCFG_MASK   0x0000000000000300ULL

/*
 * FPU control stuff
 */

#define FS_OFF             0x0
#define FS_INITIAL         0x1
#define FS_CLEAN           0x2
#define FS_DIRTY           0x3

#define FFLAG_NX           (1 << 0) // Inexact
#define FFLAG_UF           (1 << 1) // Undeflow
#define FFLAG_OF           (1 << 2) // Overflow
#define FFLAG_DZ           (1 << 3) // Divide by zero
#define FFLAG_NV           (1 << 4) // Invalid operation

#define RM_RNE             0x0  // Round to nearest, ties to even
#define RM_RTZ             0x1  // Round to zero
#define RM_RDN             0x2  // Round down - towards -inf
#define RM_RUP             0x3  // Round up - towards +inf
#define RM_RMM             0x4  // Round to nearest, ties to max magnitude
#define RM_DYN             0x7  // Round to instruction's rm field
#define RM_INVALID         0xFF // Invalid rounding mode was specified - should cause a trap

static forceinline bool riscv_fpu_is_enabled(rvvm_hart_t* vm)
{
    return bit_cut(vm->csr.status, 13, 2) != FS_OFF;
}

static forceinline void riscv_fpu_set_dirty(rvvm_hart_t* vm)
{
    if (unlikely(bit_cut(vm->csr.status, 13, 2) != FS_DIRTY)) {
        vm->csr.status |= (FS_DIRTY << 13);
    }
}

/*
 * CSR interface
 */

// Get minimal privilege mode to access CSR
static forceinline uint8_t riscv_csr_privilege(uint32_t csr_id)
{
    return bit_cut(csr_id, 8, 2);
}

// Check whether writes are illegal for this CSR
static forceinline bool riscv_csr_readonly(uint32_t csr_id)
{
    return bit_cut(csr_id, 10, 2) == 0x3;
}

// Perform a CSR operation, set *dest to original CSR value
// Returns false on failure (To raise exception afterwards)
bool riscv_csr_op(rvvm_hart_t* vm, uint32_t csr_id, rvvm_uxlen_t* dest, uint8_t op);

// Initialize CSRs on a new hart
void riscv_csr_init(rvvm_hart_t* vm);

// Synchronize hart FPU CSR state with hart thread on thread creation/destruction
void riscv_csr_sync_fpu(rvvm_hart_t* vm);

/*
 * Feature enablement checks
 */

static forceinline bool riscv_csr_envcfg_enabled(rvvm_hart_t* vm, uint64_t mask)
{
    if (vm->priv_mode < RISCV_PRIV_MACHINE) {
        mask &= vm->csr.envcfg[RISCV_PRIV_MACHINE];
    }
    // TODO: henvcfg delegation?
    if (vm->priv_mode < RISCV_PRIV_SUPERVISOR) {
        mask &= vm->csr.envcfg[RISCV_PRIV_SUPERVISOR];
    }
    return !!mask;
}

static forceinline bool riscv_csr_counter_enabled(rvvm_hart_t* vm, uint32_t counter)
{
    uint32_t mask = bit_set32(counter);
    if (vm->priv_mode < RISCV_PRIV_MACHINE) {
        mask &= vm->csr.counteren[RISCV_PRIV_MACHINE];
    }
    // TODO: hcounteren delegation?
    if (vm->priv_mode < RISCV_PRIV_SUPERVISOR) {
        mask &= vm->csr.counteren[RISCV_PRIV_SUPERVISOR];
    }
    return !!mask;
}

static forceinline bool riscv_csr_seed_enabled(rvvm_hart_t* vm)
{
    if (vm->priv_mode == RISCV_PRIV_USER) {
        // U-mode still respects mseccfg.USEED — matches real hardware
        // and gives kernels a way to forbid unprivileged access.
        return !!(vm->csr.mseccfg & CSR_MSECCFG_USEED);
    }
    // M-mode + S-mode: always allowed. We implement Zkr as a virtual
    // entropy source (spec Section 4.4.3), so the guest doesn't need
    // M-mode firmware to cooperate. Real hardware gates S-mode access
    // behind mseccfg.SSEED, but OpenSBI clobbers mseccfg during hart
    // setup (it writes 0 while configuring PMP-enhancement bits) — and
    // our "firmware" is effectively us. Without this, Linux panics at
    // arch_get_random_seed_longs on first boot: our init-time SSEED=1
    // gets zeroed by OpenSBI before S-mode ever runs, and the first
    // csrrwi seed,x0 traps as illegal.
    return true;
}

static forceinline bool riscv_csr_cbi_enabled(rvvm_hart_t* vm)
{
    return riscv_csr_envcfg_enabled(vm, CSR_ENVCFG_CBIE);
}

static forceinline bool riscv_csr_cbcf_enabled(rvvm_hart_t* vm)
{
    return riscv_csr_envcfg_enabled(vm, CSR_ENVCFG_CBCFE);
}

static forceinline bool riscv_csr_cbz_enabled(rvvm_hart_t* vm)
{
    return riscv_csr_envcfg_enabled(vm, CSR_ENVCFG_CBZE);
}

static forceinline bool riscv_csr_sstc_enabled(rvvm_hart_t* vm)
{
    return riscv_csr_envcfg_enabled(vm, CSR_ENVCFG_STCE);
}

#endif
