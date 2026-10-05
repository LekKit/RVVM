/*
rvjit_arm64.h - RVJIT ARM64 Backend
Copyright (C) 2025  LekKit <github.com/LekKit>
Copyright (C) 2022  cerg2010cerg2010 <github.com/cerg2010cerg2010>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "bit_ops.h"
#include "mem_ops.h"
#include "rvjit.h"
#include "utils.h"

#ifndef RVJIT_ARM64_H
#define RVJIT_ARM64_H

#define RVJIT_HOST_TARGET_ARM64     1

/*
 * Backend target features
 *
 * - Target requires 16-byte stack alignment
 * - Target stack grows down
 *
 * - Target supports 32-bit integer instructions
 * - Target supports 64-bit integer instructions
 * - Target supports 64-bit immediates / offsets
 * - Target zero-extends 32-bit instruction results
 *
 * - Target supports single-precision 32-bit FPU
 * - Target supports double-precision 64-bit FPU
 * - Target FPU registers are in SIMD registers
 * - Target keeps upper FPU register part
 *
 * - Target supports atomic instructions
 * - Target supports weak memory model
 */

#define RVJIT_HOST_STACK_GROW_DOWN  1
#define RVJIT_HOST_STACK_ALIGN      16

#define RVJIT_HOST_HAS_ALU32        1
#define RVJIT_HOST_HAS_ALU64        1
#define RVJIT_HOST_HAS_IMM64        1
#define RVJIT_HOST_HAS_ZEXT32       1

#define RVJIT_HOST_HAS_FPU32        1
#define RVJIT_HOST_HAS_FPU64        1
#define RVJIT_HOST_HAS_FPU_SIMD     1
#define RVJIT_HOST_HAS_FPU_KEEP     1

#define RVJIT_HOST_HAS_ATOMICS      1
#define RVJIT_HOST_HAS_WMO          1

/*
 * Backend calling convention info
 *
 * We have a native zero register (XZR)
 * NOTE: X31 acts as SP when used as load/store base or in rvjit_host_addisp()
 *
 * X0  - X15 registers are caller-saved (X0 - X7 are arguments & return values)
 * X16 - X17 registers are volatile intra-procedure-call scratch registers
 * X18 is a reserved platform register
 * x19 - X28 registers are callee-saved
 * X29 is a frame pointer register
 * X30 is a link register (Return address)
 * X31 is hardwired to zero / accesses stack pointer in special cases
 */

#define RVJIT_HOST_REGS_VOLATILE    0x0003FFFFU
#define RVJIT_HOST_REGS_CALLSAVE    0x3FF80000U

#define RVJIT_HOST_FP_REGS_VOLATILE 0xFFFF00FFU
#define RVJIT_HOST_FP_REGS_CALLSAVE 0x0000FF00U

#define RVJIT_HOST_REG_FP           29
#define RVJIT_HOST_REG_RA           30
#define RVJIT_HOST_REG_SP           31
#define RVJIT_HOST_REG_ZERO         31

#define RVJIT_HOST_REG_ARG0         0
#define RVJIT_HOST_REG_ARG1         1
#define RVJIT_HOST_REG_ARG2         2
#define RVJIT_HOST_REG_ARG3         3
#define RVJIT_HOST_REG_ARG4         4
#define RVJIT_HOST_REG_ARG5         5
#define RVJIT_HOST_REG_ARG6         6
#define RVJIT_HOST_REG_ARG7         7

#define RVJIT_HOST_REG_RET0         0
#define RVJIT_HOST_REG_RET1         1
#define RVJIT_HOST_REG_RET2         2
#define RVJIT_HOST_REG_RET3         3
#define RVJIT_HOST_REG_RET4         4
#define RVJIT_HOST_REG_RET5         5
#define RVJIT_HOST_REG_RET6         6
#define RVJIT_HOST_REG_RET7         7

#define RVJIT_HOST_FP_REG_ARG0      0
#define RVJIT_HOST_FP_REG_ARG1      1
#define RVJIT_HOST_FP_REG_ARG2      2
#define RVJIT_HOST_FP_REG_ARG3      3
#define RVJIT_HOST_FP_REG_ARG4      4
#define RVJIT_HOST_FP_REG_ARG5      5
#define RVJIT_HOST_FP_REG_ARG6      6
#define RVJIT_HOST_FP_REG_ARG7      7

#define RVJIT_HOST_FP_REG_RET0      0
#define RVJIT_HOST_FP_REG_RET1      1
#define RVJIT_HOST_FP_REG_RET2      2
#define RVJIT_HOST_FP_REG_RET3      3
#define RVJIT_HOST_FP_REG_RET4      4
#define RVJIT_HOST_FP_REG_RET5      5
#define RVJIT_HOST_FP_REG_RET6      6
#define RVJIT_HOST_FP_REG_RET7      7

/******************************************************************************

    NOTE: Interaction with X31 as SP or XZR differs by instruction
    TLDR: Immediate / extended register instructions are NOT XZR-safe!

# ADRP takes XZR as destination
adrp xzr, 0

# MOVZ, MOVK, MOVN take XZR as destination
movz xzr, 0x1337, lsl 16
movk xzr, 0, lsl 32

# 3-operand shifted register instructions take XZR as destination and as source
add xzr, x0, x0, lsl 16
adds xzr, xzr, xzr, lsl 16
sub x0, xzr, x0, lsl 16
and xzr, x0, x0, lsl 16
eon x0, x0, xzr, lsl 16
udiv xzr, x0, x0
rorv x0, xzr, x0
csel xzr, x0, x0, eq
smulh x0, x0, xzr
extr x0, xzr, x0, 63

# ADDE, SUBE take SP as rd/rs1, but not as rs2; Useful for rvjit_host_addisp() lowering
add sp, sp, xzr, uxtx
add x0, sp, x0, uxtx
sub sp, x0, x0, uxtx

# ADDSE, SUBSE take XZR as rd/rs2, SP as rs1; I guess for SP comparisons?
adds xzr, sp, xzr, uxtx
adds xzr, sp, x0, uxtx
subs xzr, sp, xzr, uxtx

# ADDI, SUBI move SP<->GPR freely and lack XZR
add x0, sp, #0
add sp, x0, #0

# ADDSI, SUBSI take XZR as destination (To become CMP), move SP->GPR otherwise (???)
adds xzr, x0, #0
adds x0, sp, #0
subs xzr, x0, #0
subs x0, sp, #0

# ANDI, ORRI, EORI take XZR as source, move GPR->SP otherwise (???)
and sp, x0, #1
and x0, xzr, #1
eor sp, xzr, #1

# ANDSI takes XZR as destination (To become CMP) and as source
ands xzr, x0, #1
ands x0, xzr, #1

# BFM, SBFM, UBFM take XZR as destination and as source
bfm xzr, xzr, 0, 0
sbfm xzr, xzr, 31, 31
ubfm xzr, xzr, 0, 31

# 2-operand instructions take XZR as destination and as source
rbit x0, xzr
clz xzr, x0
rev xzr, xzr

# Load/Store instructions take XZR as register, SP as the memory base (Makes sense)
strb wzr, [sp, #0xFFF]
ldrb wzr, [sp, #0xFFF]
ldxr xzr, [sp]

# Branch instructions take XZR as register
br xzr
blr xzr
cbz xzr, 0
cbnz xzr, 0

******************************************************************************/

/*
 * Low-level instruction encoding
 */

static inline void rvjit_arm64_emit_insn(rvjit_block_t* block, uint32_t insn)
{
    write_uint32_le(&insn, insn);
    rvjit_put_code(block, &insn, sizeof(insn));
}

#define ARM64_WIDE_MASK 0x80000000U

static inline bool rvjit_arm64_insn_is_wide(uint32_t insn)
{
    return !!(insn & ARM64_WIDE_MASK);
}

/*
 * Conditional codes
 */

#define ARM64_CC_EQ 0x00 // Equal
#define ARM64_CC_NE 0x01 // Not equal
#define ARM64_CC_HS 0x02 // Higher or same (unsigned >=)
#define ARM64_CC_LO 0x03 // Lower (unsigned <)
#define ARM64_CC_MI 0x04 // Negative
#define ARM64_CC_PL 0x05 // Positive or zero
#define ARM64_CC_VS 0x06 // Overflow
#define ARM64_CC_VC 0x07 // No overflow
#define ARM64_CC_HI 0x08 // Higher (unsigned >)
#define ARM64_CC_LS 0x09 // Lower or same (unsigned <=)
#define ARM64_CC_GE 0x0A // Signed >=
#define ARM64_CC_LT 0x0B // Signed <
#define ARM64_CC_GT 0x0C // Signed >
#define ARM64_CC_LE 0x0D // Signed <=
#define ARM64_CC_AL 0x0E // Always true
#define ARM64_CC_NV 0x0F // Never true

// Invert condition code meaning
static inline uint32_t rvjit_arm64_invert_cc(uint32_t cc)
{
    return cc ^ 1;
}

/*
 * Misc instructions
 */

#define ARM64_INSN_ADR  0x10000000U // ADR  x[4:0] = PC + (s32)(immhi[23:5]:immlo[30:29])
#define ARM64_INSN_ADRP 0x90000000U // ADRP x[4:0] = (PC[63:12] << 12) + (s32)(immhi[23:5]:immlo[30:29] << 12)
#define ARM64_INSN_PUSH 0xF81F0FE0U // STR  x[4:0], [sp, #-16]!
#define ARM64_INSN_POP  0xF84107E0U // LDR  x[4:0], [sp] # 16
#define ARM64_INSN_RET  0xD65F03C0U // RET

// Simple instruction with single register x[4:0]
static inline void rvjit_arm64_insn_reg(rvjit_block_t* block, uint32_t insn, uint32_t reg)
{
    rvjit_arm64_emit_insn(block, insn | (reg & 31));
}

/*
 * Loading immediates
 */

#define ARM64_INSN_MOVN  0x92800000U // Move (imm16[20:5] << (hw[22:21] << 4)) -> x[4:0], inverted
#define ARM64_INSN_MOVNW 0x12800000U // Move (imm16[20:5] << (hw[22:21] << 4)) -> w[4:0], inverted
#define ARM64_INSN_MOVZ  0xD2800000U // Move (imm16[20:5] << (hw[22:21] << 4)) -> x[4:0], zero other bits
#define ARM64_INSN_MOVZW 0x52800000U // Move (imm16[20:5] << (hw[22:21] << 4)) -> w[4:0], zero other bits
#define ARM64_INSN_MOVK  0xF2800000U // Move (imm16[20:5] << (hw[22:21] << 4)) -> x[4:0], keep other bits
#define ARM64_INSN_MOVKW 0x72800000U // Move (imm16[20:5] << (hw[22:21] << 4)) -> w[4:0], keep other bits

static inline void rvjit_arm64_insn_mov(rvjit_block_t* block, uint32_t insn, uint32_t reg, uint32_t imm16, uint32_t hw)
{
    rvjit_arm64_emit_insn(block, insn | (reg & 31) | ((imm16 & 0xFFFFU) << 5) | ((hw & 3) << 21));
}

/*
 * Register-register, 3 operands with optional shift immediate
 */

// Shift immediate modes
#define ARM64_SHIFT_LSL  0x00 // Shift mode: Logical shift left
#define ARM64_SHIFT_LSR  0x01 // Shift mode: Logical shift right
#define ARM64_SHIFT_ASR  0x02 // Shift mode: Arithmetic shift right
#define ARM64_SHIFT_ROR  0x03 // Shift mode: Rotate right (NOTE: Logical operations only!)

// Integer arithmetic with optional shift immediate
#define ARM64_INSN_ADD   0x8B000000U // Add x[4:0] = x[9:5] + (x[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_ADDW  0x0B000000U // Add w[4:0] = w[9:5] + (w[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_ADDS  0xAB000000U // Add x[4:0] = x[9:5] + (x[20:16] << imm6[15:10] sh[23:22]), set flags
#define ARM64_INSN_ADDSW 0x2B000000U // Add w[4:0] = w[9:5] + (w[20:16] << imm6[15:10] sh[23:22]), set flags
#define ARM64_INSN_SUB   0xCB000000U // Sub x[4:0] = x[9:5] - (x[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_SUBW  0x4B000000U // Sub w[4:0] = w[9:5] - (w[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_SUBS  0xEB000000U // Sub x[4:0] = x[9:5] - (x[20:16] << imm6[15:10] sh[23:22]), set flags
#define ARM64_INSN_SUBSW 0x6B000000U // Sub w[4:0] = w[9:5] - (w[20:16] << imm6[15:10] sh[23:22]), set flags

// Integer arithmetic with extended and scaled register
// NOTE: Those may take SP as x[4:0] or x[9:5], but XZR as x[20:16]
#define ARM64_INSN_ADDE  0x8B206000U
#define ARM64_INSN_SUBE  0xCB206000U

// Integer logical with optional shift/rotation immediate
#define ARM64_INSN_AND   0x8A000000U // AND  x[4:0] = x[9:5] & (x[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_ANDW  0x0A000000U // AND  w[4:0] = w[9:5] & (w[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_ORR   0xAA000000U // OR   x[4:0] = x[9:5] | (x[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_ORRW  0x2A000000U // OR   w[4:0] = w[9:5] | (w[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_EOR   0xCA000000U // XOR  x[4:0] = x[9:5] ^ (x[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_EORW  0x4A000000U // XOR  w[4:0] = w[9:5] ^ (w[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_ANDS  0xEA000000U // AND  x[4:0] = x[9:5] & (x[20:16] << imm6[15:10] sh[23:22]), set flags
#define ARM64_INSN_ANDSW 0x6A000000U // AND  w[4:0] = w[9:5] & (w[20:16] << imm6[15:10] sh[23:22]), set flags

#define ARM64_INSN_BIC   0x8A200000U // ANDN x[4:0] = x[9:5] & ~(x[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_BICW  0x0A200000U // ANDN w[4:0] = w[9:5] & ~(w[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_ORN   0xAA200000U // ORN  x[4:0] = x[9:5] | ~(x[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_ORNW  0x2A200000U // ORN  w[4:0] = w[9:5] | ~(w[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_EON   0xCA200000U // XNOR x[4:0] = x[9:5] ^ ~(x[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_EONW  0x4A200000U // XNOR w[4:0] = w[9:5] ^ ~(w[20:16] << imm6[15:10] sh[23:22])
#define ARM64_INSN_BICS  0xEA000000U // ANDN x[4:0] = x[9:5] & ~(x[20:16] << imm6[15:10] sh[23:22]), set flags
#define ARM64_INSN_BICSW 0x6A000000U // ANDN w[4:0] = w[9:5] & ~(w[20:16] << imm6[15:10] sh[23:22]), set flags

// Emit register-register 3-operand instruction, shifting rs2 by imm6 with mode sh
static inline void rvjit_arm64_insn_reg3_shift(rvjit_block_t* block, uint32_t insn, uint32_t rd, //
                                               uint32_t rs1, uint32_t rs2, uint32_t imm6, uint32_t sh)
{
    rvjit_arm64_emit_insn(block, insn | (rd & 31) | ((rs1 & 31) << 5) | ((rs2 & 31) << 16) //
                                     | ((imm6 & 63) << 10) | ((sh & 3) << 22));
}

// Integer
#define ARM64_INSN_UDIV    0x9AC00800U // UDIV x[4:0] = x[9:5] / x[20:16]
#define ARM64_INSN_UDIVW   0x1AC00800U // UDIV w[4:0] = w[9:5] / w[20:16]
#define ARM64_INSN_SDIV    0x9AC00C00U // SDIV x[4:0] = x[9:5] / x[20:16]
#define ARM64_INSN_SDIVW   0x1AC00C00U // SDIV w[4:0] = w[9:5] / w[20:16]
#define ARM64_INSN_LSLV    0x9AC02000U // SHL  x[4:0] = x[9:5] << x[20:16]
#define ARM64_INSN_LSLVW   0x1AC02000U // SHL  w[4:0] = w[9:5] << w[20:16]
#define ARM64_INSN_LSRV    0x9AC02400U // SHR  x[4:0] = x[9:5] >> x[20:16]
#define ARM64_INSN_LSRVW   0x1AC02400U // SHR  w[4:0] = w[9:5] >> w[20:16]
#define ARM64_INSN_ASRV    0x9AC02800U // SAR  x[4:0] = x[9:5] >> x[20:16]
#define ARM64_INSN_ASRVW   0x1AC02800U // SAR  w[4:0] = w[9:5] >> w[20:16]
#define ARM64_INSN_RORV    0x9AC02C00U // ROR  x[4:0] = ror(x[9:5], x[20:16])
#define ARM64_INSN_RORVW   0x1AC02C00U // ROR  w[4:0] = ror(w[9:5], w[20:16])

#define ARM64_INSN_SMULH   0x9B407C00U // SMULH x[4:0] = bit_mulh64(x[9:5], x[20:16])
#define ARM64_INSN_UMULH   0x9BC07C00U // UMULH x[4:0] = bit_mulhu64(x[9:5], x[20:16])

// Floating-point
#define ARM64_INSN_FMULS   0x1E200800U // FMUL s[4:0] = s[9:5] * s[20:16]
#define ARM64_INSN_FMULD   0x1E600800U // FMUL d[4:0] = d[9:5] * d[20:16]
#define ARM64_INSN_FDIVS   0x1E201800U // FDIV s[4:0] = s[9:5] / s[20:16]
#define ARM64_INSN_FDIVD   0x1E601800U // FDIV d[4:0] = d[9:5] / d[20:16]
#define ARM64_INSN_FADDS   0x1E202800U // FADD s[4:0] = s[9:5] + s[20:16]
#define ARM64_INSN_FADDD   0x1E602800U // FADD d[4:0] = d[9:5] + d[20:16]
#define ARM64_INSN_FSUBS   0x1E203800U // FSUB s[4:0] = s[9:5] - s[20:16]
#define ARM64_INSN_FSUBD   0x1E603800U // FSUB d[4:0] = d[9:5] - d[20:16]
#define ARM64_INSN_FMAXS   0x1E204800U // FMAX s[4:0] = fmax(s[9:5], s[20:16])
#define ARM64_INSN_FMAXD   0x1E604800U // FMAX d[4:0] = fmax(d[9:5], d[20:16])
#define ARM64_INSN_FMINS   0x1E205800U // FMIN s[4:0] = fmin(s[9:5], s[20:16])
#define ARM64_INSN_FMIND   0x1E605800U // FMIN d[4:0] = fmin(d[9:5], d[20:16])
#define ARM64_INSN_FMAXNMS 0x1E206800U // FMAXNM s[4:0] = fmax_or_nonnan(s[9:5], s[20:16])
#define ARM64_INSN_FMAXNMD 0x1E606800U // FMAXNM d[4:0] = fmax_or_nonnan(d[9:5], d[20:16])
#define ARM64_INSN_FMINNMS 0x1E207800U // FMINNM s[4:0] = fmin_or_nonnan(s[9:5], s[20:16])
#define ARM64_INSN_FMINNMD 0x1E607800U // FMINNM d[4:0] = fmin_or_nonnan(d[9:5], d[20:16])

// Emit register-register 3-operand instruction
static inline void rvjit_arm64_insn_reg3(rvjit_block_t* block, uint32_t insn, uint32_t rd, //
                                         uint32_t rs1, uint32_t rs2)
{
    rvjit_arm64_emit_insn(block, insn | (rd & 31) | ((rs1 & 31) << 5) | ((rs2 & 31) << 16));
}

// Integer conditional select
#define ARM64_INSN_CSEL   0x9A800000U // CSEL  x[4:0] = cond[15:12] ? x[9:5] : x[20:16]
#define ARM64_INSN_CSELW  0x1A800000U // CSEL  w[4:0] = cond[15:12] ? w[9:5] : w[20:16]
#define ARM64_INSN_CSINC  0x9A800400U // CSINC x[4:0] = cond[15:12] ? x[9:5] : (x[20:16] + 1)
#define ARM64_INSN_CSINCW 0x1A800400U // CSINC w[4:0] = cond[15:12] ? w[9:5] : (w[20:16] + 1)
#define ARM64_INSN_CSINV  0xDA800000U // CSINV x[4:0] = cond[15:12] ? x[9:5] : ~x[20:16]
#define ARM64_INSN_CSINVW 0x5A800000U // CSINV w[4:0] = cond[15:12] ? w[9:5] : ~w[20:16]
#define ARM64_INSN_CSNEG  0xDA800400U // CSNEG x[4:0] = cond[15:12] ? x[9:5] : -x[20:16]
#define ARM64_INSN_CSNEGW 0x5A800400U // CSNEG w[4:0] = cond[15:12] ? w[9:5] : -w[20:16]

// Floating-point conditional select
#define ARM64_INSN_FCSELS 0x1E200C00U // FCSEL s[4:0] = cond[15:12] ? s[9:5] : s[20:16]
#define ARM64_INSN_FCSELD 0x1E600C00U // FCSEL d[4:0] = cond[15:12] ? d[9:5] : d[20:16]

// Emit conditional select instruction based on compare flags and condition code (cc)
static inline void rvjit_arm64_insn_csel(rvjit_block_t* block, uint32_t insn, uint32_t rd, //
                                         uint32_t rs1, uint32_t rs2, uint32_t cc)
{
    rvjit_arm64_emit_insn(block, insn | (rd & 31) | ((rs1 & 31) << 5) | ((rs2 & 31) << 16) | ((cc & 0xF) << 12));
}

/*
 * Register-register comparisons
 */

// Integer
#define ARM64_INSN_CMP    0xEB00001FU // CMP x[9:5] == x[20:16], same as SUBS xzr, rs1, rs2
#define ARM64_INSN_CMPW   0x6B00001FU // CMP w[9:5] == w[20:16], same as SUBS wzr, rs1, rs2
#define ARM64_INSN_CMN    0xAB00001FU // CMN x[9:5] != x[20:16], same as ADDS xzr, rs1, rs2
#define ARM64_INSN_CMNW   0x2B00001FU // CMN w[9:5] != w[20:16], same as ADDS wzr, rs1, rs2

// Floating-point
#define ARM64_INSN_FCMPS  0x1E202000U // FCMP (quiet) s[9:5] == s[20:16]
#define ARM64_INSN_FCMPD  0x1E602000U // FCMP (quiet) d[9:5] == d[20:16]
#define ARM64_INSN_FCMPES 0x1E202010U // FCMP (signaling) s[9:5] == s[20:16]
#define ARM64_INSN_FCMPED 0x1E602010U // FCMP (signaling) d[9:5] == d[20:16]

// Emit compare instruction, compare flags are used by csel / b.cc
static inline void rvjit_arm64_insn_cmp_reg(rvjit_block_t* block, uint32_t insn, uint32_t rs1, uint32_t rs2)
{
    rvjit_arm64_emit_insn(block, insn | ((rs1 & 31) << 5) | ((rs2 & 31) << 16));
}

/*
 * Register-immediate, 3 operands with immediate
 */

#define ARM64_INSN_EXTR   0x93800000U // EXTR x[4:0] = (x[9:5] >> imms[15:10]) | (x[20:16] << ~imms[15:10])
#define ARM64_INSN_EXTRW  0x13800000U // EXTR w[4:0] = (w[9:5] >> imms[14:10]) | (w[20:16] << ~imms[14:10])

/*
 * Register-immediate, 2 operands with immediate
 */

// Integer arithmetic
#define ARM64_INSN_ADDI   0x91000000U // Add x[4:0] = x(9:5) + (imm12[21:10] << (sh[22] ? 12 : 0))
#define ARM64_INSN_ADDIW  0x11000000U // Add w[4:0] = w(9:5) + (imm12[21:10] << (sh[22] ? 12 : 0))
#define ARM64_INSN_ADDSI  0xB1000000U // Add x[4:0] = x(9:5) + (imm12[21:10] << (sh[22] ? 12 : 0)), set flags
#define ARM64_INSN_ADDSIW 0x31000000U // Add w[4:0] = w(9:5) + (imm12[21:10] << (sh[22] ? 12 : 0)), set flags
#define ARM64_INSN_SUBI   0xD1000000U // Sub x[4:0] = x(9:5) - (imm12[21:10] << (sh[22] ? 12 : 0))
#define ARM64_INSN_SUBIW  0x51000000U // Sub w[4:0] = w(9:5) - (imm12[21:10] << (sh[22] ? 12 : 0))
#define ARM64_INSN_SUBSI  0xF1000000U // Sub x[4:0] = x(9:5) - (imm12[21:10] << (sh[22] ? 12 : 0)), set flags
#define ARM64_INSN_SUBSIW 0x71000000U // Sub w[4:0] = w(9:5) - (imm12[21:10] << (sh[22] ? 12 : 0)), set flags

static inline void rvjit_arm64_insn_arith_reg2_imm(rvjit_block_t* block, uint32_t insn, uint32_t rd, //
                                                   uint32_t rs1, uint32_t imm12, uint32_t sh)
{
    rvjit_arm64_emit_insn(block, insn | (rd & 31) | ((rs1 & 31) << 5) | ((imm12 & 0xFFFU) << 10) | ((sh & 1) << 22));
}

// Convert ADDI->ADD, SUBSI->SUBS, etc
static inline uint32_t rvjit_arm64_arith_imm_to_reg(uint32_t insn)
{
    return insn - 0x06000000U;
}

// Convert ADDI->ADDE, SUBI->SUBE, etc
// NOTE: Those instruction access X31 as SP!
static inline uint32_t rvjit_arm64_arith_imm_to_reg_ext(uint32_t insn)
{
    return insn - 0x05DFA000U;
}

// Convert ADDI->SUBI, SUBSI->ADDSI, ADDW->SUBW, SUBE->ADDE etc
static inline uint32_t rvjit_arm64_arith_invert(uint32_t insn)
{
    return insn ^ 0x40000000U;
}

// Integer logical
#define ARM64_INSN_ANDI   0x92400000U // AND x[4:0] = x(9:5) & decode(imms[15:10], immr[21:16])
#define ARM64_INSN_ANDIW  0x12000000U // AND w[4:0] = w(9:5) & decode(imms[15:10], immr[21:16])
#define ARM64_INSN_ORRI   0xB2400000U // OR  x[4:0] = x(9:5) | decode(imms[15:10], immr[21:16])
#define ARM64_INSN_ORRIW  0x32000000U // OR  w[4:0] = w(9:5) | decode(imms[15:10], immr[21:16])
#define ARM64_INSN_EORI   0xD2400000U // XOR x[4:0] = x(9:5) ^ decode(imms[15:10], immr[21:16])
#define ARM64_INSN_EORIW  0x52000000U // XOR w[4:0] = w(9:5) ^ decode(imms[15:10], immr[21:16])
#define ARM64_INSN_ANDSI  0xF2400000U // AND x[4:0] = x(9:5) & decode(imms[15:10], immr[21:16]), set flags
#define ARM64_INSN_ANDSIW 0x72000000U // AND w[4:0] = w(9:5) & decode(imms[15:10], immr[21:16]), set flags

// Integer bitfield
#define ARM64_INSN_SBFM   0x93400000U // SBFM x[4:0] = sbfm(x[9:5], immr[21:16], imms[15:10])
#define ARM64_INSN_SBFMW  0x13000000U // SBFM w[4:0] = sbfm(w[9:5], immr[20:16], imms[14:10])
#define ARM64_INSN_BFM    0xB3400000U // BFM  x[4:0] = bfm(x[9:5],  immr[21:16], imms[15:10])
#define ARM64_INSN_BFMW   0x33000000U // BFM  w[4:0] = bfm(w[9:5],  immr[20:16], imms[14:10])
#define ARM64_INSN_UBFM   0xD3400000U // UBFM x[4:0] = ubfm(x[9:5], immr[21:16], imms[15:10])
#define ARM64_INSN_UBFMW  0x53000000U // UBFM w[4:0] = ubfm(w[9:5], immr[20:16], imms[14:10])

// Emit 64-bit logical instruction with immediate
static inline void rvjit_arm64_insn_logic_reg2_immx(rvjit_block_t* block, uint32_t insn, uint32_t rd, //
                                                    uint32_t rs1, uint32_t immr, uint32_t imms)
{
    rvjit_arm64_emit_insn(block, insn | (rd & 31) | ((rs1 & 31) << 5) | ((imms & 63) << 10) | ((immr & 63) << 16));
}

// Emit 32-bit logical instruction with immediate
static inline void rvjit_arm64_insn_logic_reg2_immw(rvjit_block_t* block, uint32_t insn, uint32_t rd, //
                                                    uint32_t rs1, uint32_t immr, uint32_t imms)
{
    rvjit_arm64_emit_insn(block, insn | (rd & 31) | ((rs1 & 31) << 5) | ((imms & 31) << 10) | ((immr & 31) << 16));
}

// Emit logical instruction with immediate
static inline void rvjit_arm64_insn_logic_reg2_imm(rvjit_block_t* block, uint32_t insn, uint32_t rd, //
                                                   uint32_t rs1, uint32_t immr, uint32_t imms)
{
    if (rvjit_arm64_insn_is_wide(insn)) {
        rvjit_arm64_insn_logic_reg2_immx(block, insn, rd, rs1, immr, imms);
    } else {
        rvjit_arm64_insn_logic_reg2_immw(block, insn, rd, rs1, immr, imms);
    }
}

// Convert EORI->EOR, ANDSI->ANDS, etc
static inline uint32_t rvjit_arm64_logic_imm_to_reg(uint32_t insn)
{
    return insn - 0x08000000U;
}

/*
 * Register-immediate comparisons
 */

#define ARM64_INSN_CMPI  0xF100001FU // CMP x[9:5] == imm12[21:10], same as SUBSI xzr, rs, imm
#define ARM64_INSN_CMPIW 0x7100001FU // CMP w[9:5] == imm12[21:10], same as SUBSI wzr, rs, imm
#define ARM64_INSN_CMNI  0xB100001FU // CMN x[9:5] != imm12[21:10], same as ADDSI xzr, rs, imm
#define ARM64_INSN_CMNIW 0x3100001FU // CMN w[9:5] != imm12[21:10], same as ADDSI wzr, rs, imm

// Convert CMPIW->CMPW, etc
static inline uint32_t rvjit_arm64_cmp_imm_to_reg(uint32_t insn)
{
    return insn - 0x08000000U;
}

/*
 * Register-register, 2 operands
 */

// Integer
#define ARM64_INSN_RBIT     0xDAC00000U // RBIT x[4:0] = reverse_bits(x[9:5])
#define ARM64_INSN_RBITW    0x5AC00000U // RBIT w[4:0] = reverse_bits(w[9:5])
#define ARM64_INSN_CLZ      0xDAC01000U // CLZ  x[4:0] = bit_clz64(x[9:5])
#define ARM64_INSN_CLZW     0x5AC01000U // CLZ  w[4:0] = bit_clz32(w[9:5])
#define ARM64_INSN_REV      0xDAC00C00U // REV8 x[4:0] = bit_bswap64(x[9:5])
#define ARM64_INSN_REVW     0x5AC00C00U // REV8 w[4:0] = bit_bswap32(w[9:5])

// Integer-Float
#define ARM64_INSN_FMOVWS   0x1E260000U // FMV w[4:0] = s[9:5]
#define ARM64_INSN_FMOVXD   0x9E660000U // FMV x[4:0] = d[9:5]
#define ARM64_INSN_FMOVSW   0x1E270000U // FMV s[4:0] = w[9:5]
#define ARM64_INSN_FMOVDX   0x9E670000U // FMV d[4:0] = x[9:5]

#define ARM64_INSN_FCVTZSWS 0x1E380000U // FCVT w[4:0] = (i32)s[9:5]
#define ARM64_INSN_FCVTZSXS 0x9E380000U // FCVT x[4:0] = (i64)s[9:5]
#define ARM64_INSN_FCVTZSWD 0x1E780000U // FCVT w[4:0] = (i32)d[9:5]
#define ARM64_INSN_FCVTZSXD 0x9E780000U // FCVT x[4:0] = (i64)d[9:5]

#define ARM64_INSN_FCVTZUWS 0x1E390000U // FCVT w[4:0] = (u32)s[9:5]
#define ARM64_INSN_FCVTZUXS 0x9E390000U // FCVT x[4:0] = (u64)s[9:5]
#define ARM64_INSN_FCVTZUWD 0x1E790000U // FCVT w[4:0] = (u32)d[9:5]
#define ARM64_INSN_FCVTZUXD 0x9E790000U // FCVT x[4:0] = (u64)d[9:5]

#define ARM64_INSN_SCVTFSW  0x1E220000U // FCVT s[4:0] = (i32)w[9:5]
#define ARM64_INSN_SCVTFSX  0x9E220000U // FCVT s[4:0] = (i64)x[9:5]
#define ARM64_INSN_SCVTFDW  0x1E620000U // FCVT d[4:0] = (i32)w[9:5]
#define ARM64_INSN_SCVTFDX  0x9E620000U // FCVT d[4:0] = (i64)x[9:5]

#define ARM64_INSN_UCVTFSW  0x1E230000U // FCVT s[4:0] = (u32)w[9:5]
#define ARM64_INSN_UCVTFSX  0x9E230000U // FCVT s[4:0] = (u64)x[9:5]
#define ARM64_INSN_UCVTFDW  0x1E630000U // FCVT d[4:0] = (u32)w[9:5]
#define ARM64_INSN_UCVTFDX  0x9E630000U // FCVT d[4:0] = (u64)x[9:5]

// Floating-point
#define ARM64_INSN_FCVTDS   0x1E22C000U // FCVT d[4:0] = s[9:5]
#define ARM64_INSN_FCVTSD   0x1E624000U // FCVT s[4:0] = d[9:5]

#define ARM64_INSN_FABSS    0x1E20C000U // FABS  s[4:0] = fabs(s[9:5])
#define ARM64_INSN_FABSD    0x1E60C000U // FABS  d[4:0] = fabs(d[9:5])
#define ARM64_INSN_FNEGS    0x1E214000U // FNEG  s[4:0] = -s[9:5]
#define ARM64_INSN_FNEGD    0x1E614000U // FNEG  d[4:0] = -d[9:5]
#define ARM64_INSN_FSQRTS   0x1E21C000U // FSQRT s[4:0] = sqrt(s[9:5])
#define ARM64_INSN_FSQRTD   0x1E61C000U // FSQRT d[4:0] = sqrt(d[9:5])

static inline void rvjit_arm64_insn_reg2(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs)
{
    rvjit_arm64_emit_insn(block, insn | (rd & 31) | ((rs & 31) << 5));
}

/*
 * Register-register, 4 operands
 */

// Integer
#define ARM64_INSN_MADD    0x9B000000U // MADD x[4:0] = x[9:5] * x[20:16] + x[14:10]
#define ARM64_INSN_MADDW   0x1B000000U // MADD w[4:0] = w[9:5] * w[20:16] + w[14:10]
#define ARM64_INSN_MSUB    0x9B008000U // MSUB x[4:0] = x[9:5] * x[20:16] - x[14:10]
#define ARM64_INSN_MSUBW   0x1B008000U // MSUB w[4:0] = w[9:5] * w[20:16] - w[14:10]
#define ARM64_INSN_SMADDL  0x9B200000U // MADD x[4:0] = ((s32)w[9:5] * (s32)w[20:16]) + x[14:10]
#define ARM64_INSN_SMSUBL  0x9B208000U // MSUB x[4:0] = ((s32)w[9:5] * (s32)w[20:16]) - x[14:10]
#define ARM64_INSN_UMADDL  0x9B200000U // MADD x[4:0] = ((u32)w[9:5] * (u32)w[20:16]) + x[14:10]
#define ARM64_INSN_UMSUBL  0x9B208000U // MSUB x[4:0] = ((u32)w[9:5] * (u32)w[20:16]) - x[14:10]

// Floating-point
#define ARM64_INSN_FMADDS  0x1F000000U // FMADD s[4:0] = s[9:5] * s[20:16] + s[14:10]
#define ARM64_INSN_FMADDD  0x1F400000U // FMADD d[4:0] = d[9:5] * d[20:16] + d[14:10]
#define ARM64_INSN_FMSUBS  0x1F008000U // FMSUB s[4:0] = s[9:5] * s[20:16] - s[14:10]
#define ARM64_INSN_FMSUBD  0x1F408000U // FMSUB d[4:0] = d[9:5] * d[20:16] - d[14:10]
#define ARM64_INSN_FNMADDS 0x1F200000U // FNMADD s[4:0] = -(s[9:5] * s[20:16] + s[14:10])
#define ARM64_INSN_FNMADDD 0x1F600000U // FNMADD d[4:0] = -(d[9:5] * d[20:16] + d[14:10])
#define ARM64_INSN_FNMSUBS 0x1F208000U // FNMSUB s[4:0] = -(s[9:5] * s[20:16] - s[14:10])
#define ARM64_INSN_FNMSUBD 0x1F608000U // FNMSUB d[4:0] = -(d[9:5] * d[20:16] - d[14:10])

// Emit register-register 4-operand instruction
static inline void rvjit_arm64_insn_reg4(rvjit_block_t* block, uint32_t insn, uint32_t rd, //
                                         uint32_t rs1, uint32_t rs2, uint32_t rs3)
{
    rvjit_arm64_emit_insn(block, insn | (rd & 31) | ((rs1 & 31) << 5) | ((rs2 & 31) << 16) | ((rs3 & 31) << 10));
}

/*
 * Load & Store
 */

// Integer, scaled unsigned offset
#define ARM64_INSN_STRB  0x39000000U // Store  ((u8*)x[9:5])[imm12[21:10]] = w[4:0]
#define ARM64_INSN_STRH  0x79000000U // Store ((u16*)x[9:5])[imm12[21:10]] = w[4:0]
#define ARM64_INSN_STRW  0xB9000000U // Store ((u32*)x[9:5])[imm12[21:10]] = w[4:0]
#define ARM64_INSN_STRD  0xF9000000U // Store ((u64*)x[9:5])[imm12[21:10]] = x[4:0]

#define ARM64_INSN_LDRB  0x39400000U // Load w[4:0] =  ((u8*)x[9:5])[imm12[21:10]]
#define ARM64_INSN_LDRSB 0x39800000U // Load x[4:0] =  ((s8*)x[9:5])[imm12[21:10]]
#define ARM64_INSN_LDRH  0x79400000U // Load w[4:0] = ((u16*)x[9:5])[imm12[21:10]]
#define ARM64_INSN_LDRSH 0x79800000U // Load x[4:0] = ((s16*)x[9:5])[imm12[21:10]]
#define ARM64_INSN_LDRW  0xB9400000U // Load w[4:0] = ((u32*)x[9:5])[imm12[21:10]]
#define ARM64_INSN_LDRSW 0xB9800000U // Load x[4:0] = ((s32*)x[9:5])[imm12[21:10]]
#define ARM64_INSN_LDRD  0xF9400000U // Load x[4:0] = ((u64*)x[9:5])[imm12[21:10]]

// Floating-point, scaled unsigned offset
#define ARM64_INSN_STRFS 0xBD000000U // Store ((f32*)x[9:5])[imm12[21:10]] = s[4:0]
#define ARM64_INSN_STRFD 0xFD000000U // Store ((f64*)x[9:5])[imm12[21:10]] = d[4:0]

#define ARM64_INSN_LDRFS 0xBD400000U // Load s[4:0] = ((f32*)x[9:5])[imm12[21:10]]
#define ARM64_INSN_LDRFD 0xFD400000U // Load d[4:0] = ((f64*)x[9:5])[imm12[21:10]]

// Load / store with a scaled unsigned imm12 offset
static inline void rvjit_arm64_insn_ldst(rvjit_block_t* block, uint32_t insn, uint32_t rd, //
                                         uint32_t base, uint32_t imm12)
{
    rvjit_arm64_emit_insn(block, insn | (rd & 31) | ((base & 31) << 5) | ((imm12 & 0xFFFU) << 10));
}

// Check whether the load/store instruction is a load
static inline bool rvjit_arm64_ldst_is_load(uint32_t insn)
{
    return !!(insn & 0x00C00000U);
}

static inline bool rvjit_arm64_ldst_is_fp(uint32_t insn)
{
    return !!(insn & 0x04000000U);
}

// Integer, unscaled signed offset
#define ARM64_INSN_STURB  0x38000000U // Store  *(u8*)(x[9:5] + simm9[20:12]) = w[4:0]
#define ARM64_INSN_STURH  0x78000000U // Store *(u16*)(x[9:5] + simm9[20:12]) = w[4:0]
#define ARM64_INSN_STURW  0xB8000000U // Store *(u32*)(x[9:5] + simm9[20:12]) = w[4:0]
#define ARM64_INSN_STURD  0xF8000000U // Store *(u64*)(x[9:5] + simm9[20:12]) = x[4:0]

#define ARM64_INSN_LDURB  0x38400000U // Load w[4:0] =  *(u8*)(x[9:5] + simm9[20:12])
#define ARM64_INSN_LDURSB 0x38800000U // Load x[4:0] =  *(s8*)(x[9:5] + simm9[20:12])
#define ARM64_INSN_LDURH  0x78400000U // Load w[4:0] = *(u16*)(x[9:5] + simm9[20:12])
#define ARM64_INSN_LDURSH 0x78800000U // Load x[4:0] = *(s16*)(x[9:5] + simm9[20:12])
#define ARM64_INSN_LDURW  0xB8400000U // Load w[4:0] = *(u32*)(x[9:5] + simm9[20:12])
#define ARM64_INSN_LDURSW 0xB8800000U // Load x[4:0] = *(s32*)(x[9:5] + simm9[20:12])
#define ARM64_INSN_LDURD  0xF8400000U // Load x[4:0] = *(u64*)(x[9:5] + simm9[20:12])

// Floating-point, unscaled signed offset
#define ARM64_INSN_STURFS 0xBC000000U // Store *(f32*)(x[9:5] + simm9[20:12]) = s[4:0]
#define ARM64_INSN_STURFD 0xFC000000U // Store *(f64*)(x[9:5] + simm9[20:12]) = d[4:0]

#define ARM64_INSN_LDURFS 0xBC400000U // Load s[4:0] = *(f32*)(x[9:5] + simm9[20:12])
#define ARM64_INSN_LDURFD 0xFC400000U // Load d[4:0] = *(f64*)(x[9:5] + simm9[20:12])

// Convert STRB->STURB, LDRFD->LDURFD, etc
static inline uint32_t rvjit_arm64_ldst_to_unscaled(uint32_t insn)
{
    // Scaled and unscaled base opcodes differ only in bit 24.
    // Subtracting 0x09000000U would also clear the fixed opcode bit 27.
    return insn & ~0x01000000U;
}

// Load/store with an unscaled signed imm9 offset
static inline void rvjit_arm64_insn_ldst_unscaled(rvjit_block_t* block, uint32_t insn, uint32_t rd, //
                                                  uint32_t base, uint32_t imm9)
{
    rvjit_arm64_emit_insn(block, insn | (rd & 31) | ((base & 31) << 5) | ((imm9 & 0x1FFU) << 12));
}

/*
 * Atomics
 */

#define ARM64_INSN_DMB_ISHLD 0xD50339BFU // LoadLoad fence (ACQUIRE)
#define ARM64_INSN_DMB_ISH   0xD5033BBFU // Full fence (SEQ_CST)

#define ARM64_INSN_LDXRB     0x085F7C00U // Load exclusive w[4:0] = load_reserve_u8(x[9:5])
#define ARM64_INSN_LDXRH     0x485F7C00U // Load exclusive w[4:0] = load_reserve_u16(x[9:5])
#define ARM64_INSN_LDXRW     0x885F7C00U // Load exclusive w[4:0] = load_reserve_u32(x[9:5])
#define ARM64_INSN_LDXRD     0xC85F7C00U // Load exclusive x[4:0] = load_reserve_u64(x[9:5])

#define ARM64_INSN_STXRB     0x08007C00U // Store exclusive w[20:16] = store_conditional_u8(x[9:5],  w[4:0])
#define ARM64_INSN_STXRH     0x48007C00U // Store exclusive w[20:16] = store_conditional_u16(x[9:5], w[4:0])
#define ARM64_INSN_STXRW     0x88007C00U // Store exclusive w[20:16] = store_conditional_u32(x[9:5], w[4:0])
#define ARM64_INSN_STXRD     0xC8007C00U // Store exclusive w[20:16] = store_conditional_u64(x[9:5], x[4:0])

#define ARM64_INSN_LDXP      0xC87F0000U // Load exclusive (x[4:0], x[20:16]) = load_reserve_u128(x[9:5])
#define ARM64_INSN_STXP      0xC8200000U // Store exclusive w[20:16] = store_conditional_u128(x[9:5], (x[4:0], x[14:10]))

// Load-reserved from addr to rd
static inline void rvjit_arm64_insn_lr(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t addr)
{
    rvjit_arm64_emit_insn(block, insn | (rd & 31) | ((addr & 31) << 5));
}

// Store-conditional from rs to addr, set rd to 0 on success, or 1 if reservation was invalidated
static inline void rvjit_arm64_insn_sc(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs, uint32_t addr)
{
    rvjit_arm64_emit_insn(block, insn | (rs & 31) | ((addr & 31) << 5) | ((rd & 31) << 16));
}

/*
 * Branches
 */

#define ARM64_INSN_B   0x14000000U // Jump to PC + imm26[25:0]
#define ARM64_INSN_BL  0x94000000U // Link x[30] = PC + 4; Jump to PC + imm26[25:0]
#define ARM64_INSN_BR  0xD61F0000U // Jump to x[9:5]
#define ARM64_INSN_BLR 0xD63F0000U // Link x[30] = PC + 4; Jump to x[9:5]

// Check whether jump offset can be encoded
static inline bool rvjit_arm64_valid_b(int32_t off)
{
    return !(off & 3) && sign_extend(off, 28) == off;
}

// Emit b or bl jump instruction
static inline void rvjit_arm64_insn_b(rvjit_block_t* block, uint32_t insn, int32_t off)
{
    rvjit_arm64_emit_insn(block, insn | ((off >> 2) & 0x3FFFFFFU));
}

// Emit br or blr jump instruction
static inline void rvjit_arm64_insn_br(rvjit_block_t* block, uint32_t insn, uint32_t reg)
{
    rvjit_arm64_emit_insn(block, insn | ((reg & 31) << 5));
}

// Patch a new jump instruction
static inline void rvjit_arm64_patch_b(void* addr, uint32_t insn, int32_t off)
{
    write_uint32_le(addr, insn | ((off >> 2) & 0x3FFFFFFU));
    if (unlikely(!rvjit_arm64_valid_b(off))) {
        rvvm_fatal("Illegal branch offset in rvjit_arm64_patch_b()!");
    }
}

#define ARM64_INSN_CBZ   0xB4000000U // Branch to PC + imm19[23:5] if !x[4:0]
#define ARM64_INSN_CBZW  0x34000000U // Branch to PC + imm19[23:5] if !w[4:0]
#define ARM64_INSN_CBNZ  0xB5000000U // Branch to PC + imm19[23:5] if x[4:0]
#define ARM64_INSN_CBNZW 0x35000000U // Branch to PC + imm19[23:5] if w[4:0]

#define ARM64_INSN_B_EQ  0x54000000U // Branch to PC + imm19[23:5] if EQ
#define ARM64_INSN_B_NE  0x54000001U // Branch to PC + imm19[23:5] if NE
#define ARM64_INSN_B_HS  0x54000002U // Branch to PC + imm19[23:5] if HS
#define ARM64_INSN_B_LO  0x54000003U // Branch to PC + imm19[23:5] if LO
#define ARM64_INSN_B_MI  0x54000004U // Branch to PC + imm19[23:5] if MI
#define ARM64_INSN_B_PL  0x54000005U // Branch to PC + imm19[23:5] if PL
#define ARM64_INSN_B_VS  0x54000006U // Branch to PC + imm19[23:5] if VS
#define ARM64_INSN_B_VC  0x54000007U // Branch to PC + imm19[23:5] if VC
#define ARM64_INSN_B_HI  0x54000008U // Branch to PC + imm19[23:5] if HI
#define ARM64_INSN_B_LS  0x54000009U // Branch to PC + imm19[23:5] if LS
#define ARM64_INSN_B_GE  0x5400000AU // Branch to PC + imm19[23:5] if GE
#define ARM64_INSN_B_LT  0x5400000BU // Branch to PC + imm19[23:5] if LT
#define ARM64_INSN_B_GT  0x5400000CU // Branch to PC + imm19[23:5] if GT
#define ARM64_INSN_B_LE  0x5400000DU // Branch to PC + imm19[23:5] if LE
#define ARM64_INSN_B_AL  0x5400000EU // Branch to PC + imm19[23:5] always
#define ARM64_INSN_B_NV  0x5400000FU // Branch to PC + imm19[23:5] never

// Check whether branch offset can be encoded
static inline bool rvjit_arm64_valid_branch(int32_t off)
{
    return !(off & 3) && sign_extend(off, 21) == off;
}

// Check whether branch instruction is cbz/cbnz, or b.cc
static inline bool rvjit_arm64_branch_is_cbz(uint32_t insn)
{
    return !!(insn & 0x20000000U);
}

// Invert b.cc instruction condition
static inline uint32_t rvjit_arm64_invert_bcc(uint32_t insn)
{
    return insn ^ 1;
}

// Invert cbz instruction to cbnz and vice versa
static inline uint32_t rvjit_arm64_invert_cbz(uint32_t insn)
{
    return insn ^ 0x01000000U;
}

// Invert b.cc / cbz instruction condition
static inline uint32_t rvjit_arm64_invert_branch(uint32_t insn)
{
    if (rvjit_arm64_branch_is_cbz(insn)) {
        return rvjit_arm64_invert_cbz(insn);
    } else {
        return rvjit_arm64_invert_bcc(insn);
    }
}

// Emit branch instruction
static inline void rvjit_arm64_insn_branch(rvjit_block_t* block, uint32_t insn, int32_t off)
{
    rvjit_arm64_emit_insn(block, insn | (((off >> 2) & 0x7FFFFU) << 5));
}

// Emit cbz/cbnz instruction, or b.cc if reg == 0
static inline void rvjit_arm64_insn_cbz(rvjit_block_t* block, uint32_t insn, uint32_t reg, int32_t off)
{
    rvjit_arm64_insn_branch(block, insn | (reg & 31), off);
}

// Patch a new branch instruction
static inline void rvjit_arm64_patch_branch(void* addr, uint32_t insn, int32_t off)
{
    write_uint32_le(addr, insn | (((off >> 2) & 0x7FFFFU) << 5));
    if (unlikely(!rvjit_arm64_valid_branch(off))) {
        rvvm_fatal("Invalid branch offset in rvjit_arm64_patch_branch()!");
    }
}

// Patch an existing branch instruction offset
static inline void rvjit_arm64_patch_branch_off(void* addr, int32_t off)
{
    uint32_t insn = read_uint32_le(addr) & 0xFF00001FU;
    rvjit_arm64_patch_branch(addr, insn, off);
}

/*
 * Backend helpers, Instruction lowering
 */

static inline bool rvjit_arm64_is_pow2(uint64_t x)
{
    return x && !(x & (x - 1));
}

// Produce valid immr/imms for logical imm instruction if possible
static bool rvjit_arm64_logic_gen_immr_imms(int64_t imm, bool wide, uint32_t* immr, uint32_t* imms)
{
    bool neg = imm < 0;
    if (neg) {
        imm = ~imm;
    }
    if (imm) {
        uint32_t bits = wide ? 64 : 32;
        uint32_t cnt, rot;
        if (neg) {
            rot  = bit_clz64(imm) + bits - 64;
            cnt  = bit_ctz64(imm) + rot;
            bits = cnt;
        } else {
            rot  = bits - bit_ctz64(imm);
            cnt  = rot - (bit_clz64(imm) + bits - 64);
            rot &= bits - 1;
        }
        if (rvjit_arm64_is_pow2((imm >> (bits - rot)) + 1)) {
            *immr = rot;
            *imms = cnt - 1;
            return true;
        }
    }
    return false;
}

// Load imm64 (All optimized variations)
static void rvjit_arm64_load_imm_slow(rvjit_block_t* block, uint32_t reg, uint64_t imm)
{
    uint32_t immr = 0, imms = 0;
    if (rvjit_arm64_logic_gen_immr_imms(imm, true, &immr, &imms)) {
        // Logical immediate encodes the imm
        rvjit_arm64_insn_logic_reg2_immx(block, ARM64_INSN_ORRI, reg, RVJIT_HOST_REG_ZERO, immr, imms);
    } else if ((((int64_t)(imm << 16)) >> 16) == (int64_t)imm) {
        // Negative value with at least one top halfword 0xFFFF
        rvjit_arm64_insn_mov(block, ARM64_INSN_MOVN, reg, ~imm, 0);
        for (size_t i = 1; i < 4; ++i) {
            uint16_t word = imm >> (i << 4);
            if (word != 0xFFFF) {
                rvjit_arm64_insn_mov(block, ARM64_INSN_MOVK, reg, word, i);
            }
        }
    } else {
        uint32_t insn = ARM64_INSN_MOVZ;
        for (size_t i = 0; i < 4; ++i) {
            uint16_t word = imm >> (i << 4);
            if (word) {
                rvjit_arm64_insn_mov(block, insn, reg, word, i);
                insn = ARM64_INSN_MOVK;
            }
        }
    }
}

// Load imm64 (Fast path, no-op for XZR)
static inline void rvjit_arm64_load_imm(rvjit_block_t* block, uint32_t reg, uint64_t imm)
{
    if (likely(reg != RVJIT_HOST_REG_ZERO)) {
        if (likely(!(imm >> 16))) {
            rvjit_arm64_insn_mov(block, ARM64_INSN_MOVZ, reg, imm, 0);
        } else {
            rvjit_arm64_load_imm_slow(block, reg, imm);
        }
    }
}

// Emit lowering of immediate instruction as register-register instruction, with second operand loaded from imm
static void rvjit_arm64_lower_reg2_imm(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs, int64_t imm)
{
    if (likely(rd != RVJIT_HOST_REG_ZERO)) {
        if (rd != rs) {
            rvjit_arm64_load_imm(block, rd, imm);
            rvjit_arm64_insn_reg3(block, insn, rd, rs, rd);
        } else {
            regid_t tmp = rvjit_claim_hreg(block);
            rvjit_arm64_load_imm(block, tmp, imm);
            rvjit_arm64_insn_reg3(block, insn, rd, rs, tmp);
            rvjit_free_hreg(block, tmp);
        }
    }
}

// Emit compare with imm64 (Slow path)
static void rvjit_arm64_arith_cmp_imm_slow(rvjit_block_t* block, uint32_t insn, uint32_t reg, int64_t imm)
{
    regid_t tmp = rvjit_claim_hreg(block);
    rvjit_arm64_load_imm(block, tmp, imm);
    rvjit_arm64_insn_reg3(block, rvjit_arm64_arith_imm_to_reg(insn), RVJIT_HOST_REG_ZERO, reg, tmp);
    rvjit_free_hreg(block, tmp);
}

// Emit compare with imm64, trearing X31 as XZR (Fast path)
static inline void rvjit_arm64_cmp_imm(rvjit_block_t* block, uint32_t insn, uint32_t reg, int64_t imm)
{
    // INT64_MIN cannot be negated in signed arithmetic; compare it via a register.
    if (imm < 0 && imm != INT64_MIN) {
        // Invert imm and instruction
        imm  = -imm;
        insn = rvjit_arm64_arith_invert(insn);
    }
    if (!imm) {
        rvjit_arm64_insn_reg3(block, rvjit_arm64_arith_imm_to_reg(insn), RVJIT_HOST_REG_ZERO, reg, RVJIT_HOST_REG_ZERO);
    } else if (likely(!(imm >> 12) && reg != RVJIT_HOST_REG_ZERO)) {
        rvjit_arm64_insn_arith_reg2_imm(block, insn, RVJIT_HOST_REG_ZERO, reg, imm, 0);
    } else if (!(imm >> 24) && reg != RVJIT_HOST_REG_ZERO) {
        rvjit_arm64_insn_arith_reg2_imm(block, insn, RVJIT_HOST_REG_ZERO, reg, imm >> 12, 1);
    } else {
        rvjit_arm64_arith_cmp_imm_slow(block, insn, reg, imm);
    }
}

// Check whether imm fits into unsigned imm24
static inline bool rvjit_arm64_is_imm24(uint64_t imm)
{
    return !(imm >> 24);
}

// Emit arithmetic with unsigned imm24, treating X31 as SP
static inline void rvjit_arm64_arith_imm24(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs, uint32_t imm)
{
    uint32_t up = imm >> 12;
    uint32_t lo = (imm & 0xFFF) || (!up && rd != rs);
    if (lo) {
        rvjit_arm64_insn_arith_reg2_imm(block, insn, rd, rs, imm, 0);
    }
    if (up) {
        rvjit_arm64_insn_arith_reg2_imm(block, insn, rd, lo ? rd : rs, up, 1);
    }
}

// Emit arithmetic with imm64, treating X31 as XZR (Fast path)
static inline void rvjit_arm64_arith_imm(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs, int64_t imm)
{
    if (likely(rd != RVJIT_HOST_REG_ZERO)) {
        if (rs == RVJIT_HOST_REG_ZERO) {
            // Fold before normalizing the sign; XZR is not SP here.
            uint64_t value = (insn & 0x40000000U) ? -(uint64_t)imm : (uint64_t)imm;
            rvjit_arm64_load_imm(block, rd, (insn & ARM64_WIDE_MASK) ? value : (uint32_t)value);
            return;
        }
        if (imm < 0) {
            // Invert imm and instruction
            imm  = -imm;
            insn = rvjit_arm64_arith_invert(insn);
        }
        if (!imm) {
            rvjit_arm64_insn_reg3(block, ARM64_INSN_ORR, rd, rs, RVJIT_HOST_REG_ZERO);
        } else if (likely(rvjit_arm64_is_imm24(imm))) {
            rvjit_arm64_arith_imm24(block, insn, rd, rs, imm);
        } else {
            rvjit_arm64_lower_reg2_imm(block, rvjit_arm64_arith_imm_to_reg(insn), rd, rs, imm);
        }
    }
}

// Emit addi with imm64, allowing SP access (Slow path)
static void rvjit_arm64_addisp_slow(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs, int64_t imm)
{
    if (rd == RVJIT_HOST_REG_SP || rs == RVJIT_HOST_REG_SP) {
        regid_t tmp = rvjit_claim_hreg(block);
        rvjit_arm64_load_imm(block, tmp, imm);
        rvjit_arm64_insn_reg3(block, rvjit_arm64_arith_imm_to_reg_ext(insn), rd, rs, tmp);
        rvjit_free_hreg(block, tmp);
    } else {
        rvjit_arm64_lower_reg2_imm(block, rvjit_arm64_arith_imm_to_reg(insn), rd, rs, imm);
    }
}

// Emit arithmetic with imm64, allowing SP access (Fast path)
static void rvjit_arm64_addisp(rvjit_block_t* block, uint32_t rd, uint32_t rs, int64_t imm)
{
    uint32_t insn = ARM64_INSN_ADDI;
    if (imm < 0) {
        // Invert imm and instruction
        imm  = -imm;
        insn = rvjit_arm64_arith_invert(insn);
    }
    if (likely(rvjit_arm64_is_imm24(imm))) {
        rvjit_arm64_arith_imm24(block, insn, rd, rs, imm);
    } else {
        rvjit_arm64_addisp_slow(block, insn, rd, rs, imm);
    }
}

// Emit jump address preparation (Slow path)
static void rvjit_arm64_prepare_jump_slow(rvjit_block_t* block, uint32_t reg, int64_t off, bool absolute)
{
    if (absolute) {
        rvjit_arm64_load_imm(block, reg, off);
    } else {
        rvjit_arm64_insn_reg(block, ARM64_INSN_ADR, reg);
        rvjit_arm64_arith_imm(block, ARM64_INSN_ADDI, reg, reg, off);
    }
}

// Emit absolute / relative 64-bit call / jump (Slow path)
static void rvjit_arm64_jump_slow(rvjit_block_t* block, int64_t off, bool call, bool absolute)
{
    if (call) {
        rvjit_arm64_prepare_jump_slow(block, RVJIT_HOST_REG_RA, off, absolute);
        rvjit_arm64_insn_br(block, ARM64_INSN_BR, RVJIT_HOST_REG_RA);
    } else {
        regid_t tmp = rvjit_claim_hreg(block);
        rvjit_arm64_prepare_jump_slow(block, tmp, off, absolute);
        rvjit_arm64_insn_br(block, ARM64_INSN_BR, tmp);
        rvjit_free_hreg(block, tmp);
    }
}

// Emit absolute / relative 64-bit jump (Fast path)
static inline void rvjit_arm64_jump(rvjit_block_t* block, int64_t off, bool absolute)
{
    if (!absolute && rvjit_arm64_valid_b(off)) {
        rvjit_arm64_insn_b(block, ARM64_INSN_B, off);
    } else {
        rvjit_arm64_jump_slow(block, off, false, absolute);
    }
}

// Emit relative 64-bit branch (Slow path)
static void rvjit_arm64_branch_slow(rvjit_block_t* block, uint32_t insn, int64_t off)
{
    size_t base = block->size;
    rvjit_arm64_insn_branch(block, rvjit_arm64_invert_branch(insn), 0);
    rvjit_arm64_jump(block, off + base - block->size, false);
    rvjit_arm64_patch_branch_off(block->code + base, block->size - base);
}

// Emit relative 64-bit branch (Fast path)
static inline void rvjit_arm64_branch(rvjit_block_t* block, uint32_t insn, int64_t off)
{
    if (rvjit_arm64_valid_branch(off)) {
        rvjit_arm64_insn_branch(block, insn, off);
    } else {
        rvjit_arm64_branch_slow(block, insn, off);
    }
}

/*
 * RVJIT Host instruction intrinsics
 */

// Load immedate
static inline void rvjit_host_load_imm(rvjit_block_t* block, uint32_t reg, uint64_t imm)
{
    rvjit_arm64_load_imm(block, reg, imm);
}

// Sign-extend 32-bit source to 64-bit destination
static inline void rvjit_host_sext32(rvjit_block_t* block, uint32_t rd, uint32_t rs)
{
    rvjit_arm64_insn_logic_reg2_immx(block, ARM64_INSN_SBFM, rd, rs, 0, 31);
}

// Push register onto the stack (With alignment)
static inline void rvjit_host_push(rvjit_block_t* block, uint32_t reg)
{
    rvjit_arm64_insn_reg(block, ARM64_INSN_PUSH, reg);
}

// Pop register from the stack (With alignment)
static inline void rvjit_host_pop(rvjit_block_t* block, uint32_t reg)
{
    rvjit_arm64_insn_reg(block, ARM64_INSN_POP, reg);
}

// Emit absolute / relative 64-bit jump
static inline void rvjit_host_jump(rvjit_block_t* block, int64_t off, bool absolute)
{
    rvjit_arm64_jump(block, off, absolute);
}

// Emit absolute / relative 64-bit call
static inline void rvjit_host_call(rvjit_block_t* block, int64_t off, bool absolute)
{
    rvjit_arm64_insn_reg(block, ARM64_INSN_PUSH, RVJIT_HOST_REG_RA);
    if (!absolute && rvjit_arm64_valid_b(off)) {
        rvjit_arm64_insn_b(block, ARM64_INSN_BL, off);
    } else {
        rvjit_arm64_jump_slow(block, off, true, absolute);
    }
    rvjit_arm64_insn_reg(block, ARM64_INSN_POP, RVJIT_HOST_REG_RA);
}

// Emit jump to address in register
static inline void rvjit_host_jump_reg(rvjit_block_t* block, uint32_t reg)
{
    rvjit_arm64_insn_br(block, ARM64_INSN_BR, reg);
}

// Emit call to address in register
static inline void rvjit_host_call_reg(rvjit_block_t* block, uint32_t reg)
{
    rvjit_arm64_insn_reg(block, ARM64_INSN_PUSH, RVJIT_HOST_REG_RA);
    rvjit_arm64_insn_br(block, ARM64_INSN_BLR, reg);
    rvjit_arm64_insn_reg(block, ARM64_INSN_POP, RVJIT_HOST_REG_RA);
}

// Return to caller via link address
static inline void rvjit_host_ret(rvjit_block_t* block)
{
    rvjit_arm64_emit_insn(block, ARM64_INSN_RET);
}

// Emit ret instruction patchable by rvjit_host_patch_jump()
static inline void rvjit_host_patchable_ret(rvjit_block_t* block)
{
    // Always 4-bytes, same as jmp
    rvjit_host_ret(block);
}

// Patch instruction at addr into ret
static inline void rvjit_host_patch_ret(void* addr)
{
    write_uint32_le(addr, ARM64_INSN_RET);
}

// Patch instruction at addr into a jump if offset fits, returns success
static inline bool rvjit_host_patch_jump(void* addr, int32_t off)
{
    if (rvjit_arm64_valid_b(off)) {
        rvjit_arm64_patch_b(addr, ARM64_INSN_B, off);
        return true;
    }
    return false;
}

#define RVJIT_INSN_ADD   ARM64_INSN_ADD
#define RVJIT_INSN_ADDW  ARM64_INSN_ADDW
#define RVJIT_INSN_SUB   ARM64_INSN_SUB
#define RVJIT_INSN_SUBW  ARM64_INSN_SUBW
#define RVJIT_INSN_AND   ARM64_INSN_AND
#define RVJIT_INSN_ANDW  ARM64_INSN_ANDW
#define RVJIT_INSN_OR    ARM64_INSN_ORR
#define RVJIT_INSN_ORW   ARM64_INSN_ORRW
#define RVJIT_INSN_XOR   ARM64_INSN_EOR
#define RVJIT_INSN_XORW  ARM64_INSN_EORW

#define RVJIT_INSN_ANDN  ARM64_INSN_BIC
#define RVJIT_INSN_ANDNW ARM64_INSN_BICW
#define RVJIT_INSN_ORN   ARM64_INSN_ORN
#define RVJIT_INSN_ORNW  ARM64_INSN_ORNW
#define RVJIT_INSN_XNOR  ARM64_INSN_EON
#define RVJIT_INSN_XNORW ARM64_INSN_EONW

#define RVJIT_INSN_SLL   ARM64_INSN_LSLV
#define RVJIT_INSN_SLLW  ARM64_INSN_LSLVW
#define RVJIT_INSN_SRL   ARM64_INSN_LSRV
#define RVJIT_INSN_SRLW  ARM64_INSN_LSRVW
#define RVJIT_INSN_SRA   ARM64_INSN_ASRV
#define RVJIT_INSN_SRAW  ARM64_INSN_ASRVW

// Emit register-register ALU instruction
static inline void rvjit_host_alu_reg(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs1, uint32_t rs2)
{
    rvjit_arm64_insn_reg3(block, insn, rd, rs1, rs2);
}

#define RVJIT_INSN_ADDI  ARM64_INSN_ADDI
#define RVJIT_INSN_ADDIW ARM64_INSN_ADDIW

// Emit addi instruction with signed imm64, treating X31 as XZR
static inline void rvjit_host_addi(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs, int64_t imm)
{
    rvjit_arm64_arith_imm(block, insn, rd, rs, imm);
}

// Emit addi instruction with signed imm64, may operate on SP!
static inline void rvjit_host_addisp(rvjit_block_t* block, uint32_t rd, uint32_t rs, int64_t imm)
{
    rvjit_arm64_addisp(block, rd, rs, imm);
}

#define RVJIT_INSN_ANDI  ARM64_INSN_ANDI
#define RVJIT_INSN_ANDIW ARM64_INSN_ANDIW
#define RVJIT_INSN_ORI   ARM64_INSN_ORRI
#define RVJIT_INSN_ORIW  ARM64_INSN_ORRIW
#define RVJIT_INSN_XORI  ARM64_INSN_EORI
#define RVJIT_INSN_XORIW ARM64_INSN_EORIW

// Emit logical instruction with signed imm32
static inline void rvjit_host_logic_imm(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs, int64_t imm)
{
    uint32_t immr = 0, imms = 0;
    if (likely(rd != RVJIT_HOST_REG_ZERO)) {
        bool wide = rvjit_arm64_insn_is_wide(insn);
        if (rvjit_arm64_logic_gen_immr_imms(imm, wide, &immr, &imms)) {
            // Encode immediate directly into the instruction
            if (wide) {
                rvjit_arm64_insn_logic_reg2_immx(block, insn, rd, rs, immr, imms);
            } else {
                rvjit_arm64_insn_logic_reg2_immw(block, insn, rd, rs, immr, imms);
            }
        } else {
            // Lower to loading immediate into register
            rvjit_arm64_lower_reg2_imm(block, rvjit_arm64_logic_imm_to_reg(insn), rd, rs, imm);
        }
    }
}

#define SLLI_MASK_INTERNAL 0x1U

#define RVJIT_INSN_SRAI    ARM64_INSN_SBFM
#define RVJIT_INSN_SRAIW   ARM64_INSN_SBFMW
#define RVJIT_INSN_SRLI    ARM64_INSN_UBFM
#define RVJIT_INSN_SRLIW   ARM64_INSN_UBFMW
#define RVJIT_INSN_SLLI    (ARM64_INSN_UBFM | SLLI_MASK_INTERNAL)
#define RVJIT_INSN_SLLIW   (ARM64_INSN_UBFMW | SLLI_MASK_INTERNAL)

static inline void rvjit_host_shift_imm(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs, uint32_t imm)
{
    if (likely(rd != RVJIT_HOST_REG_ZERO)) {
        uint32_t wide = rvjit_arm64_insn_is_wide(insn);
        uint32_t imms = wide ? 63 : 31;
        if (insn & SLLI_MASK_INTERNAL) {
            insn &= ~SLLI_MASK_INTERNAL;
            imms -= imm;
            imm   = -imm;
        }
        if (wide) {
            rvjit_arm64_insn_logic_reg2_immx(block, insn, rd, rs, imm, imms);
        } else {
            rvjit_arm64_insn_logic_reg2_immw(block, insn, rd, rs, imm, imms);
        }
    }
}

#define RVJIT_INSN_SLT   (ARM64_CC_GE | ARM64_WIDE_MASK)
#define RVJIT_INSN_SLTW  ARM64_CC_GE
#define RVJIT_INSN_SLTU  (ARM64_CC_HS | ARM64_WIDE_MASK)
#define RVJIT_INSN_SLTUW ARM64_CC_HS

// Set rd to result of rs1 and rs2 comparison based on insn
static inline void rvjit_host_cmp_reg(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs1, uint32_t rs2)
{
    rvjit_arm64_insn_cmp_reg(block, ARM64_INSN_CMPW | (insn & ARM64_WIDE_MASK), rs1, rs2);
    rvjit_arm64_insn_csel(block, ARM64_INSN_CSINCW, rd, RVJIT_HOST_REG_ZERO, RVJIT_HOST_REG_ZERO, insn);
}

#define RVJIT_INSN_SLTI   RVJIT_INSN_SLT
#define RVJIT_INSN_SLTIW  RVJIT_INSN_SLTW
#define RVJIT_INSN_SLTIU  RVJIT_INSN_SLTU
#define RVJIT_INSN_SLTIUW RVJIT_INSN_SLTUW

// Set rd to result of rs1 and imm comparison based on insn
static inline void rvjit_host_cmp_imm(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs1, int64_t imm)
{
    rvjit_arm64_cmp_imm(block, ARM64_INSN_CMPIW | (insn & ARM64_WIDE_MASK), rs1, imm);
    rvjit_arm64_insn_csel(block, ARM64_INSN_CSINCW, rd, RVJIT_HOST_REG_ZERO, RVJIT_HOST_REG_ZERO, insn);
}

#define RVJIT_INSN_LB  ARM64_INSN_LDRSB
#define RVJIT_INSN_LBU ARM64_INSN_LDRB
#define RVJIT_INSN_LH  ARM64_INSN_LDRSH
#define RVJIT_INSN_LHU ARM64_INSN_LDRH
#define RVJIT_INSN_LW  ARM64_INSN_LDRSW
#define RVJIT_INSN_LWU ARM64_INSN_LDRW
#define RVJIT_INSN_LD  ARM64_INSN_LDRD
#define RVJIT_INSN_LDU ARM64_INSN_LDRD

#define RVJIT_INSN_SB  ARM64_INSN_STRB
#define RVJIT_INSN_SH  ARM64_INSN_STRH
#define RVJIT_INSN_SW  ARM64_INSN_STRW
#define RVJIT_INSN_SD  ARM64_INSN_STRD

#define RVJIT_INSN_FLW ARM64_INSN_LDRFS
#define RVJIT_INSN_FLD ARM64_INSN_LDRFD

#define RVJIT_INSN_FSW ARM64_INSN_STRFS
#define RVJIT_INSN_FSD ARM64_INSN_STRFD

// Emit load/store instruction with signed imm64 offset
static inline void rvjit_host_loadstore(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t base, int64_t off)
{
    uint32_t shift = insn >> 30;
    uint32_t off_i = (off >> shift) & 0xFFFU;
    uint32_t off_s = off_i << shift;
    if (!(off - off_s)) {
        // Encode scaled positive offset directly into the instruction
        rvjit_arm64_insn_ldst(block, insn, rd, base, off_i);
    } else if (rvjit_arm64_ldst_is_load(insn) && !rvjit_arm64_ldst_is_fp(insn)) {
        // This is a scalar load with large offset that clobbers rd
        // Lower to adding base with offset into rd, then overwriting rd with the load result
        rvjit_host_addisp(block, rd, base, off - off_s);
        rvjit_arm64_insn_ldst(block, insn, rd, rd, off_i);
    } else {
        // This is a load/store with large offset which doesn't clobber rd
        // Lower to adding base with offset into temporary register
        regid_t tmp = rvjit_claim_hreg(block);
        rvjit_host_addisp(block, tmp, base, off - off_s);
        rvjit_arm64_insn_ldst(block, insn, rd, tmp, off_i);
        rvjit_free_hreg(block, tmp);
    }
}

#define RVJIT_INSN_BEQ   (ARM64_INSN_B_EQ | ARM64_WIDE_MASK)
#define RVJIT_INSN_BEQW  ARM64_INSN_B_EQ
#define RVJIT_INSN_BNE   (ARM64_INSN_B_NE | ARM64_WIDE_MASK)
#define RVJIT_INSN_BNEW  ARM64_INSN_B_NE
#define RVJIT_INSN_BLT   (ARM64_INSN_B_LT | ARM64_WIDE_MASK)
#define RVJIT_INSN_BLTW  ARM64_INSN_B_LT
#define RVJIT_INSN_BGE   (ARM64_INSN_B_GE | ARM64_WIDE_MASK)
#define RVJIT_INSN_BGEW  ARM64_INSN_B_GE
#define RVJIT_INSN_BLTU  (ARM64_INSN_B_LO | ARM64_WIDE_MASK)
#define RVJIT_INSN_BLTUW ARM64_INSN_B_LO
#define RVJIT_INSN_BGEU  (ARM64_INSN_B_HS | ARM64_WIDE_MASK)
#define RVJIT_INSN_BGEUW ARM64_INSN_B_HS

// Emit relative 64-bit branch with register comparison
static inline void rvjit_host_branch_reg(rvjit_block_t* block, uint32_t insn, //
                                         uint32_t rs1, uint32_t rs2, int64_t off)
{
    size_t   base   = block->size;
    uint32_t branch = insn & ~ARM64_WIDE_MASK;
    rvjit_arm64_insn_cmp_reg(block, ARM64_INSN_CMPW | (insn & ARM64_WIDE_MASK), rs1, rs2);
    rvjit_arm64_branch(block, branch, off + base - block->size);
}

// Emit relative 64-bit branch with immediate comparison
static inline void rvjit_host_branch_imm(rvjit_block_t* block, uint32_t insn, //
                                         uint32_t rs1, int64_t imm, int64_t off)
{
    size_t   base   = block->size;
    uint32_t branch = insn & ~ARM64_WIDE_MASK;
    if (imm || (branch != ARM64_INSN_B_EQ && branch != ARM64_INSN_B_NE)) {
        rvjit_arm64_cmp_imm(block, ARM64_INSN_CMPIW | (insn & ARM64_WIDE_MASK), rs1, imm);
    } else if (branch == ARM64_INSN_B_EQ) {
        branch = ARM64_INSN_CBZW | (rs1 & 31) | (insn & ARM64_WIDE_MASK);
    } else {
        branch = ARM64_INSN_CBNZW | (rs1 & 31) | (insn & ARM64_WIDE_MASK);
    }
    rvjit_arm64_branch(block, branch, off + base - block->size);
}

/*
 * Legacy branch helpers
 * TODO: Proper relocations and all that stuff in RVJIT middleware
 */

static inline branch_t rvjit_native_jmp(rvjit_block_t* block, branch_t handle, bool target)
{
    if (target) {
        // Set branch target
        if (handle == BRANCH_NEW) {
            // Return branch target for a backward jump
            return block->size;
        } else {
            // Patch pre-existing instruction with an offset
            size_t dest = block->size;
            rvjit_host_jump(block, dest - handle, false);
            memmove(block->code + handle, block->code + dest, block->size - dest);
            block->size = dest;
            return BRANCH_NEW;
        }
    } else {
        // Set branch entry
        if (handle == BRANCH_NEW) {
            // Reserve space for a jump instruction
            branch_t tmp = block->size;
            rvjit_host_jump(block, 0, false);
            return tmp;
        } else {
            // Emit jump instruction with a known target
            rvjit_host_jump(block, handle - block->size, false);
            return BRANCH_NEW;
        }
    }
}

static inline branch_t rvjit_native_branch_reg(rvjit_block_t* block, uint32_t insn, uint32_t rs1, uint32_t rs2,
                                               branch_t handle, bool target)
{
    if (target) {
        // Set branch target
        if (handle == BRANCH_NEW) {
            // Return branch target for a backward jump
            return block->size;
        } else {
            // Patch pre-existing instruction with an offset
            size_t dest = block->size;
            rvjit_host_branch_reg(block, insn, rs1, rs2, dest - handle);
            memmove(block->code + handle, block->code + dest, block->size - dest);
            block->size = dest;
            return BRANCH_NEW;
        }
    } else {
        // Set branch entry
        if (handle == BRANCH_NEW) {
            // Reserve space for a jump instruction
            branch_t tmp = block->size;
            rvjit_host_branch_reg(block, insn, rs1, rs2, 0);
            return tmp;
        } else {
            // Emit jump instruction with a known target
            rvjit_host_branch_reg(block, insn, rs1, rs2, handle - block->size);
            return BRANCH_NEW;
        }
    }
}

static inline branch_t rvjit_native_branch_imm(rvjit_block_t* block, uint32_t insn, uint32_t rs1, int64_t imm,
                                               branch_t handle, bool target)
{
    if (target) {
        // Set branch target
        if (handle == BRANCH_NEW) {
            // Return branch target for a backward jump
            return block->size;
        } else {
            // Patch pre-existing instruction with an offset
            size_t dest = block->size;
            rvjit_host_branch_imm(block, insn, rs1, imm, dest - handle);
            memmove(block->code + handle, block->code + dest, block->size - dest);
            block->size = dest;
            return BRANCH_NEW;
        }
    } else {
        // Set branch entry
        if (handle == BRANCH_NEW) {
            // Reserve space for a jump instruction
            branch_t tmp = block->size;
            rvjit_host_branch_imm(block, insn, rs1, imm, 0);
            return tmp;
        } else {
            // Emit jump instruction with a known target
            rvjit_host_branch_imm(block, insn, rs1, imm, handle - block->size);
            return BRANCH_NEW;
        }
    }
}

/*
 * Multiplication / Division / Remainder
 */

#define MULHSU_MASK_INTERNAL 0x1U

#define RVJIT_INSN_MUL       ARM64_INSN_MADD
#define RVJIT_INSN_MULW      ARM64_INSN_MADDW
#define RVJIT_INSN_MULH      ARM64_INSN_SMULH
#define RVJIT_INSN_MULHW     ARM64_INSN_SMADDL
#define RVJIT_INSN_MULHU     ARM64_INSN_UMULH
#define RVJIT_INSN_MULHUW    ARM64_INSN_UMADDL
#define RVJIT_INSN_MULHSU    (ARM64_INSN_UMULH | MULHSU_MASK_INTERNAL)
#define RVJIT_INSN_MULHSUW   (ARM64_INSN_UMADDL | MULHSU_MASK_INTERNAL)

static inline void rvjit_host_mul(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs1, uint32_t rs2)
{
    if ((insn | ARM64_WIDE_MASK) == RVJIT_INSN_MUL) {
        // Simple mul / mulw
        rvjit_arm64_insn_reg4(block, insn, rd, rs1, rs2, RVJIT_HOST_REG_ZERO);
    } else if (insn == RVJIT_INSN_MULH || insn == RVJIT_INSN_MULHU) {
        // Simple mulh / mulhu
        rvjit_arm64_insn_reg3(block, insn, rd, rs1, rs2);
    } else if (insn == RVJIT_INSN_MULHW || insn == RVJIT_INSN_MULHUW) {
        // Simple mulhw / mulhuw
        rvjit_arm64_insn_reg4(block, insn, rd, rs1, rs2, RVJIT_HOST_REG_ZERO);
        rvjit_host_shift_imm(block, RVJIT_INSN_SRLI, rd, rd, 32);
    } else if (insn == RVJIT_INSN_MULHSU) {
        regid_t sign = rvjit_claim_hreg(block);
        regid_t mulh = rvjit_claim_hreg(block);
        rvjit_host_shift_imm(block, RVJIT_INSN_SRAI, sign, rs1, 63);
        rvjit_arm64_insn_reg3(block, ARM64_INSN_UMULH, mulh, rs1, rs2);
        rvjit_arm64_insn_reg4(block, ARM64_INSN_MADD, rd, sign, rs2, mulh);
        rvjit_free_hreg(block, sign);
        rvjit_free_hreg(block, mulh);
    } else if (insn == RVJIT_INSN_MULHSUW) {
        regid_t srs1 = rvjit_claim_hreg(block);
        regid_t urs2 = rvjit_claim_hreg(block);
        rvjit_host_sext32(block, srs1, rs1);
        rvjit_host_logic_imm(block, RVJIT_INSN_ANDI, urs2, rs2, 0xFFFFFFFF);
        rvjit_arm64_insn_reg4(block, ARM64_INSN_MADD, rd, srs1, urs2, RVJIT_HOST_REG_ZERO);
        rvjit_host_shift_imm(block, RVJIT_INSN_SRLI, rd, rd, 32);
        rvjit_free_hreg(block, srs1);
        rvjit_free_hreg(block, urs2);
    } else {
        rvvm_fatal("Unknown instruction to rvjit_host_mul()!");
    }
}

#define REM_MASK_INTERNAL 0x1U

#define RVJIT_INSN_DIV    ARM64_INSN_SDIV
#define RVJIT_INSN_DIVW   ARM64_INSN_SDIVW
#define RVJIT_INSN_DIVU   ARM64_INSN_UDIV
#define RVJIT_INSN_DIVUW  ARM64_INSN_UDIVW

#define RVJIT_INSN_REM    (ARM64_INSN_SDIV | REM_MASK_INTERNAL)
#define RVJIT_INSN_REMW   (ARM64_INSN_SDIVW | REM_MASK_INTERNAL)
#define RVJIT_INSN_REMU   (ARM64_INSN_UDIV | REM_MASK_INTERNAL)
#define RVJIT_INSN_REMUW  (ARM64_INSN_UDIVW | REM_MASK_INTERNAL)

static inline void rvjit_host_div_rem(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs1, uint32_t rs2)
{
    // Instruction opcodes to be used
    uint32_t div_insn = insn & ~REM_MASK_INTERNAL;
    uint32_t mul_insn = ARM64_INSN_MSUBW | (insn & ARM64_WIDE_MASK);
    uint32_t add_insn = RVJIT_INSN_ADDIW | (insn & ARM64_WIDE_MASK);
    uint32_t bne_insn = RVJIT_INSN_BNEW | (insn & ARM64_WIDE_MASK);

    // Operation details
    bool wide = rvjit_arm64_insn_is_wide(insn);
    bool sign = (div_insn | ARM64_WIDE_MASK) == RVJIT_INSN_DIV;
    bool rem  = !!(insn & REM_MASK_INTERNAL);

    // Branches
    branch_t ovf_rs1   = 0;
    branch_t ovf_rs2   = 0;
    branch_t div_zero  = 0;
    branch_t exit_ovf  = 0;
    branch_t exit_zero = 0;

    rvjit_free_hreg(block, rvjit_claim_hreg(block));

    if (sign) {
        // Signed division / remainder
        uint64_t ovf = wide ? 0x8000000000000000ULL : 0x80000000U;

        // Overflow check (rs1 != 0x80000000... && rs2 != -1)
        ovf_rs1 = rvjit_native_branch_imm(block, bne_insn, rs1, ovf, BRANCH_NEW, false);
        ovf_rs2 = rvjit_native_branch_imm(block, bne_insn, rs2, -1, BRANCH_NEW, false);

        // Overflow check fallthrough
        if (rem) {
            rvjit_host_load_imm(block, rd, 0);
        } else {
            rvjit_host_load_imm(block, rd, ovf);
        }

        exit_ovf = rvjit_native_jmp(block, BRANCH_NEW, false);

        // Overflow check pass
        rvjit_native_branch_imm(block, bne_insn, rs1, ovf, ovf_rs1, true);
        rvjit_native_branch_imm(block, bne_insn, rs2, -1, ovf_rs2, true);
    }

    // Division by zero check
    div_zero = rvjit_native_branch_imm(block, bne_insn, rs2, 0, BRANCH_NEW, false);

    // Division by zero fallthrough
    if (rem) {
        rvjit_host_addi(block, add_insn, rd, rs1, 0);
    } else {
        rvjit_host_load_imm(block, rd, -1);
    }

    exit_zero = rvjit_native_jmp(block, BRANCH_NEW, false);

    // Division by zero check pass
    rvjit_native_branch_imm(block, bne_insn, rs2, 0, div_zero, true);

    if (rem) {
        // Calculate remainder
        regid_t tmp = rvjit_claim_hreg(block);
        rvjit_arm64_insn_reg3(block, div_insn, tmp, rs1, rs2);
        rvjit_arm64_insn_reg4(block, mul_insn, rd, tmp, rs2, rs1);
        rvjit_free_hreg(block, tmp);
    } else {
        // Calculate division
        rvjit_arm64_insn_reg3(block, div_insn, rd, rs1, rs2);
    }

    // Exit labels
    if (sign) {
        rvjit_native_jmp(block, exit_ovf, true);
    }
    rvjit_native_jmp(block, exit_zero, true);
}

/*
 * Floating-point (TODO)
 */

#define RVJIT_INSN_FMULS   ARM64_INSN_FMULS
#define RVJIT_INSN_FMULD   ARM64_INSN_FMULD
#define RVJIT_INSN_FDIVS   ARM64_INSN_FDIVS
#define RVJIT_INSN_FDIVD   ARM64_INSN_FDIVD
#define RVJIT_INSN_FADDS   ARM64_INSN_FADDS
#define RVJIT_INSN_FADDD   ARM64_INSN_FADDD
#define RVJIT_INSN_FSUBS   ARM64_INSN_FSUBS
#define RVJIT_INSN_FSUBD   ARM64_INSN_FSUBD
#define RVJIT_INSN_FMAXS   ARM64_INSN_FMAXS
#define RVJIT_INSN_FMAXD   ARM64_INSN_FMAXD
#define RVJIT_INSN_FMINS   ARM64_INSN_FMINS
#define RVJIT_INSN_FMIND   ARM64_INSN_FMIND
#define RVJIT_INSN_FMAXNMS ARM64_INSN_FMAXNMS
#define RVJIT_INSN_FMAXNMD ARM64_INSN_FMAXNMD
#define RVJIT_INSN_FMINNMS ARM64_INSN_FMINNMS
#define RVJIT_INSN_FMINNMD ARM64_INSN_FMINNMD

static inline void rvjit_host_fpu_reg(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs1, uint32_t rs2)
{
    rvjit_arm64_insn_reg3(block, insn, rd, rs1, rs2);
}

#define RVJIT_INSN_FSQRTS ARM64_INSN_FSQRTS
#define RVJIT_INSN_FSQRTD ARM64_INSN_FSQRTD

// TODO: Review how to mimic RISC-V fsgnj
#define RVJIT_INSN_FABSS  ARM64_INSN_FABSS
#define RVJIT_INSN_FABSD  ARM64_INSN_FABSD
#define RVJIT_INSN_FNEGS  ARM64_INSN_FNEGS
#define RVJIT_INSN_FNEGD  ARM64_INSN_FNEGD

static inline void rvjit_host_fpu_reg2(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs)
{
    rvjit_arm64_insn_reg2(block, insn, rd, rs);
}

#define RVJIT_INSN_FMVXW ARM64_INSN_FMOVWS
#define RVJIT_INSN_FMVXD ARM64_INSN_FMOVXD
#define RVJIT_INSN_FMVWX ARM64_INSN_FMOVSW
#define RVJIT_INSN_FMVDX ARM64_INSN_FMOVDX

static inline void rvjit_host_fpu_fmv(rvjit_block_t* block, uint32_t insn, uint32_t rd, uint32_t rs)
{
    rvjit_arm64_insn_reg2(block, insn, rd, rs);
}

/*
 * Atomics (TODO)
 */

static inline void rvjit_host_fence(rvjit_block_t* block, bool seq_cst)
{
    // TODO: Various fence variations
    UNUSED(seq_cst);
    rvjit_arm64_emit_insn(block, ARM64_INSN_DMB_ISH);
}

/*
 * Legacy RVJIT backend interfaces
 */

#define VM_PTR_REG RVJIT_HOST_REG_ARG0

static inline size_t rvjit_native_default_hregmask(void)
{
    return RVJIT_HOST_REGS_VOLATILE & ~(1U << RVJIT_HOST_REG_ARG0);
}

static inline size_t rvjit_native_abireclaim_hregmask(void)
{
    return RVJIT_HOST_REGS_CALLSAVE;
}

static inline void rvjit_native_push(rvjit_block_t* block, regid_t reg)
{
    rvjit_host_push(block, reg);
}

static inline void rvjit_native_pop(rvjit_block_t* block, regid_t reg)
{
    rvjit_host_pop(block, reg);
}

static inline void rvjit_native_ret(rvjit_block_t* block)
{
    rvjit_host_ret(block);
}

static inline void rvjit_native_zero_reg(rvjit_block_t* block, regid_t reg)
{
    rvjit_host_load_imm(block, reg, 0);
}

static inline void rvjit_native_setreg32(rvjit_block_t* block, regid_t reg, uint32_t imm)
{
    rvjit_host_load_imm(block, reg, imm);
}

static inline void rvjit_native_setreg32s(rvjit_block_t* block, regid_t reg, int32_t imm)
{
    rvjit_host_load_imm(block, reg, imm);
}

static inline void rvjit_native_setregw(rvjit_block_t* block, regid_t reg, uint64_t imm)
{
    rvjit_host_load_imm(block, reg, imm);
}

static inline void rvjit_native_signext(rvjit_block_t* block, regid_t reg)
{
    rvjit_host_sext32(block, reg, reg);
}

static inline void rvjit32_native_add(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_ADDW, rd, rs1, rs2);
}

static inline void rvjit32_native_sub(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_SUBW, rd, rs1, rs2);
}

static inline void rvjit32_native_or(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_ORW, rd, rs1, rs2);
}

static inline void rvjit32_native_and(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_ANDW, rd, rs1, rs2);
}

static inline void rvjit32_native_xor(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_XORW, rd, rs1, rs2);
}

static inline void rvjit32_native_sra(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_SRAW, rd, rs1, rs2);
}

static inline void rvjit32_native_srl(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_SRLW, rd, rs1, rs2);
}

static inline void rvjit32_native_sll(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_SLLW, rd, rs1, rs2);
}

static inline void rvjit32_native_addi(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit_host_addi(block, RVJIT_INSN_ADDIW, rd, rs1, imm);
}

static inline void rvjit32_native_ori(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit_host_logic_imm(block, RVJIT_INSN_ORIW, rd, rs1, imm);
}

static inline void rvjit32_native_andi(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit_host_logic_imm(block, RVJIT_INSN_ANDIW, rd, rs1, imm);
}

static inline void rvjit32_native_xori(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit_host_logic_imm(block, RVJIT_INSN_XORIW, rd, rs1, imm);
}

static inline void rvjit32_native_srai(rvjit_block_t* block, regid_t rd, regid_t rs1, uint8_t imm)
{
    rvjit_host_shift_imm(block, RVJIT_INSN_SRAIW, rd, rs1, imm);
}

static inline void rvjit32_native_srli(rvjit_block_t* block, regid_t rd, regid_t rs1, uint8_t imm)
{
    rvjit_host_shift_imm(block, RVJIT_INSN_SRLIW, rd, rs1, imm);
}

static inline void rvjit32_native_slli(rvjit_block_t* block, regid_t rd, regid_t rs1, uint8_t imm)
{
    rvjit_host_shift_imm(block, RVJIT_INSN_SLLIW, rd, rs1, imm);
}

static inline void rvjit32_native_slti(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit_host_cmp_imm(block, RVJIT_INSN_SLTIW, rd, rs1, imm);
}

static inline void rvjit32_native_sltiu(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit_host_cmp_imm(block, RVJIT_INSN_SLTIUW, rd, rs1, imm);
}

static inline void rvjit32_native_slt(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_cmp_reg(block, RVJIT_INSN_SLTW, rd, rs1, rs2);
}

static inline void rvjit32_native_sltu(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_cmp_reg(block, RVJIT_INSN_SLTUW, rd, rs1, rs2);
}

static inline void rvjit32_native_lb(rvjit_block_t* block, regid_t rd, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_LB, rd, base, off);
}

static inline void rvjit32_native_lbu(rvjit_block_t* block, regid_t rd, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_LBU, rd, base, off);
}

static inline void rvjit32_native_lh(rvjit_block_t* block, regid_t rd, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_LH, rd, base, off);
}

static inline void rvjit32_native_lhu(rvjit_block_t* block, regid_t rd, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_LHU, rd, base, off);
}

static inline void rvjit32_native_lw(rvjit_block_t* block, regid_t rd, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_LW, rd, base, off);
}

static inline void rvjit32_native_sb(rvjit_block_t* block, regid_t rs, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_SB, rs, base, off);
}

static inline void rvjit32_native_sh(rvjit_block_t* block, regid_t rs, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_SH, rs, base, off);
}

static inline void rvjit32_native_sw(rvjit_block_t* block, regid_t rs, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_SW, rs, base, off);
}

static inline branch_t rvjit32_native_beq(rvjit_block_t* block, regid_t rs1, regid_t rs2, branch_t handle, bool target)
{
    return rvjit_native_branch_reg(block, RVJIT_INSN_BEQW, rs1, rs2, handle, target);
}

static inline branch_t rvjit32_native_bne(rvjit_block_t* block, regid_t rs1, regid_t rs2, branch_t handle, bool target)
{
    return rvjit_native_branch_reg(block, RVJIT_INSN_BNEW, rs1, rs2, handle, target);
}

static inline branch_t rvjit32_native_beqz(rvjit_block_t* block, regid_t rs1, branch_t handle, bool target)
{
    return rvjit_native_branch_imm(block, RVJIT_INSN_BEQW, rs1, 0, handle, target);
}

static inline branch_t rvjit32_native_bnez(rvjit_block_t* block, regid_t rs1, branch_t handle, bool target)
{
    return rvjit_native_branch_imm(block, RVJIT_INSN_BNEW, rs1, 0, handle, target);
}

static inline branch_t rvjit32_native_blt(rvjit_block_t* block, regid_t rs1, regid_t rs2, branch_t handle, bool target)
{
    return rvjit_native_branch_reg(block, RVJIT_INSN_BLTW, rs1, rs2, handle, target);
}

static inline branch_t rvjit32_native_bge(rvjit_block_t* block, regid_t rs1, regid_t rs2, branch_t handle, bool target)
{
    return rvjit_native_branch_reg(block, RVJIT_INSN_BGEW, rs1, rs2, handle, target);
}

static inline branch_t rvjit32_native_bltu(rvjit_block_t* block, regid_t rs1, regid_t rs2, branch_t handle, bool target)
{
    return rvjit_native_branch_reg(block, RVJIT_INSN_BLTUW, rs1, rs2, handle, target);
}

static inline branch_t rvjit32_native_bgeu(rvjit_block_t* block, regid_t rs1, regid_t rs2, branch_t handle, bool target)
{
    return rvjit_native_branch_reg(block, RVJIT_INSN_BGEUW, rs1, rs2, handle, target);
}

static inline void rvjit32_native_mul(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_mul(block, RVJIT_INSN_MULW, rd, rs1, rs2);
}

static inline void rvjit32_native_mulh(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_mul(block, RVJIT_INSN_MULHW, rd, rs1, rs2);
}

static inline void rvjit32_native_mulhu(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_mul(block, RVJIT_INSN_MULHUW, rd, rs1, rs2);
}

static inline void rvjit32_native_mulhsu(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_mul(block, RVJIT_INSN_MULHSUW, rd, rs1, rs2);
}

static inline void rvjit32_native_div(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_div_rem(block, RVJIT_INSN_DIVW, rd, rs1, rs2);
}

static inline void rvjit32_native_divu(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_div_rem(block, RVJIT_INSN_DIVUW, rd, rs1, rs2);
}

static inline void rvjit32_native_rem(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_div_rem(block, RVJIT_INSN_REMW, rd, rs1, rs2);
}

static inline void rvjit32_native_remu(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_div_rem(block, RVJIT_INSN_REMUW, rd, rs1, rs2);
}

static inline void rvjit64_native_add(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_ADD, rd, rs1, rs2);
}

static inline void rvjit64_native_sub(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_SUB, rd, rs1, rs2);
}

static inline void rvjit64_native_addw(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit32_native_add(block, rd, rs1, rs2);
    rvjit_native_signext(block, rd);
}

static inline void rvjit64_native_subw(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit32_native_sub(block, rd, rs1, rs2);
    rvjit_native_signext(block, rd);
}

static inline void rvjit64_native_or(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_OR, rd, rs1, rs2);
}

static inline void rvjit64_native_and(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_AND, rd, rs1, rs2);
}

static inline void rvjit64_native_xor(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_XOR, rd, rs1, rs2);
}

static inline void rvjit64_native_sra(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_SRA, rd, rs1, rs2);
}

static inline void rvjit64_native_srl(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_SRL, rd, rs1, rs2);
}

static inline void rvjit64_native_sll(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_alu_reg(block, RVJIT_INSN_SLL, rd, rs1, rs2);
}

static inline void rvjit64_native_sraw(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit32_native_sra(block, rd, rs1, rs2);
    rvjit_host_sext32(block, rd, rd);
}

static inline void rvjit64_native_srlw(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit32_native_srl(block, rd, rs1, rs2);
    rvjit_host_sext32(block, rd, rd);
}

static inline void rvjit64_native_sllw(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit32_native_sll(block, rd, rs1, rs2);
    rvjit_host_sext32(block, rd, rd);
}

static inline void rvjit64_native_addi(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit_host_addi(block, RVJIT_INSN_ADDI, rd, rs1, imm);
}

static inline void rvjit64_native_addiw(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit32_native_addi(block, rd, rs1, imm);
    rvjit_host_sext32(block, rd, rd);
}

static inline void rvjit64_native_ori(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit_host_logic_imm(block, RVJIT_INSN_ORI, rd, rs1, imm);
}

static inline void rvjit64_native_andi(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit_host_logic_imm(block, RVJIT_INSN_ANDI, rd, rs1, imm);
}

static inline void rvjit64_native_xori(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit_host_logic_imm(block, RVJIT_INSN_XORI, rd, rs1, imm);
}

static inline void rvjit64_native_srai(rvjit_block_t* block, regid_t rd, regid_t rs1, uint8_t imm)
{
    rvjit_host_shift_imm(block, RVJIT_INSN_SRAI, rd, rs1, imm);
}

static inline void rvjit64_native_srli(rvjit_block_t* block, regid_t rd, regid_t rs1, uint8_t imm)
{
    rvjit_host_shift_imm(block, RVJIT_INSN_SRLI, rd, rs1, imm);
}

static inline void rvjit64_native_slli(rvjit_block_t* block, regid_t rd, regid_t rs1, uint8_t imm)
{
    rvjit_host_shift_imm(block, RVJIT_INSN_SLLI, rd, rs1, imm);
}

static inline void rvjit64_native_sraiw(rvjit_block_t* block, regid_t rd, regid_t rs1, uint8_t imm)
{
    rvjit32_native_srai(block, rd, rs1, imm);
    rvjit_host_sext32(block, rd, rd);
}

static inline void rvjit64_native_srliw(rvjit_block_t* block, regid_t rd, regid_t rs1, uint8_t imm)
{
    rvjit32_native_srli(block, rd, rs1, imm);
    rvjit_host_sext32(block, rd, rd);
}

static inline void rvjit64_native_slliw(rvjit_block_t* block, regid_t rd, regid_t rs1, uint8_t imm)
{
    rvjit32_native_slli(block, rd, rs1, imm);
    rvjit_host_sext32(block, rd, rd);
}

static inline void rvjit64_native_slti(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit_host_cmp_imm(block, RVJIT_INSN_SLTI, rd, rs1, imm);
}

static inline void rvjit64_native_sltiu(rvjit_block_t* block, regid_t rd, regid_t rs1, int32_t imm)
{
    rvjit_host_cmp_imm(block, RVJIT_INSN_SLTIU, rd, rs1, imm);
}

static inline void rvjit64_native_slt(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_cmp_reg(block, RVJIT_INSN_SLT, rd, rs1, rs2);
}

static inline void rvjit64_native_sltu(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_cmp_reg(block, RVJIT_INSN_SLTU, rd, rs1, rs2);
}

static inline void rvjit64_native_lb(rvjit_block_t* block, regid_t rd, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_LB, rd, base, off);
}

static inline void rvjit64_native_lbu(rvjit_block_t* block, regid_t rd, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_LBU, rd, base, off);
}

static inline void rvjit64_native_lh(rvjit_block_t* block, regid_t rd, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_LH, rd, base, off);
}

static inline void rvjit64_native_lhu(rvjit_block_t* block, regid_t rd, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_LHU, rd, base, off);
}

static inline void rvjit64_native_lw(rvjit_block_t* block, regid_t rd, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_LW, rd, base, off);
}

static inline void rvjit64_native_lwu(rvjit_block_t* block, regid_t rd, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_LWU, rd, base, off);
}

static inline void rvjit64_native_ld(rvjit_block_t* block, regid_t rd, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_LD, rd, base, off);
}

static inline void rvjit64_native_sb(rvjit_block_t* block, regid_t rs, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_SB, rs, base, off);
}

static inline void rvjit64_native_sh(rvjit_block_t* block, regid_t rs, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_SH, rs, base, off);
}

static inline void rvjit64_native_sw(rvjit_block_t* block, regid_t rs, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_SW, rs, base, off);
}

static inline void rvjit64_native_sd(rvjit_block_t* block, regid_t rs, regid_t base, int32_t off)
{
    rvjit_host_loadstore(block, RVJIT_INSN_SD, rs, base, off);
}

static inline branch_t rvjit64_native_beq(rvjit_block_t* block, regid_t rs1, regid_t rs2, branch_t handle, bool target)
{
    return rvjit_native_branch_reg(block, RVJIT_INSN_BEQ, rs1, rs2, handle, target);
}

static inline branch_t rvjit64_native_bne(rvjit_block_t* block, regid_t rs1, regid_t rs2, branch_t handle, bool target)
{
    return rvjit_native_branch_reg(block, RVJIT_INSN_BNE, rs1, rs2, handle, target);
}

static inline branch_t rvjit64_native_beqz(rvjit_block_t* block, regid_t rs1, branch_t handle, bool target)
{
    return rvjit_native_branch_imm(block, RVJIT_INSN_BEQ, rs1, 0, handle, target);
}

static inline branch_t rvjit64_native_bnez(rvjit_block_t* block, regid_t rs1, branch_t handle, bool target)
{
    return rvjit_native_branch_imm(block, RVJIT_INSN_BNE, rs1, 0, handle, target);
}

static inline branch_t rvjit64_native_blt(rvjit_block_t* block, regid_t rs1, regid_t rs2, branch_t handle, bool target)
{
    return rvjit_native_branch_reg(block, RVJIT_INSN_BLT, rs1, rs2, handle, target);
}

static inline branch_t rvjit64_native_bge(rvjit_block_t* block, regid_t rs1, regid_t rs2, branch_t handle, bool target)
{
    return rvjit_native_branch_reg(block, RVJIT_INSN_BGE, rs1, rs2, handle, target);
}

static inline branch_t rvjit64_native_bltu(rvjit_block_t* block, regid_t rs1, regid_t rs2, branch_t handle, bool target)
{
    return rvjit_native_branch_reg(block, RVJIT_INSN_BLTU, rs1, rs2, handle, target);
}

static inline branch_t rvjit64_native_bgeu(rvjit_block_t* block, regid_t rs1, regid_t rs2, branch_t handle, bool target)
{
    return rvjit_native_branch_reg(block, RVJIT_INSN_BGEU, rs1, rs2, handle, target);
}

static inline void rvjit64_native_mul(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_mul(block, RVJIT_INSN_MUL, rd, rs1, rs2);
}

static inline void rvjit64_native_mulh(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_mul(block, RVJIT_INSN_MULH, rd, rs1, rs2);
}

static inline void rvjit64_native_mulhu(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_mul(block, RVJIT_INSN_MULHU, rd, rs1, rs2);
}

static inline void rvjit64_native_mulhsu(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_mul(block, RVJIT_INSN_MULHSU, rd, rs1, rs2);
}

static inline void rvjit64_native_div(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_div_rem(block, RVJIT_INSN_DIV, rd, rs1, rs2);
}

static inline void rvjit64_native_divu(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_div_rem(block, RVJIT_INSN_DIVU, rd, rs1, rs2);
}

static inline void rvjit64_native_rem(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_div_rem(block, RVJIT_INSN_REM, rd, rs1, rs2);
}

static inline void rvjit64_native_remu(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_div_rem(block, RVJIT_INSN_REMU, rd, rs1, rs2);
}

static inline void rvjit64_native_mulw(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_mul(block, RVJIT_INSN_MULW, rd, rs1, rs2);
    rvjit_native_signext(block, rd);
}

static inline void rvjit64_native_divw(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_div_rem(block, RVJIT_INSN_DIVW, rd, rs1, rs2);
    rvjit_native_signext(block, rd);
    ;
}

static inline void rvjit64_native_divuw(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_div_rem(block, RVJIT_INSN_DIVUW, rd, rs1, rs2);
    rvjit_native_signext(block, rd);
}

static inline void rvjit64_native_remw(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_div_rem(block, RVJIT_INSN_REMW, rd, rs1, rs2);
    rvjit_native_signext(block, rd);
}

static inline void rvjit64_native_remuw(rvjit_block_t* block, regid_t rd, regid_t rs1, regid_t rs2)
{
    rvjit_host_div_rem(block, RVJIT_INSN_REMUW, rd, rs1, rs2);
    rvjit_native_signext(block, rd);
}

static inline bool rvjit_tail_jmp(rvjit_block_t* block, int32_t off)
{
    rvjit_arm64_jump(block, off, false);
    return true;
}

static inline void rvjit_patchable_ret(rvjit_block_t* block)
{
    rvjit_host_patchable_ret(block);
}

static inline void rvjit_tail_bnez(rvjit_block_t* block, regid_t addr, int32_t offset)
{
    size_t  base = block->size;
    regid_t tmp  = rvjit_claim_hreg(block);
    rvjit_host_loadstore(block, RVJIT_INSN_LW, tmp, addr, 0);
    rvjit_host_branch_imm(block, RVJIT_INSN_BNE, tmp, 0, offset + base - block->size);
    rvjit_free_hreg(block, tmp);
}

static inline void rvjit_patch_ret(void* addr)
{
    rvjit_host_patch_ret(addr);
}

static inline bool rvjit_patch_jmp(void* addr, int32_t off)
{
    return rvjit_host_patch_jump(addr, off);
}

static inline void rvjit_jmp_reg(rvjit_block_t* block, regid_t reg)
{
    rvjit_host_jump_reg(block, reg);
}

#endif
