/*
rvvm.h - RISC-V Virtual Machine
Copyright (C) 2021  LekKit <github.com/LekKit>
                    cerg2010cerg2010 <github.com/cerg2010cerg2010>
                    Mr0maks <mr.maks0443@gmail.com>
                    KotB <github.com/0xCatPKG>
                    X547 <github.com/X547>

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

#ifndef RVVM_H
#define RVVM_H

#include "rvvmlib.h"
#include "rvvm_types.h"
#include "compiler.h"
#include "utils.h"
#include "vector.h"
#include "rvtimer.h"
#include "threading.h"
#include "blk_io.h"
#include "fdtlib.h"

#ifdef USE_JIT
#include "rvjit/rvjit.h"
#endif

#define TLB_SIZE 256  // Always nonzero, power of 2 (32, 64..)

enum
{
    REGISTER_ZERO,
    REGISTER_X0 = REGISTER_ZERO,
    REGISTER_X1,
    REGISTER_X2,
    REGISTER_X3,
    REGISTER_X4,
    REGISTER_X5,
    REGISTER_X6,
    REGISTER_X7,
    REGISTER_X8,
    REGISTER_X9,
    REGISTER_X10,
    REGISTER_X11,
    REGISTER_X12,
    REGISTER_X13,
    REGISTER_X14,
    REGISTER_X15,
    REGISTER_X16,
    REGISTER_X17,
    REGISTER_X18,
    REGISTER_X19,
    REGISTER_X20,
    REGISTER_X21,
    REGISTER_X22,
    REGISTER_X23,
    REGISTER_X24,
    REGISTER_X25,
    REGISTER_X26,
    REGISTER_X27,
    REGISTER_X28,
    REGISTER_X29,
    REGISTER_X30,
    REGISTER_X31,
    REGISTER_PC,
    REGISTERS_MAX
};

#define FPU_REGISTERS_MAX REGISTERS_MAX

enum
{
    PRIVILEGE_USER,
    PRIVILEGE_SUPERVISOR,
    PRIVILEGE_HYPERVISOR,
    PRIVILEGE_MACHINE,
    PRIVILEGES_MAX
};

#define INTERRUPT_USOFTWARE    0x0
#define INTERRUPT_SSOFTWARE    0x1
#define INTERRUPT_MSOFTWARE    0x3
#define INTERRUPT_UTIMER       0x4
#define INTERRUPT_STIMER       0x5
#define INTERRUPT_MTIMER       0x7
#define INTERRUPT_UEXTERNAL    0x8
#define INTERRUPT_SEXTERNAL    0x9
#define INTERRUPT_MEXTERNAL    0xB

// Internal events delivered to the hart
#define EXT_EVENT_PAUSE        0x1 // Pause the hart in a consistent state
#define EXT_EVENT_PREEMPT      0x2 // Preempt the hart

#define TRAP_INSTR_MISALIGN    0x0
#define TRAP_INSTR_FETCH       0x1
#define TRAP_ILL_INSTR         0x2
#define TRAP_BREAKPOINT        0x3
#define TRAP_LOAD_MISALIGN     0x4
#define TRAP_LOAD_FAULT        0x5
#define TRAP_STORE_MISALIGN    0x6
#define TRAP_STORE_FAULT       0x7
#define TRAP_ENVCALL_UMODE     0x8
#define TRAP_ENVCALL_SMODE     0x9
#define TRAP_ENVCALL_MMODE     0xB
#define TRAP_INSTR_PAGEFAULT   0xC
#define TRAP_LOAD_PAGEFAULT    0xD
#define TRAP_STORE_PAGEFAULT   0xF

typedef struct rvvm_hart_t rvvm_hart_t;

/*
 * Address translation cache
 * In future, it would be nice to verify if cache-line alignment
 * gives any profit (entries scattered between cachelines waste L1)
 */
typedef struct {
    // Pointer to page (with vaddr subtracted? faster tlb translation)
    size_t ptr;
    // Make entry size a power of 2 (32 or 16 bytes)
#if !defined(HOST_64BIT) && defined(USE_RV64)
    size_t align;
#endif
    // Virtual page number per each op type (vaddr >> 12)
    virt_addr_t r;
    virt_addr_t w;
    virt_addr_t e;
#if defined(HOST_64BIT) && !defined(USE_RV64)
    virt_addr_t align[3];
#endif
} rvvm_tlb_entry_t;

#ifdef USE_JIT
typedef struct {
    // Pointer to code block
    rvjit_func_t block;
#if !defined(HOST_64BIT) && defined(USE_RV64)
    size_t align;
#endif
    // Virtual PC of this entry
    virt_addr_t pc;
#if defined(HOST_64BIT) && !defined(USE_RV64)
    virt_addr_t align;
#endif
} rvvm_jtlb_entry_t;
#endif

typedef struct {
    phys_addr_t begin; // First usable address in physical memory
    phys_addr_t size;  // Memory amount (since the region may be empty)
    vmptr_t data;      // Pointer to memory data
} rvvm_ram_t;

typedef struct {
    // Virtual page number per each op type (vaddr >> 12)
    virt_addr_t r;
    virt_addr_t w;
    virt_addr_t e;
    // Physical address of the page mapped to the device
    phys_addr_t phys;
    // The device itself
    const rvvm_mmio_dev_t* mmio;
} rvvm_mmio_tlb_t;

struct rvvm_hart_t {
    uint32_t wait_event;
    maxlen_t registers[REGISTERS_MAX];
#ifdef USE_FPU
    double fpu_registers[FPU_REGISTERS_MAX];
#endif

    // We want short offsets from vmptr to tlb
    rvvm_tlb_entry_t tlb[TLB_SIZE];
#ifdef USE_JIT
    rvvm_jtlb_entry_t jtlb[TLB_SIZE];
#endif
    rvvm_ram_t mem;
    rvvm_machine_t* machine;
    phys_addr_t root_page_table;
    uint8_t mmu_mode;
    uint8_t priv_mode;
    bool rv64;
    bool trap;
    maxlen_t trap_pc;

    bool user_traps;

    bool lrsc;
    maxlen_t lrsc_cas;

    struct {
        maxlen_t hartid;
        maxlen_t isa;
        maxlen_t status;
        maxlen_t edeleg[PRIVILEGES_MAX];
        maxlen_t ideleg[PRIVILEGES_MAX];
        maxlen_t ie;
        maxlen_t tvec[PRIVILEGES_MAX];
        maxlen_t scratch[PRIVILEGES_MAX];
        maxlen_t epc[PRIVILEGES_MAX];
        maxlen_t cause[PRIVILEGES_MAX];
        maxlen_t tval[PRIVILEGES_MAX];
        maxlen_t ip;
        maxlen_t fcsr;
    } csr;
#ifdef USE_JIT
    rvjit_block_t jit;
    bool jit_enabled;
    bool jit_compiling;
    bool block_ends;
    bool ldst_trace;
#endif
    thread_ctx_t* thread;
    cond_var_t* wfi_cond;
    rvtimer_t timer;
    uint32_t pending_irqs;
    uint32_t pending_events;
    uint32_t preempt_ms;
    // Cacheline alignment
    uint8_t align[64];
};

struct rvvm_machine_t {
    rvvm_ram_t mem;
    vector_t(rvvm_hart_t*) harts;
    vector_t(rvvm_mmio_dev_t) mmio;
    rvtimer_t timer;
    uint32_t running;
    uint32_t power_state;
    bool rv64;

    rvfile_t* bootrom_file;
    rvfile_t* kernel_file;
    rvfile_t* dtb_file;

    rvvm_reset_handler_t on_reset;
    void* reset_data;

    plic_ctx_t* plic;
    pci_bus_t*  pci_bus;
    i2c_bus_t*  i2c_bus;

    rvvm_addr_t opts[RVVM_MAX_OPTS];
#ifdef USE_FDT
    // FDT nodes for device tree generation
    struct fdt_node* fdt;
    struct fdt_node* fdt_soc;
    // Kernel cmdline
    char* cmdline;
#endif
};

#endif
