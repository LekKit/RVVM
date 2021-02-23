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

void riscv32_csr_s_init(riscv32_vm_state_t *vm)
{
    // Supervisor Trap Setup
    riscv32_csr_init(vm, 0x100, "sstatus", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x102, "sedeleg", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x103, "sideleg", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x104, "sie", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x105, "stvec", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x106, "scounteren", 0, riscv32_csr_generic_rw);

    // Supervisor Trap Handling
    riscv32_csr_init(vm, 0x140, "sscratch", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x141, "sepc", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x142, "scause", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x143, "stval", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x144, "sip", 0, riscv32_csr_generic_rw);

    // Supervisor Protection and Translation
    riscv32_csr_init(vm, 0x180, "satp", 0, riscv32_csr_generic_rw);
}
