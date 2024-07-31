/*
rvjit_arm64.h - RVJIT ARM64 Backend
Copyright (C) 2022  cerg2010cerg2010 <github.com/cerg2010cerg2010>
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

#include "rvjit.h"
#include "mem_ops.h"
#include "bit_ops.h"

#ifndef RVJIT_ARM64_H
#define RVJIT_ARM64_H

#ifdef RVJIT_ABI_SYSV
#define VM_PTR_REG 0
#endif

enum a64_regs {
    A64_XZR = 31, // when used as a source register
    A64_WZR = 31,
    A64_SP  = 31, // when used as a destination
    A64_WSP = 31
    // just use numbers for others!
};

enum a64_movw_opcs {
    A64_MOVNW = 0,
    A64_MOVZW = 2,
    A64_MOVKW = 3,
    A64_MOVN  = 4,
    A64_MOVZ  = 6,
    A64_MOVK  = 7
};

enum a64_movw_off {
    A64_MOV_0,
    A64_MOV_16,
    A64_MOV_32,
    A64_MOV_48
};

enum a64_addsub_shifted {
    A64_ADDW  = 0,
    A64_ADDSW = 1,
    A64_SUBW  = 2,
    A64_SUBSW = 3,
    A64_ADD   = 4,
    A64_ADDS  = 5,
    A64_SUB   = 6,
    A64_SUBS  = 7,
};

enum a64_addsub_imm {
    A64_ADDIW  = 0,
    A64_ADDSIW = 1,
    A64_SUBIW  = 2,
    A64_SUBSIW = 3,
    A64_ADDI   = 4,
    A64_ADDSI  = 5,
    A64_SUBI   = 6,
    A64_SUBSI  = 7,
};

enum a64_addsub_shift {
    A64_LSL = 0,
    A64_LSR = 1,
    A64_ASR = 2,
    // 3 is reserved for ADD/SUB instructions, but for logical it's:
    A64_ROR = 3,
};

enum a64_bitfield_opcs {
    A64_SBFMW = 0, // N == 0
    A64_BFMW  = 1,
    A64_UBFMW = 2,
    A64_SBFM  = 4, // N == 1
    A64_BFM   = 5,
    A64_UBFM  = 6,
};

enum a64_logical_imm {
    A64_ANDIW  = 0, // N is always 0
    A64_ORRIW  = 1,
    A64_EORIW  = 2,
    A64_ANDSIW = 3,
    A64_ANDI   = 4, // N is part of immediate
    A64_ORRI   = 5,
    A64_EORI   = 6,
    A64_ANDSI  = 7,
};

enum a64_logical_shifted {
    A64_ANDW  = (0u << 29) | (0 << 21),
    A64_BICW  = (0u << 29) | (1 << 21),
    A64_ORRW  = (1u << 29) | (0 << 21),
    A64_ORNW  = (1u << 29) | (1 << 21),
    A64_EORW  = (2u << 29) | (0 << 21),
    A64_EONW  = (2u << 29) | (1 << 21),
    A64_ANDSW = (3u << 29) | (0 << 21),
    A64_BICSW = (3u << 29) | (1 << 21),
    A64_AND   = (4u << 29) | (0 << 21),
    A64_BIC   = (4u << 29) | (1 << 21),
    A64_ORR   = (5u << 29) | (0 << 21),
    A64_ORN   = (5u << 29) | (1 << 21),
    A64_EOR   = (6u << 29) | (0 << 21),
    A64_EON   = (6u << 29) | (1 << 21),
    A64_ANDS  = (7u << 29) | (0 << 21),
    A64_BICS  = (7u << 29) | (1 << 21),
};

enum a64_csel {
    A64_CSELW  = (0u << 29) | (0 << 10),
    A64_CSINCW = (0u << 29) | (1 << 10),
    A64_CSINVW = (2u << 29) | (0 << 10),
    A64_CSNEGW = (2u << 29) | (1 << 10),
    A64_CSEL   = (4u << 29) | (0 << 10),
    A64_CSINC  = (4u << 29) | (1 << 10),
    A64_CSINV  = (6u << 29) | (0 << 10),
    A64_CSNEG  = (6u << 29) | (1 << 10),
};

enum a64_cc {
    A64_EQ, A64_NE,
    A64_CS, A64_CC,
    A64_MI, A64_PL,
    A64_VS, A64_VC,
    A64_HI, A64_LS,
    A64_GE, A64_LT,
    A64_GT, A64_LE,
    A64_AL, A64_AL2 // 1110 and 1111 are always-true
};

enum a64_b_cc {
    A64_B_EQ = (0x54U << 24) | A64_EQ,
    A64_B_NE = (0x54U << 24) | A64_NE,
    A64_B_CS = (0x54U << 24) | A64_CS,
    A64_B_CC = (0x54U << 24) | A64_CC,
    A64_B_LT = (0x54U << 24) | A64_LT,
    A64_B_GE = (0x54U << 24) | A64_GE,
    A64_CBZ  = (0xB4U << 24),
    A64_CBNZ = (0xB5U << 24),
    A64_CBZW = (0x34U << 24),
    A64_CBNZW = (0x35U << 24)
};

enum a64_ldst_imm_unsigned {
    A64_STRB   = (0u << 30) | (0 << 26) | (0 << 22),
    A64_LDRB   = (0u << 30) | (0 << 26) | (1 << 22),
    A64_LDRSB  = (0u << 30) | (0 << 26) | (2 << 22),
    A64_LDRSBW = (0u << 30) | (0 << 26) | (3 << 22),
    A64_STRH   = (1u << 30) | (0 << 26) | (0 << 22),
    A64_LDRH   = (1u << 30) | (0 << 26) | (1 << 22),
    A64_LDRSH  = (1u << 30) | (0 << 26) | (2 << 22),
    A64_LDRSHW = (1u << 30) | (0 << 26) | (3 << 22),
    A64_STRW   = (2u << 30) | (0 << 26) | (0 << 22),
    A64_LDRW   = (2u << 30) | (0 << 26) | (1 << 22),
    A64_LDRSW  = (2u << 30) | (0 << 26) | (2 << 22),
    A64_STR    = (3u << 30) | (0 << 26) | (0 << 22),
    A64_LDR    = (3u << 30) | (0 << 26) | (1 << 22),
    A64_PRFUM  = (3u << 30) | (0 << 26) | (2 << 22),
};

enum a64_dp_2src {
    A64_UDIVW = (0u << 31) | (0 << 29) | (2  << 10),
    A64_SDIVW = (0u << 31) | (0 << 29) | (3  << 10),
    A64_LSLVW = (0u << 31) | (0 << 29) | (8  << 10),
    A64_LSRVW = (0u << 31) | (0 << 29) | (9  << 10),
    A64_ASRVW = (0u << 31) | (0 << 29) | (10 << 10),
    A64_RORVW = (0u << 31) | (0 << 29) | (11 << 10),
    A64_UDIV  = (1u << 31) | (0 << 29) | (2  << 10),
    A64_SDIV  = (1u << 31) | (0 << 29) | (3  << 10),
    A64_LSLV  = (1u << 31) | (0 << 29) | (8  << 10),
    A64_LSRV  = (1u << 31) | (0 << 29) | (9  << 10),
    A64_ASRV  = (1u << 31) | (0 << 29) | (10 << 10),
    A64_RORV  = (1u << 31) | (0 << 29) | (11 << 10),
};

enum a64_dp_3src
{
    A64_MADDW   = (0u << 31) | (0 << 29) | (0 << 21) | (0 << 15),
    A64_MSUBW   = (0u << 31) | (0 << 29) | (0 << 21) | (1 << 15),
    A64_MADD    = (1u << 31) | (0 << 29) | (0 << 21) | (0 << 15),
    A64_MSUB    = (1u << 31) | (0 << 29) | (0 << 21) | (1 << 15),
    A64_SMADDL  = (1u << 31) | (0 << 29) | (1 << 21) | (0 << 15),
    A64_SMSUBL  = (1u << 31) | (0 << 29) | (1 << 21) | (1 << 15),
    A64_SMULH   = (1u << 31) | (0 << 29) | (2 << 21) | (0 << 15),
    A64_UMADDL  = (1u << 31) | (0 << 29) | (5 << 21) | (0 << 15),
    A64_UMSUBL  = (1u << 31) | (0 << 29) | (5 << 21) | (1 << 15),
    A64_UMULH   = (1u << 31) | (0 << 29) | (6 << 21) | (0 << 15),
};

static inline bool check_imm_bits(int32_t val, bitcnt_t bits)
{
    return sign_extend(val, bits) == val;
}

static inline size_t rvjit_native_default_hregmask()
{
    // X0 - X15 registers are caller-saved
    // X0 is preserver as vmptr
    // X16-X18 are possibly usable, needs more research
    return 0xFFFE;
}

static inline size_t rvjit_native_abireclaim_hregmask()
{
    // We have enough caller-saved registers, no need for push/pop as well
    return 0;
}

static inline void rvjit_native_push(rvjit_block_t* block, regid_t reg)
{
    UNUSED(block);
    UNUSED(reg);
    rvvm_fatal("Unimplemented rvjit_native_push for ARM64 backend");
}

static inline void rvjit_native_pop(rvjit_block_t* block, regid_t reg)
{
    UNUSED(block);
    UNUSED(reg);
    rvvm_fatal("Unimplemented rvjit_native_pop for ARM64 backend");
}

static inline void rvjit_native_ret(rvjit_block_t* block)
{
    rvjit_put_code(block, "\xC0\x03\x5F\xD6", 4);
}

static inline void rvjit_a64_insn32(rvjit_block_t* block, uint32_t insn)
{
    uint8_t code[sizeof(insn)];
    write_uint32_le_m(code, insn);
    rvjit_put_code(block, code, sizeof(code));
}

// Direct instruction types codegen

static inline void rvjit_a64_addsub_shifted(rvjit_block_t* block,
        enum a64_addsub_shifted opc,
        regid_t rd,
        regid_t rn,
        regid_t rm,
        enum a64_addsub_shift shift,
        uint8_t amount)
{
    rvjit_a64_insn32(block, ((uint32_t)opc << 29) | (0xB << 24) | (shift << 22) | (rm << 16) | (amount << 10) | (rn << 5) | rd);
}

static inline void rvjit_a64_addsub_imm(rvjit_block_t* block, enum a64_addsub_imm opc, regid_t rd, regid_t rn, uint16_t imm, bool shift)
{
    rvjit_a64_insn32(block, ((uint32_t)opc << 29) | (0x22 << 23) | (shift << 22) | (imm << 10) | (rn << 5) | rd);
}

static inline void rvjit_a64_logical_shifted(rvjit_block_t* block,
        enum a64_logical_shifted opc,
        regid_t rd,
        regid_t rn,
        regid_t rm,
        enum a64_addsub_shift shift,
        uint8_t amount)
{
    rvjit_a64_insn32(block, (uint32_t)opc | (0xA << 24) | (shift << 22) | (rm << 16) | (amount << 10) | (rn << 5) | rd);
}

static inline void rvjit_a64_logical_imm(rvjit_block_t* block, enum a64_logical_imm opc, regid_t rd, regid_t rn, uint8_t immr, uint8_t imms)
{
    rvjit_a64_insn32(block, ((uint32_t)opc << 29) | (0x24 << 23) | ((opc >= 4) << 22) | (immr << 16) | (imms << 10) | (rn << 5) | rd);
}

static inline void rvjit_a64_bitfield(rvjit_block_t* block, enum a64_bitfield_opcs opc, regid_t rd, regid_t rn, uint8_t immr, uint8_t imms)
{
    rvjit_a64_insn32(block, ((uint32_t)opc << 29) | (0x26 << 23) | ((opc >= 4) << 22) | (immr << 16) | (imms << 10) | (rn << 5) | rd);
}

static inline void rvjit_a64_dp_2src(rvjit_block_t* block, enum a64_dp_2src opc, regid_t rd, regid_t rn, regid_t rm)
{
    rvjit_a64_insn32(block, (uint32_t)opc | (0xD6 << 21) | (rm << 16) | (rn << 5) | rd);
}

static inline void rvjit_a64_dp_3src(rvjit_block_t* block, enum a64_dp_3src opc, regid_t rd, regid_t rn, regid_t rm, regid_t ra)
{
    rvjit_a64_insn32(block, (uint32_t)opc | (0x1B << 24) | (rm << 16) | (ra << 10) | (rn << 5) | rd);
}

static inline void rvjit_a64_movw(rvjit_block_t* block, enum a64_movw_opcs opc, regid_t rd, uint16_t imm, enum a64_movw_off off)
{
    rvjit_a64_insn32(block, ((uint32_t)opc << 29) | (0x25 << 23) | (off << 21) | (imm << 5) | rd);
}

static inline void rvjit_a64_csel(rvjit_block_t* block, enum a64_csel opc, regid_t rd, regid_t rn, regid_t rm, enum a64_cc cc)
{
    rvjit_a64_insn32(block, (uint32_t)opc | (0xD4 << 21) | (rm << 16) | (cc << 12) | (rn << 5) | rd);
}

static inline void rvjit_a64_ldst_imm_unsigned(rvjit_block_t* block, enum a64_ldst_imm_unsigned opc, regid_t rt, regid_t rn, uint16_t imm)
{
    rvjit_a64_insn32(block, (uint32_t)opc | (0x39 << 24) | (imm << 10) | (rn << 5) | rt);
}

// Wrappers, common codegen logic

static inline void rvjit_native_zero_reg(rvjit_block_t* block, regid_t reg)
{
    rvjit_a64_logical_shifted(block, A64_ORR, reg, A64_XZR, A64_XZR, A64_LSL, 0);
}

static inline void rvjit_native_signext(rvjit_block_t* block, regid_t reg)
{
    rvjit_a64_bitfield(block, A64_SBFM, reg, reg, 0, 31);
}

// Set native register reg to zero-extended 32-bit imm
static inline void rvjit_native_setreg32(rvjit_block_t* block, regid_t reg, uint32_t imm)
{
    if (imm == 0) {
        rvjit_native_zero_reg(block, reg);
    } else if ((imm & 0xFFFF) == imm) {
        rvjit_a64_movw(block, A64_MOVZ, reg, imm, A64_MOV_0);
    } else if ((imm & 0xFFFF0000) == imm) {
        rvjit_a64_movw(block, A64_MOVZ, reg, (imm >> 16) & 0xFFFF, A64_MOV_16);
    } else {
        rvjit_a64_movw(block, A64_MOVZ, reg, imm & 0xFFFF, A64_MOV_0);
        rvjit_a64_movw(block, A64_MOVK, reg, (imm >> 16) & 0xFFFF, A64_MOV_16);
    }
}

// Set native register reg to sign-extended 32-bit imm
static inline void rvjit_native_setreg32s(rvjit_block_t* block, regid_t reg, int32_t imm)
{
    if (imm >= 0) {
        rvjit_native_setreg32(block, reg, imm);
    } else if ((~imm & ~0xFFFF) == 0) {
        rvjit_a64_movw(block, A64_MOVN, reg, (~imm) & 0xFFFF, A64_MOV_0);
    } else if ((~imm & ~0xFFFF0000) == 0) {
        rvjit_a64_movw(block, A64_MOVN, reg, ~(imm >> 16) & 0xFFFF, A64_MOV_16);
    } else {
        rvjit_a64_movw(block, A64_MOVN, reg, ~imm & 0xFFFF, A64_MOV_0);
        rvjit_a64_movw(block, A64_MOVK, reg, (imm >> 16) & 0xFFFF, A64_MOV_16);
    }
}

// Set native register reg to wide imm
static inline void rvjit_native_setregw(rvjit_block_t* block, regid_t reg, uintptr_t imm)
{
    if ((imm & 0xFFFFFFFF) == imm) {
        rvjit_native_setreg32(block, reg, (uint32_t) imm);
    } else if ((~imm & 0xFFFFFFFF) == ~imm) {
        rvjit_native_setreg32s(block, reg, (int32_t) imm);
    } else {
        rvjit_a64_movw(block, A64_MOVZ, reg, imm & 0xFFFF, A64_MOV_0);
        rvjit_a64_movw(block, A64_MOVK, reg, (imm >> 16) & 0xFFFF, A64_MOV_16);
        rvjit_a64_movw(block, A64_MOVK, reg, (imm >> 32) & 0xFFFF, A64_MOV_32);
        rvjit_a64_movw(block, A64_MOVK, reg, (imm >> 48) & 0xFFFF, A64_MOV_48);
    }
}

static inline void rvjit_a64_b_reloc(void* addr, uint32_t offset)
{
    uint32_t mask = bit_mask(26);
    uint32_t insn = (0x5 << 26) | ((offset >> 2) & mask);
    write_uint32_le_m(addr, insn);
}

static inline void rvjit_a64_b(rvjit_block_t* block, uint32_t offset)
{
    uint8_t insn[4];
    rvjit_a64_b_reloc(insn, offset);
    rvjit_a64_insn32(block, read_uint32_le_m(insn));
}

static inline branch_t rvjit_native_jmp(rvjit_block_t* block, branch_t handle, bool label)
{
    if (label) {
        // We want to set a label for a branch
        if (handle == BRANCH_NEW) {
            // We don't have a handle - just set the label. This is a backward jump.
            return block->size;
        } else {
            // We have an instruction handle - this is a forward jump, relocate the address.
            rvjit_a64_b_reloc(block->code + handle, block->size - handle);
            return BRANCH_NEW;
        }
    } else {
        // We want to emit a branch instruction
        if (handle == BRANCH_NEW) {
            // We don't have an address - it will be patched in the future. This is a forward jump.
            branch_t tmp = block->size;
            rvjit_a64_b(block, 0);
            return tmp;
        } else {
            // We have a branch address - emit a full instruction. This is a backward jump.
            rvjit_a64_b(block, handle - block->size);
            return BRANCH_NEW;
        }
    }
}

static inline bool rvjit_a64_addi_shifted(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm, bool bits_64)
{
    enum a64_addsub_imm opc = (imm < 0) ? (bits_64 ? A64_SUBI : A64_SUBIW) : (bits_64 ? A64_ADDI : A64_ADDIW);
    if (imm < 0) imm = -imm;
    if (imm == (imm & 0xFFF)) {
        rvjit_a64_addsub_imm(block, opc, hrds, hrs1, imm, false);
        return true;
    } else if (imm == (imm & 0xFFF000)) {
        rvjit_a64_addsub_imm(block, opc, hrds, hrs1, imm >> 12, true);
        return true;
    }
    return false;
}

static inline void rvjit_a64_addi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm, bool bits_64)
{
    if (imm == 0) {
        if (hrds != hrs1 || !bits_64) {
            rvjit_a64_logical_shifted(block, A64_ORR, hrds, A64_XZR, hrs1, A64_LSL, 0);
        }
    } else if (!rvjit_a64_addi_shifted(block, hrds, hrs1, imm, bits_64)) {
        regid_t rtmp = rvjit_claim_hreg(block);
        rvjit_native_setreg32s(block, rtmp, imm);
        rvjit_a64_addsub_shifted(block, bits_64 ? A64_ADD : A64_ADDW, hrds, hrs1, rtmp, A64_LSL, 0);
        rvjit_free_hreg(block, rtmp);
    }
}

static inline bool rvjit_a64_ispow2(unsigned long long x)
{
    return x != 0 && (x & (x - 1)) == 0;
}

// Checks that immediate value is suitable for use in logical operation
bool rvjit_a64_check_logical_imm64(uint64_t val, unsigned *rot, unsigned *len)
{
    unsigned rotation, count;
    if ((int64_t)val < 0) {
        val = ~val;
        if (val == 0) return false;
        rotation = bit_clz64(val);
        count = rotation + bit_ctz64(val);
        if (!rvjit_a64_ispow2((val >> (count - rotation)) + 1)) {
            return false;
        }
    } else {
        if (val == 0) return false;
        rotation = (sizeof(val) * 8 - bit_ctz64(val));
        count = rotation - bit_clz64(val);
        rotation &= sizeof(val) * 8 - 1;
        if (!rvjit_a64_ispow2((val >> (sizeof(val) * 8 - rotation)) + 1)) {
            return false;
        }
    }

    *rot = rotation;
    *len = count - 1;
    return true;
}

bool rvjit_a64_check_logical_imm32(uint32_t val, unsigned *rot, unsigned *len)
{
    unsigned rotation, count;
    if ((int32_t)val < 0) {
        val = ~val;
        if (val == 0) return false;
        rotation = bit_clz32(val);
        count = rotation + bit_ctz32(val);
        if (!rvjit_a64_ispow2((val >> (count - rotation)) + 1)) {
            return false;
        }
    } else {
        if (val == 0) return false;
        rotation = (sizeof(val) * 8 - bit_ctz32(val));
        count = rotation - bit_clz32(val);
        rotation &= sizeof(val) * 8 - 1;
        if (!rvjit_a64_ispow2((val >> (sizeof(val) * 8 - rotation)) + 1)) {
            return false;
        }
    }

    *rot = rotation;
    *len = count - 1;
    return true;
}

static enum a64_logical_shifted rvjit_a64_logical_imm_to_shifted(enum a64_logical_imm opc)
{
    switch (opc) {
        case A64_ANDIW:  return A64_ANDW;
        case A64_ORRIW:  return A64_ORRW;
        case A64_EORIW:  return A64_EORW;
        case A64_ANDSIW: return A64_ANDSW;
        case A64_ANDI:   return A64_AND;
        case A64_ORRI:   return A64_ORR;
        case A64_EORI:   return A64_EOR;
        case A64_ANDSI:  return A64_ANDS;
        default: rvvm_fatal("Invalid opcode in rvjit_a64_logical_imm_to_shifted()"); return 0;
    }
}

static void rvjit_a64_native_log_op32(rvjit_block_t* block, enum a64_logical_imm opc, regid_t rd, regid_t rn, int32_t imm)
{
    unsigned rotation, count;
    if (rvjit_a64_check_logical_imm32((int64_t)imm, &rotation, &count)) {
        rvjit_a64_logical_imm(block, opc, rd, rn, rotation, count);
    } else {
        regid_t rtmp = rvjit_claim_hreg(block);
        enum a64_logical_shifted sopc = rvjit_a64_logical_imm_to_shifted(opc);
        rvjit_native_setreg32s(block, rtmp, imm);
        rvjit_a64_logical_shifted(block, sopc, rd, rn, rtmp, A64_LSL, 0);
        rvjit_free_hreg(block, rtmp);
    }
}

static void rvjit_a64_native_log_op64(rvjit_block_t* block, enum a64_logical_imm opc, regid_t rd, regid_t rn, int64_t imm)
{
    unsigned rotation, count;
    if (rvjit_a64_check_logical_imm64((int64_t)imm, &rotation, &count)) {
        rvjit_a64_logical_imm(block, opc, rd, rn, rotation | (1 << 6), count); /* N bit needs to be set */
    } else {
        regid_t rtmp = rvjit_claim_hreg(block);
        enum a64_logical_shifted sopc = rvjit_a64_logical_imm_to_shifted(opc);
        rvjit_native_setregw(block, rtmp, imm);
        rvjit_a64_logical_shifted(block, sopc, rd, rn, rtmp, A64_LSL, 0);
        rvjit_free_hreg(block, rtmp);
    }
}

static inline bool rvjit_a64_is_store(enum a64_ldst_imm_unsigned opc)
{
    return !bit_cut(opc, 22, 2);
}

static inline void rvjit_a64_mem_op(rvjit_block_t* block, enum a64_ldst_imm_unsigned opc, regid_t dest, regid_t addr, int32_t off)
{
    uint8_t mask = (opc >> 30) & 3;
    int32_t ldst_off = (off >> mask) & bit_mask(12);
    off -= (ldst_off << mask);
    if (off == 0) {
        rvjit_a64_ldst_imm_unsigned(block, opc, dest, addr, ldst_off);
    } else if (rvjit_a64_addi_shifted(block, addr, addr, off, true)) {
        rvjit_a64_ldst_imm_unsigned(block, opc, dest, addr, ldst_off);
        if (rvjit_a64_is_store(opc) || dest != addr) {
            rvjit_a64_addi_shifted(block, addr, addr, -off, true);
        }
    } else {
        regid_t rtmp = rvjit_claim_hreg(block);
        rvjit_native_setreg32s(block, rtmp, off);
        rvjit_a64_addsub_shifted(block, A64_ADD, rtmp, rtmp, addr, A64_LSL, 0);
        rvjit_a64_ldst_imm_unsigned(block, opc, dest, rtmp, ldst_off);
        rvjit_free_hreg(block, rtmp);
    }
}

static inline bool rvjit_is_valid_branch_imm(int32_t imm)
{
    return check_imm_bits(imm, 21) && (imm & 0x3) == 0;
}

static inline void rvjit_a64_b_cond_reloc(void* addr, uint32_t opcode, int32_t offset)
{
    if (!rvjit_is_valid_branch_imm(offset)) rvvm_fatal("Illegal branch offset in RVJIT!");
    uint32_t insn = opcode | ((offset << 3) & 0xFFFFE0);
    write_uint32_le_m(addr, insn);
}

static inline void rvjit_a64_b_cond(rvjit_block_t* block, uint32_t opcode, int32_t offset)
{
    uint8_t insn[4];
    rvjit_a64_b_cond_reloc((void*) &insn, opcode, offset);
    rvjit_a64_insn32(block, read_uint32_le_m(insn));
}

static inline branch_t rvjit_a64_bcc(rvjit_block_t* block, uint32_t opcode, branch_t handle, bool label)
{
    if (label) {
        // We want to set a label for a branch
        if (handle == BRANCH_NEW) {
            // We don't have a handle - just set the label. This is a backward jump.
            return block->size;
        } else {
            // We have an instruction handle - this is a forward jump, relocate the address.
            rvjit_a64_b_cond_reloc(block->code + handle, read_uint32_le_m(block->code + handle) & 0xFF00000F, block->size - handle);
            return BRANCH_NEW;
        }
    } else {
        // We want to emit a branch instruction
        if (handle == BRANCH_NEW) {
            // We don't have an address - it will be patched in the future. This is a forward jump.
            branch_t tmp = block->size;
            rvjit_a64_b_cond(block, opcode, 0);
            return tmp;
        } else {
            // We have a branch address - emit a full instruction. This is a backward jump.
            rvjit_a64_b_cond(block, opcode, handle - block->size);
            return BRANCH_NEW;
        }
    }
}

static inline void rvjit32_native_add(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_addsub_shifted(block, A64_ADDW, hrds, hrs1, hrs2, A64_LSL, 0);
}

static inline void rvjit32_native_sub(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_addsub_shifted(block, A64_SUBW, hrds, hrs1, hrs2, A64_LSL, 0);
}

static inline void rvjit32_native_or(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_logical_shifted(block, A64_ORRW, hrds, hrs1, hrs2, A64_LSL, 0);
}

static inline void rvjit32_native_and(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_logical_shifted(block, A64_ANDW, hrds, hrs1, hrs2, A64_LSL, 0);
}

static inline void rvjit32_native_xor(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_logical_shifted(block, A64_EORW, hrds, hrs1, hrs2, A64_LSL, 0);
}

static inline void rvjit32_native_sra(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_2src(block, A64_ASRVW, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_srl(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_2src(block, A64_LSRVW, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_sll(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_2src(block, A64_LSLVW, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_addi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a64_addi(block, hrds, hrs1, imm, false);
}

static inline void rvjit32_native_ori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a64_native_log_op32(block, A64_ORRIW, hrds, hrs1, imm);
}

static inline void rvjit32_native_andi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a64_native_log_op32(block, A64_ANDIW, hrds, hrs1, imm);
}

static inline void rvjit32_native_xori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a64_native_log_op32(block, A64_EORIW, hrds, hrs1, imm);
}

static inline void rvjit32_native_srai(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_a64_bitfield(block, A64_SBFMW, hrds, hrs1, imm, 31);
}

static inline void rvjit32_native_srli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_a64_bitfield(block, A64_UBFMW, hrds, hrs1, imm, 31);
}

static inline void rvjit32_native_slli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_a64_bitfield(block, A64_UBFMW, hrds, hrs1, (-imm & 31), 31 - imm);
}

static inline void rvjit32_native_slti(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    if (imm >= 0) {
        rvjit_a64_addsub_imm(block, A64_SUBSIW, A64_WZR, hrs1, imm, false);
        rvjit_a64_csel(block, A64_CSINCW, hrds, A64_WZR, A64_WZR, A64_LT ^ 1); // CSET pseudocode - invert cond
    } else {
        rvjit_a64_addsub_imm(block, A64_ADDSIW, A64_WZR, hrs1, -imm, false);
        rvjit_a64_csel(block, A64_CSINCW, hrds, A64_WZR, A64_WZR, A64_LT ^ 1);
    }
}

static inline void rvjit32_native_sltiu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    if (imm >= 0) {
        rvjit_a64_addsub_imm(block, A64_SUBSIW, A64_WZR, hrs1, imm, false);
        rvjit_a64_csel(block, A64_CSINCW, hrds, A64_WZR, A64_WZR, A64_CC ^ 1);
    } else {
        rvjit_a64_addsub_imm(block, A64_ADDSIW, A64_WZR, hrs1, -imm, false);
        rvjit_a64_csel(block, A64_CSINCW, hrds, A64_WZR, A64_WZR, A64_CC ^ 1);
    }
}

static inline void rvjit32_native_slt(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_addsub_shifted(block, A64_SUBSW, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    rvjit_a64_csel(block, A64_CSINCW, hrds, A64_WZR, A64_WZR, A64_LT ^ 1);
}

static inline void rvjit32_native_sltu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_addsub_shifted(block, A64_SUBSW, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    rvjit_a64_csel(block, A64_CSINCW, hrds, A64_WZR, A64_WZR, A64_CC ^ 1);
}

static inline void rvjit32_native_lb(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_LDRSBW, dest, addr, off);
}

static inline void rvjit32_native_lbu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_LDRB, dest, addr, off);
}

static inline void rvjit32_native_lh(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_LDRSHW, dest, addr, off);
}

static inline void rvjit32_native_lhu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_LDRH, dest, addr, off);
}

static inline void rvjit32_native_lw(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_LDRW, dest, addr, off);
}

static inline void rvjit32_native_sb(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_STRB, src, addr, off);
}

static inline void rvjit32_native_sh(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_STRH, src, addr, off);
}

static inline void rvjit32_native_sw(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_STRW, src, addr, off);
}

static inline branch_t rvjit32_native_beq(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (!target) rvjit_a64_addsub_shifted(block, A64_SUBSW, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    return rvjit_a64_bcc(block, A64_B_EQ, handle, target);
}

static inline branch_t rvjit32_native_bne(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (!target) rvjit_a64_addsub_shifted(block, A64_SUBSW, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    return rvjit_a64_bcc(block, A64_B_NE, handle, target);
}

static inline branch_t rvjit32_native_beqz(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_a64_bcc(block, A64_CBZW | hrs1, handle, target);
}

static inline branch_t rvjit32_native_bnez(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_a64_bcc(block, A64_CBNZW | hrs1, handle, target);
}

static inline branch_t rvjit32_native_blt(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (!target) rvjit_a64_addsub_shifted(block, A64_SUBSW, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    return rvjit_a64_bcc(block, A64_B_LT, handle, target);
}

static inline branch_t rvjit32_native_bge(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (!target) rvjit_a64_addsub_shifted(block, A64_SUBSW, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    return rvjit_a64_bcc(block, A64_B_GE, handle, target);
}

static inline branch_t rvjit32_native_bltu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (!target) rvjit_a64_addsub_shifted(block, A64_SUBSW, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    return rvjit_a64_bcc(block, A64_B_CC, handle, target);
}

static inline branch_t rvjit32_native_bgeu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (!target) rvjit_a64_addsub_shifted(block, A64_SUBSW, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    return rvjit_a64_bcc(block, A64_B_CS, handle, target);
}

/* RV64 */

static inline void rvjit64_native_add(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_addsub_shifted(block, A64_ADD, hrds, hrs1, hrs2, A64_LSL, 0);
}

static inline void rvjit64_native_sub(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_addsub_shifted(block, A64_SUB, hrds, hrs1, hrs2, A64_LSL, 0);
}

static inline void rvjit64_native_addw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_addsub_shifted(block, A64_ADDW, hrds, hrs1, hrs2, A64_LSL, 0);
    rvjit_native_signext(block, hrds);
}

static inline void rvjit64_native_subw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_addsub_shifted(block, A64_SUBW, hrds, hrs1, hrs2, A64_LSL, 0);
    rvjit_native_signext(block, hrds);
}

static inline void rvjit64_native_or(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_logical_shifted(block, A64_ORR, hrds, hrs1, hrs2, A64_LSL, 0);
}

static inline void rvjit64_native_and(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_logical_shifted(block, A64_AND, hrds, hrs1, hrs2, A64_LSL, 0);
}

static inline void rvjit64_native_xor(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_logical_shifted(block, A64_EOR, hrds, hrs1, hrs2, A64_LSL, 0);
}

static inline void rvjit64_native_sra(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_2src(block, A64_ASRV, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_srl(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_2src(block, A64_LSRV, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_sll(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_2src(block, A64_LSLV, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_sraw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_2src(block, A64_ASRVW, hrds, hrs1, hrs2);
    rvjit_native_signext(block, hrds);
}

static inline void rvjit64_native_srlw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_2src(block, A64_LSRVW, hrds, hrs1, hrs2);
    rvjit_native_signext(block, hrds);
}

static inline void rvjit64_native_sllw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_2src(block, A64_LSLVW, hrds, hrs1, hrs2);
    rvjit_native_signext(block, hrds);
}

static inline void rvjit64_native_addi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a64_addi(block, hrds, hrs1, imm, true);
}

static inline void rvjit64_native_addiw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit32_native_addi(block, hrds, hrs1, imm);
    rvjit_native_signext(block, hrds);
}

static inline void rvjit64_native_ori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a64_native_log_op64(block, A64_ORRI, hrds, hrs1, imm);
}

static inline void rvjit64_native_andi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a64_native_log_op64(block, A64_ANDI, hrds, hrs1, imm);
}

static inline void rvjit64_native_xori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a64_native_log_op64(block, A64_EORI, hrds, hrs1, imm);
}

static inline void rvjit64_native_srai(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_a64_bitfield(block, A64_SBFM, hrds, hrs1, imm, 63);
}

static inline void rvjit64_native_srli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_a64_bitfield(block, A64_UBFM, hrds, hrs1, imm, 63);
}

static inline void rvjit64_native_slli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_a64_bitfield(block, A64_UBFM, hrds, hrs1, (-imm & 63), 63 - imm);
}

static inline void rvjit64_native_sraiw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_a64_bitfield(block, A64_SBFMW, hrds, hrs1, imm, 31);
    rvjit_native_signext(block, hrds);
}

static inline void rvjit64_native_srliw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_a64_bitfield(block, A64_UBFMW, hrds, hrs1, imm, 31);
    rvjit_native_signext(block, hrds);
}

static inline void rvjit64_native_slliw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_a64_bitfield(block, A64_UBFMW, hrds, hrs1, (-imm & 31), 31 - imm);
    rvjit_native_signext(block, hrds);
}

static inline void rvjit64_native_slti(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    if (imm >= 0) {
        rvjit_a64_addsub_imm(block, A64_SUBSI, A64_WZR, hrs1, imm, false);
        rvjit_a64_csel(block, A64_CSINC, hrds, A64_WZR, A64_WZR, A64_LT ^ 1); // CSET pseudocode - invert cond
    } else {
        rvjit_a64_addsub_imm(block, A64_ADDSI, A64_WZR, hrs1, -imm, false);
        rvjit_a64_csel(block, A64_CSINC, hrds, A64_WZR, A64_WZR, A64_LT ^ 1);
    }
}

static inline void rvjit64_native_sltiu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    if (imm >= 0) {
        rvjit_a64_addsub_imm(block, A64_SUBSI, A64_WZR, hrs1, imm, false);
        rvjit_a64_csel(block, A64_CSINC, hrds, A64_WZR, A64_WZR, A64_CC ^ 1);
    } else {
        rvjit_a64_addsub_imm(block, A64_ADDSI, A64_WZR, hrs1, -imm, false);
        rvjit_a64_csel(block, A64_CSINC, hrds, A64_WZR, A64_WZR, A64_CC ^ 1);
    }
}

static inline void rvjit64_native_slt(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_addsub_shifted(block, A64_SUBS, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    rvjit_a64_csel(block, A64_CSINC, hrds, A64_WZR, A64_WZR, A64_LT ^ 1);
}

static inline void rvjit64_native_sltu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_addsub_shifted(block, A64_SUBS, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    rvjit_a64_csel(block, A64_CSINC, hrds, A64_WZR, A64_WZR, A64_CC ^ 1);
}

static inline void rvjit64_native_lb(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_LDRSB, dest, addr, off);
}

static inline void rvjit64_native_lbu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_LDRB, dest, addr, off);
}

static inline void rvjit64_native_lh(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_LDRSH, dest, addr, off);
}

static inline void rvjit64_native_lhu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_LDRH, dest, addr, off);
}

static inline void rvjit64_native_lw(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_LDRSW, dest, addr, off);
}

static inline void rvjit64_native_lwu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_LDRW, dest, addr, off);
}

static inline void rvjit64_native_ld(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_LDR, dest, addr, off);
}

static inline void rvjit64_native_sb(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_STRB, src, addr, off);
}

static inline void rvjit64_native_sh(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_STRH, src, addr, off);
}

static inline void rvjit64_native_sw(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_STRW, src, addr, off);
}

static inline void rvjit64_native_sd(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a64_mem_op(block, A64_STR, dest, addr, off);
}

static inline branch_t rvjit64_native_beq(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (!target) rvjit_a64_addsub_shifted(block, A64_SUBS, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    return rvjit_a64_bcc(block, A64_B_EQ, handle, target);
}

static inline branch_t rvjit64_native_bne(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (!target) rvjit_a64_addsub_shifted(block, A64_SUBS, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    return rvjit_a64_bcc(block, A64_B_NE, handle, target);
}

static inline branch_t rvjit64_native_beqz(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_a64_bcc(block, A64_CBZ | hrs1, handle, target);
}

static inline branch_t rvjit64_native_bnez(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_a64_bcc(block, A64_CBNZ | hrs1, handle, target);
}

static inline branch_t rvjit64_native_blt(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (!target) rvjit_a64_addsub_shifted(block, A64_SUBS, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    return rvjit_a64_bcc(block, A64_B_LT, handle, target);
}

static inline branch_t rvjit64_native_bge(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (!target) rvjit_a64_addsub_shifted(block, A64_SUBS, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    return rvjit_a64_bcc(block, A64_B_GE, handle, target);
}

static inline branch_t rvjit64_native_bltu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (!target) rvjit_a64_addsub_shifted(block, A64_SUBS, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    return rvjit_a64_bcc(block, A64_B_CC, handle, target);
}

static inline branch_t rvjit64_native_bgeu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    if (!target) rvjit_a64_addsub_shifted(block, A64_SUBS, A64_WZR, hrs1, hrs2, A64_LSL, 0);
    return rvjit_a64_bcc(block, A64_B_CS, handle, target);
}

// Multiply / Divide

static inline void rvjit_a64_native_div(rvjit_block_t* block, enum a64_dp_2src divopc, bool is32bit, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    branch_t ifzero = rvjit64_native_beqz(block, hrs2, BRANCH_NEW, false);

    regid_t hrmostneg = rvjit_claim_hreg(block);
    rvjit_a64_movw(block, is32bit ? A64_MOVNW : A64_MOVN, hrmostneg, 0, A64_MOV_0);
    branch_t ifmin1 = rvjit64_native_bne(block, hrs2, hrmostneg, BRANCH_NEW, false);

    if (!(divopc & (1u << 31))) {
        rvjit_native_setregw(block, hrmostneg, (uintptr_t)1 << 63);
    } else {
        rvjit_a64_movw(block, is32bit ? A64_MOVZW : A64_MOVZ, hrmostneg, 1 << 15, A64_MOV_16);
        if (!is32bit) rvjit_native_signext(block, hrmostneg);
    }
    branch_t ifmin2 = rvjit64_native_bne(block, hrs1, hrmostneg, BRANCH_NEW, false);

    rvjit_a64_addsub_shifted(block, is32bit ? A64_ADDW : A64_ADD, hrds, hrmostneg, A64_XZR, A64_LSL, 0);
    branch_t skipdiv = rvjit_native_jmp(block, BRANCH_NEW, false);

    rvjit64_native_bne(block, hrs2, hrmostneg, ifmin1, true);
    rvjit64_native_bne(block, hrs2, hrmostneg, ifmin2, true);
    rvjit_a64_dp_2src(block, divopc, hrds, hrs1, hrs2);
    if (!is32bit && !(divopc & (1u << 31))) {
        rvjit_native_signext(block, hrds);
    }

    rvjit_native_jmp(block, skipdiv, true);
    rvjit_free_hreg(block, hrmostneg);
    branch_t toend = rvjit_native_jmp(block, BRANCH_NEW, false);
    rvjit64_native_beqz(block, hrs2, ifzero, true);
    rvjit_a64_movw(block, is32bit ? A64_MOVNW : A64_MOVN, hrds, 0, A64_MOV_0);
    rvjit_native_jmp(block, toend, true);
}

static inline void rvjit_a64_native_divu(rvjit_block_t* block, enum a64_dp_2src divopc, bool is32bit, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    branch_t ifzero = rvjit64_native_beqz(block, hrs2, BRANCH_NEW, false);

    rvjit_a64_dp_2src(block, divopc, hrds, hrs1, hrs2);
    if (!is32bit && !(divopc & (1u << 31))) {
        rvjit_native_signext(block, hrds);
    }

    branch_t toend = rvjit_native_jmp(block, BRANCH_NEW, false);
    rvjit64_native_beqz(block, hrs2, ifzero, true);
    rvjit_a64_movw(block, A64_MOVN, hrds, 0, A64_MOV_0);
    rvjit_native_jmp(block, toend, true);
}

static inline void rvjit_a64_native_rem(rvjit_block_t* block, enum a64_dp_2src divopc, enum a64_dp_3src mulopc, bool is32bit, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    branch_t ifzero = rvjit64_native_beqz(block, hrs2, BRANCH_NEW, false);

    regid_t hrmostneg = rvjit_claim_hreg(block);
    rvjit_a64_movw(block, is32bit ? A64_MOVNW : A64_MOVN, hrmostneg, 0, A64_MOV_0);
    branch_t ifmin1 = rvjit64_native_bne(block, hrs2, hrmostneg, BRANCH_NEW, false);

    if (!(divopc & (1u << 31))) {
        rvjit_native_setregw(block, hrmostneg, (uintptr_t)1 << 63);
    } else {
        rvjit_a64_movw(block, is32bit ? A64_MOVZW : A64_MOVZ, hrmostneg, 1 << 15, A64_MOV_16);
        if (!is32bit) rvjit_native_signext(block, hrmostneg);
    }
    branch_t ifmin2 = rvjit64_native_bne(block, hrs1, hrmostneg, BRANCH_NEW, false);

    rvjit_native_zero_reg(block, hrds);
    branch_t skipdiv = rvjit_native_jmp(block, BRANCH_NEW, false);

    rvjit64_native_bne(block, hrs2, hrmostneg, ifmin1, true);
    rvjit64_native_bne(block, hrs2, hrmostneg, ifmin2, true);

    rvjit_a64_dp_2src(block, divopc, hrmostneg, hrs1, hrs2);
    rvjit_a64_dp_3src(block, mulopc, hrds, hrmostneg, hrs2, hrs1);
    if (!is32bit && !(divopc & (1u << 31))) {
        rvjit_native_signext(block, hrds);
    }

    rvjit_native_jmp(block, skipdiv, true);
    rvjit_free_hreg(block, hrmostneg);
    branch_t toend = rvjit_native_jmp(block, BRANCH_NEW, false);
    rvjit64_native_beqz(block, hrs2, ifzero, true);
    rvjit_a64_addsub_shifted(block, is32bit ? A64_ADDW : A64_ADD, hrds, hrs1, A64_XZR, A64_LSL, 0);
    rvjit_native_jmp(block, toend, true);
}

static inline void rvjit64_native_mul(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_3src(block, A64_MADD, hrds, hrs1, hrs2, A64_XZR);
}

static inline void rvjit64_native_mulh(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_3src(block, A64_SMULH, hrds, hrs1, hrs2, A64_XZR);
}

static inline void rvjit64_native_mulhu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_3src(block, A64_UMULH, hrds, hrs1, hrs2, A64_XZR);
}

static inline void rvjit64_native_mulhsu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    regid_t hrs1h = rvjit_claim_hreg(block);
    rvjit64_native_srai(block, hrs1h, hrs1, 63);
    regid_t hrdsu = rvjit_claim_hreg(block);
    rvjit64_native_mulhu(block, hrdsu, hrs2, hrs1);
    rvjit_a64_dp_3src(block, A64_MADD, hrds, hrs2, hrs1h, hrdsu);
    rvjit_free_hreg(block, hrs1h);
    rvjit_free_hreg(block, hrdsu);
}

static inline void rvjit64_native_div(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_native_div(block, A64_SDIV, false, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_divu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_native_divu(block, A64_UDIV, false, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_rem(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_native_rem(block, A64_SDIV, A64_MSUB, false, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_remu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_native_rem(block, A64_UDIV, A64_MSUB, false, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_mulw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_3src(block, A64_MADDW, hrds, hrs1, hrs2, A64_XZR);
    rvjit_native_signext(block, hrds);
}

static inline void rvjit64_native_divw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_native_div(block, A64_SDIVW, false, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_divuw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_native_divu(block, A64_UDIVW, false, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_remw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_native_rem(block, A64_SDIVW, A64_MSUBW, false, hrds, hrs1, hrs2);
}

static inline void rvjit64_native_remuw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_native_rem(block, A64_UDIVW, A64_MSUBW, false, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_mul(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_3src(block, A64_MADDW, hrds, hrs1, hrs2, A64_XZR);
}

static inline void rvjit32_native_mulh(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_3src(block, A64_SMADDL, hrds, hrs1, hrs2, A64_XZR);
    rvjit64_native_srli(block, hrds, hrds, 32);
}

static inline void rvjit32_native_mulhu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_dp_3src(block, A64_UMADDL, hrds, hrs1, hrs2, A64_XZR);
    rvjit64_native_srli(block, hrds, hrds, 32);
}

static inline void rvjit32_native_mulhsu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    regid_t hrsext = rvjit_claim_hreg(block);
    rvjit_a64_bitfield(block, A64_SBFM, hrsext, hrs1, 0, 31);
    regid_t hrzext = rvjit_claim_hreg(block);
    rvjit_a64_addsub_shifted(block, A64_ADDW, hrzext, hrs2, A64_XZR, A64_LSL, 0);
    rvjit64_native_mul(block, hrsext, hrzext, hrsext);
    rvjit64_native_srli(block, hrds, hrsext, 32);
    rvjit_free_hreg(block, hrsext);
    rvjit_free_hreg(block, hrzext);
}

static inline void rvjit32_native_div(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_native_div(block, A64_SDIVW, true, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_divu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_native_divu(block, A64_UDIVW, true, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_rem(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_native_rem(block, A64_SDIVW, A64_MSUB, true, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_remu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a64_native_rem(block, A64_UDIVW, A64_MSUB, true, hrds, hrs1, hrs2);
}

/*
* Linker routines
*/

static inline bool rvjit_a64_valid_reloc(int32_t offset)
{
    return check_imm_bits(offset, 28) && (offset & 0x3) == 0;
}

// Emit jump instruction (may return false if offset cannot be encoded)
static inline bool rvjit_tail_jmp(rvjit_block_t* block, int32_t offset)
{
    if (rvjit_a64_valid_reloc(offset)) {
        rvjit_a64_b(block, offset);
        return true;
    }
    return false;
}

// Emit patchable ret instruction
static inline void rvjit_patchable_ret(rvjit_block_t* block)
{
    // Always 4-bytes, same as jmp
    rvjit_native_ret(block);
}

// Jump if word pointed to by addr is nonzero (may emit nothing if the offset cannot be encoded)
// Used to check interrupts in block linkage
static inline void rvjit_tail_bnez(rvjit_block_t* block, regid_t addr, int32_t offset)
{
    size_t offset_fixup = block->size;
    int32_t off;
    regid_t tmp = rvjit_claim_hreg(block);
    rvjit32_native_lw(block, tmp, addr, 0);

    off = offset - (block->size - offset_fixup);
    if (rvjit_is_valid_branch_imm(off)) {
        // Offset fits into branch instruction
        rvjit_a64_b_cond(block, A64_CBNZW | tmp, off);
    } else {
        // Use A64_B for 28-bit offset
        branch_t l1 = rvjit32_native_beqz(block, tmp, BRANCH_NEW, false);
        off = offset - (block->size - offset_fixup);
        if (rvjit_a64_valid_reloc(off)) {
            rvjit_a64_b(block, off);
        } else {
            rvvm_warn("Unimplemented 32-bit relative jump in ARM64 RVJIT backend!");
        }
        rvjit32_native_beqz(block, tmp, l1, true);
    }

    rvjit_free_hreg(block, tmp);
}

// Patch instruction at addr into ret
static inline void rvjit_patch_ret(void* addr)
{
    write_uint32_le_m(addr, 0xD65F03C0);
}

// Patch jump instruction at addr (may return false if offset cannot be encoded)
static inline bool rvjit_patch_jmp(void* addr, int32_t offset)
{
    if (rvjit_a64_valid_reloc(offset)) {
        write_uint32_le_m(addr, 0);
        rvjit_a64_b_reloc(addr, offset);
        return true;
    }
    return false;
}

static inline void rvjit_jmp_reg(rvjit_block_t* block, regid_t reg)
{
    rvjit_a64_insn32(block, 0xD61F0000 | (reg << 5));
}

#endif
