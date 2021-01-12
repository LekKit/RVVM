/*
riscv32_priv.c - RISC-V privileged mode emulation
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

#include <assert.h>

#include "riscv.h"
#include "riscv32.h"
#include "riscv32_priv.h"
#include "bit_ops.h"

static void riscv32i_system(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    printf("RV32priv: SYSTEM instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32i_fence(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    printf("RV32priv: FENCE instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32zifence_i(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    printf("RV32priv: ZIFENCE_I instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32zicsr_csrrw(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    printf("RV32priv: CSRRW instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32zicsr_csrrs(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    printf("RV32priv: CSRRS instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32zicsr_csrrc(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    printf("RV32priv: CSRRC instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32zicsr_csrrwi(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    printf("RV32priv: CSRRWI instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32zicsr_csrrsi(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    printf("RV32priv: CSRRSI instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32zicsr_csrrci(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    printf("RV32priv: CSRRCI instruction 0x%x in VM %p\n", instruction, vm);
}

void riscv32_priv_init()
{
    smudge_opcode_ISB(RV32I_SYSTEM, riscv32i_system);
    smudge_opcode_ISB(RV32I_FENCE, riscv32i_fence);
    smudge_opcode_ISB(RV32_ZIFENCE_I, riscv32zifence_i);
    smudge_opcode_ISB(RV32_ZICSR_CSRRW, riscv32zicsr_csrrw);
    smudge_opcode_ISB(RV32_ZICSR_CSRRS, riscv32zicsr_csrrs);
    smudge_opcode_ISB(RV32_ZICSR_CSRRC, riscv32zicsr_csrrc);
    smudge_opcode_ISB(RV32_ZICSR_CSRRWI, riscv32zicsr_csrrwi);
    smudge_opcode_ISB(RV32_ZICSR_CSRRSI, riscv32zicsr_csrrsi);
    smudge_opcode_ISB(RV32_ZICSR_CSRRCI, riscv32zicsr_csrrci);
}
