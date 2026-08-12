/*
riscv_compressed.h - RISC-V Compressed ISA interpreter
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef RVVM_RISCV_COMPRESSED_H
#define RVVM_RISCV_COMPRESSED_H

// Provides entry point to riscv_emulate_i()
#include "riscv_base.h"

static forceinline uint32_t decode_c_addi4spn_imm(const uint32_t insn)
{
    return ((insn >> 1) & 0x03C0) //
         | ((insn >> 2) & 0x0008) //
         | ((insn >> 4) & 0x0004) //
         | ((insn >> 7) & 0x0030);
}

static forceinline sxlen_t decode_c_jal_imm(const uint32_t insn)
{
    const uint32_t imm = ((insn >> 1) & 0x0B40) //
                       | ((insn >> 2) & 0x000E) //
                       | ((insn >> 7) & 0x0010) //
                       | ((insn << 1) & 0x0080) //
                       | ((insn << 2) & 0x0400) //
                       | ((insn << 3) & 0x0020);
    return sign_extend(imm, 12);
}

static forceinline uint32_t decode_c_ld_off(const uint32_t insn)
{
    return ((insn << 1) & 0xC0) //
         | ((insn >> 7) & 0x38);
}

static forceinline uint32_t decode_c_lw_off(const uint32_t insn)
{
    return ((insn << 1) & 0x40) //
         | ((insn >> 4) & 0x04) //
         | ((insn >> 7) & 0x38);
}

static forceinline uint32_t decode_c_ldsp_off(const uint32_t insn)
{
    return ((insn >> 2) & 0x0018) //
         | ((insn >> 7) & 0x0020) //
         | ((insn << 4) & 0x01C0);
}

static forceinline uint32_t decode_c_lwsp_off(const uint32_t insn)
{
    return ((insn >> 2) & 0x1C) //
         | ((insn >> 7) & 0x20) //
         | ((insn << 4) & 0xC0);
}

static forceinline uint32_t decode_c_sdsp_off(const uint32_t insn)
{
    return ((insn >> 1) & 0x01C0) //
         | ((insn >> 7) & 0x0038);
}

static forceinline uint32_t decode_c_swsp_off(const uint32_t insn)
{
    return ((insn >> 1) & 0xC0) //
         | ((insn >> 7) & 0x3C);
}

static forceinline int32_t decode_c_alu_imm(const uint32_t insn)
{
    const int32_t imm = ((insn << 25) >> 1) //
                      | ((insn >> 12) << 31);
    return imm >> 26;
}

static forceinline sxlen_t decode_c_addi16sp_off(const uint32_t insn)
{
    const uint32_t imm = ((insn >> 2) & 0x0010) //
                       | ((insn << 3) & 0x0020) //
                       | ((insn << 1) & 0x0040) //
                       | ((insn << 4) & 0x0180) //
                       | ((insn >> 3) & 0x0200);
    return sign_extend(imm, 10);
}

static forceinline int32_t decode_c_lui_imm(const uint32_t insn)
{
    const int32_t imm = ((insn & 0x7C) << 24) //
                      | ((insn >> 12) << 31);
    return imm >> 14;
}

static forceinline sxlen_t decode_c_branch_imm(const uint32_t insn)
{
    const uint32_t imm = ((insn >> 2) & 0x0006) //
                       | ((insn >> 7) & 0x0018) //
                       | ((insn << 3) & 0x0020) //
                       | ((insn << 1) & 0x00C0) //
                       | ((insn >> 4) & 0x0100);
    return sign_extend(imm, 9);
}

static forceinline uint32_t decode_c_shamt(const uint32_t insn)
{
#if defined(RISCV64)
    return bit_ext_u32(insn, 2, 5) | (bit_ext_u32(insn, 12, 1) << 5);
#else
    return bit_ext_u32(insn, 2, 5);
#endif
}

static forceinline void riscv_emulate_c_c0(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t rds = bit_ext_u32(insn, 2, 3) + 8;
    const size_t rs1 = bit_ext_u32(insn, 7, 3) + 8;

    switch (bit_ext_u32(insn, 13, 3)) {
        case 0x00: { // c.addi4spn
            const xlen_t imm = decode_c_addi4spn_imm(insn);
            if (likely(imm)) {
                rvjit_trace_addi(rds, RISCV_REG_X2, imm, 2);
                riscv_write_reg(vm, rds, riscv_read_reg(vm, RISCV_REG_X2) + imm);
                return;
            }
            break;
        }
#if defined(USE_FPU)
        case 0x01: // c.fld
            if (likely(riscv_fpu_is_enabled(vm))) {
                const xlen_t off = decode_c_ld_off(insn);
                riscv_load_double(vm, riscv_read_reg(vm, rs1) + off, rds);
                return;
            }
            break;
#endif
        case 0x02: { // c.lw
            const xlen_t off = decode_c_lw_off(insn);
            rvjit_trace_lw(rds, rs1, off, 2);
            riscv_load_s32(vm, riscv_read_reg(vm, rs1) + off, rds);
            return;
        }
#if defined(USE_FPU) && !defined(RISCV64)
        case 0x03: // c.flw (RV32)
            if (likely(riscv_fpu_is_enabled(vm))) {
                const xlen_t off = decode_c_lw_off(insn);
                riscv_load_float(vm, riscv_read_reg(vm, rs1) + off, rds);
                return;
            }
            break;
#endif
#if defined(RISCV64)
        case 0x03: { // c.ld (RV64)
            const xlen_t off = decode_c_ld_off(insn);
            rvjit_trace_ld(rds, rs1, off, 2);
            riscv_load_u64(vm, riscv_read_reg(vm, rs1) + off, rds);
            return;
        }
#endif
        case 0x04: // Zcb
            switch (bit_ext_u32(insn, 10, 3)) {
                case 0x00: { // c.lbu (Zcb)
                    const xlen_t off = ((insn & 0x20) >> 4) | ((insn & 0x40) >> 6);
                    rvjit_trace_lbu(rds, rs1, off, 2);
                    riscv_load_u8(vm, riscv_read_reg(vm, rs1) + off, rds);
                    return;
                }
                case 0x01: {
                    const xlen_t off = (insn & 0x20) >> 4;
                    if (insn & 0x40) { // c.lh (Zcb)
                        rvjit_trace_lh(rds, rs1, off, 2);
                        riscv_load_s16(vm, riscv_read_reg(vm, rs1) + off, rds);
                    } else { // c.lhu (Zcb)
                        rvjit_trace_lhu(rds, rs1, off, 2);
                        riscv_load_u16(vm, riscv_read_reg(vm, rs1) + off, rds);
                    }
                    return;
                }
                case 0x02: { // c.sb (Zcb)
                    const xlen_t off = ((insn & 0x20) >> 4) | ((insn & 0x40) >> 6);
                    rvjit_trace_sb(rds, rs1, off, 2);
                    riscv_store_u8(vm, riscv_read_reg(vm, rs1) + off, rds);
                    return;
                }
                case 0x03:
                    if (!(insn & 0x40)) { // c.sh (Zcb)
                        const xlen_t off = (insn & 0x20) >> 4;
                        rvjit_trace_sh(rds, rs1, off, 2);
                        riscv_store_u16(vm, riscv_read_reg(vm, rs1) + off, rds);
                        return;
                    }
                    break;
            }
            break;
#if defined(USE_FPU)
        case 0x05: // c.fsd
            if (likely(riscv_fpu_is_enabled(vm))) {
                const xlen_t off = decode_c_ld_off(insn);
                riscv_store_double(vm, riscv_read_reg(vm, rs1) + off, rds);
                return;
            }
            break;
#endif
        case 0x06: { // c.sw
            const xlen_t off = decode_c_lw_off(insn);
            rvjit_trace_sw(rds, rs1, off, 2);
            riscv_store_u32(vm, riscv_read_reg(vm, rs1) + off, rds);
            return;
        }
#if defined(USE_FPU) && !defined(RISCV64)
        case 0x07: // c.fsw (RV32)
            if (likely(riscv_fpu_is_enabled(vm))) {
                const xlen_t off = decode_c_lw_off(insn);
                riscv_store_float(vm, riscv_read_reg(vm, rs1) + off, rds);
                return;
            }
            break;
#endif
#if defined(RISCV64)
        case 0x07: { // c.sd (RV64)
            const xlen_t off = decode_c_ld_off(insn);
            rvjit_trace_sd(rds, rs1, off, 2);
            riscv_store_u64(vm, riscv_read_reg(vm, rs1) + off, rds);
            return;
        }
#endif
    }

    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_c_c1(rvvm_hart_t* vm, const uint32_t insn)
{
    switch (bit_ext_u32(insn, 13, 3)) {
        case 0x00: { // c.addi
            const size_t  rds = bit_ext_u32(insn, 7, 5);
            const int32_t imm = decode_c_alu_imm(insn);
            rvjit_trace_addi(rds, rds, imm, 2);
            riscv_write_reg(vm, rds, riscv_read_reg(vm, rds) + imm);
            return;
        }
        case 0x01: { // c.jal (RV32), c.addiw (RV64)
#if defined(RISCV64)
            const size_t rds = bit_ext_u32(insn, 7, 5);
            if (likely(rds)) {
                const int32_t imm = decode_c_alu_imm(insn);
                rvjit_trace_addiw(rds, rds, imm, 2);
                riscv_write_reg(vm, rds, (int32_t)(riscv_read_reg(vm, rds) + imm));
                return;
            }
            break;
#else
            const xlen_t  pc  = riscv_read_reg(vm, RISCV_REG_PC);
            const int32_t off = decode_c_jal_imm(insn);
            rvjit_trace_jal(RISCV_REG_X1, off, 2);
            riscv_write_reg(vm, RISCV_REG_X1, pc + 2);
            riscv_write_reg(vm, RISCV_REG_PC, pc + off - 2);
            riscv_voluntary_preempt(vm);
            return;
#endif
        }
        case 0x02: { // c.li
            const size_t  rds = bit_ext_u32(insn, 7, 5);
            const int32_t imm = decode_c_alu_imm(insn);
            rvjit_trace_li(rds, imm, 2);
            riscv_write_reg(vm, rds, imm);
            return;
        }
        case 0x03: {
            const size_t rds = bit_ext_u32(insn, 7, 5);
            if (likely(insn & 0x107C)) {   // imm != 0
                if (rds == RISCV_REG_X2) { // c.addi16sp (rds == X2)
                    const int32_t imm = decode_c_addi16sp_off(insn);
                    rvjit_trace_addi(RISCV_REG_X2, RISCV_REG_X2, imm, 2);
                    riscv_write_reg(vm, RISCV_REG_X2, riscv_read_reg(vm, RISCV_REG_X2) + imm);
                } else { // c.lui
                    const int32_t imm = decode_c_lui_imm(insn);
                    rvjit_trace_li(rds, imm, 2);
                    riscv_write_reg(vm, rds, imm);
                }
                return;
            } else if ((rds & 0x01) && !(rds & 0x10)) { // c.mop.{1,15}
                return;
            }
            break;
        }
        case 0x05: { // c.j
            const int32_t off = decode_c_jal_imm(insn);
            rvjit_trace_jal(RISCV_REG_ZERO, off, 2);
            riscv_write_reg(vm, RISCV_REG_PC, riscv_read_reg(vm, RISCV_REG_PC) + off - 2);
            riscv_voluntary_preempt(vm);
            return;
        }
        case 0x06: { // c.beqz
            const size_t  rs1 = bit_ext_u32(insn, 7, 3) + 8;
            const int32_t off = decode_c_branch_imm(insn);
            if (likely(riscv_read_reg(vm, rs1))) {
                rvjit_trace_bne(rs1, RISCV_REG_ZERO, 2, off, 2);
            } else {
                rvjit_trace_beq(rs1, RISCV_REG_ZERO, off, 2, 2);
                riscv_write_reg(vm, RISCV_REG_PC, riscv_read_reg(vm, RISCV_REG_PC) + off - 2);
                riscv_voluntary_preempt(vm);
            }
            return;
        }
        case 0x07: { // c.bnez
            const size_t  rs1 = bit_ext_u32(insn, 7, 3) + 8;
            const int32_t off = decode_c_branch_imm(insn);
            if (likely(!riscv_read_reg(vm, rs1))) {
                rvjit_trace_beq(rs1, RISCV_REG_ZERO, 2, off, 2);
            } else {
                rvjit_trace_bne(rs1, RISCV_REG_ZERO, off, 2, 2);
                riscv_write_reg(vm, RISCV_REG_PC, riscv_read_reg(vm, RISCV_REG_PC) + off - 2);
                riscv_voluntary_preempt(vm);
            }
            return;
        }
    }

    const size_t rds  = bit_ext_u32(insn, 7, 3) + 8;
    const xlen_t reg1 = riscv_read_reg(vm, rds);

    switch (bit_ext_u32(insn, 10, 2)) {
        case 0x00: { // c.srli
#if !defined(RISCV64)
            if (unlikely(insn & 0x1000)) { // shamt[5] is reserved in RV32
                riscv_illegal_insn(vm, insn);
                return;
            }
#endif
            const bitcnt_t shamt = decode_c_shamt(insn);
            rvjit_trace_srli(rds, rds, shamt, 2);
            riscv_write_reg(vm, rds, reg1 >> shamt);
            return;
        }
        case 0x01: { // c.srai
#if !defined(RISCV64)
            if (unlikely(insn & 0x1000)) { // shamt[5] is reserved in RV32
                riscv_illegal_insn(vm, insn);
                return;
            }
#endif
            const bitcnt_t shamt = decode_c_shamt(insn);
            rvjit_trace_srai(rds, rds, shamt, 2);
            riscv_write_reg(vm, rds, ((sxlen_t)reg1) >> shamt);
            return;
        }
        case 0x02: { // c.andi
            const sxlen_t imm = decode_c_alu_imm(insn);
            rvjit_trace_andi(rds, rds, imm, 2);
            riscv_write_reg(vm, rds, reg1 & imm);
            return;
        }
    }

    const size_t rs2  = bit_ext_u32(insn, 2, 3) + 8;
    const xlen_t reg2 = riscv_read_reg(vm, rs2);
    switch (insn & 0x1C60) {
        case 0x0C00: // c.sub
            rvjit_trace_sub(rds, rds, rs2, 2);
            riscv_write_reg(vm, rds, reg1 - reg2);
            return;
        case 0x0C20: // c.xor
            rvjit_trace_xor(rds, rds, rs2, 2);
            riscv_write_reg(vm, rds, reg1 ^ reg2);
            return;
        case 0x0C40: // c.or
            rvjit_trace_or(rds, rds, rs2, 2);
            riscv_write_reg(vm, rds, reg1 | reg2);
            return;
        case 0x0C60: // c.and
            rvjit_trace_and(rds, rds, rs2, 2);
            riscv_write_reg(vm, rds, reg1 & reg2);
            return;
#if defined(RISCV64)
        case 0x1C00: // c.subw
            rvjit_trace_subw(rds, rds, rs2, 2);
            riscv_write_reg(vm, rds, (int32_t)(reg1 - reg2));
            return;
        case 0x1C20: // c.addw
            rvjit_trace_addw(rds, rds, rs2, 2);
            riscv_write_reg(vm, rds, (int32_t)(reg1 + reg2));
            return;
#endif
        case 0x1C40: // c.mul (Zcb + Zmmul)
            rvjit_trace_mul(rds, rds, rs2, 2);
            riscv_write_reg(vm, rds, reg1 * reg2);
            return;
        case 0x1C60:
            switch (bit_ext_u32(insn, 2, 3)) {
                case 0x00: // c.zext.b (Zcb)
                    rvjit_trace_andi(rds, rds, 0xFF, 2);
                    riscv_write_reg(vm, rds, (uint8_t)reg1);
                    return;
                case 0x01: // c.sext.b (Zcb + Zbb)
                    rvjit_trace_sext_b(rds, rds, 2);
                    riscv_write_reg(vm, rds, (int8_t)reg1);
                    return;
                case 0x02: // c.zext.h (Zcb + Zbb)
                    rvjit_trace_andi(rds, rds, 0xFFFF, 2);
                    riscv_write_reg(vm, rds, (uint16_t)reg1);
                    return;
                case 0x03: // c.sext.h (Zcb + Zbb)
                    rvjit_trace_sext_h(rds, rds, 2);
                    riscv_write_reg(vm, rds, (int16_t)reg1);
                    return;
#if defined(RISCV64)
                case 0x04: // c.zext.w (Zcb + Zba), RV64 only
                    rvjit_trace_shadd_uw(rds, rds, REGISTER_ZERO, 0, 2);
                    riscv_write_reg(vm, rds, (uint32_t)reg1);
                    return;
#endif
                case 0x05: // c.not (Zcb)
                    rvjit_trace_xori(rds, rds, -1, 2);
                    riscv_write_reg(vm, rds, ~reg1);
                    return;
            }
            break;
    }

    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_c_c2(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t rds = bit_ext_u32(insn, 7, 5);
    const size_t rs2 = bit_ext_u32(insn, 2, 5);

    switch (bit_ext_u32(insn, 12, 4)) {
        case 0x00:
        case 0x01: { // c.slli
#if !defined(RISCV64)
            if (unlikely(insn & 0x1000)) { // shamt[5] is reserved in RV32
                riscv_illegal_insn(vm, insn);
                return;
            }
#endif
            const uint32_t shamt = decode_c_shamt(insn);
            rvjit_trace_slli(rds, rds, shamt, 2);
            riscv_write_reg(vm, rds, riscv_read_reg(vm, rds) << shamt);
            return;
        }
#if defined(USE_FPU)
        case 0x02:
        case 0x03: // c.fldsp
            if (likely(riscv_fpu_is_enabled(vm))) {
                const xlen_t off = decode_c_ldsp_off(insn);
                riscv_load_double(vm, riscv_read_reg(vm, RISCV_REG_X2) + off, rds);
                return;
            }
            break;
#endif
        case 0x04:
        case 0x05: // c.lwsp
            if (likely(rds)) {
                const xlen_t off = decode_c_lwsp_off(insn);
                rvjit_trace_lw(rds, RISCV_REG_X2, off, 2);
                riscv_load_s32(vm, riscv_read_reg(vm, RISCV_REG_X2) + off, rds);
                return;
            }
            break;
#if defined(USE_FPU) && !defined(RISCV64)
        case 0x06:
        case 0x07: // c.flwsp (RV32)
            if (likely(riscv_fpu_is_enabled(vm))) {
                const xlen_t off = decode_c_lwsp_off(insn);
                riscv_load_float(vm, riscv_read_reg(vm, RISCV_REG_X2) + off, rds);
                return;
            }
            break;
#endif
#if defined(RISCV64)
        case 0x06:
        case 0x07: // c.ldsp (RV64)
            if (likely(rds)) {
                const xlen_t off = decode_c_ldsp_off(insn);
                rvjit_trace_ld(rds, RISCV_REG_X2, off, 2);
                riscv_load_u64(vm, riscv_read_reg(vm, RISCV_REG_X2) + off, rds);
                return;
            }
            break;
#endif
        case 0x08:
            if (rs2) { // c.mv
                rvjit_trace_addi(rds, rs2, 0, 2);
                riscv_write_reg(vm, rds, riscv_read_reg(vm, rs2));
                return;
            } else if (likely(rds)) { // c.jr
                rvjit_trace_jalr(RISCV_REG_ZERO, rds, 0, 2);
                riscv_write_reg(vm, RISCV_REG_PC,
                                (riscv_read_reg(vm, rds) & ~(xlen_t) 1) - 2);
                riscv_voluntary_preempt(vm);
                return;
            }
            break;
        case 0x09:
            if (likely(rs2)) { // c.add
                rvjit_trace_add(rds, rds, rs2, 2);
                riscv_write_reg(vm, rds, riscv_read_reg(vm, rds) + riscv_read_reg(vm, rs2));
            } else if (likely(rds)) { // c.jalr
                rvjit_trace_jalr(RISCV_REG_X1, rds, 0, 2);
                riscv_write_reg(vm, RISCV_REG_X1, riscv_read_reg(vm, RISCV_REG_PC) + 2);
                riscv_write_reg(vm, RISCV_REG_PC,
                                (riscv_read_reg(vm, rds) & ~(xlen_t) 1) - 2);
                riscv_voluntary_preempt(vm);
            } else { // c.ebreak
                riscv_breakpoint(vm);
            }
            return;
#if defined(USE_FPU)
        case 0x0A:
        case 0x0B: // c.fsdsp
            if (likely(riscv_fpu_is_enabled(vm))) {
                const xlen_t off = decode_c_sdsp_off(insn);
                riscv_store_double(vm, riscv_read_reg(vm, RISCV_REG_X2) + off, rs2);
                return;
            }
            break;
#endif
        case 0x0C:
        case 0x0D: { // c.swsp
            const xlen_t off = decode_c_swsp_off(insn);
            rvjit_trace_sw(rs2, RISCV_REG_X2, off, 2);
            riscv_store_u32(vm, riscv_read_reg(vm, RISCV_REG_X2) + off, rs2);
            return;
        }
#if defined(USE_FPU) && !defined(RISCV64)
        case 0x0E:
        case 0x0F: // c.fswsp (RV32)
            if (likely(riscv_fpu_is_enabled(vm))) {
                const xlen_t off = decode_c_lwsp_off(insn);
                riscv_store_float(vm, riscv_read_reg(vm, RISCV_REG_X2) + off, rs2);
                return;
            }
            break;
#endif
#if defined(RISCV64)
        case 0x0E:
        case 0x0F: { // c.sdsp (RV64)
            const xlen_t off = decode_c_sdsp_off(insn);
            rvjit_trace_sd(rs2, RISCV_REG_X2, off, 2);
            riscv_store_u64(vm, riscv_read_reg(vm, RISCV_REG_X2) + off, rs2);
            return;
        }
#endif
    }

    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_insn(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t op = insn & 0x03;
    switch (op) {
        case 0x00:
            riscv_emulate_c_c0(vm, insn);
            vm->registers[RISCV_REG_PC] += 2;
            return;
        case 0x01:
            riscv_emulate_c_c1(vm, insn);
            vm->registers[RISCV_REG_PC] += 2;
            return;
        case 0x02:
            riscv_emulate_c_c2(vm, insn);
            vm->registers[RISCV_REG_PC] += 2;
            return;
        case 0x03:
            riscv_emulate_i(vm, insn);
            vm->registers[RISCV_REG_PC] += 4;
            return;
    }
}

#endif
