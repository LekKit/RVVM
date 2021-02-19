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

    if(!self->callback)
        vm->registers[reg] = self->value;
    else
        riscv32i_write_register_u(vm, reg, self->callback(vm, self, RISCV32_CSR_OPERATION_READ, 0));

    return true;
}

bool riscv32_csr_write(riscv32_vm_state_t *vm, uint32_t csr, uint32_t reg)
{
    uint32_t access = cut_bits(csr, 10, 2);
    uint32_t minimal_level = cut_bits(csr, 8, 2);
    uint32_t csr8 = cut_bits(csr, 0, 8);
    riscv32_csr_t *self = &vm->csr[access][csr8];

    //TODO: error here?
    if(vm->priv_mode < minimal_level)
        return false;

    // check read only
    if(access == 0x3)
        return false;

    self->callback(vm, self, RISCV32_CSR_OPERATION_WRITE, riscv32i_read_register_u(vm, reg));
    return true;
}

bool riscv32_csr_swap(riscv32_vm_state_t *vm, uint32_t csr, uint32_t rs, uint32_t rds)
{
    uint32_t access = cut_bits(csr, 10, 2);
    uint32_t minimal_level = cut_bits(csr, 8, 2);
    uint32_t csr8 = cut_bits(csr, 0, 8);
    riscv32_csr_t *self = &vm->csr[access][csr8];
    uint32_t temp = 0;

    //TODO: error here?
    if(vm->priv_mode < minimal_level)
        return false;

    // check read only
    if(access == 0x3) {
        riscv32i_write_register_u(vm, rds, self->value);
        return true;
    }

    if(self->callback) {
        temp = self->callback(vm, self, RISCV32_CSR_OPERATION_READ, 0);
        self->callback(vm, self, RISCV32_CSR_OPERATION_WRITE, riscv32i_read_register_u(vm, rs));
        riscv32i_write_register_u(vm, rds, temp);
        return true;
    }
    return false;
}

void riscv32_csr_init(riscv32_vm_state_t *vm, const char *name, uint32_t csr, uint32_t (*callback)(riscv32_vm_state_t *, riscv32_csr_t *, uint8_t, uint32_t))
{
    uint32_t access = cut_bits(csr, 10, 2);
    uint32_t csr8 = cut_bits(csr, 0, 8);
    riscv32_csr_t *self = &vm->csr[access][csr8];

    self->name = name;

    // read only?
    if(access == 0x3) {
        self->value = callback(vm, &vm->csr[access][csr8], RISCV32_CSR_OPERATION_READ, 0);
        self->callback = NULL;
    } else {
        self->callback = callback;
    }
}

