/*
riscv_base.h - RISC-V Base Integer ISA interpreter
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef RVVM_RISCV_BASE_H
#define RVVM_RISCV_BASE_H

#include "riscv_atomics.h"
#include "riscv_common.h"
#include "riscv_priv.h"

#if defined(USE_FPU)
#include "riscv_fpu.h"
#endif

/*
 * Base 5-bit opcodes in insn[6:2]
 */
#define RISCV_OPC_LOAD     0x00
#define RISCV_OPC_LOAD_FP  0x04
#define RISCV_OPC_MISC_MEM 0x0C
#define RISCV_OPC_OP_IMM   0x10
#define RISCV_OPC_AUIPC    0x14
#define RISCV_OPC_OP_IMM32 0x18
#define RISCV_OPC_STORE    0x20
#define RISCV_OPC_STORE_FP 0x24
#define RISCV_OPC_AMO      0x2C
#define RISCV_OPC_OP       0x30
#define RISCV_OPC_LUI      0x34
#define RISCV_OPC_OP32     0x38
#define RISCV_OPC_FMADD    0x40
#define RISCV_OPC_FMSUB    0x44
#define RISCV_OPC_FNMSUB   0x48
#define RISCV_OPC_FNMADD   0x4C
#define RISCV_OPC_OP_FP    0x50
#define RISCV_OPC_OP_VEC   0x54
#define RISCV_OPC_BRANCH   0x60
#define RISCV_OPC_JALR     0x64
#define RISCV_OPC_JAL      0x6C
#define RISCV_OPC_SYSTEM   0x70

#if defined(RISCV64)
#define RISCV_OP_IMM_MASK   0xFC007000UL
#define bit_clz(val)        bit_clz64(val)
#define bit_ctz(val)        bit_ctz64(val)
#define bit_popcnt(val)     bit_popcnt64(val)
#define bit_rotl(val, bits) bit_rotl64(val, bits)
#define bit_rotr(val, bits) bit_rotr64(val, bits)
#define bit_clmul(a, b)     bit_clmul64(a, b)
#define bit_clmulh(a, b)    bit_clmulh64(a, b)
#define bit_clmulr(a, b)    bit_clmulr64(a, b)
#else
#define RISCV_OP_IMM_MASK   0xFE007000UL
#define bit_clz(val)        bit_clz32(val)
#define bit_ctz(val)        bit_ctz32(val)
#define bit_popcnt(val)     bit_popcnt32(val)
#define bit_rotl(val, bits) bit_rotl32(val, bits)
#define bit_rotr(val, bits) bit_rotr32(val, bits)
#define bit_clmul(a, b)     bit_clmul32(a, b)
#define bit_clmulh(a, b)    bit_clmulh32(a, b)
#define bit_clmulr(a, b)    bit_clmulr32(a, b)
#endif

static forceinline uint32_t decode_i_shamt(const uint32_t insn)
{
#if defined(RISCV64)
    return bit_ext_u32(insn, 20, 6);
#else
    return bit_ext_u32(insn, 20, 5);
#endif
}

static forceinline uint32_t decode_i_shift_funct7(const uint32_t insn)
{
#if defined(RISCV64)
    return (insn >> 26) << 1;
#else
    return insn >> 25;
#endif
}

static forceinline int32_t decode_i_branch_off(const uint32_t insn)
{
    return (((uint32_t)(((int32_t)insn) >> 31)) << 12) //
         | ((insn >> 7) & 0x0000001EUL)                //
         | ((insn & 0x00000080) << 4)                  //
         | ((insn >> 20) & 0x000007E0UL);
}

static forceinline int32_t decode_i_jal_off(const uint32_t insn)
{
    return (((uint32_t)(((int32_t)insn) >> 31)) << 20) //
         | (insn & 0x000FF000UL)                       //
         | ((insn >> 9) & 0x00000800UL)                //
         | ((insn >> 20) & 0x000007FEUL);
}

static inline func_clang_minsize uint64_t riscv_bit_xperm4(uint64_t val, uint64_t lut)
{
    uint64_t ret = 0;
    for (size_t i = 0; i < 64; i += 4) {
        ret |= bit_ext_u64(val, ((lut >> i) & 0x0F) << 2, 4) << i;
    }
    return ret;
}

static inline func_clang_minsize uint64_t riscv_bit_xperm8(uint64_t val, uint64_t lut)
{
    uint64_t ret = 0;
    for (size_t i = 0; i < 64; i += 8) {
        uint32_t idx = (lut >> i);
        if (!(idx & 0xF8)) {
            ret |= bit_ext_u64(val, (idx & 0x07) << 3, 8) << i;
        }
    }
    return ret;
}

static forceinline void riscv_emulate_i_opc_load(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t  rds  = bit_ext_u32(insn, 7, 5);
    const size_t  rs1  = bit_ext_u32(insn, 15, 5);
    const sxlen_t off  = bit_ext_i32(insn, 20, 12);
    const xlen_t  addr = riscv_read_reg(vm, rs1) + off;

    switch (bit_ext_u32(insn, 12, 3)) {
        case 0x00: // lb
            rvjit_trace_lb(rds, rs1, off, 4);
            riscv_load_s8(vm, addr, rds);
            return;
        case 0x01: // lh
            rvjit_trace_lh(rds, rs1, off, 4);
            riscv_load_s16(vm, addr, rds);
            return;
        case 0x02: // lw
            rvjit_trace_lw(rds, rs1, off, 4);
            riscv_load_s32(vm, addr, rds);
            return;
#if defined(RISCV64)
        case 0x03: // ld
            rvjit_trace_ld(rds, rs1, off, 4);
            riscv_load_u64(vm, addr, rds);
            return;
#endif
        case 0x04: // lbu
            rvjit_trace_lbu(rds, rs1, off, 4);
            riscv_load_u8(vm, addr, rds);
            return;
        case 0x05: // lhu
            rvjit_trace_lhu(rds, rs1, off, 4);
            riscv_load_u16(vm, addr, rds);
            return;
#if defined(RISCV64)
        case 0x06: // lwu
            rvjit_trace_lwu(rds, rs1, off, 4);
            riscv_load_u32(vm, addr, rds);
            return;
#endif
    }

    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_i_opc_imm(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t rds = bit_ext_u32(insn, 7, 5);
    const size_t rs1 = bit_ext_u32(insn, 15, 5);
    const xlen_t imm = bit_ext_i32(insn, 20, 12);
    const xlen_t src = riscv_read_reg(vm, rs1);

    switch (bit_ext_u32(insn, 12, 3)) {
        case 0x00: // addi
            rvjit_trace_addi(rds, rs1, imm, 4);
            riscv_write_reg(vm, rds, src + imm);
            return;
        case 0x01: {
            const uint32_t shamt = decode_i_shamt(insn);
            switch (decode_i_shift_funct7(insn)) {
                case 0x00: // slli
                    rvjit_trace_slli(rds, rs1, shamt, 4);
                    riscv_write_reg(vm, rds, src << shamt);
                    return;
#if defined(RISCV32)
                case 0x04:
                    switch (shamt) {
                        case 0x0F: // zip (Zbkb), RV32 only
                            riscv_write_reg(vm, rds, bit_zip32(src));
                            return;
                    }
                    break;
#endif
                case 0x14: // bseti (Zbs)
                    rvjit_trace_bseti(rds, rs1, shamt, 4);
                    riscv_write_reg(vm, rds, src | (((xlen_t)1U) << shamt));
                    return;
                case 0x24: // bclri (Zbs)
                    rvjit_trace_bclri(rds, rs1, shamt, 4);
                    riscv_write_reg(vm, rds, src & ~(((xlen_t)1U) << shamt));
                    return;
                case 0x34: // binvi (Zbs)
                    rvjit_trace_binvi(rds, rs1, shamt, 4);
                    riscv_write_reg(vm, rds, src ^ (((xlen_t)1U) << shamt));
                    return;
                case 0x30:
                    switch (shamt) {
                        case 0x00: // clz (Zbb)
                            // TODO: JIT
                            riscv_write_reg(vm, rds, bit_clz(src));
                            return;
                        case 0x01: // ctz (Zbb)
                            // TODO: JIT
                            riscv_write_reg(vm, rds, bit_ctz(src));
                            return;
                        case 0x02: // cpop (Zbb)
                            // TODO: JIT
                            riscv_write_reg(vm, rds, bit_popcnt(src));
                            return;
                        case 0x04: // sext.b (Zbb)
                            rvjit_trace_sext_b(rds, rs1, 4);
                            riscv_write_reg(vm, rds, (int8_t)src);
                            return;
                        case 0x05: // sext.h (Zbb)
                            rvjit_trace_sext_h(rds, rs1, 4);
                            riscv_write_reg(vm, rds, (int16_t)src);
                            return;
                    }
                    break;
            }
            break;
        }
        case 0x02: // slti
            rvjit_trace_slti(rds, rs1, imm, 4);
            riscv_write_reg(vm, rds, (((sxlen_t)src) < ((sxlen_t)imm)) ? 1 : 0);
            return;
        case 0x03: // sltiu
            rvjit_trace_sltiu(rds, rs1, imm, 4);
            riscv_write_reg(vm, rds, (src < imm) ? 1 : 0);
            return;
        case 0x04: // xori
            rvjit_trace_xori(rds, rs1, imm, 4);
            riscv_write_reg(vm, rds, src ^ imm);
            return;
        case 0x05: {
            const uint32_t shamt = decode_i_shamt(insn);
            switch (decode_i_shift_funct7(insn)) {
                case 0x00: // srli
                    rvjit_trace_srli(rds, rs1, shamt, 4);
                    riscv_write_reg(vm, rds, src >> shamt);
                    return;
                case 0x20: // srai
                    rvjit_trace_srai(rds, rs1, shamt, 4);
                    riscv_write_reg(vm, rds, ((sxlen_t)src) >> shamt);
                    return;
#if defined(RISCV32)
                case 0x04:
                    switch (shamt) {
                        case 0x0F: // unzip (Zbkb), RV32 only
                            riscv_write_reg(vm, rds, bit_unzip32(src));
                            return;
                    }
                    break;
#endif
                case 0x14:
                    if (likely(shamt == 0x07)) { // orc.b (Zbb)
                        // TODO: JIT
                        riscv_write_reg(vm, rds, bit_orc_b(src));
                        return;
                    }
                    break;
                case 0x24: // bexti (Zbs)
                    rvjit_trace_bexti(rds, rs1, shamt, 4);
                    riscv_write_reg(vm, rds, (src >> shamt) & 1);
                    return;
                case 0x34:
                    switch (shamt) {
                        case 0x07: // brev8 (Zbkb)
                            riscv_write_reg(vm, rds, bit_brev8(src));
                            return;
#if defined(RISCV64)
                        case 0x38: // rev8 (Zbb), RV64 encoding
                            riscv_write_reg(vm, rds, byteswap_uint64(src));
                            return;
#else
                        case 0x18: // rev8 (Zbb), RV32 encoding
                            riscv_write_reg(vm, rds, byteswap_uint32(src));
                            return;
#endif
                    }
                    break;
                case 0x30: // rori (Zbb)
                    rvjit_trace_rori(rds, rs1, shamt, 4);
                    riscv_write_reg(vm, rds, bit_rotr(src, shamt));
                    return;
            }
            break;
        }
        case 0x06: // ori
            rvjit_trace_ori(rds, rs1, imm, 4);
            riscv_write_reg(vm, rds, src | imm);
            return;
        case 0x07: // andi
            rvjit_trace_andi(rds, rs1, imm, 4);
            riscv_write_reg(vm, rds, src & imm);
            return;
    }

    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_i_auipc(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t rds = bit_ext_u32(insn, 7, 5);
    const xlen_t imm = (int32_t)(insn & 0xFFFFF000UL);
    const xlen_t pc  = riscv_read_reg(vm, RISCV_REG_PC);

    rvjit_trace_auipc(rds, imm, 4);
    riscv_write_reg(vm, rds, pc + imm);
}

#if defined(RISCV64)

static forceinline void riscv_emulate_i_opc_imm32(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t   rds = bit_ext_u32(insn, 7, 5);
    const size_t   rs1 = bit_ext_u32(insn, 15, 5);
    const uint32_t src = riscv_read_reg(vm, rs1);

    if (likely(!bit_ext_u32(insn, 12, 3))) { // addiw
        const uint32_t imm = bit_ext_i32(insn, 20, 12);
        rvjit_trace_addiw(rds, rs1, imm, 4);
        riscv_write_reg(vm, rds, (int32_t)(src + imm));
        return;
    }

    switch (insn & 0xFE007000UL) {
        case 0x00001000UL: { // slliw
            const uint32_t shamt = bit_ext_u32(insn, 20, 5);
            rvjit_trace_slliw(rds, rs1, shamt, 4);
            riscv_write_reg(vm, rds, (int32_t)(src << shamt));
            return;
        }
        case 0x08001000:
        case 0x0A001000: { // slli.uw (Zba)
            const uint32_t shamt = bit_ext_u32(insn, 20, 6);
            rvjit_trace_slli_uw(rds, rs1, shamt, 4);
            riscv_write_reg(vm, rds, ((xlen_t)src) << shamt);
            return;
        }
        case 0x60001000UL:
            switch (bit_ext_u32(insn, 20, 5)) {
                case 0x00: // clzw (Zbb)
                    // TODO: JIT
                    riscv_write_reg(vm, rds, bit_clz32(src));
                    return;
                case 0x01: // ctzw (Zbb)
                    // TODO: JIT
                    riscv_write_reg(vm, rds, bit_ctz32(src));
                    return;
                case 0x02: // cpopw (Zbb)
                    // TODO: JIT
                    riscv_write_reg(vm, rds, bit_popcnt32(src));
                    return;
            }
            break;
        case 0x00005000UL: { // srli
            const uint32_t shamt = bit_ext_u32(insn, 20, 5);
            rvjit_trace_srliw(rds, rs1, shamt, 4);
            riscv_write_reg(vm, rds, (int32_t)(src >> shamt));
            return;
        }
        case 0x40005000UL: { // srai
            const uint32_t shamt = bit_ext_u32(insn, 20, 5);
            rvjit_trace_sraiw(rds, rs1, shamt, 4);
            riscv_write_reg(vm, rds, ((int32_t)src) >> shamt);
            return;
        }
        case 0x60005000UL: { // roriw (Zbb)
            const uint32_t shamt = bit_ext_u32(insn, 20, 5);
            rvjit_trace_roriw(rds, rs1, shamt, 4);
            riscv_write_reg(vm, rds, (int32_t)bit_rotr32(src, shamt));
            return;
        }
    }

    riscv_illegal_insn(vm, insn);
}

#endif

static forceinline void riscv_emulate_i_opc_store(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t  rs1  = bit_ext_u32(insn, 15, 5);
    const size_t  rs2  = bit_ext_u32(insn, 20, 5);
    const sxlen_t off  = (int32_t)((((uint32_t)(((int32_t)insn) >> 25)) << 5) | bit_ext_u32(insn, 7, 5));
    const xlen_t  addr = riscv_read_reg(vm, rs1) + off;

    switch (bit_ext_u32(insn, 12, 3)) {
        case 0x00: // sb
            rvjit_trace_sb(rs2, rs1, off, 4);
            riscv_store_u8(vm, addr, rs2);
            return;
        case 0x01: // sh
            rvjit_trace_sh(rs2, rs1, off, 4);
            riscv_store_u16(vm, addr, rs2);
            return;
        case 0x02: // sw
            rvjit_trace_sw(rs2, rs1, off, 4);
            riscv_store_u32(vm, addr, rs2);
            return;
#if defined(RISCV64)
        case 0x03: // sd
            rvjit_trace_sd(rs2, rs1, off, 4);
            riscv_store_u64(vm, addr, rs2);
            return;
#endif
    }

    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_i_opc_op(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t rds  = bit_ext_u32(insn, 7, 5);
    const size_t rs1  = bit_ext_u32(insn, 15, 5);
    const size_t rs2  = bit_ext_u32(insn, 20, 5);
    const xlen_t reg1 = riscv_read_reg(vm, rs1);
    const xlen_t reg2 = riscv_read_reg(vm, rs2);

    switch (insn & 0xFE007000UL) {
        case 0x00000000UL: // add
            rvjit_trace_add(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 + reg2);
            return;
        case 0x40000000UL: // sub
            rvjit_trace_sub(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 - reg2);
            return;
        case 0x00001000UL: // sll
            rvjit_trace_sll(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 << (reg2 & bit_mask(SHAMT_BITS)));
            return;
        case 0x00002000UL: // slt
            rvjit_trace_slt(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, (((sxlen_t)reg1) < ((sxlen_t)reg2)) ? 1 : 0);
            return;
        case 0x00003000UL: // sltu
            rvjit_trace_sltu(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, (reg1 < reg2) ? 1 : 0);
            return;
        case 0x00004000UL: // xor
            rvjit_trace_xor(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 ^ reg2);
            return;
        case 0x00005000UL: // srl
            rvjit_trace_srl(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 >> (reg2 & bit_mask(SHAMT_BITS)));
            return;
        case 0x40005000UL: // sra
            rvjit_trace_sra(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, ((sxlen_t)reg1) >> (reg2 & bit_mask(SHAMT_BITS)));
            return;
        case 0x00006000UL: // or
            rvjit_trace_or(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 | reg2);
            return;
        case 0x00007000UL: // and
            rvjit_trace_and(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 & reg2);
            return;
        /*
         * Multiplication / Division
         */
        case 0x02000000UL: // mul
            rvjit_trace_mul(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 * reg2);
            return;
        case 0x02001000UL: // mulh
            rvjit_trace_mulh(rds, rs1, rs2, 4);
#if defined(RISCV64)
            riscv_write_reg(vm, rds, mulh_uint64(reg1, reg2));
#else
            riscv_write_reg(vm, rds, ((int64_t)(sxlen_t)reg1 * (int64_t)(sxlen_t)reg2) >> 32);
#endif
            return;
        case 0x02002000UL: // mulhsu
            rvjit_trace_mulhsu(rds, rs1, rs2, 4);
#if defined(RISCV64)
            riscv_write_reg(vm, rds, mulhsu_uint64(reg1, reg2));
#else
            riscv_write_reg(vm, rds, ((int64_t)(sxlen_t)reg1 * (uint64_t)reg2) >> 32);
#endif
            return;
        case 0x02003000UL: // mulhu
            rvjit_trace_mulhu(rds, rs1, rs2, 4);
#if defined(RISCV64)
            riscv_write_reg(vm, rds, mulhu_uint64(reg1, reg2));
#else
            riscv_write_reg(vm, rds, ((uint64_t)reg1 * (uint64_t)reg2) >> 32);
#endif
            return;
        case 0x02004000UL: { // div
            sxlen_t result = -1;
            rvjit_trace_div(rds, rs1, rs2, 4);
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
        case 0x02005000UL: { // divu
            xlen_t result = (sxlen_t)-1;
            rvjit_trace_divu(rds, rs1, rs2, 4);
            // division by zero check (we already setup result var for error)
            if (reg2 != 0) {
                result = reg1 / reg2;
            }
            riscv_write_reg(vm, rds, result);
            return;
        }
        case 0x02006000UL: { // rem
            sxlen_t result = reg1;
            rvjit_trace_rem(rds, rs1, rs2, 4);
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
        case 0x02007000UL: { // remu
            xlen_t result = reg1;
            rvjit_trace_remu(rds, rs1, rs2, 4);
            // division by zero check (we already setup result var for error)
            if (reg2 != 0) {
                result = reg1 % reg2;
            }
            riscv_write_reg(vm, rds, result);
            return;
        }
        /*
         * Zbkb: Bit-manipulation for Cryptography
         */
        case 0x08004000UL: // pack (Zbkb)
#if defined(RISCV64)
            riscv_write_reg(vm, rds, ((uint32_t)reg1) | (((uint64_t)reg2) << 32));
#else
            if (!rs2) { // zext.h (Zbb), RV32 encoding
                rvjit_trace_andi(rds, rs1, 0xFFFF, 4);
            }
            riscv_write_reg(vm, rds, ((uint16_t)reg1) | (reg2 << 16));
#endif
            return;
        case 0x08007000UL: // packh (Zbkb)
            if (!rs2) {
                rvjit_trace_andi(rds, rs1, 0xFF, 4);
            }
            riscv_write_reg(vm, rds, ((uint8_t)reg1) | (((uint32_t)((uint8_t)reg2)) << 8));
            return;
        /*
         * Zbc: Carry-less multiplication
         */
        case 0x0A001000UL: // clmul (Zbc)
            riscv_write_reg(vm, rds, bit_clmul(reg1, reg2));
            return;
        case 0x0A002000UL: // clmulr (Zbc)
            riscv_write_reg(vm, rds, bit_clmulr(reg1, reg2));
            return;
        case 0x0A003000UL: // clmulh (Zbc)
            riscv_write_reg(vm, rds, bit_clmulh(reg1, reg2));
            return;
        /*
         * Zbb: Basic bit-manipulation
         */
        case 0x0A004000UL: // min (Zbb)
            rvjit_trace_min(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, EVAL_MIN((sxlen_t)reg1, (sxlen_t)reg2));
            return;
        case 0x0A005000UL: // minu (Zbb)
            rvjit_trace_minu(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, EVAL_MIN(reg1, reg2));
            return;
        case 0x0A006000UL: // max (Zbb)
            rvjit_trace_max(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, EVAL_MAX((sxlen_t)reg1, (sxlen_t)reg2));
            return;
        case 0x0A007000UL: // maxu (Zbb)
            rvjit_trace_maxu(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, EVAL_MAX(reg1, reg2));
            return;
        case 0x40004000UL: // xnor (Zbb)
            rvjit_trace_xnor(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 ^ ~reg2);
            return;
        case 0x40006000UL: // orn (Zbb)
            rvjit_trace_orn(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 | ~reg2);
            return;
        case 0x40007000UL: // andn (Zbb)
            rvjit_trace_andn(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 & ~reg2);
            return;
        case 0x60001000UL: // rol (Zbb)
            rvjit_trace_rol(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, bit_rotl(reg1, reg2 & bit_mask(SHAMT_BITS)));
            return;
        case 0x60005000UL: // ror (Zbb)
            rvjit_trace_ror(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, bit_rotr(reg1, reg2 & bit_mask(SHAMT_BITS)));
            return;
        /*
         * Zicond: Integer conditionals
         */
        case 0x0E005000UL: // czero.eqz (Zicond)
            // TODO: JIT
            riscv_write_reg(vm, rds, reg2 ? reg1 : 0);
            return;
        case 0x0E007000UL: // czero.nez (Zicond)
            // TODO: JIT
            riscv_write_reg(vm, rds, reg2 ? 0 : reg1);
            return;
        /*
         * Zba: Address generation
         */
        case 0x20002000UL: // sh1add (Zba)
            rvjit_trace_shadd(rds, rs1, rs2, 1, 4);
            riscv_write_reg(vm, rds, reg2 + (reg1 << 1));
            return;
        case 0x20004000UL: // sh2add (Zba)
            rvjit_trace_shadd(rds, rs1, rs2, 2, 4);
            riscv_write_reg(vm, rds, reg2 + (reg1 << 2));
            return;
        case 0x20006000UL: // sh3add (Zba)
            rvjit_trace_shadd(rds, rs1, rs2, 3, 4);
            riscv_write_reg(vm, rds, reg2 + (reg1 << 3));
            return;
        /*
         * Zbkx: Crossbar permutations
         */
        case 0x28002000UL: // xperm4 (Zbkx)
            riscv_write_reg(vm, rds, riscv_bit_xperm4(reg1, reg2));
            return;
        case 0x28004000UL: // xperm8 (Zbkx)
            riscv_write_reg(vm, rds, riscv_bit_xperm8(reg1, reg2));
            return;
        /*
         * Zbs: Bitset manipulation
         */
        case 0x28001000UL: // bset (Zbs)
            rvjit_trace_bset(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 | (((xlen_t)1U) << (reg2 & bit_mask(SHAMT_BITS))));
            return;
        case 0x48001000UL: // bclr (Zbs)
            rvjit_trace_bclr(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 & ~(((xlen_t)1U) << (reg2 & bit_mask(SHAMT_BITS))));
            return;
        case 0x48005000UL: // bext (Zbs)
            rvjit_trace_bext(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, (reg1 >> (reg2 & bit_mask(SHAMT_BITS))) & 1);
            return;
        case 0x68001000UL: // binv (Zbs)
            rvjit_trace_binv(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, reg1 ^ (((xlen_t)1U) << (reg2 & bit_mask(SHAMT_BITS))));
            return;
    }

    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_i_lui(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t rds = bit_ext_u32(insn, 7, 5);
    const xlen_t imm = (int32_t)(insn & 0xFFFFF000UL);

    rvjit_trace_li(rds, imm, 4);
    riscv_write_reg(vm, rds, imm);
}

#if defined(RISCV64)

static forceinline void riscv_emulate_i_opc_op32(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t   rds  = bit_ext_u32(insn, 7, 5);
    const size_t   rs1  = bit_ext_u32(insn, 15, 5);
    const size_t   rs2  = bit_ext_u32(insn, 20, 5);
    const uint32_t reg1 = riscv_read_reg(vm, rs1);
    const uint32_t reg2 = riscv_read_reg(vm, rs2);

    switch (insn & 0xFE007000UL) {
        case 0x00000000UL: // addw
            rvjit_trace_addw(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, (int32_t)(reg1 + reg2));
            return;
        case 0x40000000UL: // subw
            rvjit_trace_subw(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, (int32_t)(reg1 - reg2));
            return;
        case 0x00001000UL: // sllw
            rvjit_trace_sllw(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, (int32_t)(reg1 << (reg2 & 0x1F)));
            return;
        case 0x00005000UL: // srlw
            rvjit_trace_srlw(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, (int32_t)(reg1 >> (reg2 & 0x1F)));
            return;
        case 0x40005000UL: // sraw
            rvjit_trace_sraw(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, (int32_t)(((int32_t)reg1) >> (reg2 & 0x1F)));
            return;
        /*
         * Multiplication / Division
         */
        case 0x02000000UL: // mulw
            rvjit_trace_mulw(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, (int32_t)(reg1 * reg2));
            return;
        case 0x02004000UL: { // divw
            int32_t result = -1;
            rvjit_trace_divw(rds, rs1, rs2, 4);
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
        case 0x02005000UL: { // divuw
            uint32_t result = -1;
            rvjit_trace_divuw(rds, rs1, rs2, 4);
            // overflow
            if (reg2 != 0) {
                result = reg1 / reg2;
            }
            riscv_write_reg(vm, rds, (int32_t)result);
            return;
        }
        case 0x02006000UL: { // remw
            int32_t result = reg1;
            rvjit_trace_remw(rds, rs1, rs2, 4);
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
        case 0x02007000UL: { // remuw
            uint32_t result = reg1;
            rvjit_trace_remuw(rds, rs1, rs2, 4);
            // division by zero check (we already setup result var for error)
            if (reg2 != 0) {
                result = reg1 % reg2;
            }
            riscv_write_reg(vm, rds, (int32_t)result);
            return;
        }
        /*
         * Zbkb: Bit-manipulation for Cryptography
         */
        case 0x08004000UL: // packw (Zbkb)
            if (!rs2) {    // zext.h (Zbb), RV64 encoding
                rvjit_trace_andi(rds, rs1, 0xFFFF, 4);
            }
            riscv_write_reg(vm, rds, (int32_t)(((uint16_t)reg1) | (reg2 << 16)));
            return;
        /*
         * Zbb: Basic bit-manipulation
         */
        case 0x60001000UL: // rolw (Zbb)
            rvjit_trace_rolw(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, (int32_t)bit_rotl32(reg1, reg2 & bit_mask(SHAMT_BITS)));
            return;
        case 0x60005000UL: // rorw (Zbb)
            rvjit_trace_rorw(rds, rs1, rs2, 4);
            riscv_write_reg(vm, rds, (int32_t)bit_rotr32(reg1, reg2 & bit_mask(SHAMT_BITS)));
            return;
        /*
         * Zba: Address generation
         */
        case 0x08000000UL: // add.uw (Zba)
            rvjit_trace_shadd_uw(rds, rs1, rs2, 0, 4);
            riscv_write_reg(vm, rds, riscv_read_reg(vm, rs2) + ((xlen_t)reg1));
            return;
        case 0x20002000UL: // sh1add.uw (Zba)
            rvjit_trace_shadd_uw(rds, rs1, rs2, 1, 4);
            riscv_write_reg(vm, rds, riscv_read_reg(vm, rs2) + (((xlen_t)reg1) << 1));
            return;
        case 0x20004000UL: // sh2add.uw (Zba)
            rvjit_trace_shadd_uw(rds, rs1, rs2, 2, 4);
            riscv_write_reg(vm, rds, riscv_read_reg(vm, rs2) + (((xlen_t)reg1) << 2));
            return;
        case 0x20006000UL: // sh3add.uw (Zba)
            rvjit_trace_shadd_uw(rds, rs1, rs2, 3, 4);
            riscv_write_reg(vm, rds, riscv_read_reg(vm, rs2) + (((xlen_t)reg1) << 3));
            return;
    }

    riscv_illegal_insn(vm, insn);
}

#endif

static forceinline void riscv_emulate_i_opc_branch(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t  rs1 = bit_ext_u32(insn, 15, 5);
    const size_t  rs2 = bit_ext_u32(insn, 20, 5);
    const sxlen_t off = decode_i_branch_off(insn);

    switch (bit_ext_u32(insn, 12, 3)) {
        case 0x00: // beq
            if (riscv_read_reg(vm, rs1) == riscv_read_reg(vm, rs2)) {
                const xlen_t pc = riscv_read_reg(vm, RISCV_REG_PC);
                rvjit_trace_beq(rs1, rs2, off, 4, 4);
                riscv_write_reg(vm, RISCV_REG_PC, pc + off - 4);
                riscv_voluntary_preempt(vm);
            } else {
                rvjit_trace_bne(rs1, rs2, 4, off, 4);
            }
            return;
        case 0x01: // bne
            if (riscv_read_reg(vm, rs1) != riscv_read_reg(vm, rs2)) {
                const xlen_t pc = riscv_read_reg(vm, RISCV_REG_PC);
                rvjit_trace_bne(rs1, rs2, off, 4, 4);
                riscv_write_reg(vm, RISCV_REG_PC, pc + off - 4);
                riscv_voluntary_preempt(vm);
            } else {
                rvjit_trace_beq(rs1, rs2, 4, off, 4);
            }
            return;
        case 0x04: // blt
            if (riscv_read_reg_s(vm, rs1) < riscv_read_reg_s(vm, rs2)) {
                const xlen_t pc = riscv_read_reg(vm, RISCV_REG_PC);
                rvjit_trace_blt(rs1, rs2, off, 4, 4);
                riscv_write_reg(vm, RISCV_REG_PC, pc + off - 4);
                riscv_voluntary_preempt(vm);
            } else {
                rvjit_trace_bge(rs1, rs2, 4, off, 4);
            }
            return;
        case 0x05: // bge
            if (riscv_read_reg_s(vm, rs1) >= riscv_read_reg_s(vm, rs2)) {
                const xlen_t pc = riscv_read_reg(vm, RISCV_REG_PC);
                rvjit_trace_bge(rs1, rs2, off, 4, 4);
                riscv_write_reg(vm, RISCV_REG_PC, pc + off - 4);
                riscv_voluntary_preempt(vm);
            } else {
                rvjit_trace_blt(rs1, rs2, 4, off, 4);
            }
            return;
        case 0x06: // bltu
            if (riscv_read_reg(vm, rs1) < riscv_read_reg(vm, rs2)) {
                const xlen_t pc = riscv_read_reg(vm, RISCV_REG_PC);
                rvjit_trace_bltu(rs1, rs2, off, 4, 4);
                riscv_write_reg(vm, RISCV_REG_PC, pc + off - 4);
                riscv_voluntary_preempt(vm);
            } else {
                rvjit_trace_bgeu(rs1, rs2, 4, off, 4);
            }
            return;
        case 0x07: // bgeu
            if (riscv_read_reg(vm, rs1) >= riscv_read_reg(vm, rs2)) {
                const xlen_t pc = riscv_read_reg(vm, RISCV_REG_PC);
                rvjit_trace_bgeu(rs1, rs2, off, 4, 4);
                riscv_write_reg(vm, RISCV_REG_PC, pc + off - 4);
                riscv_voluntary_preempt(vm);
            } else {
                rvjit_trace_bltu(rs1, rs2, 4, off, 4);
            }
            return;
    }

    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_i_jalr(rvvm_hart_t* vm, const uint32_t insn)
{
    if (likely(!bit_ext_u32(insn, 12, 3))) {
        const size_t  rds      = bit_ext_u32(insn, 7, 5);
        const size_t  rs1      = bit_ext_u32(insn, 15, 5);
        const sxlen_t off      = bit_ext_i32(insn, 20, 12);
        const xlen_t  pc       = riscv_read_reg(vm, RISCV_REG_PC);
        const xlen_t  jmp_addr = riscv_read_reg(vm, rs1);
        rvjit_trace_jalr(rds, rs1, off, 4);
        riscv_write_reg(vm, rds, pc + 4);
        riscv_write_reg(vm, RISCV_REG_PC, ((jmp_addr + off) & (~(xlen_t)1)) - 4);
        riscv_voluntary_preempt(vm);
    } else {
        riscv_illegal_insn(vm, insn);
    }
}

static forceinline void riscv_emulate_i_jal(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t  rds = bit_ext_u32(insn, 7, 5);
    const sxlen_t off = decode_i_jal_off(insn);
    const xlen_t  pc  = riscv_read_reg(vm, RISCV_REG_PC);

    rvjit_trace_jal(rds, off, 4);
    riscv_write_reg(vm, rds, pc + 4);
    riscv_write_reg(vm, RISCV_REG_PC, pc + off - 4);
    riscv_voluntary_preempt(vm);
}

static forceinline void riscv_emulate_i(rvvm_hart_t* vm, const uint32_t insn)
{
    switch (insn & 0x7C) {
        case RISCV_OPC_LOAD:
            riscv_emulate_i_opc_load(vm, insn);
            return;
#if defined(USE_FPU)
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
#if defined(RISCV64)
        case RISCV_OPC_OP_IMM32:
            riscv_emulate_i_opc_imm32(vm, insn);
            return;
#endif
        case RISCV_OPC_STORE:
            riscv_emulate_i_opc_store(vm, insn);
            return;
#if defined(USE_FPU)
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
#if defined(RISCV64)
        case RISCV_OPC_OP32:
            riscv_emulate_i_opc_op32(vm, insn);
            return;
#endif
#if defined(USE_FPU)
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
