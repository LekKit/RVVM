#pragma once

#include "riscv32.h"

#define RISCV32I_VERSION 21 // 2.1

#define RISCV32I_ILLEGAL_OPCODE1 0x00000000 // anyway illegal
#define RISCV32I_ILLEGAL_OPCODE2 0xFFFFFFFF // anyway illegal

#define RISCV32_OPCODE_REGISER_LEN 5 // 5 bits for register
#define RISCV32_OPCODE_I_IMM_LEN 11 // 11 bits for imm
#define RISCV32_OPCODE_I_FUNCT7_LEN 7 // 7 bits for funct7
#define RISCV32_OPCODE_FUNCT3_LEN 3 // 3 bits for funct3

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

void riscv32i_emulate(risc32_vm_state_t *vm, uint32_t instruction);
