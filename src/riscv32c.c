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
#include "riscv32_mmu.h"
#include "riscv32i.h"
#include "riscv32c.h"
#include "bit_ops.h"

// translate compressed register encoding into normal
static inline uint32_t riscv32c_reg(uint32_t reg)
{
    //NOTE: register index is hard limited to 8, since encoding is 3 bits
    return REGISTER_X8 + reg;
}

// Decode c.j / c.jal offset
static inline int16_t decode_jal_imm(uint16_t imm)
{
    imm = (bit_cut(imm, 1, 3) << 1)  |
          (bit_cut(imm, 9, 1) << 4)  |
          (bit_cut(imm, 0, 1) << 5)  |
          (bit_cut(imm, 5, 1) << 6)  |
          (bit_cut(imm, 4, 1) << 7)  |
          (bit_cut(imm, 7, 2) << 8)  |
          (bit_cut(imm, 6, 1) << 10) |
          (bit_cut(imm, 10, 1) << 11);
    return sign_extend(imm, 12);
}

static void riscv32c_addi4spn(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    // Add imm*4 to stack pointer (X2), store into rds
    uint32_t rds = riscv32c_reg(bit_cut(instruction, 2, 3));
    uint32_t imm =  (bit_cut(instruction, 6, 1)  << 2) |
                    (bit_cut(instruction, 5, 1)  << 3) |
                    (bit_cut(instruction, 11, 2) << 4) |
                    (bit_cut(instruction, 7, 4)  << 6);
    uint32_t rsp = riscv32i_read_register_u(vm, REGISTER_X2);
    riscv32i_write_register_u(vm, rds, rsp + imm);
    riscv32_debug(vm, "RV32C: c.addi4spn %r, %d", rds, imm);
}

static void riscv32c_addi_nop(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    // Add 6-bit signed immediate to rds (this also serves as NOP for X0 reg)
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t src_reg = riscv32i_read_register_u(vm, rds);
    int32_t imm = sign_extend((bit_cut(instruction, 12, 1) << 5) |
                              (bit_cut(instruction, 2, 5)), 6);

    riscv32i_write_register_u(vm, rds, src_reg + imm);
    riscv32_debug(vm, "RV32C: c.addi %r, %d", rds, imm);
}

static void riscv32c_slli(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    // Left shift rds by imm, store into rds
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t src_reg = riscv32i_read_register_u(vm, rds);
    uint32_t shamt = bit_cut(instruction, 2, 5);

    riscv32i_write_register_u(vm, rds, src_reg << shamt);
    riscv32_debug(vm, "RV32C: c.slli %r, %d", rds, shamt);
}

static void riscv32c_fld(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    riscv32_debug_always(vm, "RV32C: unimplemented FLD: %h", instruction);
}

static void riscv32c_jal(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    // Save PC+2 into X1 (return addr), jump to PC+offset
    uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);
    int32_t offset = decode_jal_imm(bit_cut(instruction, 2, 11));

    riscv32i_write_register_u(vm, REGISTER_X1, pc + 2);
    riscv32i_write_register_u(vm, REGISTER_PC, pc + offset - 2);
    riscv32_debug(vm, "RV32C: c.jal %d", offset);
}

static void riscv32c_fldsp(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    riscv32_debug_always(vm, "RV32C: unimplemented FLDSP: %h", instruction);
}

static void riscv32c_lw(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    // Read 32-bit integer from address rs1+offset to rds
    uint32_t rds = riscv32c_reg(bit_cut(instruction, 2, 3));
    uint32_t rs1 = riscv32c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 6, 1)  << 2) |
                      (bit_cut(instruction, 10, 3) << 3) |
                      (bit_cut(instruction, 5, 1)  << 6);

    uint32_t addr = riscv32i_read_register_u(vm, rs1) + offset;
    uint8_t val[sizeof(uint32_t)];

    if (riscv32_mem_op(vm, addr, val, sizeof(uint32_t), MMU_READ)) {
        riscv32i_write_register_u(vm, rds, read_uint32_le(val));
    }
    riscv32_debug(vm, "RV32C: c.lw %r, %r, %d", rds, rs1, offset);
}

static void riscv32c_li(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    uint32_t rds = bit_cut(instruction, 7, 5);
    int32_t imm = sign_extend((bit_cut(instruction, 12, 1) << 5) |
                              (bit_cut(instruction, 2, 5)), 6);

    riscv32i_write_register_s(vm, rds, imm);
    riscv32_debug(vm, "RV32C: c.li %r, %d", rds, imm);
}

static void riscv32c_lwsp(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    // Read 32-bit integer from address sp+offset to rds
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t offset = (bit_cut(instruction, 4, 3)  << 2) |
                      (bit_cut(instruction, 12, 1) << 5) |
                      (bit_cut(instruction, 2, 2)  << 6);

    uint32_t addr = riscv32i_read_register_u(vm, REGISTER_X2) + offset;
    uint8_t val[sizeof(uint32_t)];

    if (riscv32_mem_op(vm, addr, val, sizeof(uint32_t), MMU_READ)) {
        riscv32i_write_register_u(vm, rds, read_uint32_le(val));
    }
    riscv32_debug(vm, "RV32C: c.lwsp %r, %d", rds, offset);
}

static void riscv32c_flw(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    riscv32_debug_always(vm, "RV32C: unimplemented FLW: %h", instruction);
}

static void riscv32c_addi16sp_lui(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t imm;

    if (rds == REGISTER_X2) {
        imm = (bit_cut(instruction, 6, 1) << 4) |
              (bit_cut(instruction, 2, 1) << 5) |
              (bit_cut(instruction, 5, 1) << 6) |
              (bit_cut(instruction, 3, 2) << 7) |
              (bit_cut(instruction, 12, 1) << 9);

        uint32_t rsp = riscv32i_read_register_u(vm, REGISTER_X2);
        riscv32i_write_register_u(vm, REGISTER_X2, rsp + sign_extend(imm, 10));
        riscv32_debug(vm, "RV32C: c.addi16sp %d", sign_extend(imm, 10));
    } else {
        bool bit17 = !!bit_cut(instruction, 12, 1);
        imm = (bit_cut(instruction, 2, 5) << 12) |
              (bit17 << 17) | bit17 * (-(uint32_t)1 << 17);

        riscv32i_write_register_u(vm, rds, imm);
        riscv32_debug(vm, "RV32C: c.lui %r, %h", rds, imm);
    }
}

static void riscv32c_flwsp(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    riscv32_debug_always(vm, "RV32C: unimplemented FLWSP: %h", instruction);
}

static void riscv32c_alops1(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    // goddamn glue opcode
    uint32_t rds = riscv32c_reg(bit_cut(instruction, 7, 3));
    uint32_t reg1 = riscv32i_read_register_u(vm, rds);
    uint32_t opc = bit_cut(instruction, 10, 2);

    if (opc == 0) {
        // c.srli
        uint32_t shamt = bit_cut(instruction, 2, 5);
        riscv32i_write_register_u(vm, rds, reg1 >> shamt);
        riscv32_debug(vm, "RV32C: c.srli %r, %d", rds, shamt);
    } else if (opc == 1) {
        // c.srai
        uint32_t shamt = bit_cut(instruction, 2, 5);
        riscv32i_write_register_u(vm, rds, ((int32_t)reg1) >> shamt);
        riscv32_debug(vm, "RV32C: c.srai %r, %d", rds, shamt);
    } else if (opc == 2) {
        // c.andi
        int32_t imm = sign_extend((bit_cut(instruction, 12, 1) << 5) |
                                   bit_cut(instruction, 2, 5), 6);
        riscv32i_write_register_u(vm, rds, reg1 & imm);
        riscv32_debug(vm, "RV32C: c.andi %r, %h", rds, imm);
    } else {
        opc = bit_cut(instruction, 5, 2);
        uint32_t rs2 = riscv32c_reg(bit_cut(instruction, 2, 3));
        uint32_t reg2 = riscv32i_read_register_u(vm, rs2);

        if (opc == 0) {
            // c.sub
            riscv32i_write_register_u(vm, rds, reg1 - reg2);
            riscv32_debug(vm, "RV32C: c.sub %r, %r", rds, rs2);
        } else if (opc == 1) {
            // c.xor
            riscv32i_write_register_u(vm, rds, reg1 ^ reg2);
            riscv32_debug(vm, "RV32C: c.xor %r, %r", rds, rs2);
        } else if (opc == 2) {
            // c.or
            riscv32i_write_register_u(vm, rds, reg1 | reg2);
            riscv32_debug(vm, "RV32C: c.or %r, %r", rds, rs2);
        } else {
            // c.and
            riscv32i_write_register_u(vm, rds, reg1 & reg2);
            riscv32_debug(vm, "RV32C: c.and %r, %r", rds, rs2);
        }
    }
}

static void riscv32c_alops2(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    uint32_t rds = bit_cut(instruction, 7, 5);
    uint32_t rs2 = bit_cut(instruction, 2, 5);

    if (bit_check(instruction, 12)) {
        if (rds != 0) {
            if (rs2 != 0) {
                // c.add
                uint32_t reg1 = riscv32i_read_register_u(vm, rds);
                uint32_t reg2 = riscv32i_read_register_u(vm, rs2);
                riscv32i_write_register_u(vm, rds, reg1 + reg2);
                riscv32_debug(vm, "RV32C: c.add %r, %r", rds, rs2);
            } else {
                // c.jalr
                uint32_t reg1 = riscv32i_read_register_u(vm, rds);
                uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);
                riscv32i_write_register_u(vm, REGISTER_X1, pc + 2);
                riscv32i_write_register_u(vm, REGISTER_PC, reg1 - 2);
                riscv32_debug(vm, "RV32C: c.jalr %r", rds);
            }
        } else {
            // c.ebreak
            riscv32_trap(vm, TRAP_BREAKPOINT, 0);
            riscv32_debug(vm, "RV32C: c.ebreak");
        }
    } else {
        if (rs2 != 0) {
            // c.mv
            uint32_t reg2 = riscv32i_read_register_u(vm, rs2);
            riscv32i_write_register_u(vm, rds, reg2);
            riscv32_debug(vm, "RV32C: c.mv %r, %r", rds, rs2);
        } else {
            // c.jr
            uint32_t reg1 = riscv32i_read_register_u(vm, rds);
            riscv32i_write_register_u(vm, REGISTER_PC, reg1 - 2);
            riscv32_debug(vm, "RV32C: c.jr %r", rds);
        }
    }
}

static void riscv32c_fsd(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    riscv32_debug_always(vm, "RV32C: unimplemented FSD: %h", instruction);
}

static void riscv32c_j(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    // Jump to PC+offset
    uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);
    int32_t offset = decode_jal_imm(bit_cut(instruction, 2, 11));

    riscv32i_write_register_u(vm, REGISTER_PC, pc + offset - 2);
    riscv32_debug(vm, "RV32C: c.j %d", offset);
}

static void riscv32c_fsdsp(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    riscv32_debug_always(vm, "RV32C: unimplemented FSDSP: %h", instruction);
}

static void riscv32c_sw(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    // Write 32-bit integer rs2 to address rs1+offset
    uint32_t rs2 = riscv32c_reg(bit_cut(instruction, 2, 3));
    uint32_t rs1 = riscv32c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 6, 1)  << 2) |
                      (bit_cut(instruction, 10, 3) << 3) |
                      (bit_cut(instruction, 5, 1)  << 6);

    uint32_t addr = riscv32i_read_register_u(vm, rs1) + offset;
    uint8_t val[sizeof(uint32_t)];
    write_uint32_le(val, riscv32i_read_register_u(vm, rs2));
    riscv32_mem_op(vm, addr, val, sizeof(uint32_t), MMU_WRITE);

    riscv32_debug(vm, "RV32C: c.sw %r, %r, %d", rs2, rs1, offset);
}

static void riscv32c_beqz(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    uint32_t rds = riscv32c_reg(bit_cut(instruction, 7, 3));
    uint32_t reg1 = riscv32i_read_register_u(vm, rds);
    if (reg1 == 0) {
        uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);
        uint32_t imm = (bit_cut(instruction, 3, 2) << 1)  |
                       (bit_cut(instruction, 10, 2) << 3) |
                       (bit_cut(instruction, 2, 1) << 5)  |
                       (bit_cut(instruction, 5, 2) << 6)  |
                       (bit_cut(instruction, 12, 1) << 8);
        int32_t offset = sign_extend(imm, 9);

        riscv32i_write_register_u(vm, REGISTER_PC, pc + offset - 2);
    }
    riscv32_debug(vm, "RV32C: c.beqz %r", rds);
}

static void riscv32c_swsp(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    // Write 32-bit integer rs2 to address sp+offset
    uint32_t rs2 = bit_cut(instruction, 2, 5);
    uint32_t offset = (bit_cut(instruction, 9, 4)  << 2) |
                      (bit_cut(instruction, 7, 2)  << 6);

    uint32_t addr = riscv32i_read_register_u(vm, REGISTER_X2) + offset;
    uint8_t val[sizeof(uint32_t)];
    write_uint32_le(val, riscv32i_read_register_u(vm, rs2));
    riscv32_mem_op(vm, addr, val, sizeof(uint32_t), MMU_WRITE);

    riscv32_debug(vm, "RV32C: c.swsp %r, %d", rs2, offset);
}

static void riscv32c_fsw(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    riscv32_debug_always(vm, "RV32C: unimplemented FSW: %h", instruction);
}

static void riscv32c_bnez(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    uint32_t rds = riscv32c_reg(bit_cut(instruction, 7, 3));
    uint32_t reg1 = riscv32i_read_register_u(vm, rds);
    if (reg1 != 0) {
        uint32_t pc = riscv32i_read_register_u(vm, REGISTER_PC);
        uint32_t imm = (bit_cut(instruction, 3, 2) << 1)  |
                       (bit_cut(instruction, 10, 2) << 3) |
                       (bit_cut(instruction, 2, 1) << 5)  |
                       (bit_cut(instruction, 5, 2) << 6)  |
                       (bit_cut(instruction, 12, 1) << 8);
        int32_t offset = sign_extend(imm, 9);

        riscv32i_write_register_u(vm, REGISTER_PC, pc + offset - 2);
    }
    riscv32_debug(vm, "RV32C: c.bnez %r", rds);
}

static void riscv32c_fswsp(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    riscv32_debug_always(vm, "RV32C: unimplemented FSWSP: %h", instruction);
}

static void (*opcodes[32])(riscv32_vm_state_t *vm, const uint16_t instruction);

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

void riscv32c_emulate(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    uint16_t funcid = RISCV32C_GET_FUNCID(instruction);
    opcodes[funcid](vm, instruction);
}
