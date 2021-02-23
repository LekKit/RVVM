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

void riscv32_csr_u_init(riscv32_vm_state_t *vm)
{
    // User Trap Setup
    riscv32_csr_init(vm, 0x000, "ustatus", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x004, "uie", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x005, "utvec", 0, riscv32_csr_generic_rw);

    // User Trap Handling
    riscv32_csr_init(vm, 0x040, "uscratch", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x041, "uepc", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x042, "ucause", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x043, "utval", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x044, "uip", 0, riscv32_csr_generic_rw);

    // User Floating-Point CSRs
    riscv32_csr_init(vm, 0x001, "fflags", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x002, "frm", 0, riscv32_csr_generic_rw);
    riscv32_csr_init(vm, 0x003, "fcsr", 0, riscv32_csr_generic_rw);

    // User Counter/Timers
    riscv32_csr_init(vm, 0xC00, "cycle", 0, riscv32_csr_generic_ro);
    riscv32_csr_init(vm, 0xC01, "time", 0, riscv32_csr_generic_ro);
    riscv32_csr_init(vm, 0xC02, "instret", 0, riscv32_csr_generic_ro);
    riscv32_csr_init(vm, 0xC80, "cycleh", 0, riscv32_csr_generic_ro);
    riscv32_csr_init(vm, 0xC81, "timeh", 0, riscv32_csr_generic_ro);
    riscv32_csr_init(vm, 0xC82, "instreth", 0, riscv32_csr_generic_ro);

    for (uint32_t i=3; i<32; ++i) {
        riscv32_csr_init(vm, 0xC03+i, "hpmcounter", 0, riscv32_csr_generic_ro);
        riscv32_csr_init(vm, 0xC83+i, "hpmcounterh", 0, riscv32_csr_generic_ro);
    }
}
