/*
riscv32_csr.h - RISC-V Control and Status Register
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

#include "riscv32.h"
#include "bit_ops.h"

#define CSR_SWAP       0x0
#define CSR_SETBITS    0x1
#define CSR_CLEARBITS  0x2

inline bool riscv32_csr_op(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint32_t op)
{
    uint32_t csr8 = cut_bits(csr_id, 0, 8);
    uint32_t priv = cut_bits(csr_id, 8, 2);
    riscv32_csr_t *self = &vm->csr[priv][csr8];
    if (priv > vm->priv_mode)
        return false;
    else
        return self->handler(vm, self, dest, op);
}

inline bool csr_helper_check_rw(riscv32_csr_t* csr, uint32_t val, uint32_t op)
{
    switch (op) {
    case CSR_SWAP:
        return val != csr->value;
    case CSR_SETBITS:
        return val != 0;
    case CSR_CLEARBITS:
        return val != 0;
    default:
        return true;
    }
}

inline void csr_helper_rw(riscv32_csr_t* csr, uint32_t* dest, uint32_t op)
{
    uint32_t tmp;
    switch (op) {
    case CSR_SWAP:
        tmp = csr->value;
        csr->value = *dest;
        *dest = tmp;
        break;
    case CSR_SETBITS:
        tmp = csr->value;
        csr->value = tmp | *dest;
        *dest = tmp;
        break;
    case CSR_CLEARBITS:
        tmp = csr->value;
        csr->value = tmp & (~*dest);
        *dest = tmp;
        break;
    }
}

void riscv32_csr_init(riscv32_vm_state_t *vm, uint32_t csr_id, const char *name, uint32_t def_val, riscv32_csr_handler_t handler);
bool riscv32_csr_generic_rw(riscv32_vm_state_t *vm, riscv32_csr_t* csr, uint32_t* dest, uint32_t op);
bool riscv32_csr_generic_ro(riscv32_vm_state_t *vm, riscv32_csr_t* csr, uint32_t* dest, uint32_t op);
bool riscv32_csr_illegal(riscv32_vm_state_t *vm, riscv32_csr_t* csr, uint32_t* dest, uint32_t op);
