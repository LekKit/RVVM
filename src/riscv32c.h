/*
riscv32c.h - RISC-V C instructions extension definitions
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

#include "riscv.h"
#include "riscv32.h"

#define RISCV32C_VERSION 20 // 2.0
#define RISCV32C_OPCODE_MASK 0x3
#define RISCV32C_GET_OPCODE(x) (x & RISCV32C_OPCODE_MASK)
#define RISCV32C_GET_FUNCID(x) (((x >> 13) << 2) | (x & RISCV32C_OPCODE_MASK))

/*
* Definitions for instructions, consisting of 5 bits each.
* Upper 3 bits are func3, lower 2 bits are opcode
*/
/* opcode 0 */
#define RVC_ADDI4SPN     0x0
#define RVC_FLD          0x4
#define RVC_LW           0x8
#define RVC_FLW          0xC
#define RVC_RESERVED1    0x10
#define RVC_FSD          0x14
#define RVC_SW           0x18
#define RVC_FSW          0x1C
/* opcode 1 */
#define RVC_ADDI_NOP     0x1  // this is also NOP when rs/rd == 0
#define RVC_JAL          0x5
#define RVC_LI           0x9
#define RVC_ADDI16SP_LUI 0xD  // this is ADDI16SP when rd == 2 or LUI (rd!=0)
#define RVC_ALOPS1       0x11 // a lot of operations packed tightly, idk about performance
#define RVC_J            0x15
#define RVC_BEQZ         0x19
#define RVC_BNEZ         0x1D
/* opcode 2 */
#define RVC_SLLI         0x2
#define RVC_FLDSP        0x6
#define RVC_LWSP         0xA
#define RVC_FLWSP        0xE
#define RVC_ALOPS2       0x12 // again
#define RVC_FSDSP        0x16
#define RVC_SWSP         0x1A
#define RVC_FSWSP        0x1E

/*
For many RVC instructions, zero-valued immediates are disallowed and x0 is not a valid 5-bit register specifier.
These restrictions free up encoding space for other instructions requiring feweroperand bits
*/

/*

opcode
[0:1]
0x00 || 0x01 || 0x02

CR Format (Compressed Register)
[0:1]    [2:6]   [7:11]    [12:15]
opcode    rs2    rds/rs1   funct4

CI Format
[0:1]    [2:6]  [7:11]     [12:13]  [13:15]
opcode   imm    rds/rs1      imm    funct3



*/

void riscv32c_emulate(risc32_vm_state_t *vm, uint16_t instruction);
