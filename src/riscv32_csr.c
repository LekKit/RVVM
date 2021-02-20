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
#include "riscv32i.h"
#include "riscv32_csr.h"
#include "bit_ops.h"

void riscv32_csr_init(riscv32_vm_state_t *vm, uint32_t csr_id, const char *name, uint32_t def_val, bool (*handler)(riscv32_vm_state_t *vm, riscv32_csr_t* csr, uint32_t* dest, uint32_t op))
{
    uint32_t csr8 = cut_bits(csr_id, 0, 8);
    uint32_t priv = cut_bits(csr_id, 8, 2);
    riscv32_csr_t *self = &vm->csr[priv][csr8];

    self->name = name;
    self->handler = handler;
    self->value = def_val;
}

bool riscv32_csr_generic_rw(riscv32_vm_state_t *vm, riscv32_csr_t* csr, uint32_t* dest, uint32_t op)
{
    UNUSED(vm);
    csr_helper_rw(csr, dest, op);
    return true;
}

bool riscv32_csr_generic_ro(riscv32_vm_state_t *vm, riscv32_csr_t* csr, uint32_t* dest, uint32_t op)
{
    UNUSED(vm);
    if (csr_helper_check_rw(csr, *dest, op)) {
        return false;
    } else {
        *dest = csr->value;
        return true;
    }
}

bool riscv32_csr_illegal(riscv32_vm_state_t *vm, riscv32_csr_t* csr, uint32_t* dest, uint32_t op)
{
    UNUSED(vm);
    UNUSED(csr);
    UNUSED(dest);
    UNUSED(op);
    return false;
}
