/*
riscv_m.c - RISC-V M Decoder, Interpreter
Copyright (C) 2021  LekKit <github.com/LekKit>
                    Mr0maks <mr.maks0443@gmail.com>

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

#define RISCV_CPU_SOURCE

#include "bit_ops.h"
#include "riscv_cpu.h"

static void riscv_m_mul(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    sxlen_t reg1 = riscv_read_register_s(vm, rs1);
    sxlen_t reg2 = riscv_read_register_s(vm, rs2);

    riscv_write_register(vm, rds, (int64_t)reg1 * (int64_t)reg2);
}

static void riscv_m_mulh(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    sxlen_t reg1 = riscv_read_register_s(vm, rs1);
    sxlen_t reg2 = riscv_read_register_s(vm, rs2);

#ifdef RV64
#ifdef INT128_SUPPORT
    riscv_write_register(vm, rds, ((int128_t)reg1 * (int128_t)reg2) >> 64);
#else
    riscv_write_register(vm, rds, ((int64_t)reg1 >> 32) * ((int64_t)reg2 >> 32));
#endif
#else
    riscv_write_register(vm, rds, ((int64_t)reg1 * (int64_t)reg2) >> 32);
#endif
}

static void riscv_m_mulhsu(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    sxlen_t reg1 = riscv_read_register_s(vm, rs1);
    xlen_t reg2 = riscv_read_register(vm, rs2);

#ifdef RV64
#ifdef INT128_SUPPORT
    riscv_write_register(vm, rds, ((int128_t)reg1 * (uint128_t)reg2) >> 64);
#else
    riscv_write_register(vm, rds, ((int64_t)reg1 >> 32) * ((uint64_t)reg2 >> 32));
#endif
#else
    riscv_write_register(vm, rds, ((int64_t)reg1 * (uint64_t)reg2) >> 32);
#endif
}

static void riscv_m_mulhu(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    xlen_t reg1 = riscv_read_register(vm, rs1);
    xlen_t reg2 = riscv_read_register(vm, rs2);

#ifdef RV64
#ifdef INT128_SUPPORT
    riscv_write_register(vm, rds, ((uint128_t)reg1 * (uint128_t)reg2) >> 64);
#else
    riscv_write_register(vm, rds, ((uint64_t)reg1 >> 32) * ((uint64_t)reg2 >> 32));
#endif
#else
    riscv_write_register(vm, rds, ((uint64_t)reg1 * (uint64_t)reg2) >> 32);
#endif
}

static void riscv_m_div(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    sxlen_t reg1 = riscv_read_register_s(vm, rs1);
    sxlen_t reg2 = riscv_read_register_s(vm, rs2);
    sxlen_t result = -1;

    // overflow
    if (reg1 == DIV_OVERFLOW_RS1 && reg2 == -1) {
        result = DIV_OVERFLOW_RS1;
    // division by zero check (we already setup result var for error)
    } else if (reg2 != 0) {
        result = reg1 / reg2;
    }

    riscv_write_register(vm, rds, result);
}

static void riscv_m_divu(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    xlen_t reg1 = riscv_read_register(vm, rs1);
    xlen_t reg2 = riscv_read_register(vm, rs2);
    xlen_t result = (sxlen_t)-1;

    // division by zero check (we already setup result var for error)
    if (reg2 != 0) {
        result = reg1 / reg2;
    }

    riscv_write_register(vm, rds, result);
}

static void riscv_m_rem(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    sxlen_t reg1 = riscv_read_register_s(vm, rs1);
    sxlen_t reg2 = riscv_read_register_s(vm, rs2);
    sxlen_t result = reg1;

    // overflow
    if (reg1 == DIV_OVERFLOW_RS1 && reg2 == -1) {
        result = 0;
    // division by zero check (we already setup result var for error)
    } else if (reg2 != 0) {
        result = reg1 % reg2;
    }

    riscv_write_register(vm, rds, result);
}

static void riscv_m_remu(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    xlen_t reg1 = riscv_read_register(vm, rs1);
    xlen_t reg2 = riscv_read_register(vm, rs2);
    xlen_t result = reg1;

    // division by zero check (we already setup result var for error)
    if (reg2 != 0) {
        result = reg1 % reg2;
    }

    riscv_write_register(vm, rds, result);
}

#ifdef RV64
    // Implement RV64M-only instructions
#endif

void riscv_m_init(rvvm_hart_t* vm)
{
    riscv_install_opcode_R(vm, RVM_MUL, riscv_m_mul);
    riscv_install_opcode_R(vm, RVM_MULH, riscv_m_mulh);
    riscv_install_opcode_R(vm, RVM_MULHSU, riscv_m_mulhsu);
    riscv_install_opcode_R(vm, RVM_MULHU, riscv_m_mulhu);
    riscv_install_opcode_R(vm, RVM_DIV, riscv_m_div);
    riscv_install_opcode_R(vm, RVM_DIVU, riscv_m_divu);
    riscv_install_opcode_R(vm, RVM_REM, riscv_m_rem);
    riscv_install_opcode_R(vm, RVM_REMU, riscv_m_remu);
#ifdef RV64
    // Install RV64M-only instructions
#endif
}
