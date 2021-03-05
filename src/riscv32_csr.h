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

#define CSR_GENERIC_MASK 0xFFFFFFFF

typedef bool (*riscv32_csr_handler_t)(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op);

struct riscv32_csr_t {
    const char *name;
    riscv32_csr_handler_t handler;
};

extern riscv32_csr_t riscv32_csr_list[4096];

inline bool riscv32_csr_op(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint32_t op)
{
    uint32_t priv = cut_bits(csr_id, 8, 2);
    if (priv > vm->priv_mode)
        return false;
    else
        return riscv32_csr_list[csr_id].handler(vm, csr_id, dest, op);
}

inline uint32_t csr_helper_rw(uint32_t csr_val, uint32_t* dest, uint32_t op, uint32_t mask)
{
    uint32_t tmp = 0;
    switch (op) {
    case CSR_SWAP:
        tmp = (*dest & mask) | (csr_val & (~mask));
        *dest = csr_val & mask;
        break;
    case CSR_SETBITS:
        tmp = csr_val | (*dest & mask);
        *dest = csr_val & mask;
        break;
    case CSR_CLEARBITS:
        tmp = csr_val & (~(*dest & mask));
        *dest = csr_val & mask;
        break;
    }
    return tmp;
}

inline void csr_helper_masked(uint32_t* csr, uint32_t* dest, uint32_t op, uint32_t mask)
{
    *csr = csr_helper_rw(*csr, dest, op, mask);
}

inline void csr_helper(uint32_t* csr, uint32_t* dest, uint32_t op)
{
    *csr = csr_helper_rw(*csr, dest, op, CSR_GENERIC_MASK);
}

void riscv32_csr_init(uint32_t csr_id, const char *name, riscv32_csr_handler_t handler);
bool riscv32_csr_unimp(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op);
bool riscv32_csr_illegal(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op);

void riscv32_csr_m_init();
void riscv32_csr_s_init();
void riscv32_csr_u_init();
