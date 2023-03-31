/*
rvjit_riscv.h - RVJIT RISC-V Backend
Copyright (C) 2021  LekKit <github.com/LekKit>

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

#include "rvjit.h"
#include "mem_ops.h"
#include "bit_ops.h"
#include "compiler.h"
#include "utils.h"

#ifndef RVJIT_RISCV_H
#define RVJIT_RISCV_H

#define RISCV_REG_ZERO 0x0
#define RISCV_REG_RA   0x1
#define RISCV_REG_SP   0x2
#define RISCV_REG_A0   0xA

#ifdef RVJIT_ABI_SYSV
#define VM_PTR_REG RISCV_REG_A0
#endif

static inline size_t rvjit_native_default_hregmask()
{
    // t0-t6, a0-a7 are caller-saved
    // a0 is preserved as vmptr
    return rvjit_hreg_mask(5)  |
           rvjit_hreg_mask(6)  |
           rvjit_hreg_mask(7)  |
           rvjit_hreg_mask(11) |
           rvjit_hreg_mask(12) |
           rvjit_hreg_mask(13) |
           rvjit_hreg_mask(14) |
           rvjit_hreg_mask(15) |
           rvjit_hreg_mask(16) |
           rvjit_hreg_mask(17) |
           rvjit_hreg_mask(28) |
           rvjit_hreg_mask(29) |
           rvjit_hreg_mask(30) |
           rvjit_hreg_mask(31);
}

static inline size_t rvjit_native_abireclaim_hregmask()
{
    // We have enough caller-saved registers, no need for push/pop as well
    return 0;
}

static inline bool rvjit_is_valid_imm(int32_t imm)
{
    return (((int32_t)(((uint32_t)imm) << 20)) >> 20) == imm;
}

static inline void rvjit_riscv_20imm_op(rvjit_block_t* block, uint32_t opcode, regid_t reg, int32_t imm)
{
    uint8_t code[4];
    write_uint32_le_m(code, opcode | (reg << 7) | (imm & 0xFFFFF000));
    rvjit_put_code(block, code, 4);
}

// Load [31:12] bits of the register from 20-bit imm, signextend & zero lower bits
static inline void rvjit_riscv_lui(rvjit_block_t* block, regid_t reg, int32_t imm)
{
    rvjit_riscv_20imm_op(block, 0x37, reg, imm);
}

// Load PC + [31:12] imm to register
static inline void rvjit_riscv_auipc(rvjit_block_t* block, regid_t reg, int32_t imm)
{
    rvjit_riscv_20imm_op(block, 0x17, reg, imm);
}

#define RISCV_R_ADD   0x00000033
#define RISCV_R_SUB   0x40000033
#define RISCV_R_XOR   0x00004033
#define RISCV_R_OR    0x00006033
#define RISCV_R_AND   0x00007033
#define RISCV_R_SLL   0x00001033
#define RISCV_R_SRL   0x00005033
#define RISCV_R_SRA   0x40005033
#define RISCV_R_SLT   0x00002033
#define RISCV_R_SLTU  0x00003033
#define RISCV_R_MUL   0x02000033
#define RISCV_R_MULH  0x02001033
#define RISCV_R_MULHS 0x02002033
#define RISCV_R_MULHU 0x02003033
#define RISCV_R_DIV   0x02004033
#define RISCV_R_DIVU  0x02005033
#define RISCV_R_REM   0x02006033
#define RISCV_R_REMU  0x02007033

#define RISCV_R_ADDW  0x0000003B
#define RISCV_R_SUBW  0x4000003B
#define RISCV_R_SLLW  0x0000103B
#define RISCV_R_SRLW  0x0000503B
#define RISCV_R_SRAW  0x4000503B
#define RISCV_R_MULW  0x0200003B
#define RISCV_R_DIVW  0x0200403B
#define RISCV_R_DIVUW 0x0200503B
#define RISCV_R_REMW  0x0200603B
#define RISCV_R_REMUW 0x0200703B

#ifdef RVJIT_NATIVE_64BIT
#define RISCV32_R_ADD  RISCV_R_ADDW
#define RISCV32_R_SUB  RISCV_R_SUBW
#define RISCV32_R_SLL  RISCV_R_SLLW
#define RISCV32_R_SRL  RISCV_R_SRLW
#define RISCV32_R_SRA  RISCV_R_SRAW
#define RISCV32_R_MUL  RISCV_R_MULW
#define RISCV32_R_DIV  RISCV_R_DIVW
#define RISCV32_R_DIVU RISCV_R_DIVUW
#define RISCV32_R_REM  RISCV_R_REMW
#define RISCV32_R_REMU RISCV_R_REMUW
#else
#define RISCV32_R_ADD  RISCV_R_ADD
#define RISCV32_R_SUB  RISCV_R_SUB
#define RISCV32_R_SLL  RISCV_R_SLL
#define RISCV32_R_SRL  RISCV_R_SRL
#define RISCV32_R_SRA  RISCV_R_SRA
#define RISCV32_R_MUL  RISCV_R_MUL
#define RISCV32_R_DIV  RISCV_R_DIV
#define RISCV32_R_DIVU RISCV_R_DIVU
#define RISCV32_R_REM  RISCV_R_REM
#define RISCV32_R_REMU RISCV_R_REMU
#endif

// R-type operation
static inline void rvjit_riscv_r_op(rvjit_block_t* block, uint32_t opcode, regid_t rds, regid_t rs1, regid_t rs2)
{
    uint8_t code[4];
    write_uint32_le_m(code, opcode | (rds << 7) | (rs1 << 15) | (rs2 << 20));
    rvjit_put_code(block, code, 4);
}

#define RISCV_I_ADDI  0x00000013
#define RISCV_I_XORI  0x00004013
#define RISCV_I_ORI   0x00006013
#define RISCV_I_ANDI  0x00007013
#define RISCV_I_SLLI  0x00001013
#define RISCV_I_SRLI  0x00005013
#define RISCV_I_SRAI  0x40005013
#define RISCV_I_SLTI  0x00002013
#define RISCV_I_SLTIU 0x00003013

#define RISCV_I_ADDIW 0x0000001B
#define RISCV_I_SLLIW 0x0000101B
#define RISCV_I_SRLIW 0x0000501B
#define RISCV_I_SRAIW 0x4000501B

#ifdef RVJIT_NATIVE_64BIT
#define RISCV32_I_ADDI RISCV_I_ADDIW
#define RISCV32_I_SLLI RISCV_I_SLLIW
#define RISCV32_I_SRLI RISCV_I_SRLIW
#define RISCV32_I_SRAI RISCV_I_SRAIW
#else
#define RISCV32_I_ADDI RISCV_I_ADDI
#define RISCV32_I_SLLI RISCV_I_SLLI
#define RISCV32_I_SRLI RISCV_I_SRLI
#define RISCV32_I_SRAI RISCV_I_SRAI
#endif

#define RISCV_I_JALR  0x00000067

// Loads encoded as I-type (rs is addr, imm is offset)
#define RISCV_I_LB    0x00000003
#define RISCV_I_LH    0x00001003
#define RISCV_I_LW    0x00002003
#define RISCV_I_LD    0x00003003
#define RISCV_I_LBU   0x00004003
#define RISCV_I_LHU   0x00005003
#define RISCV_I_LWU   0x00006003

// I-type operation (sign-extended 12-bit immediate)
static inline void rvjit_riscv_i_op_internal(rvjit_block_t* block, uint32_t opcode, regid_t rds, regid_t rs, uint32_t imm)
{
    uint8_t code[4];
    write_uint32_le_m(code, opcode | (rds << 7) | (rs << 15) | (((uint32_t)imm) << 20));
    rvjit_put_code(block, code, 4);
}

// Set native register reg to sign-extended 32-bit imm
static inline void rvjit_native_setreg32s(rvjit_block_t* block, regid_t reg, int32_t imm)
{
    if (!rvjit_is_valid_imm(imm)) {
        // Upper 20 bits aren't sign-extension
        if (imm & 0x800) {
            // Lower 12-bit part will sign-extend and subtract 0x1000 from LUI value
            imm += 0x1000;
        }
        rvjit_riscv_lui(block, reg, imm);
        if ((imm & 0xFFF) != 0) {
            rvjit_riscv_i_op_internal(block, RISCV32_I_ADDI, reg, reg, imm & 0xFFF);
        }
    } else {
        rvjit_riscv_i_op_internal(block, RISCV_I_ADDI, reg, RISCV_REG_ZERO, imm & 0xFFF);
    }
}

// Set native register reg to zero-extended 32-bit imm
static inline void rvjit_native_setreg32(rvjit_block_t* block, regid_t reg, uint32_t imm)
{
    rvjit_native_setreg32s(block, reg, imm);
}

// Convert I-type opcodes to R-type opcodes
static inline uint32_t riscv_i_to_r_op(uint32_t opcode)
{
    // wow big brain isa designers moment
    return opcode | 0x20;
}

static inline bool riscv_is_load_op(uint32_t opcode)
{
    return (opcode & 0xFF) == 0x03;
}

// I-type operation (sign-extended 32-bit immediate)
static inline void rvjit_riscv_i_op(rvjit_block_t* block, uint32_t opcode, regid_t rds, regid_t rs, int32_t imm)
{
    if (likely(rvjit_is_valid_imm(imm))) {
        rvjit_riscv_i_op_internal(block, opcode, rds, rs, imm);
    } else if (!riscv_is_load_op(opcode)) {
        // Immediate doesn't fit in a single instruction
        if ((opcode == RISCV_I_ADDI || opcode == RISCV_I_ADDIW) && rvjit_is_valid_imm(imm >> 1)) {
            // Lower to 2 consequent addi
            rvjit_riscv_i_op_internal(block, opcode, rds, rs, imm >> 1);
            rvjit_riscv_i_op_internal(block, opcode, rds, rds, imm - (imm >> 1));
        } else {
            // Reclaim register, load 32-bit imm, use in R-type op
            regid_t rtmp = rvjit_claim_hreg(block);
            rvjit_native_setreg32s(block, rtmp, imm);
            rvjit_riscv_r_op(block, riscv_i_to_r_op(opcode), rds, rs, rtmp);
            rvjit_free_hreg(block, rtmp);
        }
    } else {
        int32_t imm_lo = sign_extend(imm, 12);
        regid_t rtmp = rvjit_claim_hreg(block);
        rvjit_riscv_lui(block, rtmp, imm - imm_lo);
        rvjit_riscv_r_op(block, RISCV_R_ADD, rtmp, rtmp, rs);
        rvjit_riscv_i_op_internal(block, opcode, rds, rtmp, imm_lo);
        rvjit_free_hreg(block, rtmp);
    }
}

#define RISCV_S_SB    0x00000023
#define RISCV_S_SH    0x00001023
#define RISCV_S_SW    0x00002023
#define RISCV_S_SD    0x00003023

static inline void rvjit_riscv_s_op_internal(rvjit_block_t* block, uint32_t opcode, regid_t reg, regid_t addr, int32_t offset)
{
    uint8_t code[4];
    write_uint32_le_m(code, opcode | ((offset & 0x1F) << 7) | (addr << 15)
                                   | (reg << 20) | (((uint32_t)offset >> 5) << 25));
    rvjit_put_code(block, code, 4);
}

// Store op (sign-extended 12-bit offset)
static inline void rvjit_riscv_s_op(rvjit_block_t* block, uint32_t opcode, regid_t reg, regid_t addr, int32_t offset)
{
    if (likely(rvjit_is_valid_imm(offset))) {
        rvjit_riscv_s_op_internal(block, opcode, reg, addr, offset);
    } else {
        int32_t imm_lo = sign_extend(offset, 12);
        regid_t rtmp = rvjit_claim_hreg(block);
        rvjit_riscv_lui(block, rtmp, offset - imm_lo);
        rvjit_riscv_r_op(block, RISCV_R_ADD, rtmp, rtmp, addr);
        rvjit_riscv_s_op_internal(block, opcode, reg, rtmp, imm_lo);
        rvjit_free_hreg(block, rtmp);
    }
}

#define RISCV_B_BEQ   0x00000063
#define RISCV_B_BNE   0x00001063
#define RISCV_B_BLT   0x00004063
#define RISCV_B_BGE   0x00005063
#define RISCV_B_BLTU  0x00006063
#define RISCV_B_BGEU  0x00007063

// Branch op (sign-extended 12-bit offset * 2)
static inline void rvjit_riscv_b_op(rvjit_block_t* block, uint32_t opcode, regid_t rs1, regid_t rs2, int32_t offset)
{
    uint8_t code[4];
    if (unlikely(!rvjit_is_valid_imm(offset >> 1))) rvvm_fatal("Illegal branch offset in RVJIT!");
    write_uint32_le_m(code, opcode | (rs1 << 15) | (rs2 << 20) | ((offset & 0x1E) << 7) | ((offset & 0x800) >> 4)
                                   | (((uint32_t)offset & 0x7E0) << 20) | (((uint32_t)offset & 0x1000) << 19));
    rvjit_put_code(block, code, 4);
}

// Relative jump, stores return address to reg (sign-extended 21-bit offset)
static inline void rvjit_riscv_jal(rvjit_block_t* block, regid_t reg, int32_t offset)
{
    uint8_t code[4];
    write_uint32_le_m(code, 0x6F | ((uint32_t)reg << 7)
                                 | (((uint32_t)(offset >> 1) & 0x3FF) << 21)
                                 | (((uint32_t)(offset >> 11) & 0x1)  << 20)
                                 | (((uint32_t)(offset >> 12) & 0xFF) << 12)
                                 | (((uint32_t)(offset >> 20) & 0x1)  << 31));
    rvjit_put_code(block, code, 4);
}

// Patching used for forward branches
static inline void rvjit_riscv_branch_patch(void* addr, int32_t offset)
{
    uint32_t opcode = read_uint32_le_m(addr) & 0x1FFF07F;
    if (unlikely(!rvjit_is_valid_imm(offset >> 1))) rvvm_fatal("Illegal branch patch offset in RVJIT!");
    write_uint32_le_m(addr, opcode | (((uint32_t)offset & 0x1E) << 7) | ((offset & 0x800) >> 4)
                                   | (((uint32_t)offset & 0x7E0) << 20) | (((uint32_t)offset & 0x1000) << 19));
}

static inline void rvjit_riscv_jal_patch(void* addr, int32_t offset)
{
    uint32_t opcode = 0x6F | (read_uint32_le_m(addr) & 0xFFF);
    write_uint32_le_m(addr, opcode | (((uint32_t)(offset >> 1) & 0x3FF) << 21)
                                   | (((uint32_t)(offset >> 11) & 0x1)  << 20)
                                   | (((uint32_t)(offset >> 12) & 0xFF) << 12)
                                   | (((uint32_t)(offset >> 20) & 0x1)  << 31));
}

#ifdef RVJIT_NATIVE_64BIT
#define RISCV_S_SIZET RISCV_S_SD
#define RISCV_L_SIZET RISCV_I_LD
#else
#define RISCV_S_SIZET RISCV_S_SW
#define RISCV_L_SIZET RISCV_I_LW
#endif

/*
 * Basic functionality
 */
static inline void rvjit_native_zero_reg(rvjit_block_t* block, regid_t reg)
{
    rvjit_riscv_i_op(block, RISCV_I_ADDI, reg, RISCV_REG_ZERO, 0);
}

static inline void rvjit_native_ret(rvjit_block_t* block)
{
    // May use compressed instruction (jr ra)
    rvjit_riscv_i_op(block, RISCV_I_JALR, RISCV_REG_ZERO, RISCV_REG_RA, 0);
}

static inline void rvjit_native_push(rvjit_block_t* block, regid_t reg)
{
    UNUSED(block);
    UNUSED(reg);
    rvvm_fatal("Unimplemented rvjit_native_push for RISC-V backend");
}

static inline void rvjit_native_pop(rvjit_block_t* block, regid_t reg)
{
    UNUSED(block);
    UNUSED(reg);
    rvvm_fatal("Unimplemented rvjit_native_pop for RISC-V backend");
}

// Set native register reg to wide imm
static inline void rvjit_native_setregw(rvjit_block_t* block, regid_t reg, uintptr_t imm)
{
#ifdef RVJIT_NATIVE_64BIT
    if (imm >> 32) {
        regid_t tmp = rvjit_claim_hreg(block);
        rvjit_native_setreg32(block, tmp, imm >> 32);
        rvjit_riscv_i_op(block, RISCV_I_SLLI, tmp, tmp, 32);
        rvjit_native_setreg32(block, reg, (imm << 32) >> 32);
        rvjit_riscv_r_op(block, RISCV_R_OR, reg, reg, tmp);
        rvjit_free_hreg(block, tmp);
    } else {
        rvjit_native_setreg32(block, reg, imm);
    }
#else
    rvjit_native_setreg32(block, reg, imm);
#endif
}

// Call a function pointed to by native register
static inline void rvjit_native_callreg(rvjit_block_t* block, regid_t reg)
{
    rvjit_riscv_i_op(block, RISCV_I_ADDI, RISCV_REG_SP, RISCV_REG_SP, -16);
    rvjit_riscv_s_op(block, RISCV_S_SIZET, RISCV_REG_RA, RISCV_REG_SP, 16 - sizeof(size_t));
    rvjit_riscv_i_op(block, RISCV_I_JALR, RISCV_REG_RA, reg, 0);
    rvjit_riscv_s_op(block, RISCV_L_SIZET, RISCV_REG_RA, RISCV_REG_SP, 16 - sizeof(size_t));
    rvjit_riscv_i_op(block, RISCV_I_ADDI, RISCV_REG_SP, RISCV_REG_SP, 16);
}

static inline branch_t rvjit_native_jmp(rvjit_block_t* block, branch_t handle, bool target)
{
    if (target) {
        // This is a jump label
        if (handle == BRANCH_NEW) {
            // Backward jump: Save label address
            return block->size;
        } else {
            // Forward jump: Patch jump offset
            rvjit_riscv_jal_patch(block->code + handle, block->size - handle);
            return BRANCH_NEW;
        }
    } else {
        // This is a jump entry
        if (handle == BRANCH_NEW) {
            // Forward jump: Emit instruction, patch it later
            branch_t tmp = block->size;
            rvjit_riscv_jal(block, RISCV_REG_ZERO, 0);
            return tmp;
        } else {
            // Backward jump: Emit instruction using label address
            rvjit_riscv_jal(block, RISCV_REG_ZERO, handle - block->size);
            return BRANCH_NEW;
        }
    }
}

static branch_t rvjit_riscv_branch_entry(rvjit_block_t* block, uint32_t opcode, regid_t hrs1, regid_t hrs2, branch_t handle)
{
    if (handle == BRANCH_NEW) {
        branch_t tmp = block->size;
        rvjit_riscv_b_op(block, opcode, hrs1, hrs2, 0);
        return tmp;
    } else {
        rvjit_riscv_b_op(block, opcode, hrs1, hrs2, handle - block->size);
        return BRANCH_NEW;
    }
}

static branch_t rvjit_riscv_branch_target(rvjit_block_t* block, branch_t handle)
{
    if (handle == BRANCH_NEW) {
        return block->size;
    } else {
        // Patch jump offset
        rvjit_riscv_branch_patch(block->code + handle, block->size - handle);
        return BRANCH_NEW;
    }
}

static inline branch_t rvjit_riscv_branch(rvjit_block_t* block, uint32_t opcode, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (target) {
        return rvjit_riscv_branch_target(block, handle);
    } else {
        return rvjit_riscv_branch_entry(block, opcode, hrs1, hrs2, handle);
    }
}

/*
 * Linker routines
 */

static inline bool rvjit_is_valid_jal_imm(int32_t imm)
{
    return (((int32_t)(((uint32_t)imm) << 11)) >> 11) == imm;
}

// Emit jump instruction (may return false if offset cannot be encoded)
static inline bool rvjit_tail_jmp(rvjit_block_t* block, int32_t offset)
{
    if (rvjit_is_valid_jal_imm(offset)) {
        rvjit_riscv_jal(block, RISCV_REG_ZERO, offset);
    } else {
        regid_t tmp = rvjit_claim_hreg(block);
        rvjit_riscv_auipc(block, tmp, offset + ((offset & 0x800) << 1));
        rvjit_riscv_i_op_internal(block, RISCV_I_JALR, RISCV_REG_ZERO, tmp, offset & 0xFFF);
        rvjit_free_hreg(block, tmp);
    }
    return true;
}

// Emit patchable ret instruction
static inline void rvjit_patchable_ret(rvjit_block_t* block)
{
    // Always 4-bytes, same as JAL
    rvjit_riscv_i_op(block, RISCV_I_JALR, RISCV_REG_ZERO, RISCV_REG_RA, 0);
}

// Jump if word pointed to by addr is nonzero (may emit nothing if the offset cannot be encoded)
// Used to check interrupts in block linkage
static inline void rvjit_tail_bnez(rvjit_block_t* block, regid_t addr, int32_t offset)
{
    size_t offset_fixup = block->size;
    int32_t off;
    regid_t tmp = rvjit_claim_hreg(block);
    rvjit_riscv_i_op(block, RISCV_I_LW, tmp, addr, 0);

    off = offset - (block->size - offset_fixup);
    if (rvjit_is_valid_imm(off >> 1)) {
        // Offset fits into branch instruction
        rvjit_riscv_b_op(block, RISCV_B_BNE, RISCV_REG_ZERO, tmp, off);
    } else {
        // Use jal for 21-bit offset or auipc + jalr for full 32-bit offset
        branch_t l1 = rvjit_riscv_branch(block, RISCV_B_BEQ, RISCV_REG_ZERO, tmp, BRANCH_NEW, false);
        off = offset - (block->size - offset_fixup);
        if (rvjit_is_valid_jal_imm(off)) {
            rvjit_riscv_jal(block, RISCV_REG_ZERO, off);
        } else {
            rvjit_riscv_auipc(block, tmp, off + ((off & 0x800) << 1));
            rvjit_riscv_i_op_internal(block, RISCV_I_JALR, RISCV_REG_ZERO, tmp, off & 0xFFF);
        }
        rvjit_riscv_branch(block, RISCV_B_BEQ, RISCV_REG_ZERO, tmp, l1, true);
    }

    rvjit_free_hreg(block, tmp);
}

// Patch instruction at addr into ret
static inline void rvjit_patch_ret(void* addr)
{
    write_uint32_le_m(addr, 0x00008067);
}

// Patch jump instruction at addr (may return false if offset cannot be encoded)
static inline bool rvjit_patch_jmp(void* addr, int32_t offset)
{
    if (rvjit_is_valid_jal_imm(offset)) {
        write_uint32_le_m(addr, 0);
        rvjit_riscv_jal_patch(addr, offset);
        return true;
    } else {
        return false;
    }
}

static inline void rvjit_jmp_reg(rvjit_block_t* block, regid_t reg)
{
    rvjit_riscv_i_op(block, RISCV_I_JALR, RISCV_REG_ZERO, reg, 0);
}

/*
 * RV32
 */
static inline void rvjit32_native_add(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV32_R_ADD, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_sub(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV32_R_SUB, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_or(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_OR, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_and(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_AND, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_xor(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_XOR, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_sra(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV32_R_SRA, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_srl(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV32_R_SRL, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_sll(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV32_R_SLL, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_addi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV32_I_ADDI, hrds, hrs1, imm);
}

static inline void rvjit32_native_ori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_ORI, hrds, hrs1, imm);
}

static inline void rvjit32_native_andi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_ANDI, hrds, hrs1, imm);
}

static inline void rvjit32_native_xori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_XORI, hrds, hrs1, imm);
}

static inline void rvjit32_native_srai(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_riscv_i_op(block, RISCV32_I_SRAI, hrds, hrs1, imm);
}

static inline void rvjit32_native_srli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_riscv_i_op(block, RISCV32_I_SRLI, hrds, hrs1, imm);
}

static inline void rvjit32_native_slli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_riscv_i_op(block, RISCV32_I_SLLI, hrds, hrs1, imm);
}

static inline void rvjit32_native_slti(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_SLTI, hrds, hrs1, imm);
}

static inline void rvjit32_native_sltiu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_SLTIU, hrds, hrs1, imm);
}

static inline void rvjit32_native_slt(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_SLT, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_sltu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_SLTU, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_lb(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_riscv_i_op(block, RISCV_I_LB, dest, addr, off);
}

static inline void rvjit32_native_lbu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_riscv_i_op(block, RISCV_I_LBU, dest, addr, off);
}

static inline void rvjit32_native_lh(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_riscv_i_op(block, RISCV_I_LH, dest, addr, off);
}

static inline void rvjit32_native_lhu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_riscv_i_op(block, RISCV_I_LHU, dest, addr, off);
}

static inline void rvjit32_native_lw(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_riscv_i_op(block, RISCV_I_LW, dest, addr, off);
}

static inline void rvjit32_native_sb(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_riscv_s_op(block, RISCV_S_SB, src, addr, off);
}

static inline void rvjit32_native_sh(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_riscv_s_op(block, RISCV_S_SH, src, addr, off);
}

static inline void rvjit32_native_sw(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_riscv_s_op(block, RISCV_S_SW, src, addr, off);
}

static inline branch_t rvjit32_native_beq(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BEQ, hrs1, hrs2, handle, target);
}

static inline branch_t rvjit32_native_bne(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BNE, hrs1, hrs2, handle, target);
}

static inline branch_t rvjit32_native_beqz(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BEQ, hrs1, RISCV_REG_ZERO, handle, target);
}

static inline branch_t rvjit32_native_bnez(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BNE, hrs1, RISCV_REG_ZERO, handle, target);
}

static inline branch_t rvjit32_native_blt(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BLT, hrs1, hrs2, handle, target);
}

static inline branch_t rvjit32_native_bge(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BGE, hrs1, hrs2, handle, target);
}

static inline branch_t rvjit32_native_bltu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BLTU, hrs1, hrs2, handle, target);
}

static inline branch_t rvjit32_native_bgeu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BGEU, hrs1, hrs2, handle, target);
}

static inline void rvjit32_native_mul(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV32_R_MUL, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_mulh(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
#ifdef RVJIT_NATIVE_64BIT
    rvjit_riscv_r_op(block, RISCV_R_MUL, hrds, hrs1, hrs2);
    rvjit_riscv_i_op(block, RISCV_I_SRAI, hrds, hrds, 32);
#else
    rvjit_riscv_r_op(block, RISCV_R_MULH, hrds, hrs1, hrs2);
#endif
}

static inline void rvjit32_native_mulhu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
#ifdef RVJIT_NATIVE_64BIT
    regid_t zrs1 = rvjit_claim_hreg(block);
    regid_t zrs2 = rvjit_claim_hreg(block);
    rvjit_riscv_i_op(block, RISCV_I_SLLI, zrs1, hrs1, 32);
    rvjit_riscv_i_op(block, RISCV_I_SLLI, zrs2, hrs2, 32);
    rvjit_riscv_r_op(block, RISCV_R_MULHU, hrds, zrs1, zrs2);
    rvjit_riscv_i_op(block, RISCV_I_SRAI, hrds, hrds, 32);
    rvjit_free_hreg(block, zrs1);
    rvjit_free_hreg(block, zrs2);
#else
    rvjit_riscv_r_op(block, RISCV_R_MULHU, hrds, hrs1, hrs2);
#endif
}

static inline void rvjit32_native_mulhsu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
#ifdef RVJIT_NATIVE_64BIT
    regid_t zrs2 = rvjit_claim_hreg(block);
    rvjit_riscv_i_op(block, RISCV_I_SLLI, zrs2, hrs2, 32);
    rvjit_riscv_i_op(block, RISCV_I_SRLI, zrs2, zrs2, 32);
    rvjit_riscv_r_op(block, RISCV_R_MUL, hrds, hrs1, zrs2);
    rvjit_riscv_i_op(block, RISCV_I_SRAI, hrds, hrds, 32);
    rvjit_free_hreg(block, zrs2);
#else
    rvjit_riscv_r_op(block, RISCV_R_MULHS, hrds, hrs1, hrs2);
#endif
}

static inline void rvjit32_native_div(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV32_R_DIV, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_divu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV32_R_DIVU, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_rem(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV32_R_REM, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_remu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV32_R_REMU, hrds, hrs1, hrs2);
}

/*
 * RV64
 */
#ifdef RVJIT_NATIVE_64BIT
static inline void rvjit64_native_add(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_ADD, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_addw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_ADDW, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_sub(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_SUB, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_subw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_SUBW, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_or(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_OR, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_and(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_AND, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_xor(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_XOR, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_sra(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_SRA, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_sraw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_SRAW, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_srl(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_SRL, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_srlw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_SRLW, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_sll(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_SLL, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_sllw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_SLLW, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_addi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_ADDI, hrds, hrs1, imm);
}

static inline void rvjit64_native_addiw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_ADDIW, hrds, hrs1, imm);
}

static inline void rvjit64_native_ori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_ORI, hrds, hrs1, imm);
}

static inline void rvjit64_native_andi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_ANDI, hrds, hrs1, imm);
}

static inline void rvjit64_native_xori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_XORI, hrds, hrs1, imm);
}

static inline void rvjit64_native_srli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_SRLI, hrds, hrs1, imm);
}

static inline void rvjit64_native_srliw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_SRLIW, hrds, hrs1, imm);
}

static inline void rvjit64_native_srai(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_SRAI, hrds, hrs1, imm);
}

static inline void rvjit64_native_sraiw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_SRAIW, hrds, hrs1, imm);
}

static inline void rvjit64_native_slli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_SLLI, hrds, hrs1, imm);
}

static inline void rvjit64_native_slliw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_SLLIW, hrds, hrs1, imm);
}

static inline void rvjit64_native_slti(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_SLTI, hrds, hrs1, imm);
}

static inline void rvjit64_native_sltiu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_riscv_i_op(block, RISCV_I_SLTIU, hrds, hrs1, imm);
}

static inline void rvjit64_native_slt(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_SLT, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_sltu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_SLTU, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_lb(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_riscv_i_op(block, RISCV_I_LB, dest, addr, off);
}

static inline void rvjit64_native_lbu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_riscv_i_op(block, RISCV_I_LBU, dest, addr, off);
}

static inline void rvjit64_native_lh(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_riscv_i_op(block, RISCV_I_LH, dest, addr, off);
}

static inline void rvjit64_native_lhu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_riscv_i_op(block, RISCV_I_LHU, dest, addr, off);
}

static inline void rvjit64_native_lw(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_riscv_i_op(block, RISCV_I_LW, dest, addr, off);
}

static inline void rvjit64_native_lwu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_riscv_i_op(block, RISCV_I_LWU, dest, addr, off);
}

static inline void rvjit64_native_ld(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_riscv_i_op(block, RISCV_I_LD, dest, addr, off);
}

static inline void rvjit64_native_sb(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_riscv_s_op(block, RISCV_S_SB, src, addr, off);
}

static inline void rvjit64_native_sh(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_riscv_s_op(block, RISCV_S_SH, src, addr, off);
}

static inline void rvjit64_native_sw(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_riscv_s_op(block, RISCV_S_SW, src, addr, off);
}

static inline void rvjit64_native_sd(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_riscv_s_op(block, RISCV_S_SD, src, addr, off);
}

static inline branch_t rvjit64_native_beq(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BEQ, hrs1, hrs2, handle, target);
}

static inline branch_t rvjit64_native_bne(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BNE, hrs1, hrs2, handle, target);
}

static inline branch_t rvjit64_native_beqz(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BEQ, hrs1, RISCV_REG_ZERO, handle, target);
}

static inline branch_t rvjit64_native_bnez(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BNE, hrs1, RISCV_REG_ZERO, handle, target);
}

static inline branch_t rvjit64_native_blt(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BLT, hrs1, hrs2, handle, target);
}

static inline branch_t rvjit64_native_bge(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BGE, hrs1, hrs2, handle, target);
}

static inline branch_t rvjit64_native_bltu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BLTU, hrs1, hrs2, handle, target);
}

static inline branch_t rvjit64_native_bgeu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_riscv_branch(block, RISCV_B_BGEU, hrs1, hrs2, handle, target);
}

static inline void rvjit64_native_mul(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_MUL, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_mulh(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_MULH, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_mulhu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_MULHU, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_mulhsu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_MULHS, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_div(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_DIV, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_divu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_DIVU, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_rem(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_REM, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_remu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_REMU, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_mulw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_MULW, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_divw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_DIVW, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_divuw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_DIVUW, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_remw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_REMW, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_remuw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_riscv_r_op(block, RISCV_R_REMUW, hrds, hrs1, hrs2);
}

#endif

#endif
