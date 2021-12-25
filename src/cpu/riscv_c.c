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
#include "riscv_mmu.h"
#include "compiler.h"

#include "rvjit/rvjit_emit.h"

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

    rvjit_addi(rds, REGISTER_X2, imm, 2);

    riscv_write_register(vm, rds, rsp + imm);
}

static void riscv_c_addi(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Add 6-bit signed immediate to rds (this also serves as NOP for X0 reg)
    regid_t rds = bit_cut(instruction, 7, 5);
    xlen_t src_reg = riscv_read_register(vm, rds);
    sxlen_t imm = sign_extend((bit_cut(instruction, 12, 1) << 5) |
                             (bit_cut(instruction, 2, 5)), 6);

    rvjit_addi(rds, rds, imm, 2);

    riscv_write_register(vm, rds, src_reg + imm);
}

static void riscv_c_slli(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Left shift rds by imm, store into rds
    regid_t rds = bit_cut(instruction, 7, 5);
    xlen_t src_reg = riscv_read_register(vm, rds);
#ifdef RV64
    uint8_t shamt = bit_cut(instruction, 2, 5) |
                   (bit_cut(instruction, 12, 1) << 5);
#else
    uint8_t shamt = bit_cut(instruction, 2, 5);
#endif

    rvjit_slli(rds, rds, shamt, 2);

    riscv_write_register(vm, rds, src_reg << shamt);
}

#ifndef RV64
static void riscv_c_jal(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Save PC+2 into X1 (return addr), jump to PC+offset
    xlen_t pc = riscv_read_register(vm, REGISTER_PC);
    sxlen_t offset = decode_jal_imm(bit_cut(instruction, 2, 11));

    rvjit_jal(REGISTER_X1, offset, 2);

    riscv_write_register(vm, REGISTER_X1, pc + 2);
    riscv_write_register(vm, REGISTER_PC, pc + offset - 2);
}
#endif

static void riscv_c_lw(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read 32-bit integer from address rs1+offset to rds
    regid_t rds = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 6, 1)  << 2) |
                      (bit_cut(instruction, 10, 3) << 3) |
                      (bit_cut(instruction, 5, 1)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    
    rvjit_lw(rds, rs1, offset, 2);

    riscv_load_s32(vm, addr, rds);
}

static void riscv_c_li(rvvm_hart_t *vm, const uint16_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    sxlen_t imm = sign_extend((bit_cut(instruction, 12, 1) << 5) |
                             (bit_cut(instruction, 2, 5)), 6);

    rvjit_li(rds, imm, 2);

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
    
    rvjit_lw(rds, REGISTER_X2, offset, 2);

    riscv_load_s32(vm, addr, rds);
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

        rvjit_addi(REGISTER_X2, REGISTER_X2, sign_extend(imm, 10), 2);

        xlen_t rsp = riscv_read_register(vm, REGISTER_X2);
        riscv_write_register(vm, REGISTER_X2, rsp + sign_extend(imm, 10));
    } else {
        imm = (bit_cut(instruction, 2, 5)  << 12) |
              (bit_cut(instruction, 12, 1) << 17);

        rvjit_li(rds, sign_extend(imm, 18), 2);

        riscv_write_register(vm, rds, sign_extend(imm, 18));
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
#ifdef RV64
        uint8_t shamt = bit_cut(instruction, 2, 5) |
                       (bit_cut(instruction, 12, 1) << 5);
#else
        uint8_t shamt = bit_cut(instruction, 2, 5);
#endif
        rvjit_srli(rds, rds, shamt, 2);
        riscv_write_register(vm, rds, reg1 >> shamt);
    } else if (opc == 1) {
        // c.srai
#ifdef RV64
        uint8_t shamt = bit_cut(instruction, 2, 5) |
                       (bit_cut(instruction, 12, 1) << 5);
#else
        uint8_t shamt = bit_cut(instruction, 2, 5);
#endif
        rvjit_srai(rds, rds, shamt, 2);
        riscv_write_register(vm, rds, ((sxlen_t)reg1) >> shamt);
    } else if (opc == 2) {
        // c.andi
        sxlen_t imm = sign_extend((bit_cut(instruction, 12, 1) << 5) |
                                  bit_cut(instruction, 2, 5), 6);
        rvjit_andi(rds, rds, imm, 2);
        riscv_write_register(vm, rds, reg1 & imm);
    } else {
        opc = bit_cut(instruction, 5, 2);
        regid_t rs2 = riscv_c_reg(bit_cut(instruction, 2, 3));
        xlen_t reg2 = riscv_read_register(vm, rs2);
#ifdef RV64
        if (!bit_check(instruction, 12)) {
#endif
            if (opc == 0) {
                // c.sub
                rvjit_sub(rds, rds, rs2, 2);
                riscv_write_register(vm, rds, reg1 - reg2);
            } else if (opc == 1) {
                // c.xor
                rvjit_xor(rds, rds, rs2, 2);
                riscv_write_register(vm, rds, reg1 ^ reg2);
            } else if (opc == 2) {
                // c.or
                rvjit_or(rds, rds, rs2, 2);
                riscv_write_register(vm, rds, reg1 | reg2);
            } else {
                // c.and
                rvjit_and(rds, rds, rs2, 2);
                riscv_write_register(vm, rds, reg1 & reg2);
            }
#ifdef RV64
        } else {
            if (opc == 0) {
                // c.subw
                rvjit_subw(rds, rds, rs2, 2);
                vm->registers[rds] = (int32_t)(reg1 - reg2);
            } else if (opc == 1) {
                // c.addw
                rvjit_addw(rds, rds, rs2, 2);
                vm->registers[rds] = (int32_t)(reg1 + reg2);
            } else  {
                riscv_trap(vm, TRAP_ILL_INSTR, instruction);
            }
        }
#endif
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
                rvjit_add(rds, rds, rs2, 2);
                riscv_write_register(vm, rds, reg1 + reg2);
            } else {
                // c.jalr
                xlen_t reg1 = riscv_read_register(vm, rds);
                xlen_t pc = riscv_read_register(vm, REGISTER_PC);

                rvjit_jalr(REGISTER_X1, rds, 0, 2);

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
            rvjit_addi(rds, rs2, 0, 2);
            riscv_write_register(vm, rds, reg2);
        } else {
            // c.jr
            xlen_t reg1 = riscv_read_register(vm, rds);

            rvjit_jalr(REGISTER_ZERO, rds, 0, 2);

            riscv_write_register(vm, REGISTER_PC, reg1 - 2);
        }
    }
}

static void riscv_c_j(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Jump to PC+offset
    xlen_t pc = riscv_read_register(vm, REGISTER_PC);
    sxlen_t offset = decode_jal_imm(bit_cut(instruction, 2, 11));

    rvjit_jal(REGISTER_ZERO, offset, 2);

    riscv_write_register(vm, REGISTER_PC, pc + offset - 2);
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
    
    rvjit_sw(rs2, rs1, offset, 2);

    riscv_store_u32(vm, addr, rs2);
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
    sxlen_t offset = decode_branch_imm(instruction);
    if (reg1 == 0) {
        xlen_t pc = riscv_read_register(vm, REGISTER_PC);

        rvjit_beq(rds, REGISTER_ZERO, offset, 2, 2);

        riscv_write_register(vm, REGISTER_PC, pc + offset - 2);
    } else {
        rvjit_bne(rds, REGISTER_ZERO, 2, offset, 2);
    }
}

static void riscv_c_swsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write 32-bit integer rs2 to address sp+offset
    regid_t rs2 = bit_cut(instruction, 2, 5);
    uint32_t offset = (bit_cut(instruction, 9, 4)  << 2) |
                      (bit_cut(instruction, 7, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    
    rvjit_sw(rs2, REGISTER_X2, offset, 2);

    riscv_store_u32(vm, addr, rs2);
}

static void riscv_c_bnez(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Conditional jump if rds != 0
    regid_t rds = riscv_c_reg(bit_cut(instruction, 7, 3));
    xlen_t reg1 = riscv_read_register(vm, rds);
    sxlen_t offset = decode_branch_imm(instruction);
    if (reg1 != 0) {
        xlen_t pc = riscv_read_register(vm, REGISTER_PC);

        rvjit_bne(rds, REGISTER_ZERO, offset, 2, 2);

        riscv_write_register(vm, REGISTER_PC, pc + offset - 2);
    } else {
        rvjit_beq(rds, REGISTER_ZERO, 2, offset, 2);
    }
}

#ifdef RV64

static void riscv64c_ld(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read 64-bit integer from address rs1+offset to rds
    regid_t rds = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 10, 3) << 3) |
                      (bit_cut(instruction, 5, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    
    rvjit_ld(rds, rs1, offset, 2);

    riscv_load_u64(vm, addr, rds);
}

static void riscv64c_ldsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read 64-bit integer from address sp+offset to rds
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t offset = (bit_cut(instruction, 5, 2)  << 3) |
                      (bit_cut(instruction, 12, 1) << 5) |
                      (bit_cut(instruction, 2, 3)  << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    
    rvjit_ld(rds, REGISTER_X2, offset, 2);

    riscv_load_u64(vm, addr, rds);
}

static void riscv64c_sd(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write 64-bit integer rs2 to address rs1+offset
    regid_t rs2 = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 10, 3) << 3) |
                      (bit_cut(instruction, 5, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    
    rvjit_sd(rs2, rs1, offset, 2);

    riscv_store_u64(vm, addr, rs2);
}

static void riscv64c_sdsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write 64-bit integer rs2 to address sp+offset
    regid_t rs2 = bit_cut(instruction, 2, 5);
    uint32_t offset = (bit_cut(instruction, 10, 3) << 3) |
                      (bit_cut(instruction, 7, 3)  << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    
    rvjit_sd(rs2, REGISTER_X2, offset, 2);

    riscv_store_u64(vm, addr, rs2);
}

static void riscv64c_addiw(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Add 6-bit signed immediate to rds (this also serves as NOP for X0 reg)
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t src_reg = riscv_read_register(vm, rds);
    uint32_t imm = sign_extend(bit_cut(instruction, 2, 5) |
                              (bit_cut(instruction, 12, 1) << 5), 6);

    rvjit_addiw(rds, rds, imm, 2);

    vm->registers[rds] = (int32_t)(src_reg + imm);
}

#endif

void riscv_c_init(rvvm_hart_t* vm)
{
    riscv_install_opcode_C(vm, RVC_ADDI4SPN, riscv_c_addi4spn);
    riscv_install_opcode_C(vm, RVC_ADDI, riscv_c_addi);
    riscv_install_opcode_C(vm, RVC_SLLI, riscv_c_slli);
#ifndef RV64
    riscv_install_opcode_C(vm, RVC_JAL, riscv_c_jal);
#endif
    riscv_install_opcode_C(vm, RVC_LW, riscv_c_lw);
    riscv_install_opcode_C(vm, RVC_LI, riscv_c_li);
    riscv_install_opcode_C(vm, RVC_LWSP, riscv_c_lwsp);
    riscv_install_opcode_C(vm, RVC_ADDI16SP_LUI, riscv_c_addi16sp_lui);

    // Those are tricky fuckers and need additional decoding
    // basically a glue instruction for CR and CA instructions
    riscv_install_opcode_C(vm, RVC_ALOPS1, riscv_c_alops1);
    riscv_install_opcode_C(vm, RVC_ALOPS2, riscv_c_alops2);

    riscv_install_opcode_C(vm, RVC_J, riscv_c_j);
    riscv_install_opcode_C(vm, RVC_SW, riscv_c_sw);
    riscv_install_opcode_C(vm, RVC_BEQZ, riscv_c_beqz);
    riscv_install_opcode_C(vm, RVC_SWSP, riscv_c_swsp);
    riscv_install_opcode_C(vm, RVC_BNEZ, riscv_c_bnez);
#ifdef RV64
    riscv_install_opcode_C(vm, RV64C_SD, riscv64c_sd);
    riscv_install_opcode_C(vm, RV64C_LD, riscv64c_ld);
    riscv_install_opcode_C(vm, RV64C_SDSP, riscv64c_sdsp);
    riscv_install_opcode_C(vm, RV64C_LDSP, riscv64c_ldsp);
    riscv_install_opcode_C(vm, RV64C_ADDIW, riscv64c_addiw);
#else
    // Clear RV64C-only instructions from decoder
    // c.addiw is jal in rv32
    // Expect that FPU is initialized later
    riscv_install_opcode_C(vm, RV64C_SD, riscv_c_illegal_insn);
    riscv_install_opcode_C(vm, RV64C_LD, riscv_c_illegal_insn);
    riscv_install_opcode_C(vm, RV64C_SDSP, riscv_c_illegal_insn);
    riscv_install_opcode_C(vm, RV64C_LDSP, riscv_c_illegal_insn);
#endif
}
