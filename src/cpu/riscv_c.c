/*
riscv_c.c - RISC-V C Decoder, Interpreter
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
#include "riscv32_mmu.h"
#include "compiler.h"

// translate compressed register encoding into normal
static inline regid_t riscv_c_reg(regid_t reg)
{
    //NOTE: register index is hard limited to 8, since encoding is 3 bits
    return REGISTER_X8 + reg;
}

// Decode c.j / c.jal offset
static inline sxlen_t decode_jal_imm(uint16_t imm)
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

static void riscv_c_addi4spn(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Add imm*4 to stack pointer (X2), store into rds
    regid_t rds = riscv_c_reg(bit_cut(instruction, 2, 3));
    xlen_t rsp = riscv_read_register(vm, REGISTER_X2);
    uint32_t imm =  (bit_cut(instruction, 6, 1)  << 2) |
                    (bit_cut(instruction, 5, 1)  << 3) |
                    (bit_cut(instruction, 11, 2) << 4) |
                    (bit_cut(instruction, 7, 4)  << 6);

    riscv_write_register(vm, rds, rsp + imm);
}

static void riscv_c_addi(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Add 6-bit signed immediate to rds (this also serves as NOP for X0 reg)
    regid_t rds = bit_cut(instruction, 7, 5);
    xlen_t src_reg = riscv_read_register(vm, rds);
    sxlen_t imm = sign_extend((bit_cut(instruction, 12, 1) << 5) |
                             (bit_cut(instruction, 2, 5)), 6);

    riscv_write_register(vm, rds, src_reg + imm);
}

static void riscv_c_slli(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Left shift rds by imm, store into rds
    regid_t rds = bit_cut(instruction, 7, 5);
    xlen_t src_reg = riscv_read_register(vm, rds);
    uint8_t shamt = bit_cut(instruction, 2, SHAMT_BITS);

    riscv_write_register(vm, rds, src_reg << shamt);
}

static void riscv_c_fld(rvvm_hart_t *vm, const uint16_t instruction)
{
    if (unlikely(!fpu_is_enabled(vm))) {
        riscv_c_illegal_insn(vm, instruction);
        return;
    }

    // Read double-precision floating point value from address rs1+offset to rds
    regid_t rds = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 5, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    uint8_t val[sizeof(double)];

    if (riscv_mem_op(vm, addr, val, sizeof(val), MMU_READ)) {
        fpu_write_register64(vm, rds, read_fp64(val));
    }
}

#ifndef RV64
static void riscv_c_jal(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Save PC+2 into X1 (return addr), jump to PC+offset
    xlen_t pc = riscv_read_register(vm, REGISTER_PC);
    sxlen_t offset = decode_jal_imm(bit_cut(instruction, 2, 11));

    riscv_write_register(vm, REGISTER_X1, pc + 2);
    riscv_write_register(vm, REGISTER_PC, pc + offset - 2);
}
#endif

static void riscv_c_fldsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    if (unlikely(!fpu_is_enabled(vm))) {
        riscv_c_illegal_insn(vm, instruction);
        return;
    }

    // Read double-precision floating point value from address sp+offset to rds
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t offset = (bit_cut(instruction, 5, 2)  << 3)
                    | (bit_cut(instruction, 12, 1) << 5)
                    | (bit_cut(instruction, 2, 3)  << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    uint8_t val[sizeof(double)];

    if (riscv_mem_op(vm, addr, val, sizeof(val), MMU_READ)) {
        fpu_write_register64(vm, rds, read_fp64(val));
    }
}

static void riscv_c_lw(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read 32-bit integer from address rs1+offset to rds
    regid_t rds = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 6, 1)  << 2) |
                      (bit_cut(instruction, 10, 3) << 3) |
                      (bit_cut(instruction, 5, 1)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    uint8_t val[sizeof(uint32_t)];

    if (riscv_mem_op(vm, addr, val, sizeof(uint32_t), MMU_READ)) {
        riscv_write_register(vm, rds, read_uint32_le(val));
    }
}

static void riscv_c_li(rvvm_hart_t *vm, const uint16_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    sxlen_t imm = sign_extend((bit_cut(instruction, 12, 1) << 5) |
                             (bit_cut(instruction, 2, 5)), 6);

    riscv_write_register(vm, rds, imm);
}

static void riscv_c_lwsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read 32-bit integer from address sp+offset to rds
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t offset = (bit_cut(instruction, 4, 3)  << 2) |
                      (bit_cut(instruction, 12, 1) << 5) |
                      (bit_cut(instruction, 2, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    uint8_t val[sizeof(uint32_t)];

    if (riscv_mem_op(vm, addr, val, sizeof(uint32_t), MMU_READ)) {
        riscv_write_register(vm, rds, read_uint32_le(val));
    }
}

static void riscv_c_flw(rvvm_hart_t *vm, const uint16_t instruction)
{
    if (unlikely(!fpu_is_enabled(vm))) {
        riscv_c_illegal_insn(vm, instruction);
        return;
    }

    // Read single-precision floating point value from address rs1+offset to rds
    regid_t rds = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 6, 1)  << 2)
                    | (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 5, 1)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    uint8_t val[sizeof(float)];

    if (riscv_mem_op(vm, addr, val, sizeof(val), MMU_READ)) {
        fpu_write_register32(vm, rds, read_fp32(val));
    }
}

static void riscv_c_addi16sp_lui(rvvm_hart_t *vm, const uint16_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t imm;

    if (rds == REGISTER_X2) {
        imm = (bit_cut(instruction, 6, 1) << 4) |
              (bit_cut(instruction, 2, 1) << 5) |
              (bit_cut(instruction, 5, 1) << 6) |
              (bit_cut(instruction, 3, 2) << 7) |
              (bit_cut(instruction, 12, 1) << 9);

        xlen_t rsp = riscv_read_register(vm, REGISTER_X2);
        riscv_write_register(vm, REGISTER_X2, rsp + sign_extend(imm, 10));
    } else {
        imm = (bit_cut(instruction, 2, 5)  << 12) |
              (bit_cut(instruction, 12, 1) << 17);

        riscv_write_register(vm, rds, sign_extend(imm, 18));
    }
}

static void riscv_c_flwsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    if (unlikely(!fpu_is_enabled(vm))) {
        riscv_c_illegal_insn(vm, instruction);
        return;
    }

    // Read single-precision floating point value from address sp+offset to rds
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t offset = (bit_cut(instruction, 4, 3)  << 2)
                    | (bit_cut(instruction, 12, 1) << 5)
                    | (bit_cut(instruction, 2, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    uint8_t val[sizeof(float)];

    if (riscv_mem_op(vm, addr, val, sizeof(val), MMU_READ)) {
        fpu_write_register32(vm, rds, read_fp32(val));
    }
}

static void riscv_c_alops1(rvvm_hart_t *vm, const uint16_t instruction)
{
    // goddamn glue opcode
    regid_t rds = riscv_c_reg(bit_cut(instruction, 7, 3));
    xlen_t reg1 = riscv_read_register(vm, rds);
    uint8_t opc = bit_cut(instruction, 10, 2);

    if (opc == 0) {
        // c.srli
        uint8_t shamt = bit_cut(instruction, 2, SHAMT_BITS);
        riscv_write_register(vm, rds, reg1 >> shamt);
    } else if (opc == 1) {
        // c.srai
        uint8_t shamt = bit_cut(instruction, 2, SHAMT_BITS);
        riscv_write_register(vm, rds, ((sxlen_t)reg1) >> shamt);
    } else if (opc == 2) {
        // c.andi
        sxlen_t imm = sign_extend((bit_cut(instruction, 12, 1) << 5) |
                                  bit_cut(instruction, 2, 5), 6);
        riscv_write_register(vm, rds, reg1 & imm);
    } else {
        opc = bit_cut(instruction, 5, 2);
        regid_t rs2 = riscv_c_reg(bit_cut(instruction, 2, 3));
        xlen_t reg2 = riscv_read_register(vm, rs2);

        if (opc == 0) {
            // c.sub
            riscv_write_register(vm, rds, reg1 - reg2);
        } else if (opc == 1) {
            // c.xor
            riscv_write_register(vm, rds, reg1 ^ reg2);
        } else if (opc == 2) {
            // c.or
            riscv_write_register(vm, rds, reg1 | reg2);
        } else {
            // c.and
            riscv_write_register(vm, rds, reg1 & reg2);
        }
    }
}

static void riscv_c_alops2(rvvm_hart_t *vm, const uint16_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs2 = bit_cut(instruction, 2, 5);

    if (bit_check(instruction, 12)) {
        if (rds != 0) {
            if (rs2 != 0) {
                // c.add
                xlen_t reg1 = riscv_read_register(vm, rds);
                xlen_t reg2 = riscv_read_register(vm, rs2);
                riscv_write_register(vm, rds, reg1 + reg2);
            } else {
                // c.jalr
                xlen_t reg1 = riscv_read_register(vm, rds);
                xlen_t pc = riscv_read_register(vm, REGISTER_PC);
                riscv_write_register(vm, REGISTER_X1, pc + 2);
                riscv_write_register(vm, REGISTER_PC, reg1 - 2);
            }
        } else {
            // c.ebreak
            riscv_trap(vm, TRAP_BREAKPOINT, 0);
        }
    } else {
        if (rs2 != 0) {
            // c.mv
            xlen_t reg2 = riscv_read_register(vm, rs2);
            riscv_write_register(vm, rds, reg2);
        } else {
            // c.jr
            xlen_t reg1 = riscv_read_register(vm, rds);
            riscv_write_register(vm, REGISTER_PC, reg1 - 2);
        }
    }
}

static void riscv_c_fsd(rvvm_hart_t *vm, const uint16_t instruction)
{
    if (unlikely(!fpu_is_enabled(vm))) {
        riscv_c_illegal_insn(vm, instruction);
        return;
    }

    // Write double-precision floating point value rs2 to address rs1+offset
    regid_t rs2 = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 5, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    uint8_t val[sizeof(double)];
    write_fp64(val, fpu_read_register64(vm, rs2));
    riscv_mem_op(vm, addr, val, sizeof(val), MMU_WRITE);
}

static void riscv_c_j(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Jump to PC+offset
    xlen_t pc = riscv_read_register(vm, REGISTER_PC);
    sxlen_t offset = decode_jal_imm(bit_cut(instruction, 2, 11));

    riscv_write_register(vm, REGISTER_PC, pc + offset - 2);
}

static void riscv_c_fsdsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    if (unlikely(!fpu_is_enabled(vm))) {
        riscv_c_illegal_insn(vm, instruction);
        return;
    }

    // Write double-precision floating point value rs2 to address sp+offset
    regid_t rs2 = bit_cut(instruction, 2, 5);
    uint32_t offset = (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 7, 3) << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    uint8_t val[sizeof(double)];
    write_fp64(val, fpu_read_register64(vm, rs2));
    riscv_mem_op(vm, addr, val, sizeof(val), MMU_WRITE);
}

static void riscv_c_sw(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write 32-bit integer rs2 to address rs1+offset
    regid_t rs2 = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 6, 1)  << 2) |
                      (bit_cut(instruction, 10, 3) << 3) |
                      (bit_cut(instruction, 5, 1)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    uint8_t val[sizeof(uint32_t)];
    write_uint32_le(val, riscv_read_register(vm, rs2));
    riscv_mem_op(vm, addr, val, sizeof(uint32_t), MMU_WRITE);
}

static inline sxlen_t decode_branch_imm(const uint16_t instruction)
{
    const uint32_t imm = (bit_cut(instruction, 3, 2) << 1)  |
                         (bit_cut(instruction, 10, 2) << 3) |
                         (bit_cut(instruction, 2, 1) << 5)  |
                         (bit_cut(instruction, 5, 2) << 6)  |
                         (bit_cut(instruction, 12, 1) << 8);
    return sign_extend(imm, 9);
}

static void riscv_c_beqz(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Conditional jump if rds == 0
    regid_t rds = riscv_c_reg(bit_cut(instruction, 7, 3));
    xlen_t reg1 = riscv_read_register(vm, rds);
    if (reg1 == 0) {
        xlen_t pc = riscv_read_register(vm, REGISTER_PC);
        sxlen_t offset = decode_branch_imm(instruction);

        riscv_write_register(vm, REGISTER_PC, pc + offset - 2);
    }
}

static void riscv_c_swsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write 32-bit integer rs2 to address sp+offset
    regid_t rs2 = bit_cut(instruction, 2, 5);
    uint32_t offset = (bit_cut(instruction, 9, 4)  << 2) |
                      (bit_cut(instruction, 7, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    uint8_t val[sizeof(uint32_t)];
    write_uint32_le(val, riscv_read_register(vm, rs2));
    riscv_mem_op(vm, addr, val, sizeof(uint32_t), MMU_WRITE);
}

static void riscv_c_fsw(rvvm_hart_t *vm, const uint16_t instruction)
{
    if (unlikely(!fpu_is_enabled(vm))) {
        riscv_c_illegal_insn(vm, instruction);
        return;
    }

    // Write single-precision floating point value rs2 to address rs1+offset
    regid_t rs2 = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 6, 1)  << 2)
                    | (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 5, 1)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    uint8_t val[sizeof(float)];
    write_fp32(val, fpu_read_register32(vm, rs2));
    riscv_mem_op(vm, addr, val, sizeof(val), MMU_WRITE);
}

static void riscv_c_bnez(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Conditional jump if rds != 0
    regid_t rds = riscv_c_reg(bit_cut(instruction, 7, 3));
    xlen_t reg1 = riscv_read_register(vm, rds);
    if (reg1 != 0) {
        xlen_t pc = riscv_read_register(vm, REGISTER_PC);
        sxlen_t offset = decode_branch_imm(instruction);

        riscv_write_register(vm, REGISTER_PC, pc + offset - 2);
    }
}

static void riscv_c_fswsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    if (unlikely(!fpu_is_enabled(vm))) {
        riscv_c_illegal_insn(vm, instruction);
        return;
    }

    // Write single-precision floating point value rs2 to address sp+offset
    regid_t rs2 = bit_cut(instruction, 2, 5);
    uint32_t offset = (bit_cut(instruction, 9, 4) << 2)
                    | (bit_cut(instruction, 7, 2) << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    uint8_t val[sizeof(float)];
    write_fp32(val, fpu_read_register32(vm, rs2));
    riscv_mem_op(vm, addr, val, sizeof(val), MMU_WRITE);
}

void riscv_c_init()
{
    riscv_install_opcode_C(RVC_ADDI4SPN, riscv_c_addi4spn);
    riscv_install_opcode_C(RVC_ADDI, riscv_c_addi);
    riscv_install_opcode_C(RVC_SLLI, riscv_c_slli);
    riscv_install_opcode_C(RVC_FLD, riscv_c_fld);
#ifndef RV64
    riscv_install_opcode_C(RVC_JAL, riscv_c_jal);
#endif
    riscv_install_opcode_C(RVC_FLDSP, riscv_c_fldsp);
    riscv_install_opcode_C(RVC_LW, riscv_c_lw);
    riscv_install_opcode_C(RVC_LI, riscv_c_li);
    riscv_install_opcode_C(RVC_LWSP, riscv_c_lwsp);
    riscv_install_opcode_C(RVC_FLW, riscv_c_flw);
    riscv_install_opcode_C(RVC_ADDI16SP_LUI, riscv_c_addi16sp_lui);
    riscv_install_opcode_C(RVC_FLWSP, riscv_c_flwsp);

    // Those are tricky fuckers and need additional decoding
    // basically a glue instruction for CR and CA instructions
    riscv_install_opcode_C(RVC_ALOPS1, riscv_c_alops1);
    riscv_install_opcode_C(RVC_ALOPS2, riscv_c_alops2);

    riscv_install_opcode_C(RVC_FSD, riscv_c_fsd);
    riscv_install_opcode_C(RVC_J, riscv_c_j);
    riscv_install_opcode_C(RVC_FSDSP, riscv_c_fsdsp);
    riscv_install_opcode_C(RVC_SW, riscv_c_sw);
    riscv_install_opcode_C(RVC_BEQZ, riscv_c_beqz);
    riscv_install_opcode_C(RVC_SWSP, riscv_c_swsp);
    riscv_install_opcode_C(RVC_FSW, riscv_c_fsw);
    riscv_install_opcode_C(RVC_BNEZ, riscv_c_bnez);
    riscv_install_opcode_C(RVC_FSWSP, riscv_c_fswsp);
}
