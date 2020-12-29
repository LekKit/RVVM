#pragma once

#include "riscv.h"
#include "riscv32.h"

#define RISCV32C_VERSION 20 // 2.0
#define RISCV32C_OPCODE_MASK 0x3
#define RISCV32C_GET_OPCODE(x) (x & RISCV32C_OPCODE_MASK)

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
