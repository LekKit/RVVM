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

#define CSR_GENERIC_MASK (-(reg_t)1)

#define CSR_STATUS_UIE 0
#define CSR_STATUS_SIE 1
#define CSR_STATUS_MIE 3
#define CSR_STATUS_UPIE 4
#define CSR_STATUS_SPIE 5
#define CSR_STATUS_MPIE 7
#define CSR_STATUS_SPP 8

#define CSR_STATUS_MPP_START 11
#define CSR_STATUS_MPP_SIZE 2
#define CSR_STATUS_FS_START 13
#define CSR_STATUS_FS_SIZE 2
#define CSR_STATUS_XS_START 15
#define CSR_STATUS_XS_SIZE 2

#define CSR_STATUS_MPRV 17
#define CSR_STATUS_SUM 18
#define CSR_STATUS_MXR 19
#define CSR_STATUS_TVM 20
#define CSR_STATUS_TW 21
#define CSR_STATUS_TSR 22

#define CSR_STATUS_UXL_START 32
#define CSR_STATUS_UXL_SIZE 2
#define CSR_STATUS_SXL_START 34
#define CSR_STATUS_SXL_SIZE 2

#define CSR_STATUS_SD(vm) (XLEN(vm) - 1)

typedef bool (*riscv32_csr_handler_t)(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op);

struct riscv32_csr_t {
    const char *name;
    riscv32_csr_handler_t handler;
};

extern riscv32_csr_t riscv32_csr_list[4096];

static inline bool riscv32_csr_op(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint32_t op)
{
    uint32_t priv = cut_bits(csr_id, 8, 2);
    if (priv > vm->priv_mode)
        return false;
    else
        return riscv32_csr_list[csr_id].handler(vm, csr_id, dest, op);
}

static inline uint32_t csr_helper_rw(reg_t csr_val, reg_t* dest, uint32_t op, reg_t mask)
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

static inline void csr_helper_masked(reg_t* csr, reg_t* dest, uint32_t op, reg_t mask)
{
    *csr = csr_helper_rw(*csr, dest, op, mask);
}

static inline void csr_helper(reg_t* csr, reg_t* dest, uint32_t op)
{
    *csr = csr_helper_rw(*csr, dest, op, CSR_GENERIC_MASK);
}

void riscv32_csr_init(uint32_t csr_id, const char *name, riscv32_csr_handler_t handler);
bool riscv32_csr_unimp(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op);
bool riscv32_csr_illegal(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op);
void riscv32_csr_isa_change(riscv32_vm_state_t *vm, uint8_t priv, uint8_t target_isa);

void riscv32_csr_m_init();
void riscv32_csr_s_init();
void riscv32_csr_u_init();
