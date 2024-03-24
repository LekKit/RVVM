/*
riscv_csr.h - RISC-V Control and Status Registers
Copyright (C) 2021  LekKit <github.com/LekKit>

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

#ifndef RISCV_CSR_H
#define RISCV_CSR_H

#include "rvvm.h"
#include "bit_ops.h"

#define CSR_SWAP       0x0
#define CSR_SETBITS    0x1
#define CSR_CLEARBITS  0x2

#define CSR_STATUS_MPRV (1 << 17)
#define CSR_STATUS_SUM  (1 << 18)
#define CSR_STATUS_MXR  (1 << 19)
#define CSR_STATUS_TVM  (1 << 20)
#define CSR_STATUS_TW   (1 << 21)
#define CSR_STATUS_TSR  (1 << 22)

#define CSR_SATP_MODE_PHYS   0
#define CSR_SATP_MODE_SV32   1
#define CSR_SATP_MODE_SV39   8
#define CSR_SATP_MODE_SV48   9
#define CSR_SATP_MODE_SV57   10

#define CSR_MISA_RV32  0x40000000U
#define CSR_MISA_RV64  0x8000000000000000ULL

#ifdef USE_FPU
// FPU-control stuff
#define FS_OFF      0
#define FS_INITIAL  1
#define FS_CLEAN    2
#define FS_DIRTY    3

#define FFLAG_NX (1 << 0) /* inexact */
#define FFLAG_UF (1 << 1) /* undeflow */
#define FFLAG_OF (1 << 2) /* overflow */
#define FFLAG_DZ (1 << 3) /* divide by zero */
#define FFLAG_NV (1 << 4) /* invalid operation */

#define RM_RNE 0 /* round to nearest, ties to even */
#define RM_RTZ 1 /* round to zero */
#define RM_RDN 2 /* round down - towards -inf */
#define RM_RUP 3 /* round up - towards +inf */
#define RM_RMM 4 /* round to nearest, ties to max magnitude */
#define RM_DYN 7 /* round to instruction's rm field */
#define RM_INVALID 255 /* invalid rounding mode was specified - should cause a trap */

uint8_t fpu_set_rm(rvvm_hart_t* vm, uint8_t newrm);

static inline bool fpu_is_enabled(rvvm_hart_t* vm)
{
    return bit_cut(vm->csr.status, 13, 2) != FS_OFF;
}

static inline void fpu_set_fs(rvvm_hart_t* vm, uint8_t value)
{
#ifdef USE_PRECISE_FS
    vm->csr.status = bit_replace(vm->csr.status, 13, 2, value);
#else
    UNUSED(vm);
    UNUSED(value);
#endif
}

#endif

typedef bool (*riscv_csr_handler_t)(rvvm_hart_t* vm, maxlen_t* dest, uint8_t op);

extern riscv_csr_handler_t riscv_csr_list[4096];

static inline bool riscv_csr_op(rvvm_hart_t* vm, uint32_t csr_id, maxlen_t* dest, uint8_t op)
{
    uint8_t priv = bit_cut(csr_id, 8, 2);
    if (priv > vm->priv_mode) {
        return false;
    } else {
#ifdef USE_RV64
        if (!vm->rv64) *dest = (uint32_t)*dest;
#endif
        return riscv_csr_list[csr_id](vm, dest, op);
    }
}

// Called once from riscv_hart_init()
void riscv_csr_global_init();

#endif
