/*
riscv32_csr_m.c - RISC-V Machine Level Control and Status Registers
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

#define RISCV32_MISA_RV32  0x40000000
#define RISCV32_MISA_RV64  0x80000000
#define RISCV32_MISA_RV128 0xC0000000

static uint32_t riscv32_mkmisa(const char* str)
{
    uint32_t ret = RISCV32_MISA_RV32;
    while (*str) {
        ret |= (1 << (*str - 'A'));
        str++;
    }
    return ret;
}

void riscv32_csr_m_init(riscv32_vm_state_t *vm)
{
    riscv32_csr_init(vm, 0xF11, "mvendor", 0, riscv32_csr_generic_ro);
    riscv32_csr_init(vm, 0xF12, "marchid", 0, riscv32_csr_generic_ro);
    riscv32_csr_init(vm, 0xF13, "mimpid", 0, riscv32_csr_generic_ro);
    riscv32_csr_init(vm, 0xF14, "mhartid", 0, riscv32_csr_generic_ro);

    riscv32_csr_init(vm, 0x300, "mstatus", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x301, "misa", riscv32_mkmisa("ICMAS"), riscv32_csr_generic_ro);
    riscv32_csr_init(vm, 0x302, "medeleg", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x303, "mideleg", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x304, "mie", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x305, "mtvec", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x306, "mcounteren", 0, riscv32_csr_generic_rw);

    riscv32_csr_init(vm, 0x340, "mscratch", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x341, "mepc", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x342, "mcause", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x343, "mtval", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x344, "mip", 0, riscv32_csr_generic_rw);

    riscv32_csr_init(vm, 0x3A0, "pmpcfg0", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3A1, "pmpcfg1", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3A2, "pmpcfg2", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3A3, "pmpcfg3", 0, riscv32_csr_generic_rw);

    riscv32_csr_init(vm, 0x3B0, "pmpaddr0", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3B1, "pmpaddr1", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3B2, "pmpaddr2", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3B3, "pmpaddr3", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3B4, "pmpaddr4", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3B5, "pmpaddr5", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3B6, "pmpaddr6", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3B7, "pmpaddr7", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3B8, "pmpaddr8", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3B9, "pmpaddr9", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3BA, "pmpaddr10", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3BB, "pmpaddr11", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3BC, "pmpaddr12", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3BD, "pmpaddr13", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3BE, "pmpaddr14", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x3BF, "pmpaddr15", 0, riscv32_csr_generic_rw);
}
