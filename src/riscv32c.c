/*
riscv32c.c - RISC-V C instructions extension emulator
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

#include "riscv.h"
#include "riscv32.h"
#include "riscv32i.h"
#include "riscv32c.h"
#include "bit_ops.h"

// translate compressed register encoding into normal
static inline uint32_t riscv32c_reg(uint32_t reg)
{
    //NOTE: register index is hard limited to 8, since encoding is 3 bits
    return REGISTER_X8 + reg;
}

static void riscv32c_illegal_insn(risc32_vm_state_t *vm, const uint16_t instruction)
{
    riscv32_error(vm, "RVC: illegal instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_addi4spn(risc32_vm_state_t *vm, const uint16_t instruction)
{
    // Add imm*4 to stack pointer (X2), store into rds
    uint32_t rds = cut_bits(instruction, 2, 3);
    uint32_t imm = (cut_bits(instruction, 11, 2) << 8) |
                   (cut_bits(instruction, 7, 4) << 4) |
                   (cut_bits(instruction, 5, 1) << 3) |
                   (cut_bits(instruction, 6, 1) << 2);
    uint32_t rsp = riscv32i_read_register_u(vm, REGISTER_X2);
    riscv32i_write_register_u(vm, riscv32c_reg(rds), rsp + imm);
    printf("RVC: c.addi4spn %s, %d in VM %p\n", riscv32i_translate_register(riscv32c_reg(rds)), imm, vm);
}

static void riscv32c_addi_nop(risc32_vm_state_t *vm, const uint16_t instruction)
{
    // Add 6-bit signed immediate to rds (this also serves as NOP for X0 reg)
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t src_reg = riscv32i_read_register_u(vm, rds);
    int32_t imm = sign_extend((cut_bits(instruction, 12, 1) << 5) |
                              (cut_bits(instruction, 2, 5)), 6);

    riscv32i_write_register_u(vm, rds, src_reg + imm);
    printf("RVC: c.addi %s, %d in VM %p\n", riscv32i_translate_register(rds), imm, vm);
}

static void riscv32c_slli(risc32_vm_state_t *vm, const uint16_t instruction)
{
    // Left shift rds by imm, store into rds
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t src_reg = riscv32i_read_register_u(vm, rds);
    uint32_t shamt = cut_bits(instruction, 2, 5);

    riscv32i_write_register_u(vm, rds, src_reg << shamt);
    printf("RVC: c.slli %s, %d in VM %p\n", riscv32i_translate_register(rds), shamt, vm);
}

static void riscv32c_fld(risc32_vm_state_t *vm, const uint16_t instruction)
{
    printf("RVC: FLD instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_jal(risc32_vm_state_t *vm, const uint16_t instruction)
{
    // Save PC+2 into X1 (return addr), jump to PC+offset
    uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);
    // This is total bullshit, we need translation table here in future
    uint32_t imm = (cut_bits(instruction, 3, 3) << 1)  |
                   (cut_bits(instruction, 11, 1) << 4) |
                   (cut_bits(instruction, 2, 1) << 5)  |
                   (cut_bits(instruction, 7, 1) << 6)  |
                   (cut_bits(instruction, 6, 1) << 7)  |
                   (cut_bits(instruction, 9, 2) << 8)  |
                   (cut_bits(instruction, 8, 1) << 10) |
                   (cut_bits(instruction, 12, 1) << 11);
    int32_t offset = sign_extend(imm, 12);

    riscv32i_write_register_u(vm, REGISTER_X1, pc + 2);
    riscv32i_write_register_u(vm, REGISTER_PC, pc + offset - 2);
    printf("RVC: c.jal %d in VM %p\n", offset, vm);
}

static void riscv32c_fldsp(risc32_vm_state_t *vm, const uint16_t instruction)
{
    printf("RVC: FLDSP instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_lw(risc32_vm_state_t *vm, const uint16_t instruction)
{
    printf("RVC: LW instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_li(risc32_vm_state_t *vm, const uint16_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 5);
    int32_t imm = sign_extend((cut_bits(instruction, 12, 1) << 5) |
                              (cut_bits(instruction, 2, 5)), 6);

    riscv32i_write_register_s(vm, rds, imm);
    printf("RVC: c.li %s, %d in VM %p\n", riscv32i_translate_register(rds), imm, vm);
}

static void riscv32c_lwsp(risc32_vm_state_t *vm, const uint16_t instruction)
{
    printf("RVC: LWSP instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_flw(risc32_vm_state_t *vm, const uint16_t instruction)
{
    printf("RVC: FLW instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_addi16sp_lui(risc32_vm_state_t *vm, const uint16_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t imm;

    if (rds == REGISTER_X2) {
        imm = (cut_bits(instruction, 6, 1) << 4) |
              (cut_bits(instruction, 2, 1) << 5) |
              (cut_bits(instruction, 5, 1) << 6) |
              (cut_bits(instruction, 3, 2) << 7) |
              (cut_bits(instruction, 12, 1) << 9);

        uint32_t rsp = riscv32i_read_register_u(vm, REGISTER_X2);
        riscv32i_write_register_u(vm, REGISTER_X2, rsp + sign_extend(imm, 10));
        printf("RVC: c.addi16sp %d in VM %p\n", sign_extend(imm, 10), vm);
    } else {
        imm = (cut_bits(instruction, 2, 5) << 12) |
              (cut_bits(instruction, 12, 1) << 17);

        riscv32i_write_register_u(vm, rds, imm);
        printf("RVC: c.lui %s, %d in VM %p\n", riscv32i_translate_register(rds), imm, vm);
    }
}

static void riscv32c_flwsp(risc32_vm_state_t *vm, const uint16_t instruction)
{
    printf("RVC: FLWSP instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_alops1(risc32_vm_state_t *vm, const uint16_t instruction)
{
    // goddamn glue opcode
    uint32_t rds = cut_bits(instruction, 7, 3);
    uint32_t reg1 = riscv32i_read_register_u(vm, riscv32c_reg(rds));
    uint32_t opc = cut_bits(instruction, 10, 2);

    if (opc == 0) {
        // c.srli
        uint32_t shamt = cut_bits(instruction, 2, 5);
        riscv32i_write_register_u(vm, riscv32c_reg(rds), reg1 >> shamt);
        printf("RVC: c.srli %s, %d in VM %p\n", riscv32i_translate_register(riscv32c_reg(rds)), shamt, vm);
    } else if (opc == 1) {
        // c.srai
        uint32_t shamt = cut_bits(instruction, 2, 5);
        riscv32i_write_register_u(vm, riscv32c_reg(rds), ((int32_t)reg1) >> shamt);
        printf("RVC: c.srai %s, %d in VM %p\n", riscv32i_translate_register(riscv32c_reg(rds)), shamt, vm);
    } else if (opc == 2) {
        // c.andi
        int32_t imm = sign_extend((cut_bits(instruction, 12, 1) << 5) | cut_bits(instruction, 2, 5), 6);
        riscv32i_write_register_u(vm, riscv32c_reg(rds), reg1 & imm);
        printf("RVC: c.andi %s, %d in VM %p\n", riscv32i_translate_register(riscv32c_reg(rds)), imm, vm);
    } else {
        opc = cut_bits(instruction, 5, 2);
        uint32_t rs2 = cut_bits(instruction, 2, 3);
        uint32_t reg2 = riscv32i_read_register_u(vm, riscv32c_reg(rs2));

        if (opc == 0) {
            // c.sub
            riscv32i_write_register_u(vm, riscv32c_reg(rds), reg1 - reg2);
            printf("RVC: c.sub %s, %s in VM %p\n", riscv32i_translate_register(riscv32c_reg(rds)), riscv32i_translate_register(riscv32c_reg(rs2)), vm);
        } else if (opc == 1) {
            // c.xor
            riscv32i_write_register_u(vm, riscv32c_reg(rds), reg1 ^ reg2);
            printf("RVC: c.xor %s, %s in VM %p\n", riscv32i_translate_register(riscv32c_reg(rds)), riscv32i_translate_register(riscv32c_reg(rs2)), vm);
        } else if (opc == 2) {
            // c.or
            riscv32i_write_register_u(vm, riscv32c_reg(rds), reg1 | reg2);
            printf("RVC: c.or %s, %s in VM %p\n", riscv32i_translate_register(riscv32c_reg(rds)), riscv32i_translate_register(riscv32c_reg(rs2)), vm);
        } else {
            // c.and
            riscv32i_write_register_u(vm, riscv32c_reg(rds), reg1 & reg2);
            printf("RVC: c.and %s, %s in VM %p\n", riscv32i_translate_register(riscv32c_reg(rds)), riscv32i_translate_register(riscv32c_reg(rs2)), vm);
        }
    }
}

static void riscv32c_alops2(risc32_vm_state_t *vm, const uint16_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs2 = cut_bits(instruction, 2, 5);

    if (is_bit_set(instruction, 12)) {
        if (rds != 0) {
            if (rs2 != 0) {
                // c.add
                uint32_t reg1 = riscv32i_read_register_u(vm, rds);
                uint32_t reg2 = riscv32i_read_register_u(vm, rs2);
                riscv32i_write_register_u(vm, rds, reg1 + reg2);
                printf("RVC: c.add %s, %s in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs2), vm);
            } else {
                // c.jalr
                uint32_t reg1 = riscv32i_read_register_u(vm, rds);
                uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);
                riscv32i_write_register_u(vm, REGISTER_X1, pc + 2);
                riscv32i_write_register_u(vm, REGISTER_PC, reg1 - 2);
                printf("RVC: c.jalr %s in VM %p\n", riscv32i_translate_register(rds), vm);
            }
        } else {
            // c.ebreak
            printf("RVC: c.ebreak in VM %p\n", vm);
        }
    } else {
        if (rs2 != 0) {
            // c.mv
            uint32_t reg2 = riscv32i_read_register_u(vm, rs2);
            riscv32i_write_register_u(vm, rds, reg2);
            printf("RVC: c.mv %s, %s in VM %p\n", riscv32i_translate_register(rds), riscv32i_translate_register(rs2), vm);
        } else {
            // c.jr
            uint32_t reg1 = riscv32i_read_register_u(vm, rds);
            riscv32i_write_register_u(vm, REGISTER_PC, reg1 - 2);
            printf("RVC: c.jr %s in VM %p\n", riscv32i_translate_register(rds), vm);
        }
    }
}

static void riscv32c_fsd(risc32_vm_state_t *vm, const uint16_t instruction)
{
    printf("RVC: FSD instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_j(risc32_vm_state_t *vm, const uint16_t instruction)
{
    // Jump to PC+offset
    uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);
    // This is total bullshit, we need translation table here in future
    uint32_t imm = (cut_bits(instruction, 3, 3) << 1)  |
                   (cut_bits(instruction, 11, 1) << 4) |
                   (cut_bits(instruction, 2, 1) << 5)  |
                   (cut_bits(instruction, 7, 1) << 6)  |
                   (cut_bits(instruction, 6, 1) << 7)  |
                   (cut_bits(instruction, 9, 2) << 8)  |
                   (cut_bits(instruction, 8, 1) << 10) |
                   (cut_bits(instruction, 12, 1) << 11);
    int32_t offset = sign_extend(imm, 12);

    riscv32i_write_register_u(vm, REGISTER_PC, pc + offset - 2);
    printf("RVC: c.j %d in VM %p\n", offset, vm);
}

static void riscv32c_fsdsp(risc32_vm_state_t *vm, const uint16_t instruction)
{
    printf("RVC: FSDSP instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_sw(risc32_vm_state_t *vm, const uint16_t instruction)
{
    printf("RVC: SW instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_beqz(risc32_vm_state_t *vm, const uint16_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 3);
    uint32_t reg1 = riscv32i_read_register_u(vm, riscv32c_reg(rds));
    if (reg1 == 0) {
        uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);
        uint32_t imm = (cut_bits(instruction, 3, 2) << 1)  |
                       (cut_bits(instruction, 10, 2) << 3) |
                       (cut_bits(instruction, 2, 1) << 5)  |
                       (cut_bits(instruction, 5, 2) << 6)  |
                       (cut_bits(instruction, 12, 1) << 8);
        int32_t offset = sign_extend(imm, 9);

        riscv32i_write_register_u(vm, REGISTER_PC, pc + offset - 2);
    }
    printf("RVC: c.beqz %s in VM %p\n", riscv32i_translate_register(riscv32c_reg(rds)), vm);
}

static void riscv32c_swsp(risc32_vm_state_t *vm, const uint16_t instruction)
{
    printf("RVC: SWSP instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_fsw(risc32_vm_state_t *vm, const uint16_t instruction)
{
    printf("RVC: FSW instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_bnez(risc32_vm_state_t *vm, const uint16_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 3);
    uint32_t reg1 = riscv32i_read_register_u(vm, riscv32c_reg(rds));
    if (reg1 != 0) {
        uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);
        uint32_t imm = (cut_bits(instruction, 3, 2) << 1)  |
                       (cut_bits(instruction, 10, 2) << 3) |
                       (cut_bits(instruction, 2, 1) << 5)  |
                       (cut_bits(instruction, 5, 2) << 6)  |
                       (cut_bits(instruction, 12, 1) << 8);
        int32_t offset = sign_extend(imm, 9);

        riscv32i_write_register_u(vm, REGISTER_PC, pc + offset - 2);
    }
    printf("RVC: c.bnez %s in VM %p\n", riscv32i_translate_register(riscv32c_reg(rds)), vm);
}

static void riscv32c_fswsp(risc32_vm_state_t *vm, const uint16_t instruction)
{
    printf("RVC: FSWSP instruction 0x%x in VM %p\n", instruction, vm);
}

static void (*opcodes[32])(risc32_vm_state_t *vm, const uint16_t instruction);

void riscv32c_init()
{
    for (uint32_t i=0; i<32; ++i) opcodes[i] = riscv32c_illegal_insn;

    opcodes[RVC_ADDI4SPN] = riscv32c_addi4spn;
    opcodes[RVC_ADDI_NOP] = riscv32c_addi_nop;
    opcodes[RVC_SLLI] = riscv32c_slli;
    opcodes[RVC_FLD] = riscv32c_fld;
    opcodes[RVC_JAL] = riscv32c_jal;
    opcodes[RVC_FLDSP] = riscv32c_fldsp;
    opcodes[RVC_LW] = riscv32c_lw;
    opcodes[RVC_LI] = riscv32c_li;
    opcodes[RVC_LWSP] = riscv32c_lwsp;
    opcodes[RVC_FLW] = riscv32c_flw;
    opcodes[RVC_ADDI16SP_LUI] = riscv32c_addi16sp_lui;
    opcodes[RVC_FLWSP] = riscv32c_flwsp;

    // Those are tricky fuckers and need additional decoding
    // basically a glue instruction for CR and CA instructions
    opcodes[RVC_ALOPS1] = riscv32c_alops1;
    opcodes[RVC_ALOPS2] = riscv32c_alops2;

    opcodes[RVC_FSD] = riscv32c_fsd;
    opcodes[RVC_J] = riscv32c_j;
    opcodes[RVC_FSDSP] = riscv32c_fsdsp;
    opcodes[RVC_SW] = riscv32c_sw;
    opcodes[RVC_BEQZ] = riscv32c_beqz;
    opcodes[RVC_SWSP] = riscv32c_swsp;
    opcodes[RVC_FSW] = riscv32c_fsw;
    opcodes[RVC_BNEZ] = riscv32c_bnez;
    opcodes[RVC_FSWSP] = riscv32c_fswsp;
}

void riscv32c_emulate(risc32_vm_state_t *vm, const uint16_t instruction)
{
    uint16_t funcid = RISCV32C_GET_FUNCID(instruction);
    opcodes[funcid](vm, instruction);
}
