/*
rvvm.h - The RISC-V Virtual Machine
Copyright (C) 2021  LekKit <github.com/LekKit>
                    cerg2010cerg2010 <github.com/cerg2010cerg2010>
                    Mr0maks <mr.maks0443@gmail.com>
                    KotB <github.com/0xCatPKG>
                    X547 <github.com/X547>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef RVVM_INTERNAL_H
#define RVVM_INTERNAL_H

#include <rvvm/rvvm.h>

#include <util/atomics.h>
#include <util/blk_io.h>
#include <util/fpu_types.h>
#include <util/rvtimer.h>
#include <util/rvvm_types.h>
#include <util/threading.h>
#include <util/utils.h>
#include <util/vector.h>

#include <util/fdtlib.h> // TODO: Replace with rvvm_fdt

#include <core/gdbstub.h>

#if defined(USE_JIT)
#include <rvjit/rvjit.h>
#endif

#define RVVM_TLB_SIZE      256 // Always nonzero & power of 2 (32, 64..)
#define RVVM_TLB_MASK      (RVVM_TLB_SIZE - 1)

#define RVVM_RVV_VLEN      256
#define RVVM_RVV_SIZE      (RVVM_RVV_VLEN >> 3)

#define RVVM_OPTS_ARR_SIZE 0x09

BUILD_ASSERT(RVVM_TLB_SIZE);
BUILD_ASSERT(!(RVVM_TLB_SIZE & RVVM_TLB_MASK));

#define RISCV_REG_ZERO              0
#define RISCV_REG_X0                0
#define RISCV_REG_X1                1
#define RISCV_REG_X2                2
#define RISCV_REG_X3                3
#define RISCV_REG_X4                4
#define RISCV_REG_X5                5
#define RISCV_REG_X6                6
#define RISCV_REG_X7                7
#define RISCV_REG_X8                8
#define RISCV_REG_X9                9
#define RISCV_REG_X10               10
#define RISCV_REG_X11               11
#define RISCV_REG_X12               12
#define RISCV_REG_X13               13
#define RISCV_REG_X14               14
#define RISCV_REG_X15               15
#define RISCV_REG_X16               16
#define RISCV_REG_X17               17
#define RISCV_REG_X18               18
#define RISCV_REG_X19               19
#define RISCV_REG_X20               20
#define RISCV_REG_X21               21
#define RISCV_REG_X22               22
#define RISCV_REG_X23               23
#define RISCV_REG_X24               24
#define RISCV_REG_X25               25
#define RISCV_REG_X26               26
#define RISCV_REG_X27               27
#define RISCV_REG_X28               28
#define RISCV_REG_X29               29
#define RISCV_REG_X30               30
#define RISCV_REG_X31               31
#define RISCV_REG_PC                32

#define RISCV_REGS_MAX              33
#define RISCV_FPU_REGS_MAX          32

#define RISCV_PRIV_USER             0
#define RISCV_PRIV_SUPERVISOR       1
#define RISCV_PRIV_HYPERVISOR       2
#define RISCV_PRIV_MACHINE          3

#define RISCV_PRIVS_MAX             4

#define RISCV_INTERRUPT_SSOFTWARE   0x1 // S-mode Software Interrupt (ACLINT-SSWI)
#define RISCV_INTERRUPT_MSOFTWARE   0x3 // M-mode Software Interrupt (ACLINT-MSWI)
#define RISCV_INTERRUPT_STIMER      0x5 // S-mode Timer Interrupt (Sstc)
#define RISCV_INTERRUPT_MTIMER      0x7 // M-mode Timer Interrupt (ACLINT-MTIMER)
#define RISCV_INTERRUPT_SEXTERNAL   0x9 // S-mode External Interrupt (PLIC)
#define RISCV_INTERRUPT_MEXTERNAL   0xB // M-mode External Interrupt (PLIC)
#define RISCV_INTERRUPT_COUNTER_OVF 0xD // Local Counter Overflow (Sscofpmf)

#define RISCV_TRAP_INSN_MISALIGN    0x0 // Instruction Misaligned
#define RISCV_TRAP_INSN_FETCH       0x1 // Instruction Fetch Fault
#define RISCV_TRAP_ILL_INSN         0x2 // Illegal Instruction
#define RISCV_TRAP_BREAKPOINT       0x3 // Software Breakpoint
#define RISCV_TRAP_LOAD_MISALIGN    0x4 // Load Misaligned
#define RISCV_TRAP_LOAD_FAULT       0x5 // Load Fault
#define RISCV_TRAP_STORE_MISALIGN   0x6 // Store Misaligned
#define RISCV_TRAP_STORE_FAULT      0x7 // Store Fault
#define RISCV_TRAP_ECALL_UMODE      0x8 // Environment call from U-mode
#define RISCV_TRAP_ECALL_SMODE      0x9 // Environment call from S-mode
#define RISCV_TRAP_ECALL_MMODE      0xB // Environment call from M-mode
#define RISCV_TRAP_INSN_PAGEFAULT   0xC // Instruction fetch pagefault
#define RISCV_TRAP_LOAD_PAGEFAULT   0xD // Load pagefault
#define RISCV_TRAP_STORE_PAGEFAULT  0xF // Store pagefault

#if defined(USE_RV64)

typedef uint64_t rvvm_uxlen_t;
typedef int64_t  rvvm_sxlen_t;

#else

typedef uint32_t rvvm_uxlen_t;
typedef int32_t  rvvm_sxlen_t;

#endif

/*
 * Address translation cache (TLB)
 */

// clang-format off
typedef randomized_struct align_type(32)
{
    // Pointer to page (With vaddr subtracted, for faster TLB translation)
    size_t ptr;
#if defined(HOST_32BIT)
    // Make structure size a power of 2 (32 bytes) regardless of align_type() support
    size_t align;
#endif
    // Virtual page number per each op type (vaddr >> 12)
    rvvm_addr_t r;
    rvvm_addr_t w;
    rvvm_addr_t e;
} rvvm_tlb_entry_t;
// clang-format on

#if defined(USE_JIT)

typedef struct align_type(16) randomize_layout {
    // Pointer to code block
    rvjit_func_t block;
#if defined(HOST_32BIT)
    // Make structure size a power of 2 (16 bytes) regardless of align_type() support
    size_t align;
#endif
    // Virtual PC of this entry
    rvvm_addr_t pc;
} rvvm_jit_tlb_entry_t;

// Those structure sizes are mandatory if we are going to use JIT
// For pure interpreter this doesn't matter, so obscure architectures are fine
BUILD_ASSERT(sizeof(rvvm_tlb_entry_t) == 32);
BUILD_ASSERT(sizeof(rvvm_jit_tlb_entry_t) == 16);

#endif

/*
 * Physical RAM region
 */

typedef struct randomize_layout {
    rvvm_addr_t addr; // Physical memory base address (Should be page-aligned)
    size_t      size; // Physical memory amount (Should be page-aligned)
    void*       data; // Pointer to memory data (Preferably page-aligned)
} rvvm_ram_t;

/*
 * AIA register file
 */

// Maximum amount of external AIA interrupts in RVVM
#define RVVM_AIA_IRQ_LIMIT 256
#define RVVM_AIA_ARR_LEN   (RVVM_AIA_IRQ_LIMIT >> 5)

typedef struct randomize_layout {
    uint32_t eidelivery;
    uint32_t eithreshold;
    uint32_t eip[RVVM_AIA_ARR_LEN];
    uint32_t eie[RVVM_AIA_ARR_LEN];
} rvvm_aia_regfile_t;

/*
 * Hart structure
 */

struct align_cacheline rvvm_hart_t {
    uint32_t running;

    rvvm_uxlen_t registers[RISCV_REGS_MAX];

#if defined(USE_FPU)
    fpu_f64_t fpu_registers[RISCV_FPU_REGS_MAX];
#endif

    // We want short offsets from vmptr to tlb
    rvvm_tlb_entry_t tlb[RVVM_TLB_SIZE];

#if defined(USE_JIT)
    rvvm_jit_tlb_entry_t jtlb[RVVM_TLB_SIZE];
#endif

#if defined(USE_RVV)
    uint8_t rvv_state[RVVM_RVV_SIZE << 5];
#endif

    /*
     * Everything below here isn't accessed by JIT
     */

    randomized_fields_start

        rvvm_ram_t  mem;
    rvvm_machine_t* machine;
    rvvm_addr_t     root_page_table;
    uint8_t         mmu_mode;
    uint8_t         priv_mode;
    bool            rv64;

    bool        trap;
    rvvm_addr_t trap_pc;

    bool userland;

    bool         lrsc;
    rvvm_uxlen_t lrsc_addr;
    rvvm_uxlen_t lrsc_cas;

    struct randomize_layout {
        rvvm_uxlen_t status;

        rvvm_uxlen_t ie;
        rvvm_uxlen_t ip;

        rvvm_uxlen_t isa;

        rvvm_uxlen_t edeleg[RISCV_PRIVS_MAX];
        rvvm_uxlen_t ideleg[RISCV_PRIVS_MAX];
        rvvm_uxlen_t tvec[RISCV_PRIVS_MAX];
        rvvm_uxlen_t scratch[RISCV_PRIVS_MAX];
        rvvm_uxlen_t epc[RISCV_PRIVS_MAX];
        rvvm_uxlen_t cause[RISCV_PRIVS_MAX];
        rvvm_uxlen_t tval[RISCV_PRIVS_MAX];

        rvvm_uxlen_t iselect[RISCV_PRIVS_MAX];
        rvvm_uxlen_t counteren[RISCV_PRIVS_MAX];
        uint64_t     envcfg[RISCV_PRIVS_MAX];
        uint64_t     mseccfg;

        uint32_t fcsr;
        uint32_t vcsr;
        uint32_t vtype;
        uint32_t hartid;
    } csr;

#if defined(USE_JIT)
    rvjit_block_t jit;
    bool          jit_enabled;
    bool          jit_compiling;
    bool          jit_block_ends;
    bool          jit_skip_exec;
#endif

    // AIA register files for M/S modes
    rvvm_aia_regfile_t* aia;

    thread_ctx_t* thread;
    cond_var_t*   wfi_cond;

    rvtimecmp_t mtimecmp;
    rvtimecmp_t stimecmp;

    uint32_t pending_irqs;
    uint32_t pending_events;
    uint32_t preempt_ms;

    randomized_fields_end
};

struct randomize_layout rvvm_machine_t {
    rvvm_ram_t                 mem;
    vector_t(rvvm_hart_t*)     harts;
    vector_t(rvvm_mmio_dev_t*) mmio_devs;
    vector_t(rvvm_mmio_dev_t*) msi_targets;
    rvtimer_t                  timer;

    uint32_t running;
    uint32_t power_state;
    bool     rv64;

    rvfile_t* fw_file;
    rvfile_t* kernel_file;
    rvfile_t* fdt_file;

    rvvm_intc_t* intc;
    pci_bus_t*   pci_bus;
    i2c_bus_t*   i2c_bus;

    gdb_server_t* gdbstub;

    rvvm_addr_t opts[RVVM_OPTS_ARR_SIZE];

#if defined(USE_FDT)
    // FDT nodes for device tree generation
    struct fdt_node* fdt;
    struct fdt_node* fdt_soc;
#endif
};

void rvvm_append_isa_string(rvvm_machine_t* machine, const char* str);

#endif
