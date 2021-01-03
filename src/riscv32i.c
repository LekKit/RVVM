/*
riscv32i.c - RISC-V Interger instruction emulator
Copyright (C) 2020  Mr0maks <mr.maks0443@gmail.com>

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
#include "riscv32i.h"
#include "bit_ops.h"

// translate register number into abi name
const char *riscv32i_translate_register(uint32_t reg)
{
    assert(reg < REGISTERS_MAX);
    switch (reg) {
    case REGISTER_ZERO: return "zero";
    case REGISTER_X1: return "ra";
    case REGISTER_X2: return "sp";
    case REGISTER_X3: return "gp";
    case REGISTER_X4: return "tp";
    case REGISTER_X5: return "t0";
    case REGISTER_X6: return "t1";
    case REGISTER_X7: return "t2";
    case REGISTER_X8: return "s0/fp";
    case REGISTER_X9: return "s1";
    case REGISTER_X10: return "a0";
    case REGISTER_X11: return "a1";
    case REGISTER_X12: return "a2";
    case REGISTER_X13: return "a3";
    case REGISTER_X14: return "a4";
    case REGISTER_X15: return "a5";
    case REGISTER_X16: return "a6";
    case REGISTER_X17: return "a7";
    case REGISTER_X18: return "s2";
    case REGISTER_X19: return "s3";
    case REGISTER_X20: return "s4";
    case REGISTER_X21: return "s5";
    case REGISTER_X22: return "s6";
    case REGISTER_X23: return "s7";
    case REGISTER_X24: return "s8";
    case REGISTER_X25: return "s9";
    case REGISTER_X26: return "s10";
    case REGISTER_X27: return "s11";
    case REGISTER_X28: return "t3";
    case REGISTER_X29: return "t4";
    case REGISTER_X30: return "t5";
    case REGISTER_X31: return "t6";
    case REGISTER_PC: return "pc";
    default: return "unknown";
    }
}

static void riscv32i_lui(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Place upper imm into register rds (lower 12 bits are zero)
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t imm = (instruction & 0xFFFFF000);

    riscv32i_write_register_u(vm, rds, imm);

    printf("RV32I: lui %s, 0x%x in VM %p\n", riscv32i_translate_register(rds), imm, vm);
}

static void riscv32i_auipc(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Add upper imm to PC, place the result to register rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t imm = (instruction & 0xFFFFF000);
    uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);

    riscv32i_write_register_u(vm, rds, pc + imm);

    printf("RV32I: auipc %s, 0x%x in VM %p\n", riscv32i_translate_register(rds), imm, vm);
}

static void riscv32i_jal(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Store PC+4 to rds, jump to PC+offset; remember further PC increment!
    // offset is signed imm * 2, left shift one more bit for *2
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t imm = cut_bits(instruction, 31, 1) << 20 |
                   cut_bits(instruction, 12, 8) << 12 |
                   cut_bits(instruction, 20, 1) << 11 |
                   cut_bits(instruction, 21, 10) << 1;
    int32_t offset = sign_extend(imm, 21);
    uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);

    riscv32i_write_register_u(vm, rds, pc + 4);
    riscv32i_write_register_u(vm, REGISTER_PC, ((int32_t)pc) + offset - 4);

    printf("RV32I: jal %d in VM %p\n", offset, vm);
}

static void riscv32i_srli_srai(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Perform right arithmetical/logical bitshift on rs1 by imm, store into rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t shamt = cut_bits(instruction, 20, 5);
    uint32_t src_reg = riscv32i_read_register_s(vm, rs1);

    if (cut_bits(instruction, 25, 7)) {
        riscv32i_write_register_s(vm, rds, ((int32_t)src_reg) >> shamt);
        printf("RV32I: srai %s, %s, %d in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), shamt, vm);
    } else {
        riscv32i_write_register_s(vm, rds, src_reg >> shamt);
        printf("RV32I: srli %s, %s, %d in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), shamt, vm);
    }
}

static void riscv32i_add_sub(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Add/sub rs2 and rs1, place into rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t reg1 = riscv32i_read_register_s(vm, rs1);
    uint32_t reg2 = riscv32i_read_register_s(vm, rs2);

    if (cut_bits(instruction, 25, 7)) {
        riscv32i_write_register_s(vm, rds, reg1 - reg2);
        printf("RV32I: sub %s, %s, %s in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), vm);
    } else {
        riscv32i_write_register_s(vm, rds, reg1 + reg2);
        printf("RV32I: add %s, %s, %s in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), vm);
    }
}

static void riscv32i_ecall_ebreak(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("RV32I: ECALL/EBREAK instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32i_srl_sra(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Perform right arithmetical/logical bitshift on rs1 by rs2, store into rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t reg1 = riscv32i_read_register_s(vm, rs1);
    uint32_t reg2 = riscv32i_read_register_s(vm, rs2);

    if (cut_bits(instruction, 25, 7)) {
        riscv32i_write_register_s(vm, rds, ((int32_t)reg1) >> (reg2 & gen_mask(5)));
        printf("RV32I: srl %s, %s, %s in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), vm);
    } else {
        riscv32i_write_register_s(vm, rds, reg1 >> (reg2 & gen_mask(5)));
        printf("RV32I: srl %s, %s, %s in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), vm);
    }
}

static void riscv32i_jalr(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Save PC+4 to rds, jump to PC+rs1 (signed)
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t imm = cut_bits(instruction, 20, 12);
    int32_t offset = sign_extend(imm, 20);
    uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);

    riscv32i_write_register_u(vm, rds, pc + 4);
    riscv32i_write_register_u(vm, REGISTER_PC, ((((int32_t)pc) + offset)&(~0x1)) - 4);

    printf("RV32I: jalr %s, %s, %d in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), offset, vm);
}

static void riscv32i_beq(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Conditional jump when rs1 == rs2
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t imm;
    uint32_t pc;
    int32_t offset = 0;

    if (riscv32i_read_register_s(vm, rs1) == riscv32i_read_register_s(vm, rs2)) {
        imm = cut_bits(instruction, 31, 1) << 11 |
              cut_bits(instruction, 7, 1)  << 10 |
              cut_bits(instruction, 25, 6) << 4  |
              cut_bits(instruction, 8, 4);
        offset = sign_extend(imm, 12);

        pc = riscv32i_read_register_u(vm, REGISTER_PC);
        riscv32i_write_register_u(vm, REGISTER_PC, ((int32_t)pc) + offset - 4);
    }

    printf("RV32I: beq %s, %s, %d in VM %p\n", riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), offset, vm);
}

static void riscv32i_bne(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Conditional jump when rs1 != rs2
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t imm;
    uint32_t pc;
    int32_t offset = 0;

    if (riscv32i_read_register_s(vm, rs1) != riscv32i_read_register_s(vm, rs2)) {
        imm = cut_bits(instruction, 31, 1) << 11 |
              cut_bits(instruction, 7, 1)  << 10 |
              cut_bits(instruction, 25, 6) << 4  |
              cut_bits(instruction, 8, 4);
        offset = sign_extend(imm, 12);

        pc = riscv32i_read_register_u(vm, REGISTER_PC);
        riscv32i_write_register_u(vm, REGISTER_PC, ((int32_t)pc) + offset - 4);
    }

    printf("RV32I: bne %s, %s, %d in VM %p\n", riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), offset, vm);
}

static void riscv32i_blt(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Conditional jump when rs1 < rs2 (signed)
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t imm;
    uint32_t pc;
    int32_t offset = 0;

    if (((int32_t)riscv32i_read_register_s(vm, rs1)) < ((int32_t)riscv32i_read_register_s(vm, rs2))) {
        imm = cut_bits(instruction, 31, 1) << 11 |
              cut_bits(instruction, 7, 1)  << 10 |
              cut_bits(instruction, 25, 6) << 4  |
              cut_bits(instruction, 8, 4);
        offset = sign_extend(imm, 12);

        pc = riscv32i_read_register_u(vm, REGISTER_PC);
        riscv32i_write_register_u(vm, REGISTER_PC, ((int32_t)pc) + offset - 4);
    }

    printf("RV32I: blt %s, %s, %d in VM %p\n", riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), offset, vm);
}

static void riscv32i_bge(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Conditional jump when rs1 >= rs2 (signed)
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t imm;
    uint32_t pc;
    int32_t offset = 0;

    if (((int32_t)riscv32i_read_register_s(vm, rs1)) >= ((int32_t)riscv32i_read_register_s(vm, rs2))) {
        imm = cut_bits(instruction, 31, 1) << 11 |
              cut_bits(instruction, 7, 1)  << 10 |
              cut_bits(instruction, 25, 6) << 4  |
              cut_bits(instruction, 8, 4);
        offset = sign_extend(imm, 12);

        pc = riscv32i_read_register_u(vm, REGISTER_PC);
        riscv32i_write_register_u(vm, REGISTER_PC, ((int32_t)pc) + offset - 4);
    }

    printf("RV32I: bge %s, %s, %d in VM %p\n", riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), offset, vm);
}

static void riscv32i_bltu(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Conditional jump when rs1 > rs2
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t imm;
    uint32_t pc;
    int32_t offset = 0;

    if (riscv32i_read_register_s(vm, rs1) > riscv32i_read_register_s(vm, rs2)) {
        imm = cut_bits(instruction, 31, 1) << 11 |
              cut_bits(instruction, 7, 1)  << 10 |
              cut_bits(instruction, 25, 6) << 4  |
              cut_bits(instruction, 8, 4);
        offset = sign_extend(imm, 12);

        pc = riscv32i_read_register_u(vm, REGISTER_PC);
        riscv32i_write_register_u(vm, REGISTER_PC, ((int32_t)pc) + offset - 4);
    }

    printf("RV32I: bltu %s, %s, %d in VM %p\n", riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), offset, vm);
}

static void riscv32i_bgeu(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Conditional jump when rs1 >= rs2
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t imm;
    uint32_t pc;
    int32_t offset = 0;

    if (riscv32i_read_register_s(vm, rs1) >= riscv32i_read_register_s(vm, rs2)) {
        imm = cut_bits(instruction, 31, 1) << 11 |
              cut_bits(instruction, 7, 1)  << 10 |
              cut_bits(instruction, 25, 6) << 4  |
              cut_bits(instruction, 8, 4);
        offset = sign_extend(imm, 12);

        pc = riscv32i_read_register_u(vm, REGISTER_PC);
        riscv32i_write_register_u(vm, REGISTER_PC, ((int32_t)pc) + offset - 4);
    }

    printf("RV32I: bgeu %s, %s, %d in VM %p\n", riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), offset, vm);
}

static void riscv32i_lb(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("RV32I: LB instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32i_lh(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("RV32I: LH instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32i_lw(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("RV32I: LW instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32i_lbu(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("RV32I: LBU instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32i_lhu(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("RV32I: LHU instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32i_sb(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("RV32I: SB instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32i_sh(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("RV32I: SH instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32i_sw(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("RV32I: SW instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32i_addi(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Add signed immediate to rs1, store to rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    int32_t imm = sign_extend(cut_bits(instruction, 20, 12), 12);
    uint32_t src_reg = riscv32i_read_register_s(vm, rs1);

    riscv32i_write_register_s(vm, rds, src_reg + imm);
    printf("RV32I: addi %s, %s, %d in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), imm, vm);
}

static void riscv32i_slti(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Set rds to 1 if rs1 < imm (signed)
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    int32_t imm = sign_extend(cut_bits(instruction, 20, 12), 12);
    int32_t src_reg = riscv32i_read_register_s(vm, rs1);

    riscv32i_write_register_s(vm, rds, (src_reg < imm) ? 1 : 0);
    printf("RV32I: sltiu %s, %s, %d in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), imm, vm);
}

static void riscv32i_sltiu(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Set rds to 1 if rs1 < imm
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t imm = cut_bits(instruction, 20, 12);
    uint32_t src_reg = riscv32i_read_register_s(vm, rs1);

    riscv32i_write_register_s(vm, rds, (src_reg < imm) ? 1 : 0);
    printf("RV32I: sltiu %s, %s, %d in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), imm, vm);
}

static void riscv32i_xori(risc32_vm_state_t *vm, uint32_t instruction)
{
    // XOR rs1 with sign-extended imm, store to rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    int32_t imm = sign_extend(cut_bits(instruction, 20, 12), 12);
    uint32_t src_reg = riscv32i_read_register_s(vm, rs1);

    riscv32i_write_register_s(vm, rds, src_reg ^ imm);
    printf("RV32I: xori %s, %s, %d in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), imm, vm);
}

static void riscv32i_ori(risc32_vm_state_t *vm, uint32_t instruction)
{
    // OR rs1 with sign-extended imm, store to rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    int32_t imm = sign_extend(cut_bits(instruction, 20, 12), 12);
    uint32_t src_reg = riscv32i_read_register_s(vm, rs1);

    riscv32i_write_register_s(vm, rds, src_reg | imm);
    printf("RV32I: ori %s, %s, %d in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), imm, vm);
}

static void riscv32i_andi(risc32_vm_state_t *vm, uint32_t instruction)
{
    // AND rs1 with sign-extended imm, store to rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    int32_t imm = sign_extend(cut_bits(instruction, 20, 12), 12);
    uint32_t src_reg = riscv32i_read_register_s(vm, rs1);

    riscv32i_write_register_s(vm, rds, src_reg & imm);
    printf("RV32I: andi %s, %s, %d in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), imm, vm);
}

static void riscv32i_slli(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Left-shift rs1 by immediate, store to rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t shamt = cut_bits(instruction, 20, 5);
    uint32_t src_reg = riscv32i_read_register_s(vm, rs1);

    riscv32i_write_register_s(vm, rds, src_reg << shamt);
    printf("RV32I: slli %s, %s, %d in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), shamt, vm);
}

static void riscv32i_sll(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Left-shift rs1 by rs2, store to rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t reg1 = riscv32i_read_register_s(vm, rs1);
    uint32_t reg2 = riscv32i_read_register_s(vm, rs2);

    riscv32i_write_register_s(vm, rds, reg1 << (reg2 & gen_mask(5)));
    printf("RV32I: sll %s, %s, %s in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), vm);
}

static void riscv32i_slt(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Set rds to 1 if rs1 < rs2 (signed)
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    int32_t reg1 = riscv32i_read_register_u(vm, rs1);
    int32_t reg2 = riscv32i_read_register_u(vm, rs2);

    riscv32i_write_register_u(vm, rds, (reg1 < reg2) ? 1 : 0);
    printf("RV32I: slt %s, %s, %s in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), vm);
}

static void riscv32i_sltu(risc32_vm_state_t *vm, uint32_t instruction)
{
    // Set rds to 1 if rs1 < rs2
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t reg1 = riscv32i_read_register_u(vm, rs1);
    uint32_t reg2 = riscv32i_read_register_u(vm, rs2);

    riscv32i_write_register_u(vm, rds, (reg1 < reg2) ? 1 : 0);
    printf("RV32I: sltu %s, %s, %s in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), vm);
}

static void riscv32i_xor(risc32_vm_state_t *vm, uint32_t instruction)
{
    // XOR rs1 with rs2, store to rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t reg1 = riscv32i_read_register_u(vm, rs1);
    uint32_t reg2 = riscv32i_read_register_u(vm, rs2);

    riscv32i_write_register_u(vm, rds, reg1 ^ reg2);
    printf("RV32I: xor %s, %s, %s in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), vm);
}

static void riscv32i_or(risc32_vm_state_t *vm, uint32_t instruction)
{
    // OR rs1 with rs2, store to rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t reg1 = riscv32i_read_register_u(vm, rs1);
    uint32_t reg2 = riscv32i_read_register_u(vm, rs2);

    riscv32i_write_register_u(vm, rds, reg1 | reg2);
    printf("RV32I: or %s, %s, %s in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), vm);
}

static void riscv32i_and(risc32_vm_state_t *vm, uint32_t instruction)
{
    // AND rs1 with rs2, store to rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t reg1 = riscv32i_read_register_u(vm, rs1);
    uint32_t reg2 = riscv32i_read_register_u(vm, rs2);

    riscv32i_write_register_u(vm, rds, reg1 & reg2);
    printf("RV32I: and %s, %s, %s in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs1), riscv32i_translate_register(rs2), vm);
}

static void riscv32i_fence(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("RV32I: FENCE instruction 0x%x in VM %p\n", instruction, vm);
}

void riscv32i_init()
{
    smudge_opcode_func3(RV32I_LUI, riscv32i_lui);
    smudge_opcode_func3(RV32I_AUIPC, riscv32i_auipc);
    smudge_opcode_func3(RV32I_JAL, riscv32i_jal);

    riscv32_opcodes[RV32I_SRLI_SRAI] = riscv32i_srli_srai;
    riscv32_opcodes[RV32I_ADD_SUB] = riscv32i_add_sub;
    riscv32_opcodes[RV32I_ECALL_EBREAK] = riscv32i_ecall_ebreak;
    riscv32_opcodes[RV32I_SRL_SRA] = riscv32i_srl_sra;

    riscv32_opcodes[RV32I_JALR] = riscv32i_jalr;
    riscv32_opcodes[RV32I_BEQ] = riscv32i_beq;
    riscv32_opcodes[RV32I_BNE] = riscv32i_bne;
    riscv32_opcodes[RV32I_BLT] = riscv32i_blt;
    riscv32_opcodes[RV32I_BGE] = riscv32i_bge;
    riscv32_opcodes[RV32I_BLTU] = riscv32i_bltu;
    riscv32_opcodes[RV32I_BGEU] = riscv32i_bgeu;
    riscv32_opcodes[RV32I_LB] = riscv32i_lb;
    riscv32_opcodes[RV32I_LH] = riscv32i_lh;
    riscv32_opcodes[RV32I_LW] = riscv32i_lw;
    riscv32_opcodes[RV32I_LBU] = riscv32i_lbu;
    riscv32_opcodes[RV32I_LHU] = riscv32i_lhu;
    riscv32_opcodes[RV32I_SB] = riscv32i_sb;
    riscv32_opcodes[RV32I_SH] = riscv32i_sh;
    riscv32_opcodes[RV32I_SW] = riscv32i_sw;
    riscv32_opcodes[RV32I_ADDI] = riscv32i_addi;
    riscv32_opcodes[RV32I_SLTI] = riscv32i_slti;
    riscv32_opcodes[RV32I_SLTIU] = riscv32i_sltiu;
    riscv32_opcodes[RV32I_XORI] = riscv32i_xori;
    riscv32_opcodes[RV32I_ORI] = riscv32i_ori;
    riscv32_opcodes[RV32I_ANDI] = riscv32i_andi;
    riscv32_opcodes[RV32I_SLLI] = riscv32i_slli;
    riscv32_opcodes[RV32I_SLL] = riscv32i_sll;
    riscv32_opcodes[RV32I_SLT] = riscv32i_slt;
    riscv32_opcodes[RV32I_SLTU] = riscv32i_sltu;
    riscv32_opcodes[RV32I_XOR] = riscv32i_xor;
    riscv32_opcodes[RV32I_OR] = riscv32i_or;
    riscv32_opcodes[RV32I_AND] = riscv32i_and;
    riscv32_opcodes[RV32I_FENCE] = riscv32i_fence;
}

// We already check instruction for correct code
void riscv32i_emulate(risc32_vm_state_t *vm, uint32_t instruction)
{
    uint32_t funcid = RISCV32_GET_FUNCID(instruction);
    riscv32_opcodes[funcid](vm, instruction);
}
