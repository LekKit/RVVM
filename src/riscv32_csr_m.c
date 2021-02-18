/*
riscv32_csr_m.c - RISC-V Machine Control and Status Registers
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

#include "riscv32.h"
#include "riscv32_csr.h"

#define RISCV32_MISA_CSR_RV32 0x40000000
#define RISCV32_MISA_CSR_RV64 0x80000000
#define RISCV32_MISA_CSR_RV128 0xc0000000

#define RISCV32_MISA_CSR_A (1u << 0) // Atomic extension
#define RISCV32_MISA_CSR_B (1u << 1) // Reversed for Bit Maniplation extension
#define RISCV32_MISA_CSR_C (1u << 2) // Compressed extension
#define RISCV32_MISA_CSR_D (1u << 3) // Double-precision floating-point extension
#define RISCV32_MISA_CSR_E (1u << 4) // RV32E base ISA
#define RISCV32_MISA_CSR_F (1u << 5) // Single-precision floating-point extension
#define RISCV32_MISA_CSR_G (1u << 6) // Additional standard extension present
#define RISCV32_MISA_CSR_H (1u << 7) // Hypervisor extension
#define RISCV32_MISA_CSR_I (1u << 8) // RV32I/64I/128I base ISA
#define RISCV32_MISA_CSR_J (1u << 9) // Reversed for Dynamically Translated Languages extension
#define RISCV32_MISA_CSR_K (1u << 10) // Reversed
#define RISCV32_MISA_CSR_L (1u << 11) // Reversed for Decimical floating-point extension
#define RISCV32_MISA_CSR_M (1u << 12) // Integer Multiply/Divide extension
#define RISCV32_MISA_CSR_N (1u << 13) // User level interrups supported
#define RISCV32_MISA_CSR_O (1u << 14) // Reversed
#define RISCV32_MISA_CSR_P (1u << 15) // Reversed for Packet-SIMD extension
#define RISCV32_MISA_CSR_Q (1u << 16) // Quad-precision floating-point extension
#define RISCV32_MISA_CSR_R (1u << 17) // Reversed
#define RISCV32_MISA_CSR_S (1u << 18) // Supervisor mode implemented
#define RISCV32_MISA_CSR_T (1u << 19) // Reversed for Transactional Memory extension
#define RISCV32_MISA_CSR_U (1u << 20) // User mode implemented
#define RISCV32_MISA_CSR_V (1u << 21) // Reversed for Vector extension
#define RISCV32_MISA_CSR_W (1u << 22) // Reversed
#define RISCV32_MISA_CSR_X (1u << 23) // Non-standard extension present
#define RISCV32_MISA_CSR_Y (1u << 24) // Reversed
#define RISCV32_MISA_CSR_Z (1u << 25) // Reversed

// can be 0 but we setup it normaly
static uint32_t riscv32_csr_misa_read(riscv32_vm_state_t *vm, riscv32_csr_t *self)
{
    //TODO: allow change bits
    return RISCV32_MISA_CSR_RV32 | RISCV32_MISA_CSR_I | RISCV32_MISA_CSR_C | RISCV32_MISA_CSR_M;
}

static uint32_t riscv32_csr_unimplemented_m(riscv32_vm_state_t *vm, riscv32_csr_t *self)
{
    return 0;
}

void riscv32_csr_m_init(riscv32_vm_state_t *vm)
{
    riscv32_csr_init(vm, "misa", 0xF11, riscv32_csr_misa_read, NULL);
}
