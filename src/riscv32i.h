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

/* no func3, need to be smudged */
#define RV32I_LUI          0x37
#define RV32I_AUIPC        0x17
#define RV32I_JAL          0x6F
/*
* These have func7 and need additional decoding
* Also, RV32I_ADD_SUB overlaps MUL by func3+opcode mask, need to figure that out soon
*/
#define RV32I_SRLI_SRAI    0x293
#define RV32I_ADD_SUB      0x33
#define RV32I_ECALL_EBREAK 0x73
#define RV32I_SRL_SRA      0x2B3
/* normal */
#define RV32I_JALR         0x67
#define RV32I_BEQ          0x63
#define RV32I_BNE          0xE3
#define RV32I_BLT          0x263
#define RV32I_BGE          0x2E3
#define RV32I_BLTU         0x363
#define RV32I_BGEU         0x3E3
#define RV32I_LB           0x3
#define RV32I_LH           0x83
#define RV32I_LW           0x103
#define RV32I_LBU          0x203
#define RV32I_LHU          0x283
#define RV32I_SB           0x23
#define RV32I_SH           0xA3
#define RV32I_SW           0x123
#define RV32I_ADDI         0x13
#define RV32I_SLTI         0x113
#define RV32I_SLTIU        0x193
#define RV32I_XORI         0x213
#define RV32I_ORI          0x313
#define RV32I_ANDI         0x393
#define RV32I_SLLI         0x93
#define RV32I_SLL          0xB3
#define RV32I_SLT          0x133
#define RV32I_SLTU         0x1B3
#define RV32I_XOR          0x233
#define RV32I_OR           0x333
#define RV32I_AND          0x3B3
#define RV32I_FENCE        0xF

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
