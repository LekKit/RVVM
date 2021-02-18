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

static const char *riscv32_csr_level(uint32_t level)
{
    switch (level) {
    case 0: return "U";
    case 1: return "S";
    case 2: return "H";
    case 4: return "M";
    default: return "unknown";
    }
}

bool riscv32_csr_read(riscv32_vm_state_t *vm, uint32_t csr, uint32_t reg)
{
    uint32_t access = cut_bits(csr, 10, 2);
    uint32_t minimal_level = cut_bits(csr, 8, 2);
    uint32_t csr8 = cut_bits(csr, 0, 8);
    riscv32_csr_t *self = &vm->csr[access][csr8];

    //TODO: error here?
    if(vm->priv_mode < minimal_level)
        return false;

    if(self->name != NULL)
        printf("%s %x %x %x %x\n", vm->csr[access][csr8].name, csr, csr8, access, minimal_level);
    else
        printf("%x %x %x %x\n", csr, csr8, access, minimal_level);

    if(self->callback_r)
        vm->registers[reg] = self->value;
    else
        riscv32i_write_register_u(vm, reg, self->callback_r(vm, self));

    return true;
}

bool riscv32_csr_write(riscv32_vm_state_t *vm, uint32_t csr, uint32_t reg)
{
    uint32_t access = cut_bits(csr, 10, 2);
    uint32_t minimal_level = cut_bits(csr, 8, 2);
    uint32_t csr8 = cut_bits(csr, 0, 8);
    riscv32_csr_t *self = &vm->csr[access][csr8];

    // check read only
    if(access == 0x3)
        return false;

    //TODO: error here?
    if(vm->priv_mode < minimal_level)
        return false;

    self->callback_w(vm, self, riscv32i_read_register_u(vm, reg));
    return true;
}

bool riscv32_csr_swap(riscv32_vm_state_t *vm, uint32_t csr, uint32_t rs, uint32_t rds)
{
    uint32_t access = cut_bits(csr, 10, 2);
    uint32_t minimal_level = cut_bits(csr, 8, 2);
    uint32_t csr8 = cut_bits(csr, 0, 8);
    riscv32_csr_t *self = &vm->csr[access][csr8];
    uint32_t temp = 0;

    // check read only
    if(access == 0x3)
        return false;

    //TODO: error here?
    if(vm->priv_mode < minimal_level)
        return false;

    if(self->callback_r)
    {
        temp = self->callback_r(vm, self);
        self->callback_w(vm, self, riscv32i_read_register_u(vm, rs));
        riscv32i_write_register_u(vm, rds, temp);
        return true;
    }
    return false;
}

void riscv32_csr_init(riscv32_vm_state_t *vm, const char *name, uint32_t csr, uint32_t (*callback_r)(riscv32_vm_state_t *vm, riscv32_csr_t *self), void (*callback_w)(riscv32_vm_state_t *vm, riscv32_csr_t *self, uint32_t value))
{
    uint32_t access = cut_bits(csr, 10, 2);
    uint32_t csr8 = cut_bits(csr, 0, 8);

    vm->csr[access][csr8].name = name;

    // read only
    if(access == 0x3)
    {
        vm->csr[access][csr8].value = callback_r(vm, &vm->csr[access][csr8]);
    } else {
        vm->csr[access][csr8].callback_r = callback_r;
        vm->csr[access][csr8].callback_w = callback_w;
    }
}

