/*
riscv32.h - RISC-V virtual machine code definitions
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

#pragma once

#include "riscv.h"
#include "rvvm_types.h"
#include "rvtimer.h"
#include "utils.h"

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

#define FPU_REGISTERS_MAX 32

enum
{
    PRIVILEGE_USER,
    PRIVILEGE_SUPERVISOR,
    PRIVILEGE_HYPERVISOR,
    PRIVILEGE_MACHINE
};

#define INTERRUPT_MASK 0x80000000

#define INTERRUPT_USOFTWARE    0x0
#define INTERRUPT_SSOFTWARE    0x1
#define INTERRUPT_MSOFTWARE    0x3
#define INTERRUPT_UTIMER       0x4
#define INTERRUPT_STIMER       0x5
#define INTERRUPT_MTIMER       0x7
#define INTERRUPT_UEXTERNAL    0x8
#define INTERRUPT_SEXTERNAL    0x9
#define INTERRUPT_MEXTERNAL    0xB

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

#define TLB_SIZE 256  // Always nonzero, power of 2 (1, 2, 4..)

// Address translation cache
typedef struct {
    vaddr_t pte;    // Upper 20 bits of virtual address + access bits
    vmptr_t ptr;    // Page address in emulator memory
} riscv32_tlb_t;

typedef struct {
    vmptr_t data;   // Pointer to 0x0 physical address (Do not use out of physical memory boundaries!)
    paddr_t begin;  // First usable address in physical memory
    paddr_t size;   // Amount of usable memory after mem_begin
} riscv32_phys_mem_t;

typedef struct rvvm_hart_t rvvm_hart_t;
typedef struct riscv32_csr_t riscv32_csr_t;
typedef struct riscv32_mmio_device_t riscv32_mmio_device_t;

typedef bool (*riscv32_mmio_handler_t)(struct rvvm_hart_t* vm, riscv32_mmio_device_t* device, uint32_t addr, void* dest, uint32_t size, uint8_t access);

struct riscv32_mmio_device_t {
    paddr_t base_addr;
    paddr_t end_addr;
    riscv32_mmio_handler_t handler;
    void* data;
};

typedef struct {
    uint32_t count;
    riscv32_mmio_device_t regions[256];
} riscv32_mmio_regions_t;

struct rvvm_hart_t {
    size_t wait_event;
    maxlen_t registers[REGISTERS_MAX];
    riscv32_tlb_t tlb[TLB_SIZE];
    riscv32_phys_mem_t mem;
    riscv32_mmio_regions_t mmio;

    struct {
        uint32_t status;
        uint32_t edeleg[4];
        uint32_t ideleg[4];
        uint32_t ie;
        uint32_t tvec[4];
        uint32_t counteren[4];
        uint32_t scratch[4];
        uint32_t epc[4];
        uint32_t cause[4];
        uint32_t tval[4];
        uint32_t ip;
        uint32_t fcsr;
    } csr;
    fmaxlen_t fpu_registers[FPU_REGISTERS_MAX];
    paddr_t root_page_table;
    bool mmu_virtual;
    uint8_t priv_mode;
    rvtimer_t timer;
    bool ev_trap;
    bool ev_int; // delivered from IRQ thread
    uint32_t ev_int_mask;
};

#define RISCV32I_OPCODE_MASK 0x3

//#define RV_DEBUG
//#define RV_DEBUG_FULL
//#define RV_DEBUG_SINGLESTEP

enum reg_status
{
    S_OFF,
    S_INITIAL,
    S_CLEAN,
    S_DIRTY
};

/* Sets the FS and SD fields of mstatus CSR */
void fpu_set_fs(rvvm_hart_t *vm, uint8_t value);

/* Checks that FS is not set to S_OFF */
bool fpu_is_enabled(rvvm_hart_t *vm);

enum
{
    RM_RNE = 0, /* round to nearest, ties to even */
    RM_RTZ = 1, /* round to zero */
    RM_RDN = 2, /* round down - towards -inf */
    RM_RUP = 3, /* round up - towards +inf */
    RM_RMM = 4, /* round to nearest, ties to max magnitude */
    RM_DYN = 7, /* round to instruction's rm field */
    RM_INVALID = 255, /* invalid rounding mode was specified - should cause a trap */
};

/* type for rounding mode */
typedef uint8_t rm_t;
/* Sets rounding mode, returns previous value */
rm_t fpu_set_rm(rvvm_hart_t *vm, rm_t newrm);
/* Enables/disables FPU instructions */
extern void riscv32f_enable(bool enable);
extern void riscv32d_enable(bool enable);
#if MAX_XLEN > 32
extern void riscv64f_enable(bool enable);
extern void riscv64d_enable(bool enable);
#endif

void riscv32_debug_func(const rvvm_hart_t *vm, const char* fmt, ...);

#ifdef RV_DEBUG
#define riscv32_debug_always riscv32_debug_func
#else
#define riscv32_debug_always(...)
#endif

#ifdef RV_DEBUG_FULL
#define riscv32_debug riscv32_debug_func
#else
#define riscv32_debug(...)
#endif

#define UNUSED(x) (void)x

rvvm_hart_t *riscv32_create_vm();
void riscv32_run(rvvm_hart_t *vm);
void riscv32_destroy_vm(rvvm_hart_t *vm);
void riscv32_dump_registers(rvvm_hart_t *vm);
void riscv32_illegal_insn(rvvm_hart_t *vm, const uint32_t instruction);
void riscv32c_illegal_insn(rvvm_hart_t *vm, const uint16_t instruction);
void riscv32_priv_init();
bool riscv32_handle_ip(rvvm_hart_t *vm, bool wfi);
void riscv32_interrupt(rvvm_hart_t *vm, uint32_t cause);
void riscv32_trap(rvvm_hart_t *vm, uint32_t cause, uint32_t tval);
