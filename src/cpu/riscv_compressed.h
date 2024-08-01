/*
riscv_compressed.h - RISC-V Compressed ISA interpreter
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

#ifndef RISCV_COMPRESSED_H
#define RISCV_COMPRESSED_H

// Provides entry point to riscv_emulate_i()
#include "riscv_base.h"

static forceinline xlen_t decode_c_addi4spn_imm(const uint16_t insn)
{
    return (bit_cut(insn, 6, 1)  << 2)
         | (bit_cut(insn, 5, 1)  << 3)
         | (bit_cut(insn, 11, 2) << 4)
         | (bit_cut(insn, 7, 4)  << 6);
}

static forceinline sxlen_t decode_c_jal_imm(const uint16_t insn)
{
    const xlen_t imm = (bit_cut(insn, 3, 3)  << 1)
                     | (bit_cut(insn, 11, 1) << 4)
                     | (bit_cut(insn, 2, 1)  << 5)
                     | (bit_cut(insn, 7, 1)  << 6)
                     | (bit_cut(insn, 6, 1)  << 7)
                     | (bit_cut(insn, 9, 2)  << 8)
                     | (bit_cut(insn, 8, 1)  << 10)
                     | (bit_cut(insn, 12, 1) << 11);
    return sign_extend(imm, 12);
}

static forceinline xlen_t decode_c_ld_off(const uint16_t insn)
{
    return (bit_cut(insn, 10, 3) << 3)
         | (bit_cut(insn, 5, 2)  << 6);
}

static forceinline xlen_t decode_c_lw_off(const uint16_t insn)
{
    return (bit_cut(insn, 6, 1)  << 2)
         | (bit_cut(insn, 10, 3) << 3)
         | (bit_cut(insn, 5, 1)  << 6);
}

static forceinline xlen_t decode_c_ldsp_off(const uint16_t insn)
{
    return (bit_cut(insn, 5, 2)  << 3)
         | (bit_cut(insn, 12, 1) << 5)
         | (bit_cut(insn, 2, 3)  << 6);
}

static forceinline xlen_t decode_c_lwsp_off(const uint16_t insn)
{
    return (bit_cut(insn, 4, 3)  << 2)
         | (bit_cut(insn, 12, 1) << 5)
         | (bit_cut(insn, 2, 2)  << 6);
}

static forceinline xlen_t decode_c_sdsp_off(const uint16_t insn)
{
    return (bit_cut(insn, 10, 3) << 3)
         | (bit_cut(insn, 7, 3)  << 6);
}

static forceinline xlen_t decode_c_swsp_off(const uint16_t insn)
{
    return (bit_cut(insn, 9, 4) << 2)
         | (bit_cut(insn, 7, 2) << 6);
}

static forceinline sxlen_t decode_c_alu_imm(const uint16_t insn)
{
    return sign_extend((bit_cut(insn, 12, 1) << 5)
                     | (bit_cut(insn, 2, 5)), 6);
}

static forceinline sxlen_t decode_c_addi16sp_off(const uint16_t insn)
{
    return sign_extend((bit_cut(insn, 6, 1)  << 4)
                     | (bit_cut(insn, 2, 1)  << 5)
                     | (bit_cut(insn, 5, 1)  << 6)
                     | (bit_cut(insn, 3, 2)  << 7)
                     | (bit_cut(insn, 12, 1) << 9), 10);
}

static forceinline sxlen_t decode_c_lui_imm(const uint16_t insn)
{
    return sign_extend((bit_cut(insn, 2, 5)  << 12)
                     | (bit_cut(insn, 12, 1) << 17), 18);
}

static forceinline sxlen_t decode_c_branch_imm(const uint16_t insn)
{
    const xlen_t imm = (bit_cut(insn, 3, 2)  << 1)
                     | (bit_cut(insn, 10, 2) << 3)
                     | (bit_cut(insn, 2, 1)  << 5)
                     | (bit_cut(insn, 5, 2)  << 6)
                     | (bit_cut(insn, 12, 1) << 8);
    return sign_extend(imm, 9);
}

static forceinline bitcnt_t decode_c_shamt(const uint16_t insn)
{
#ifdef RV64
    return bit_cut(insn, 2, 5)
        | (bit_cut(insn, 12, 1) << 5);
#else
    return bit_cut(insn, 2, 5);
#endif
}

static forceinline void riscv_emulate_c_c0(rvvm_hart_t* vm, const uint16_t insn)
{
    const regid_t rds = bit_cut(insn, 2, 3) + 8;
    const regid_t rs1 = bit_cut(insn, 7, 3) + 8;
    switch (insn >> 13) {
        case 0x0: // c.addi4spn
            if (likely(insn)) {
                const xlen_t imm = decode_c_addi4spn_imm(insn);
                const xlen_t sp = riscv_read_reg(vm, REGISTER_X2);
                rvjit_trace_addi(rds, REGISTER_X2, imm, 2);
                riscv_write_reg(vm, rds, sp + imm);
                return;
            }
            break;
#ifdef USE_FPU
        case 0x1:
            if (likely(fpu_is_enabled(vm))) { // c.fld
                const xlen_t offset = decode_c_ld_off(insn);
                const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
                riscv_load_double(vm, addr, rds);
                return;
            }
            break;
#endif
        case 0x2: { // c.lw
            const xlen_t offset = decode_c_lw_off(insn);
            const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
            rvjit_trace_lw(rds, rs1, offset, 2);
            riscv_load_s32(vm, addr, rds);
            return;
        }
#if defined(USE_FPU) && !defined(RV64)
        case 0x3:
            if (likely(fpu_is_enabled(vm))) { // c.flw (RV32)
                const xlen_t offset = decode_c_lw_off(insn);
                const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
                riscv_load_float(vm, addr, rds);
                return;
            }
            break;
#endif
#ifdef RV64
        case 0x3: { // c.ld (RV64)
            const xlen_t offset = decode_c_ld_off(insn);
            const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
            rvjit_trace_ld(rds, rs1, offset, 2);
            riscv_load_u64(vm, addr, rds);
            return;
        }
#endif
        case 0x4: // Zcb
            switch (bit_cut(insn, 10, 3)) {
                case 0x0: { // c.lbu (Zcb)
                    const xlen_t offset = ((insn & 0x20) >> 4) | ((insn & 0x40) >> 6);
                    const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
                    rvjit_trace_lbu(rds, rs1, offset, 2);
                    riscv_load_u8(vm, addr, rds);
                    return;
                }
                case 0x1: {
                    const xlen_t offset = (insn & 0x20) >> 4;
                    const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
                    if (insn & 0x40) { // c.lh (Zcb)
                        rvjit_trace_lh(rds, rs1, offset, 2);
                        riscv_load_s16(vm, addr, rds);
                    } else { // c.lhu (Zcb)
                        rvjit_trace_lhu(rds, rs1, offset, 2);
                        riscv_load_u16(vm, addr, rds);
                    }
                    return;
                }
                case 0x2: { // c.sb (Zcb)
                    const xlen_t offset = ((insn & 0x20) >> 4) | ((insn & 0x40) >> 6);
                    const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
                    rvjit_trace_sb(rds, rs1, offset, 2);
                    riscv_store_u8(vm, addr, rds);
                    return;
                }
                case 0x3:
                    if (!(insn & 0x40)) { // c.sh (Zcb)
                        const xlen_t offset = (insn & 0x20) >> 4;
                        const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
                        rvjit_trace_sh(rds, rs1, offset, 2);
                        riscv_store_u16(vm, addr, rds);
                        return;

                    }
                    break;
            }
            break;
#ifdef USE_FPU
        case 0x5:
            if (likely(fpu_is_enabled(vm))) { // c.fsd
                const xlen_t offset = decode_c_ld_off(insn);
                const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
                riscv_store_double(vm, addr, rds);
                return;
            }
            break;
#endif
        case 0x6: { // c.sw
            const xlen_t offset = decode_c_lw_off(insn);
            const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
            rvjit_trace_sw(rds, rs1, offset, 2);
            riscv_store_u32(vm, addr, rds);
            return;
        }
#if defined(USE_FPU) && !defined(RV64)
        case 0x7:
            if (likely(fpu_is_enabled(vm))) { // c.fsw (RV32)
                const xlen_t offset = decode_c_lw_off(insn);
                const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
                riscv_store_float(vm, addr, rds);
                return;
            }
            break;
#endif
#ifdef RV64
        case 0x7: { // c.sd (RV64)
            const xlen_t offset = decode_c_ld_off(insn);
            const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
            rvjit_trace_sd(rds, rs1, offset, 2);
            riscv_store_u64(vm, addr, rds);
            return;
        }
#endif
    }
    riscv_illegal_insn(vm, insn);
}


static forceinline void riscv_emulate_c_misc_alu(rvvm_hart_t* vm, const uint16_t insn)
{
    const regid_t rds = bit_cut(insn, 7, 3) + 8;
    const xlen_t reg1 = riscv_read_reg(vm, rds);
    const uint8_t funct6 = bit_cut(insn, 10, 2);

    switch (funct6) {
        case 0x0: { // c.srli
            const bitcnt_t shamt = decode_c_shamt(insn);
            rvjit_trace_srli(rds, rds, shamt, 2);
            riscv_write_reg(vm, rds, reg1 >> shamt);
            return;
        }
        case 0x1: { // c.srai
            const bitcnt_t shamt = decode_c_shamt(insn);
            rvjit_trace_srai(rds, rds, shamt, 2);
            riscv_write_reg(vm, rds, ((sxlen_t)reg1) >> shamt);
            return;
        }
        case 0x2: { // c.andi
            const sxlen_t imm = decode_c_alu_imm(insn);
            rvjit_trace_andi(rds, rds, imm, 2);
            riscv_write_reg(vm, rds, reg1 & imm);
            return;
        }
        case 0x3: {
            const uint8_t funct2 = bit_cut(insn, 5, 2);
            const regid_t rs2 = bit_cut(insn, 2, 3) + 8;
            if (!bit_check(insn, 12)) {
                const xlen_t reg2 = riscv_read_reg(vm, rs2);
                switch (funct2) {
                    case 0x0: // c.sub
                        rvjit_trace_sub(rds, rds, rs2, 2);
                        riscv_write_reg(vm, rds, reg1 - reg2);
                        return;
                    case 0x1: // c.xor
                        rvjit_trace_xor(rds, rds, rs2, 2);
                        riscv_write_reg(vm, rds, reg1 ^ reg2);
                        return;
                    case 0x2: // c.or
                        rvjit_trace_or(rds, rds, rs2, 2);
                        riscv_write_reg(vm, rds, reg1 | reg2);
                        return;
                    case 0x3: // c.and
                        rvjit_trace_and(rds, rds, rs2, 2);
                        riscv_write_reg(vm, rds, reg1 & reg2);
                        return;
                }
            } else {
                switch (funct2) {
#ifdef RV64
                    case 0x0: { // c.subw
                        const xlen_t reg2 = riscv_read_reg(vm, rs2);
                        rvjit_trace_subw(rds, rds, rs2, 2);
                        riscv_write_reg(vm, rds, (int32_t)(reg1 - reg2));
                        return;
                    }
                    case 0x1: { // c.addw
                        const xlen_t reg2 = riscv_read_reg(vm, rs2);
                        rvjit_trace_addw(rds, rds, rs2, 2);
                        riscv_write_reg(vm, rds, (int32_t)(reg1 + reg2));
                        return;
                    }
#endif
                    case 0x2: { // c.mul (Zcb + M)
                        const xlen_t reg2 = riscv_read_reg(vm, rs2);
                        rvjit_trace_mul(rds, rds, rs2, 2);
                        riscv_write_reg(vm, rds, reg1 * reg2);
                        return;
                    }
                    case 0x3:
                        switch (bit_cut(insn, 2, 3)) {
                            case 0x0: // c.zext.b (Zcb)
                                rvjit_trace_andi(rds, rds, 0xFF, 2);
                                riscv_write_reg(vm, rds, (uint8_t)reg1);
                                return;
                            case 0x1: // c.sext.b (Zcb + Zbb)
                                rvjit_trace_sext_b(rds, rds, 2);
                                riscv_write_reg(vm, rds, (int8_t)reg1);
                                return;
                            case 0x2: // c.zext.h (Zcb + Zbb)
                                rvjit_trace_andi(rds, rds, 0xFFFF, 2);
                                riscv_write_reg(vm, rds, (uint16_t)reg1);
                                return;
                            case 0x3: // c.sext.h (Zcb + Zbb)
                                rvjit_trace_sext_h(rds, rds, 2);
                                riscv_write_reg(vm, rds, (int16_t)reg1);
                                return;
#ifdef RV64
                            case 0x4: // c.zext.w (Zcb + Zba), RV64 only
                                rvjit_trace_shadd_uw(rds, rds, REGISTER_ZERO, 0, 2);
                                riscv_write_reg(vm, rds, (uint32_t)reg1);
                                return;
#endif
                            case 0x5: // c.not (Zcb)
                                rvjit_trace_xori(rds, rds, -1, 2);
                                riscv_write_reg(vm, rds, ~reg1);
                                return;
                        }
                        break;;
                }
            }
        }
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_c_c1(rvvm_hart_t* vm, const uint16_t insn)
{
    switch (insn >> 13) {
        case 0x0: { // c.addi
            const regid_t rds = bit_cut(insn, 7, 5);
            const xlen_t src = riscv_read_reg(vm, rds);
            const sxlen_t imm = decode_c_alu_imm(insn);
            rvjit_trace_addi(rds, rds, imm, 2);
            riscv_write_reg(vm, rds, src + imm);
            return;
        }
        case 0x1: { // c.jal (RV32), c.addiw (RV64)
#ifdef RV64
            const regid_t rds = bit_cut(insn, 7, 5);
            const xlen_t src = riscv_read_reg(vm, rds);
            const sxlen_t imm = decode_c_alu_imm(insn);
            rvjit_trace_addiw(rds, rds, imm, 2);
            riscv_write_reg(vm, rds, (int32_t)(src + imm));
#else
            const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);
            const sxlen_t offset = decode_c_jal_imm(insn);
            rvjit_trace_jal(REGISTER_X1, offset, 2);
            riscv_write_reg(vm, REGISTER_X1, pc + 2);
            riscv_write_reg(vm, REGISTER_PC, pc + offset - 2);
#endif
            return;
        }
        case 0x2: { // c.li
            const regid_t rds = bit_cut(insn, 7, 5);
            const sxlen_t imm = decode_c_alu_imm(insn);
            rvjit_trace_li(rds, imm, 2);
            riscv_write_reg(vm, rds, imm);
            return;
        }
        case 0x3: { // c.addi16sp (rds == X2), c.lui (rds != X2)
            const regid_t rds = bit_cut(insn, 7, 5);
            if (rds == REGISTER_X2) {
                const sxlen_t off = decode_c_addi16sp_off(insn);
                const xlen_t sp = riscv_read_reg(vm, REGISTER_X2);
                rvjit_trace_addi(REGISTER_X2, REGISTER_X2, off, 2);
                riscv_write_reg(vm, REGISTER_X2, sp + off);
            } else {
                const sxlen_t imm = decode_c_lui_imm(insn);
                rvjit_trace_li(rds, imm, 2);
                riscv_write_reg(vm, rds, imm);
            }
            return;
        }
        case 0x4: // MISC ALU
            riscv_emulate_c_misc_alu(vm, insn);
            return;
        case 0x5: { // c.j
            const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);
            const sxlen_t offset = decode_c_jal_imm(insn);
            rvjit_trace_jal(REGISTER_ZERO, offset, 2);
            riscv_write_reg(vm, REGISTER_PC, pc + offset - 2);
            return;
        }
        case 0x6: { // c.beqz
            const regid_t rs1 = bit_cut(insn, 7, 3) + 8;
            const xlen_t src = riscv_read_reg(vm, rs1);
            const sxlen_t offset = decode_c_branch_imm(insn);
            if (src == 0) {
                const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);
                rvjit_trace_beq(rs1, REGISTER_ZERO, offset, 2, 2);
                riscv_write_reg(vm, REGISTER_PC, pc + offset - 2);
            } else {
                rvjit_trace_bne(rs1, REGISTER_ZERO, 2, offset, 2);
            }
            return;
        }
        case 0x7: { // c.bnez
            const regid_t rs1 = bit_cut(insn, 7, 3) + 8;
            const xlen_t src = riscv_read_reg(vm, rs1);
            const sxlen_t offset = decode_c_branch_imm(insn);
            if (src != 0) {
                const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);
                rvjit_trace_bne(rs1, REGISTER_ZERO, offset, 2, 2);
                riscv_write_reg(vm, REGISTER_PC, pc + offset - 2);
            } else {
                rvjit_trace_beq(rs1, REGISTER_ZERO, 2, offset, 2);
            }
            return;
        }
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_c_jr_mv(rvvm_hart_t* vm, const uint16_t insn)
{
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs2 = bit_cut(insn, 2, 5);

    if (bit_check(insn, 12)) {
        if (rds != 0) {
            if (rs2 != 0) {
                // c.add
                const xlen_t reg1 = riscv_read_reg(vm, rds);
                const xlen_t reg2 = riscv_read_reg(vm, rs2);
                rvjit_trace_add(rds, rds, rs2, 2);
                riscv_write_reg(vm, rds, reg1 + reg2);
            } else {
                // c.jalr
                const xlen_t reg1 = riscv_read_reg(vm, rds);
                const xlen_t pc = riscv_read_reg(vm, REGISTER_PC);
                rvjit_trace_jalr(REGISTER_X1, rds, 0, 2);
                riscv_write_reg(vm, REGISTER_X1, pc + 2);
                riscv_write_reg(vm, REGISTER_PC, reg1 - 2);
            }
        } else {
            // c.ebreak
            riscv_trap(vm, TRAP_BREAKPOINT, 0);
        }
    } else {
        if (rs2 != 0) {
            // c.mv
            const xlen_t reg2 = riscv_read_reg(vm, rs2);
            rvjit_trace_addi(rds, rs2, 0, 2);
            riscv_write_reg(vm, rds, reg2);
        } else {
            // c.jr
            const xlen_t reg1 = riscv_read_reg(vm, rds);
            rvjit_trace_jalr(REGISTER_ZERO, rds, 0, 2);
            riscv_write_reg(vm, REGISTER_PC, reg1 - 2);
        }
    }
}

static forceinline void riscv_emulate_c_c2(rvvm_hart_t* vm, const uint16_t insn)
{
    switch (insn >> 13) {
        case 0x0: { // c.slli
            const regid_t rds = bit_cut(insn, 7, 5);
            const xlen_t src = riscv_read_reg(vm, rds);
            const bitcnt_t shamt = decode_c_shamt(insn);
            rvjit_trace_slli(rds, rds, shamt, 2);
            riscv_write_reg(vm, rds, src << shamt);
            return;
        }
#ifdef USE_FPU
        case 0x1:
            if (likely(fpu_is_enabled(vm))) { // c.fldsp
                const regid_t rds = bit_cut(insn, 7, 5);
                const xlen_t offset = decode_c_ldsp_off(insn);
                const xlen_t addr = riscv_read_reg(vm, REGISTER_X2) + offset;
                riscv_load_double(vm, addr, rds);
                return;
            }
            break;
#endif
        case 0x2: { // c.lwsp
            const regid_t rds = bit_cut(insn, 7, 5);
            const xlen_t offset = decode_c_lwsp_off(insn);
            const xlen_t addr = riscv_read_reg(vm, REGISTER_X2) + offset;
            rvjit_trace_lw(rds, REGISTER_X2, offset, 2);
            riscv_load_s32(vm, addr, rds);
            return;
        }
#if defined(USE_FPU) && !defined(RV64)
        case 0x3:
            if (likely(fpu_is_enabled(vm))) { // c.flwsp (RV32)
                const regid_t rds = bit_cut(insn, 7, 5);
                const xlen_t offset = decode_c_lwsp_off(insn);
                const xlen_t addr = riscv_read_reg(vm, REGISTER_X2) + offset;
                riscv_load_float(vm, addr, rds);
                return;
            }
            break;
#endif
#ifdef RV64
        case 0x3: { // c.ldsp (RV64)
            const regid_t rds = bit_cut(insn, 7, 5);
            const xlen_t offset = decode_c_ldsp_off(insn);
            const xlen_t addr = riscv_read_reg(vm, REGISTER_X2) + offset;
            rvjit_trace_ld(rds, REGISTER_X2, offset, 2);
            riscv_load_u64(vm, addr, rds);
            return;
        }
#endif
        case 0x4:
            riscv_emulate_c_jr_mv(vm, insn);
            return;
#ifdef USE_FPU
        case 0x5:
            if (likely(fpu_is_enabled(vm))) { // c.fsdsp
                const regid_t rds = bit_cut(insn, 2, 5);
                const xlen_t offset = decode_c_sdsp_off(insn);
                const xlen_t addr = riscv_read_reg(vm, REGISTER_X2) + offset;
                riscv_store_double(vm, addr, rds);
                return;
            }
            break;
#endif
        case 0x6: { // c.swsp
            const regid_t rds = bit_cut(insn, 2, 5);
            const xlen_t offset = decode_c_swsp_off(insn);
            const xlen_t addr = riscv_read_reg(vm, REGISTER_X2) + offset;
            rvjit_trace_sw(rds, REGISTER_X2, offset, 2);
            riscv_store_u32(vm, addr, rds);
            return;
        }
#if defined(USE_FPU) && !defined(RV64)
        case 0x7:
            if (likely(fpu_is_enabled(vm))) { // c.fswsp (RV32)
                const regid_t rds = bit_cut(insn, 2, 5);
                const xlen_t offset = decode_c_lwsp_off(insn);
                const xlen_t addr = riscv_read_reg(vm, REGISTER_X2) + offset;
                riscv_store_float(vm, addr, rds);
                return;
            }
            break;
#endif
#ifdef RV64
        case 0x7: { // c.sdsp (RV64)
            const regid_t rds = bit_cut(insn, 2, 5);
            const xlen_t offset = decode_c_sdsp_off(insn);
            const xlen_t addr = riscv_read_reg(vm, REGISTER_X2) + offset;
            rvjit_trace_sd(rds, REGISTER_X2, offset, 2);
            riscv_store_u64(vm, addr, rds);
            return;
        }
#endif
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_insn(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t op = insn & 0x3;
    switch (op) {
        case 0x0:
            riscv_emulate_c_c0(vm, insn);
            vm->registers[REGISTER_PC] += 2;
            return;
        case 0x1:
            riscv_emulate_c_c1(vm, insn);
            vm->registers[REGISTER_PC] += 2;
            return;
        case 0x2:
            riscv_emulate_c_c2(vm, insn);
            vm->registers[REGISTER_PC] += 2;
            return;
        case 0x3:
            riscv_emulate_i(vm, insn);
            vm->registers[REGISTER_PC] += 4;
            return;
    }
}

#endif
