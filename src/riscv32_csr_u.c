/*
riscv32_csr_u.c - RISC-V User Level Control and Status Registers
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

#include "riscv32.h"
#include "riscv32_csr.h"

#define CSR_USTATUS_MASK 0x11

extern uint64_t clint_mtime;

static bool riscv32_csr_time(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    UNUSED(op);
    rvtimer_update(&vm->timer);
    *dest = vm->timer.time;
    return true;
}

static bool riscv32_csr_timeh(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    UNUSED(op);
    *dest = vm->timer.time >> 32;
    return true;
}

void riscv32_csr_u_init()
{
    // User Trap Setup
    riscv32_csr_init(0x000, "ustatus", riscv32_csr_unimp);
    riscv32_csr_init(0x004, "uie", riscv32_csr_unimp);
    riscv32_csr_init(0x005, "utvec", riscv32_csr_unimp);

    // User Trap Handling
    riscv32_csr_init(0x040, "uscratch", riscv32_csr_unimp);
    riscv32_csr_init(0x041, "uepc", riscv32_csr_unimp);
    riscv32_csr_init(0x042, "ucause", riscv32_csr_unimp);
    riscv32_csr_init(0x043, "utval", riscv32_csr_unimp);
    riscv32_csr_init(0x044, "uip", riscv32_csr_unimp);

    // User Floating-Point CSRs
    riscv32_csr_init(0x001, "fflags", riscv32_csr_unimp);
    riscv32_csr_init(0x002, "frm", riscv32_csr_unimp);
    riscv32_csr_init(0x003, "fcsr", riscv32_csr_unimp);

    // User Counter/Timers
    riscv32_csr_init(0xC00, "cycle", riscv32_csr_unimp);
    riscv32_csr_init(0xC01, "time", riscv32_csr_time);
    riscv32_csr_init(0xC02, "instret", riscv32_csr_unimp);
    riscv32_csr_init(0xC80, "cycleh", riscv32_csr_unimp);
    riscv32_csr_init(0xC81, "timeh", riscv32_csr_timeh);
    riscv32_csr_init(0xC82, "instreth", riscv32_csr_unimp);

    for (uint32_t i=3; i<32; ++i) {
        riscv32_csr_init(0xC00+i, "hpmcounter", riscv32_csr_unimp);
        riscv32_csr_init(0xC80+i, "hpmcounterh", riscv32_csr_unimp);
    }
}
