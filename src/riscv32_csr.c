/*
riscv32_csr.c - RISC-V Control and Status Register
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
#include "bit_ops.h"

riscv32_csr_t riscv32_csr_list[4096];

void riscv32_csr_init(uint32_t csr_id, const char *name, riscv32_csr_handler_t handler)
{
    riscv32_csr_list[csr_id].name = name;
    riscv32_csr_list[csr_id].handler = handler;
}

bool riscv32_csr_unimp(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(vm);
    UNUSED(dest);
    UNUSED(op);
    riscv32_debug_always(vm, "unimplemented csr %c!!!", csr_id);
    return false;
}

bool riscv32_csr_illegal(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(vm);
    UNUSED(csr_id);
    UNUSED(dest);
    UNUSED(op);
    return false;
}
