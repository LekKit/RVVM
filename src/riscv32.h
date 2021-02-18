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

#define TLB_SIZE 32  // Always nonzero, power of 2 (1, 2, 4..)

typedef struct riscv32_vm_state_s riscv32_vm_state_t;

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

typedef struct riscv32_csr_s {
    const char *name;
    void (*callback_w)(riscv32_vm_state_t *vm, struct riscv32_csr_s*self, uint32_t value);
    uint32_t (*callback_r)(riscv32_vm_state_t *vm, struct riscv32_csr_s *self);
    uint32_t value;
} riscv32_csr_t;

struct riscv32_vm_state_s {
    uint32_t registers[REGISTERS_MAX];
    riscv32_phys_mem_t mem;
    riscv32_tlb_t tlb[TLB_SIZE];
    uint32_t root_page_table;
    riscv32_csr_t csr[4][256];
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

riscv32_vm_state_t *riscv32_create_vm();
void riscv32_run(riscv32_vm_state_t *vm);
void riscv32_destroy_vm(riscv32_vm_state_t *vm);
void riscv32_dump_registers(riscv32_vm_state_t *vm);
void riscv32_illegal_insn(riscv32_vm_state_t *vm, const uint32_t instruction);
void riscv32c_illegal_insn(riscv32_vm_state_t *vm, const uint16_t instruction);
void riscv32m_init();
void riscv32c_init();
void riscv32i_init();
void riscv32_priv_init();
