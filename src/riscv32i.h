/*
riscv32i.h - RISC-V Interger instruction emulator definitions
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
#include "riscv32i_registers.h"

#define RISCV32I_VERSION 21 // 2.1

// Are those defines even used?
#define RISCV32I_ILLEGAL_OPCODE1 0x00000000 // anyway illegal
#define RISCV32I_ILLEGAL_OPCODE2 0xFFFFFFFF // anyway illegal

#define RISCV32_OPCODE_REGISER_LEN 5 // 5 bits for register
#define RISCV32_OPCODE_I_IMM_LEN 11 // 11 bits for imm
#define RISCV32_OPCODE_I_FUNCT7_LEN 7 // 7 bits for funct7
#define RISCV32_OPCODE_FUNCT3_LEN 3 // 3 bits for funct3

// U/J type instructions
#define RV32I_LUI          0xD
#define RV32I_AUIPC        0x5
#define RV32I_JAL          0x1B
#define RV32I_SYSTEM       0x1C // let it just be here
// R-type instructions
#define RV32I_SLLI         0x24
#define RV32I_SRLI_SRAI    0xA4
#define RV32I_ADD_SUB      0xC
#define RV32I_SLL          0x2C
#define RV32I_SLT          0x4C
#define RV32I_SLTU         0x6C
#define RV32I_XOR          0x8C
#define RV32I_SRL_SRA      0xAC
#define RV32I_OR           0xCC
#define RV32I_AND          0xEC
// I/S/B type instructions
#define RV32I_JALR         0x19
#define RV32I_BEQ          0x18
#define RV32I_BNE          0x38
#define RV32I_BLT          0x98
#define RV32I_BGE          0xB8
#define RV32I_BLTU         0xD8
#define RV32I_BGEU         0xF8
#define RV32I_LB           0x0
#define RV32I_LH           0x20
#define RV32I_LW           0x40
#define RV32I_LBU          0x80
#define RV32I_LHU          0xA0
#define RV32I_SB           0x8
#define RV32I_SH           0x28
#define RV32I_SW           0x48
#define RV32I_ADDI         0x4
#define RV32I_SLTI         0x44
#define RV32I_SLTIU        0x64
#define RV32I_XORI         0x84
#define RV32I_ORI          0xC4
#define RV32I_ANDI         0xE4
#define RV32I_FENCE        0x3

/*
opcode
[0:1] [2:6]
 0x3  opcode
*/

/*
R type
[0:6]   [7:11]        [12:14]  [15:19]       [20:24]       [25:31]
opcode  dst register  funct3   src1 register src2 register funct7
*/

/*
I type
[0:6]   [7:11]        [12:14]  [15:19]       [20:31]
opcode  dst register  funct3   src1 register imm [0:11]
*/

/*
S type
[0:6]   [7:11]        [12:14]  [15:19]       [20:24]       [25:31]
opcode  imm[0:4]      funct3   src1 register src2 register imm[4:11]
*/

/*
B type
[0:6]   [7]      [8:11]     [12:14]  [15:19]       [20:24]       [25:30]   [31]
opcode  imm[11]  imm[1:4]   funct3   src1 register src2 register imm[5:10] imm[12]
*/

void riscv32i_emulate(risc32_vm_state_t *vm, const uint32_t instruction);
