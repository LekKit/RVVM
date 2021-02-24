/*
riscv32.h - RISC-V virtual machine code definitions
Copyright (C) 2021  Mr0maks <mr.maks0443@gmail.com>
                    LekKit <github.com/LekKit>

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

#define TLB_SIZE 32  // Always nonzero, power of 2 (1, 2, 4..)

// Address translation cache
typedef struct {
    uint32_t pte;    // Upper 20 bits of virtual address + access bits
    uint8_t* ptr;    // Page address in emulator memory
} riscv32_tlb_t;

typedef struct {
    uint8_t* data;   // Pointer to 0x0 physical address (Do not use out of physical memory boundaries!)
    uint32_t begin;  // First usable address in physical memory
    uint32_t size;   // Amount of usable memory after mem_begin
} riscv32_phys_mem_t;

typedef struct riscv32_vm_state_t riscv32_vm_state_t;
typedef struct riscv32_csr_t riscv32_csr_t;
typedef struct riscv32_mmio_device_t riscv32_mmio_device_t;

typedef bool (*riscv32_mmio_handler_t)(struct riscv32_vm_state_t* vm, riscv32_mmio_device_t* device, uint32_t addr, void* dest, uint32_t size, uint8_t access);
typedef bool (*riscv32_csr_handler_t)(riscv32_vm_state_t *vm, riscv32_csr_t* csr, uint32_t* dest, uint32_t op);

struct riscv32_csr_t {
    const char *name;
    riscv32_csr_handler_t handler;
    uint32_t value;
};

struct riscv32_mmio_device_t {
    uint32_t base_addr;
    uint32_t end_addr;
    riscv32_mmio_handler_t handler;
    void* data;
};

typedef struct {
    uint32_t count;
    riscv32_mmio_device_t regions[256];
} riscv32_mmio_regions_t;

struct riscv32_vm_state_t {
    size_t wait_event;
    uint32_t registers[REGISTERS_MAX];
    riscv32_tlb_t tlb[TLB_SIZE];
    riscv32_phys_mem_t mem;
    riscv32_mmio_regions_t mmio;

    // To be replaced by CSR
    uint32_t mcause, mtval;
    uint32_t root_page_table;
    riscv32_csr_t csr[4096];
    bool mmu_virtual; // To be replaced by CSR
    uint8_t priv_mode;
};

#define RISCV32I_OPCODE_MASK 0x3

#define RISCV_ALIGN_32 4 // 4 byte align
#define RISCV_ALIGN_16 2 // 2 byte align
#define RISCV_ILEN 4 // 4 byte opcode len

#define RISCV32_LITTLE_ENDIAN (1u << 0)
#define RISCV32_IIS_I (1u << 1) // base and minimal ISA with 32 registers
#define RISCV32_IIS_E (1u << 2) // base and minimal ISA with 16 registers

#define RISCV32_HAVE_NONSTANDART_EXTENSION (1u << 3) // mark cpu with custom opcodes to enable hacks
#define RISCV32_HAVE_M_EXTENSION (1u << 4) // multiplication and division for intergers
#define RISCV32_HAVE_C_EXTENSION (1u << 5) // compressed instructions extension

/*
* Concatenate func7[25] func3[14:12] and opcode[6:2] into 9-bit id for decoding.
* This is tricky for non-R type instructions since there's no func3/func7,
* so we will simply smudge function pointers for those all over the jumptable.
* Theoreticaly, this could be optimized more.
*/
#define RISCV32_GET_FUNCID(x) (((x >> 17) & 0x100) | ((x >> 7) & 0xE0) | ((x >> 2) & 0x1F))

extern void (*riscv32_opcodes[512])(riscv32_vm_state_t *vm, const uint32_t instruction);

/*
* The trick mentioned earlier, to decode non-R type instructions properly.
* smudge_opcode_UJ for U/J types (no func3 or func7)
* smudge_opcode_ISB for I/S/B types (no func7, but has func3)
* R-type instructions (both func3 and func7 present) are simply put into table
*/
void smudge_opcode_UJ(uint32_t opcode, void (*func)(riscv32_vm_state_t*, const uint32_t));
void smudge_opcode_ISB(uint32_t opcode, void (*func)(riscv32_vm_state_t*, const uint32_t));

//#define RV_DEBUG
//#define RV_DEBUG_SINGLESTEP

void riscv32_debug_always(const riscv32_vm_state_t *vm, const char* fmt, ...);

#ifdef RV_DEBUG
#define riscv32_debug riscv32_debug_always
#else
#define riscv32_debug(...)
#endif

#define UNUSED(x) (void)x

riscv32_vm_state_t *riscv32_create_vm();
void riscv32_run(riscv32_vm_state_t *vm);
void riscv32_destroy_vm(riscv32_vm_state_t *vm);
void riscv32_dump_registers(riscv32_vm_state_t *vm);
void riscv32_illegal_insn(riscv32_vm_state_t *vm, const uint32_t instruction);
void riscv32c_illegal_insn(riscv32_vm_state_t *vm, const uint16_t instruction);
void riscv32m_init();
void riscv32c_init();
void riscv32i_init();
void riscv32a_init();
void riscv32_priv_init();
void riscv32_interrupt(riscv32_vm_state_t *vm, uint32_t cause);
void riscv32_trap(riscv32_vm_state_t *vm, uint32_t cause, uint32_t tval);
