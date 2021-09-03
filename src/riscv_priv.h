/*
riscv_priv.h - RISC-V Privileged
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

#ifndef RISCV_PRIV_H
#define RISCV_PRIV_H

#include "rvvm.h"

void riscv_priv_init(rvvm_hart_t* vm);

// Here are some opcodes from RV32I set to clarify their priv-related usage.

#define RV_PRIV_SYSTEM        0x1C // ecall, ebreak, uret/sret/mret, wfi, sfence.vma, hfence
#define RV_PRIV_FENCE         0x3
#define RV_PRIV_ZIFENCE_I     0x23
#define RV_PRIV_ZICSR_CSRRW   0x3C
#define RV_PRIV_ZICSR_CSRRS   0x5C
#define RV_PRIV_ZICSR_CSRRC   0x7C
#define RV_PRIV_ZICSR_CSRRWI  0xBC
#define RV_PRIV_ZICSR_CSRRSI  0xDC
#define RV_PRIV_ZICSR_CSRRCI  0xFC

// Precise instruction values for SYSTEM opcode decoding
#define RV_PRIV_S_ECALL       0x73
#define RV_PRIV_S_EBREAK      0x100073
#define RV_PRIV_S_URET        0x200073
#define RV_PRIV_S_SRET        0x10200073
#define RV_PRIV_S_MRET        0x30200073
#define RV_PRIV_S_WFI         0x10500073

// Privileged FENCE instructions mask and decoding
#define RV_PRIV_S_FENCE_MASK  0xFE007FFF
#define RV_PRIV_S_SFENCE_VMA  0x12000073
#define RV_PRIV_S_HFENCE_BVMA 0x22000073
#define RV_PRIV_S_HFENCE_GVMA 0xA2000073

#endif
