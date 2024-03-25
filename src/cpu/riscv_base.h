/*
riscv_base.h - RISC-V Base Integer ISA interpreter
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.

Alternatively, the contents of this file may be used under the terms
of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or any later version.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef RISCV_BASE_H
#define RISCV_BASE_H

#include "riscv_priv.h"
#include "riscv_atomics.h"

#ifdef USE_FPU
#include "riscv_fpu.h"
#endif

// Base 5-bit opcodes in inst[6:2]
#define RISCV_OPC_LOAD     0x0
#define RISCV_OPC_LOAD_FP  0x1
#define RISCV_OPC_MISC_MEM 0x3
#define RISCV_OPC_OP_IMM   0x4
#define RISCV_OPC_AUIPC    0x5
#define RISCV_OPC_OP_IMM32 0x6
#define RISCV_OPC_STORE    0x8
#define RISCV_OPC_STORE_FP 0x9
#define RISCV_OPC_AMO      0xB
#define RISCV_OPC_OP       0xC
#define RISCV_OPC_LUI      0xD
#define RISCV_OPC_OP32     0xE
#define RISCV_OPC_FMADD    0x10
#define RISCV_OPC_FMSUB    0x11
#define RISCV_OPC_FNMSUB   0x12
#define RISCV_OPC_FNMADD   0x13
#define RISCV_OPC_OP_FP    0x14
#define RISCV_OPC_BRANCH   0x18
#define RISCV_OPC_JALR     0x19
#define RISCV_OPC_JAL      0x1B
#define RISCV_OPC_SYSTEM   0x1C

static forceinline bitcnt_t decode_i_shamt(const uint32_t insn)
{
#ifdef RV64
    return bit_cut(insn, 20, 6);
#else
    return bit_cut(insn, 20, 5);
#endif
}

// TODO optimize?
static forceinline bitcnt_t decode_i_shift_funct7(const uint32_t insn)
{
#ifdef RV64
    return (insn >> 26) << 1;
#else
    return insn >> 25;
#endif
}

static forceinline sxlen_t decode_i_branch_off(const uint32_t insn)
{
    const xlen_t imm = (bit_cut(insn, 31, 1) << 12)
                     | (bit_cut(insn, 7, 1)  << 11)
                     | (bit_cut(insn, 25, 6) << 5)
                     | (bit_cut(insn, 8, 4)  << 1);
    return sign_extend(imm, 13);
}

static forceinline sxlen_t decode_i_jal_off(const uint32_t insn)
{
    // May be replaced by translation table
    const xlen_t  imm = (bit_cut(insn, 31, 1)  << 20)
                      | (bit_cut(insn, 12, 8)  << 12)
                      | (bit_cut(insn, 20, 1)  << 11)
                      | (bit_cut(insn, 21, 10) << 1);
    return sign_extend(imm, 21);
}

static forceinline void riscv_emulate_i_opc_load(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t funct3 = bit_cut(insn, 12, 3);
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const sxlen_t offset = sign_extend(bit_cut(insn, 20, 12), 12);
    const xlen_t  addr = riscv_read_reg(vm, rs1) + offset;
    switch (funct3) {
        case 0x0: // lb
            rvjit_lb(rds, rs1, offset, 4);
            riscv_load_s8(vm, addr, rds);
            return;
        case 0x1: // lh
            rvjit_lh(rds, rs1, offset, 4);
            riscv_load_s16(vm, addr, rds);
            return;
        case 0x2: // lw
            rvjit_lw(rds, rs1, offset, 4);
            riscv_load_s32(vm, addr, rds);
            return;
#ifdef RV64
        case 0x3: // ld
            rvjit_ld(rds, rs1, offset, 4);
            riscv_load_u64(vm, addr, rds);
            return;
#endif
        case 0x4: // lbu
            rvjit_lbu(rds, rs1, offset, 4);
            riscv_load_u8(vm, addr, rds);
            return;
        case 0x5: // lhu
            rvjit_lhu(rds, rs1, offset, 4);
            riscv_load_u16(vm, addr, rds);
            return;
#ifdef RV64
        case 0x6: // lwu
            rvjit_lwu(rds, rs1, offset, 4);
            riscv_load_u32(vm, addr, rds);
            return;
#endif
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_i_opc_imm(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t funct3 = bit_cut(insn, 12, 3);
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const xlen_t imm = sign_extend(bit_cut(insn, 20, 12), 12);
    const xlen_t src = riscv_read_reg(vm, rs1);
    switch (funct3) {
        case 0x0: // addi
            rvjit_addi(rds, rs1, imm, 4);
            riscv_write_reg(vm, rds, src + imm);
            return;
        case 0x1: // slli
            if (!decode_i_shift_funct7(insn)) {
                const bitcnt_t shamt = decode_i_shamt(insn);
                rvjit_slli(rds, rs1, shamt, 4);
                riscv_write_reg(vm, rds, src << shamt);
                return;
            }
            break;
        case 0x2: // slti
            rvjit_slti(rds, rs1, imm, 4);
            riscv_write_reg(vm, rds, (((sxlen_t)src) < ((sxlen_t)imm)) ? 1 : 0);
            return;
        case 0x3: // sltiu
            rvjit_sltiu(rds, rs1, imm, 4);
            riscv_write_reg(vm, rds, (src < imm) ? 1 : 0);
            return;
        case 0x4: // xori
            rvjit_xori(rds, rs1, imm, 4);
            riscv_write_reg(vm, rds, src ^ imm);
            return;
        case 0x5:
            switch (decode_i_shift_funct7(insn)) {
                case 0x0: { // srli
                    const bitcnt_t shamt = decode_i_shamt(insn);
                    rvjit_srli(rds, rs1, shamt, 4);
                    riscv_write_reg(vm, rds, src >> shamt);
                    return;
                }
                case 0x20: { // srai
                    const bitcnt_t shamt = decode_i_shamt(insn);
                    rvjit_srai(rds, rs1, shamt, 4);
                    riscv_write_reg(vm, rds, ((sxlen_t)src) >> shamt);
                    return;
                }
            }
            break;
        case 0x6: // ori
            rvjit_ori(rds, rs1, imm, 4);
            riscv_write_reg(vm, rds, src | imm);
            return;
        case 0x7: // andi
            rvjit_andi(rds, rs1, imm, 4);
            riscv_write_reg(vm, rds, src & imm);
            return;
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_i_auipc(rvvm_hart_t* vm, const uint32_t insn)
{
    const regid_t rds = bit_cut(insn, 7, 5);
    const xlen_t imm = sign_extend(insn & 0xFFFFF000, 32);
    const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);

    rvjit_auipc(rds, imm, 4);
    riscv_write_reg(vm, rds, pc + imm);
}

#ifdef RV64

static forceinline void riscv_emulate_i_opc_imm32(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t funct3 = bit_cut(insn, 12, 3);
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const uint32_t src = riscv_read_reg(vm, rs1);
    switch (funct3) {
        case 0x0: { // addiw
            const uint32_t imm = sign_extend(bit_cut(insn, 20, 12), 12);
            rvjit_addiw(rds, rs1, imm, 4);
            vm->registers[rds] = (int32_t)(src + imm);
            return;
        }
        case 0x1:
            if (!(insn >> 25)) { // slliw
                const bitcnt_t shamt = bit_cut(insn, 20, 5);
                rvjit_slliw(rds, rs1, shamt, 4);
                riscv_write_reg(vm, rds, (int32_t)(src << shamt));
                return;
            }
            break;
        case 0x5:
            switch (insn >> 25) {
                case 0x0: { // srli
                    const bitcnt_t shamt = bit_cut(insn, 20, 5);
                    rvjit_srliw(rds, rs1, shamt, 4);
                    riscv_write_reg(vm, rds, (int32_t)(src >> shamt));
                    return;
                }
                case 0x20: { // srai
                    const bitcnt_t shamt = bit_cut(insn, 20, 5);
                    rvjit_sraiw(rds, rs1, shamt, 4);
                    riscv_write_reg(vm, rds, ((int32_t)src) >> shamt);
                    return;
                }
            }
            break;
    }
    riscv_illegal_insn(vm, insn);
}

#endif

static forceinline void riscv_emulate_i_opc_store(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t funct3 = bit_cut(insn, 12, 3);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const regid_t rs2 = bit_cut(insn, 20, 5);
    const sxlen_t offset = sign_extend(bit_cut(insn, 7, 5) | (bit_cut(insn, 25, 7) << 5), 12);
    const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
    switch (funct3) {
        case 0x0: { // sb
            rvjit_sb(rs2, rs1, offset, 4);
            riscv_store_u8(vm, addr, rs2);
            return;
        }
        case 0x1: { // sh
            rvjit_sh(rs2, rs1, offset, 4);
            riscv_store_u16(vm, addr, rs2);
            return;
        }
        case 0x2: { // sw
            rvjit_sw(rs2, rs1, offset, 4);
            riscv_store_u32(vm, addr, rs2);
            return;
        }
#ifdef RV64
        case 0x3: { // sd
            rvjit_sd(rs2, rs1, offset, 4);
            riscv_store_u64(vm, addr, rs2);
            return;
        }
#endif
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_i_opc_op(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t funct3 = bit_cut(insn, 12, 3);
    const uint32_t funct7 = insn >> 25;
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const regid_t rs2 = bit_cut(insn, 20, 5);
    const xlen_t reg1 = riscv_read_reg(vm, rs1);
    const xlen_t reg2 = riscv_read_reg(vm, rs2);
    switch (funct3) {
        case 0x0:
            switch (funct7) {
                case 0x0: // add
                    rvjit_add(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, reg1 + reg2);
                    return;
                case 0x20: // sub
                    rvjit_sub(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, reg1 - reg2);
                    return;
                case 0x1: // mul
                    rvjit_mul(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, reg1 * reg2);
                    return;
            }
            break;
        case 0x1:
            switch (funct7) {
                case 0x0: // sll
                    rvjit_sll(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, reg1 << (reg2 & bit_mask(SHAMT_BITS)));
                    return;
                case 0x1: // mulh
                    rvjit_mulh(rds, rs1, rs2, 4);
#ifdef RV64
                    riscv_write_reg(vm, rds, mulh_uint64(reg1, reg2));
#else
                    riscv_write_reg(vm, rds, ((int64_t)(sxlen_t)reg1 * (int64_t)(sxlen_t)reg2) >> 32);
#endif
                    return;
            }
            break;
        case 0x2:
            switch (funct7) {
                case 0x0: // slt
                    rvjit_slt(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, (((sxlen_t)reg1) < ((sxlen_t)reg2)) ? 1 : 0);
                    return;
                case 0x1: // mulhsu
                    rvjit_mulhsu(rds, rs1, rs2, 4);
#ifdef RV64
                    riscv_write_reg(vm, rds, mulhsu_uint64(reg1, reg2));
#else
                    riscv_write_reg(vm, rds, ((int64_t)(sxlen_t)reg1 * (uint64_t)reg2) >> 32);
#endif
                    return;
                case 0x10: // sh1add (Zba)
                    riscv_write_reg(vm, rds, reg2 + (reg1 << 1));
                    return;
            }
            break;
        case 0x3:
            switch (funct7) {
                case 0x0: // sltu
                    rvjit_sltu(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, (reg1 < reg2) ? 1 : 0);
                    return;
                case 0x1: // mulhu
                    rvjit_mulhu(rds, rs1, rs2, 4);
#ifdef RV64
                    riscv_write_reg(vm, rds, mulhu_uint64(reg1, reg2));
#else
                    riscv_write_reg(vm, rds, ((uint64_t)reg1 * (uint64_t)reg2) >> 32);
#endif
                    return;
            }
            break;
        case 0x4:
            switch (funct7) {
                case 0x0: // xor
                    rvjit_xor(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, reg1 ^ reg2);
                    return;
                case 0x1: { // div
                    sxlen_t result = -1;
                    rvjit_div(rds, rs1, rs2, 4);
                    if ((sxlen_t)reg1 == DIV_OVERFLOW_RS1 && (sxlen_t)reg2 == -1) {
                        // overflow
                        result = DIV_OVERFLOW_RS1;
                    } else if (reg2 != 0) {
                        // division by zero check (we already setup result var for error)
                        result = (sxlen_t)reg1 / (sxlen_t)reg2;
                    }
                    riscv_write_reg(vm, rds, result);
                    return;
                }
                case 0x10: // sh2add (Zba)
                    riscv_write_reg(vm, rds, reg2 + (reg1 << 2));
                    return;
            }
            break;
        case 0x5:
            switch (funct7) {
                case 0x0: // srl
                    rvjit_srl(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, reg1 >> (reg2 & bit_mask(SHAMT_BITS)));
                    return;
                case 0x20: // sra
                    rvjit_sra(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, ((sxlen_t)reg1) >> (reg2 & bit_mask(SHAMT_BITS)));
                    return;
                case 0x1: { // divu
                    xlen_t result = (sxlen_t)-1;
                    rvjit_divu(rds, rs1, rs2, 4);
                    // division by zero check (we already setup result var for error)
                    if (reg2 != 0) {
                        result = reg1 / reg2;
                    }
                    riscv_write_reg(vm, rds, result);
                    return;
                }
            }
            break;
        case 0x6:
            switch (funct7) {
                case 0x0: // or
                    rvjit_or(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, reg1 | reg2);
                    return;
                case 0x1: { // rem
                    sxlen_t result = reg1;
                    rvjit_rem(rds, rs1, rs2, 4);
                    // overflow
                    if ((sxlen_t)reg1 == DIV_OVERFLOW_RS1 && (sxlen_t)reg2 == -1) {
                        result = 0;
                    // division by zero check (we already setup result var for error)
                    } else if (reg2 != 0) {
                        result = (sxlen_t)reg1 % (sxlen_t)reg2;
                    }
                    riscv_write_reg(vm, rds, result);
                    return;
                }
                case 0x10: // sh3add (Zba)
                    riscv_write_reg(vm, rds, reg2 + (reg1 << 3));
                    return;
            }
            break;
        case 0x7:
            switch (funct7) {
                case 0x0: // and
                    rvjit_and(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, reg1 & reg2);
                    return;
                case 0x1: { // remu
                    xlen_t result = reg1;
                    rvjit_remu(rds, rs1, rs2, 4);
                    // division by zero check (we already setup result var for error)
                    if (reg2 != 0) {
                        result = reg1 % reg2;
                    }
                    riscv_write_reg(vm, rds, result);
                    return;
                }
            }
            break;
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_i_lui(rvvm_hart_t* vm, const uint32_t insn)
{
    const regid_t rds = bit_cut(insn, 7, 5);
    const xlen_t imm = sign_extend(insn & 0xFFFFF000, 32);

    rvjit_li(rds, imm, 4);
    riscv_write_reg(vm, rds, imm);
}

#ifdef RV64

static forceinline void riscv_emulate_i_opc_op32(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t funct3 = bit_cut(insn, 12, 3);
    const uint32_t funct7 = insn >> 25;
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const regid_t rs2 = bit_cut(insn, 20, 5);
    const uint32_t reg1 = riscv_read_reg(vm, rs1);
    const uint32_t reg2 = riscv_read_reg(vm, rs2);
    switch (funct3) {
        case 0x0:
            switch (funct7) {
                case 0x0: // addw
                    rvjit_addw(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, (int32_t)(reg1 + reg2));
                    return;
                case 0x20: // subw
                    rvjit_subw(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, (int32_t)(reg1 - reg2));
                    return;
                case 0x1: // mulw
                    rvjit_mul(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, (int32_t)(reg1 * reg2));
                    return;
                case 0x4: // add.uw (Zba)
                    riscv_write_reg(vm, rds, riscv_read_reg(vm, rs2) + ((uint64_t)reg1));
                    return;
            }
            break;
        case 0x1:
            switch (funct7) {
                case 0x0: // sllw
                    rvjit_sllw(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, (int32_t)(reg1 << (reg2 & 0x1F)));
                    return;
            }
            break;
        case 0x2:
            switch (funct7) {
                case 0x10: // sh1add.uw (Zba)
                    riscv_write_reg(vm, rds, riscv_read_reg(vm, rs2) + (((uint64_t)reg1) << 1));
                    return;
            }
            break;
        case 0x4:
            switch (funct7) {
                case 0x1: { // divw
                    int32_t result = -1;
                    rvjit_divw(rds, rs1, rs2, 4);
                    // overflow
                    if ((int32_t)reg1 == ((int32_t)0x80000000U) && (int32_t)reg2 == -1) {
                        result = ((int32_t)0x80000000U);
                    // division by zero check (we already setup result var for error)
                    } else if (reg2 != 0) {
                        result = (int32_t)reg1 / (int32_t)reg2;
                    }
                    riscv_write_reg(vm, rds, result);
                    return;
                }
                case 0x10: // sh2add.uw (Zba)
                    riscv_write_reg(vm, rds, riscv_read_reg(vm, rs2) + (((uint64_t)reg1) << 2));
                    return;
            }
            break;
        case 0x5:
            switch (funct7) {
                case 0x0: // srlw
                    rvjit_srlw(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, (int32_t)(reg1 >> (reg2 & 0x1F)));
                    return;
                case 0x20: // sraw
                    rvjit_sraw(rds, rs1, rs2, 4);
                    riscv_write_reg(vm, rds, (int32_t)(((int32_t)reg1) >> (reg2 & 0x1F)));
                    return;
                case 0x1: { // divuw
                    uint32_t result = -1;
                    rvjit_divuw(rds, rs1, rs2, 4);
                    // overflow
                    if (reg2 != 0) {
                        result = reg1 / reg2;
                    }
                    riscv_write_reg(vm, rds, (int32_t)result);
                    return;
                }
            }
            break;
        case 0x6:
            switch (funct7) {
                case 0x1: { // remw
                    int32_t result = reg1;
                    rvjit_remw(rds, rs1, rs2, 4);
                    // overflow
                    if ((int32_t)reg1 == ((int32_t)0x80000000U) && (int32_t)reg2 == -1) {
                        result = 0;
                    // division by zero check (we already setup result var for error)
                    } else if (reg2 != 0) {
                        result = (int32_t)reg1 % (int32_t)reg2;
                    }
                    riscv_write_reg(vm, rds, result);
                    return;
                }
                case 0x10: // sh3add.uw (Zba)
                    riscv_write_reg(vm, rds, riscv_read_reg(vm, rs2) + (((uint64_t)reg1) << 3));
                    return;
            }
            break;
        case 0x7:
            switch (funct7) {
                case 0x1: { // remuw
                    uint32_t result = reg1;
                    rvjit_remuw(rds, rs1, rs2, 4);
                    // division by zero check (we already setup result var for error)
                    if (reg2 != 0) {
                        result = reg1 % reg2;
                    }
                    riscv_write_reg(vm, rds, (int32_t)result);
                    return;
                }
            }
            break;
    }
    riscv_illegal_insn(vm, insn);
}

#endif

static forceinline void riscv_emulate_i_opc_branch(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t funct3 = bit_cut(insn, 12, 3);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const regid_t rs2 = bit_cut(insn, 20, 5);
    const sxlen_t offset = decode_i_branch_off(insn);
    switch (funct3) {
        case 0x0: // beq
            if (riscv_read_reg(vm, rs1) == riscv_read_reg(vm, rs2)) {
                const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);
                rvjit_beq(rs1, rs2, offset, 4, 4);
                riscv_write_reg(vm, REGISTER_PC, pc + offset - 4);
            } else {
                rvjit_bne(rs1, rs2, 4, offset, 4);
            }
            return;
        case 0x1: // bne
            if (riscv_read_reg(vm, rs1) != riscv_read_reg(vm, rs2)) {
                const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);
                rvjit_bne(rs1, rs2, offset, 4, 4);
                riscv_write_reg(vm, REGISTER_PC, pc + offset - 4);
            } else {
                rvjit_beq(rs1, rs2, 4, offset, 4);
            }
            return;
        case 0x4: // blt
            if (riscv_read_reg_s(vm, rs1) < riscv_read_reg_s(vm, rs2)) {
                const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);
                rvjit_blt(rs1, rs2, offset, 4, 4);
                riscv_write_reg(vm, REGISTER_PC, pc + offset - 4);
            } else {
                rvjit_bge(rs1, rs2, 4, offset, 4);
            }
            return;
        case 0x5: // bge
            if (riscv_read_reg_s(vm, rs1) >= riscv_read_reg_s(vm, rs2)) {
                const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);
                rvjit_bge(rs1, rs2, offset, 4, 4);
                riscv_write_reg(vm, REGISTER_PC, pc + offset - 4);
            } else {
                rvjit_blt(rs1, rs2, 4, offset, 4);
            }
            return;
        case 0x6: // bltu
            if (riscv_read_reg(vm, rs1) < riscv_read_reg(vm, rs2)) {
                const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);
                rvjit_bltu(rs1, rs2, offset, 4, 4);
                riscv_write_reg(vm, REGISTER_PC, pc + offset - 4);
            } else {
                rvjit_bgeu(rs1, rs2, 4, offset, 4);
            }
            return;
        case 0x7: // bgeu
            if (riscv_read_reg(vm, rs1) >= riscv_read_reg(vm, rs2)) {
                const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);
                rvjit_bgeu(rs1, rs2, offset, 4, 4);
                riscv_write_reg(vm, REGISTER_PC, pc + offset - 4);
            } else {
                rvjit_bltu(rs1, rs2, 4, offset, 4);
            }
            return;
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_i_jalr(rvvm_hart_t* vm, const uint32_t insn)
{
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const sxlen_t offset = sign_extend(bit_cut(insn, 20, 12), 12);
    const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);
    const xlen_t jmp_addr = riscv_read_reg(vm, rs1);

    rvjit_jalr(rds, rs1, offset, 4);
    riscv_write_reg(vm, rds, pc + 4);
    riscv_write_reg(vm, REGISTER_PC, ((jmp_addr + offset)&(~(xlen_t)1)) - 4);
}

static forceinline void riscv_emulate_i_jal(rvvm_hart_t* vm, const uint32_t insn)
{
    const regid_t rds = bit_cut(insn, 7, 5);
    const sxlen_t offset = decode_i_jal_off(insn);
    const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);

    rvjit_jal(rds, offset, 4);
    riscv_write_reg(vm, rds, pc + 4);
    riscv_write_reg(vm, REGISTER_PC, pc + offset - 4);
}

static forceinline void riscv_emulate_i(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t op = bit_cut(insn, 2, 5);
    switch (op) {
        case RISCV_OPC_LOAD:
            riscv_emulate_i_opc_load(vm, insn);
            return;
#ifdef USE_FPU
        case RISCV_OPC_LOAD_FP:
            riscv_emulate_f_opc_load(vm, insn);
            return;
#endif
        case RISCV_OPC_MISC_MEM:
            riscv_emulate_opc_misc_mem(vm, insn);
            return;
        case RISCV_OPC_OP_IMM:
            riscv_emulate_i_opc_imm(vm, insn);
            return;
        case RISCV_OPC_AUIPC:
            riscv_emulate_i_auipc(vm, insn);
            return;
#ifdef RV64
        case RISCV_OPC_OP_IMM32:
            riscv_emulate_i_opc_imm32(vm, insn);
            return;
#endif
        case RISCV_OPC_STORE:
            riscv_emulate_i_opc_store(vm, insn);
            return;
#ifdef USE_FPU
        case RISCV_OPC_STORE_FP:
            riscv_emulate_f_opc_store(vm, insn);
            return;
#endif
        case RISCV_OPC_AMO:
            riscv_emulate_a_opc_amo(vm, insn);
            return;
        case RISCV_OPC_OP:
            riscv_emulate_i_opc_op(vm, insn);
            return;
        case RISCV_OPC_LUI:
            riscv_emulate_i_lui(vm, insn);
            return;
#ifdef RV64
        case RISCV_OPC_OP32:
            riscv_emulate_i_opc_op32(vm, insn);
            return;
#endif
#ifdef USE_FPU
        case RISCV_OPC_FMADD:
            riscv_emulate_f_fmadd(vm, insn);
            return;
        case RISCV_OPC_FMSUB:
            riscv_emulate_f_fmsub(vm, insn);
            return;
        case RISCV_OPC_FNMSUB:
            riscv_emulate_f_fnmsub(vm, insn);
            return;
        case RISCV_OPC_FNMADD:
            riscv_emulate_f_fnmadd(vm, insn);
            return;
        case RISCV_OPC_OP_FP:
            riscv_emulate_f_opc_op(vm, insn);
            return;
#endif
        case RISCV_OPC_BRANCH:
            riscv_emulate_i_opc_branch(vm, insn);
            return;
        case RISCV_OPC_JALR:
            riscv_emulate_i_jalr(vm, insn);
            return;
        case RISCV_OPC_JAL:
            riscv_emulate_i_jal(vm, insn);
            return;
        case RISCV_OPC_SYSTEM:
            riscv_emulate_opc_system(vm, insn);
            return;
    }
    riscv_illegal_insn(vm, insn);
}

#endif
