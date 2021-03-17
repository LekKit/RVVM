/*
riscv32m.c - RISC-V M instructions extension emulator
Copyright (C) 2021  Mr0maks <mr.maks0443@gmail.com>

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

#include "riscv.h"
#include "riscv32.h"
#include "riscv32i.h"
#include "riscv32m.h"
#include "bit_ops.h"

void riscv32m_mul(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t rs1 = bit_cut(instruction, 15, 5);
    uint32_t rs2 = bit_cut(instruction, 20, 5);
    uint32_t reg1 = riscv32i_read_register_u(vm, rs1);
    uint32_t reg2 = riscv32i_read_register_u(vm, rs2);

    riscv32i_write_register_u(vm, rds, reg1 * reg2);
    riscv32_debug(vm, "RV32M: mul %r, %r, %r", rds, rs1, rs2);
}

void riscv32m_mulh(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t rs1 = bit_cut(instruction, 15, 5);
    uint32_t rs2 = bit_cut(instruction, 20, 5);
    int64_t reg1 = riscv32i_read_register_s(vm, rs1);
    int64_t reg2 = riscv32i_read_register_s(vm, rs2);

    riscv32i_write_register_s(vm, rds, (reg1 * reg2) >> 32);
    riscv32_debug(vm, "RV32M: mulh %r, %r, %r", rds, rs1, rs2);
}

void riscv32m_mulhsu(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t rs1 = bit_cut(instruction, 15, 5);
    uint32_t rs2 = bit_cut(instruction, 20, 5);
    int64_t reg1 = riscv32i_read_register_s(vm, rs1);
    uint64_t reg2 = riscv32i_read_register_u(vm, rs2);

    riscv32i_write_register_u(vm, rds, (reg1 * reg2) >> 32);
    riscv32_debug(vm, "RV32M: mulhsu %r, %r, %r", rds, rs1, rs2);
}

void riscv32m_mulhu(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t rs1 = bit_cut(instruction, 15, 5);
    uint32_t rs2 = bit_cut(instruction, 20, 5);
    uint64_t reg1 = riscv32i_read_register_u(vm, rs1);
    uint64_t reg2 = riscv32i_read_register_u(vm, rs2);

    riscv32i_write_register_u(vm, rds, (reg1 * reg2) >> 32);
    riscv32_debug(vm, "RV32M: mulhu %r, %r, %r", rds, rs1, rs2);
}

void riscv32m_div(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t rs1 = bit_cut(instruction, 15, 5);
    uint32_t rs2 = bit_cut(instruction, 20, 5);
    int32_t reg1 = riscv32i_read_register_s(vm, rs1);
    int32_t reg2 = riscv32i_read_register_s(vm, rs2);
    int32_t result = 0xFFFFFFFF;

    // overflow
    if (reg1 == -2147483648 && reg2 == -1) {
        result = -2147483648;
    // division by zero check (we already setup result var for error)
    } else if (reg2 != 0) {
        result = reg1 / reg2;
    }

    riscv32i_write_register_s(vm, rds, result);
    riscv32_debug(vm, "RV32M: div %r, %r, %r", rds, rs1, rs2);
}

void riscv32m_divu(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t rs1 = bit_cut(instruction, 15, 5);
    uint32_t rs2 = bit_cut(instruction, 20, 5);
    uint32_t reg1 = riscv32i_read_register_u(vm, rs1);
    uint32_t reg2 = riscv32i_read_register_u(vm, rs2);
    uint32_t result = 0xFFFFFFFF;

    // division by zero check (we already setup result var for error)
    if (reg2 != 0) {
        result = reg1 / reg2;
    }

    riscv32i_write_register_u(vm, rds, result);
    riscv32_debug(vm, "RV32M: divu %r, %r, %r", rds, rs1, rs2);
}

void riscv32m_rem(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t rs1 = bit_cut(instruction, 15, 5);
    uint32_t rs2 = bit_cut(instruction, 20, 5);
    int32_t reg1 = riscv32i_read_register_u(vm, rs1);
    int32_t reg2 = riscv32i_read_register_u(vm, rs2);
    int32_t result = reg1;

    // overflow
    if (reg1 == -2147483648 && reg2 == -1) {
        result = 0;
    // division by zero check (we already setup result var for error)
    } else if (reg2 != 0) {
        result = reg1 % reg2;
    }

    riscv32i_write_register_s(vm, rds, result);
    riscv32_debug(vm, "RV32M: rem %r, %r, %r", rds, rs1, rs2);
}

void riscv32m_remu(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t rs1 = bit_cut(instruction, 15, 5);
    uint32_t rs2 = bit_cut(instruction, 20, 5);
    uint32_t reg1 = riscv32i_read_register_u(vm, rs1);
    uint32_t reg2 = riscv32i_read_register_u(vm, rs2);
    uint32_t result = reg1;

    // division by zero check (we already setup result var for error)
    if (reg2 != 0) {
        result = reg1 % reg2;
    }

    riscv32i_write_register_u(vm, rds, result);
    riscv32_debug(vm, "RV32M: renu %r, %r, %r", rds, rs1, rs2);
}

void riscv32m_init()
{
    riscv32_opcodes[RV32M_MUL] = riscv32m_mul;
    riscv32_opcodes[RV32M_MULH] = riscv32m_mulh;
    riscv32_opcodes[RV32M_MULHSU] = riscv32m_mulhsu;
    riscv32_opcodes[RV32M_MULHU] = riscv32m_mulhu;
    riscv32_opcodes[RV32M_DIV] = riscv32m_div;
    riscv32_opcodes[RV32M_DIVU] = riscv32m_divu;
    riscv32_opcodes[RV32M_REM] = riscv32m_rem;
    riscv32_opcodes[RV32M_REMU] = riscv32m_remu;
}
