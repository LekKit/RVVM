/*
rvjit_arm.h - RVJIT ARM Backend
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

#if defined(__linux__)
#include <sys/auxv.h>
#else
#warning No RVJIT ARM CPU flags detection for target OS
#endif

#ifndef RVJIT_ARM_H
#define RVJIT_ARM_H

#define rvjit_a32_assert(expr) \
do { \
    if (unlikely(!(expr))) rvvm_fatal("Assertion (" #expr ") failed at " SOURCE_LINE); \
} while (0)

static int32_t rvjit_a32_soft_idiv(int32_t a, int32_t b)
{
    return a / b;
}

extern uint32_t rvjit_a32_soft_uidiv(uint32_t a, uint32_t b)
{
    return a / b;
}

#define RVJIT_ARM_IDIVA (1 << 17)

static uint32_t rvjit_a32_hwcaps = 0;

static inline void rvjit_a32_test_cpu()
{
#ifdef __linux__
    rvjit_a32_hwcaps = getauxval(AT_HWCAP);
#endif
    if (rvjit_a32_hwcaps & RVJIT_ARM_IDIVA) {
        rvvm_info("RVJIT detected ARM IDIV/UDIV extension");
    }
}

static bool rvjit_a32_check_div()
{
    DO_ONCE(rvjit_a32_test_cpu());
    return !!(rvjit_a32_hwcaps & RVJIT_ARM_IDIVA);
}

static inline bool check_imm_bits(int32_t val, bitcnt_t bits)
{
    return sign_extend(val, bits) == val;
}

#ifdef RVJIT_ABI_SYSV
#define VM_PTR_REG 0 // Argument/scratch reg 1
#endif

static inline size_t rvjit_native_default_hregmask()
{
    return rvjit_hreg_mask(1)  /* argument/result/scratch reg 2 */
         | rvjit_hreg_mask(2)  /* argument/scratch reg 3 */
         | rvjit_hreg_mask(3); /* argument/scratch reg 4 */
}

static inline size_t rvjit_native_abireclaim_hregmask()
{
    return rvjit_hreg_mask(4)   /* variable reg 1 */
         | rvjit_hreg_mask(5)   /* variable reg 2 */
         | rvjit_hreg_mask(6)   /* variable reg 3 */
         | rvjit_hreg_mask(7)   /* variable reg 4 */
         | rvjit_hreg_mask(8)   /* variable reg 5 */
         | rvjit_hreg_mask(9)   /* platform/variable reg 6 */
         | rvjit_hreg_mask(10)  /* variable reg 7 */
         | rvjit_hreg_mask(11); /* frame pointer/variable reg 8 */
}

enum a32_regs
{
    A32_FP = 11, /* frame pointer */
    A32_IP = 12, /* intra-procedure call scratch register */
    A32_SP = 13, /* stack pointer */
    A32_LR = 14, /* link register (return address) */
    A32_PC = 15, /* program counter */
    /* use numbers for others! */
};

enum a32_cc
{
    A32_EQ, A32_NE,
    A32_CS, A32_CC,
    A32_MI, A32_PL,
    A32_VS, A32_VC,
    A32_HI, A32_LS,
    A32_GE, A32_LT,
    A32_GT, A32_LE,
    A32_AL, A32_UNCOND
};
typedef uint32_t a32_cc_t;

enum a32_shtype
{
    A32_LSL = 0,
    A32_LSR = 1,
    A32_ASR = 2,
    A32_ROR = 3,
};
typedef uint32_t a32_shtype_t;

enum a32_dp_opcs
{
    A32_AND  = (0  << 21) | (0 << 20),
    A32_ANDS = (0  << 21) | (1 << 20),
    A32_EOR  = (1  << 21) | (0 << 20),
    A32_EORS = (1  << 21) | (1 << 20),
    A32_SUB  = (2  << 21) | (0 << 20),
    A32_SUBS = (2  << 21) | (1 << 20),
    A32_RSB  = (3  << 21) | (0 << 20),
    A32_RSBS = (3  << 21) | (1 << 20),
    A32_ADD  = (4  << 21) | (0 << 20),
    A32_ADDS = (4  << 21) | (1 << 20),
    A32_ADC  = (5  << 21) | (0 << 20),
    A32_ADCS = (5  << 21) | (1 << 20),
    A32_SBC  = (6  << 21) | (0 << 20),
    A32_SBCS = (6  << 21) | (1 << 20),
    A32_RSC  = (7  << 21) | (0 << 20),
    A32_RSCS = (7  << 21) | (1 << 20),
    A32_TST  = (8  << 21) | (1 << 20),
    A32_BX   = (9  << 21) | (0 << 20),
    A32_TEQ  = (9  << 21) | (1 << 20),
    A32_CMP  = (10 << 21) | (1 << 20),
    A32_CMN  = (11 << 21) | (1 << 20),
    A32_ORR  = (12 << 21) | (0 << 20),
    A32_ORRS = (12 << 21) | (1 << 20),
    A32_MOV  = (13 << 21) | (0 << 20),
    A32_MOVS = (13 << 21) | (1 << 20),
    A32_BIC  = (14 << 21) | (0 << 20),
    A32_BICS = (14 << 21) | (1 << 20),
    A32_MVN  = (15 << 21) | (0 << 20),
    A32_MVNS = (15 << 21) | (1 << 20),
};
typedef uint32_t a32_dp_opcs_t;

enum a32_ma_opcs
{
    A32_MUL  =   (0 << 21) | (0 << 20),
    A32_MULS =   (0 << 21) | (1 << 20),
    // NOTE: MLA/MLAS use rdlo as ra and rdhi as rds
    A32_MLA  =   (1 << 21) | (0 << 20),
    A32_MLAS =   (1 << 21) | (1 << 20),
    // NOTE: UMAAL use rdhi and rdlo for accumulate accumulate
    A32_UMAAL =  (2 << 21) | (0 << 20),
    // NOTE: MLS use rdlo as ra and rdhi as rds
    A32_MLS =    (3 << 21) | (0 << 20),
    A32_UMULL =  (4 << 21) | (0 << 20),
    A32_UMULLS = (4 << 21) | (1 << 20),
    // NOTE: UMLAL/UMLALS use rdhi:rdlo for accumulate
    A32_UMLAL =  (5 << 21) | (0 << 20),
    A32_UMLALS = (5 << 21) | (1 << 20),
    A32_SMULL =  (6 << 21) | (0 << 20),
    A32_SMULLS = (6 << 21) | (1 << 20),
    // NOTE: SMLAL/SMLALS use rdhi:rdlo for accumulate
    A32_SMLAL =  (7 << 21) | (0 << 20),
    A32_SMLALS = (7 << 21) | (0 << 20),
};
typedef uint32_t a32_ma_opcs_t;

enum a32_md_opcs
{
    A32_SDIV = (0x71 << 20),
    A32_UDIV = (0x73 << 20),
};
typedef uint32_t a32_md_opcs_t;

static inline void rvjit_a32_insn32(rvjit_block_t* block, uint32_t insn)
{
    uint8_t code[sizeof(insn)];
    write_uint32_le_m(code, insn);
    rvjit_put_code(block, code, sizeof(code));
}

static inline bool rvjit_a32_encode_imm(uint32_t imm, uint8_t* pimm, uint8_t* prot)
{
    uint8_t rotation = 0;

    // No rotation required
    if ((imm & 0xff) == imm) {
        *pimm = imm;
        *prot = 0;
        return true;
    }

    // If the value is split between top and the bottom part of the register, rotate it out
    if ((imm & 0xFFFF) && (imm & 0xFFFF0000)) {
        imm = bit_rotr32(imm, 8);
        rotation = 8;
    }

    uint8_t ctz = bit_ctz32(imm);
    rotation += ctz;
    rotation &= 31;
    imm = bit_rotr32(imm, ctz);

    // Rotation must be an even number, lower amount preferred
    if (rotation & 1) {
        --rotation;
        imm = bit_rotl32(imm, 1);
    }

    // Immediate must fit in one byte
    if (imm & ~0xff) {
        return false;
    }

    *prot = (32 - (rotation & 31)) & 31;
    *pimm = imm;
    return true;
}

static inline uint32_t rvjit_a32_shifter_imm(uint8_t imm, uint8_t rotate)
{
    rvjit_a32_assert((rotate & 1) == 0);
    return (1 << 25) | (rotate << 7) | imm;
}

static inline uint32_t rvjit_a32_shifter_reg_imm(regid_t rm, a32_shtype_t shtype, uint8_t shamt)
{
    rvjit_a32_assert((rm & ~15) == 0);
    rvjit_a32_assert((shamt & ~31) == 0);
    return (0 << 25) | (shamt << 7) | (shtype << 5) | (0 << 4) | rm;
}

static inline uint32_t rvjit_a32_shifter_reg_reg(regid_t rm, a32_shtype_t shtype, regid_t rs)
{
    rvjit_a32_assert((rm & ~15) == 0);
    rvjit_a32_assert((rs & ~15) == 0);
    return (0 << 25) | (rs << 8) | (shtype << 5) | (1 << 4) | rm;
}

static inline void rvjit_a32_dp(rvjit_block_t* block, a32_dp_opcs_t op, a32_cc_t cc, regid_t rd, regid_t rn, uint32_t shifter)
{
    rvjit_a32_assert((rd & ~15) == 0);
    rvjit_a32_assert((rn & ~15) == 0);
    rvjit_a32_insn32(block, (cc << 28) | op | shifter | (rn << 16) | (rd << 12));
}

static inline void rvjit_a32_ma(rvjit_block_t *block, a32_ma_opcs_t op, a32_cc_t cc, regid_t rdlo, regid_t rdhi, regid_t rn, regid_t rm)
{
    rvjit_a32_assert((rdhi & ~15) == 0);
    rvjit_a32_assert((rdlo & ~15) == 0);
    rvjit_a32_assert((rn & ~15) == 0);
    rvjit_a32_assert((rm & ~15) == 0);
    rvjit_a32_assert((rdhi != rdlo));
    rvjit_a32_insn32(block, (cc << 28) | op | (rdhi << 16) | (rdlo << 12) | (rm << 8) | (1 << 7) | (1 << 4) | rn);
}

static inline void rvjit_a32_ma2(rvjit_block_t *block, a32_ma_opcs_t op, a32_cc_t cc, regid_t rd, regid_t ra, regid_t rn, regid_t rm)
{
    rvjit_a32_assert((rd & ~15) == 0);
    rvjit_a32_assert((ra & ~15) == 0);
    rvjit_a32_assert((rn & ~15) == 0);
    rvjit_a32_assert((rm & ~15) == 0);
    rvjit_a32_insn32(block, (cc << 28) | op | (rd << 16) | (ra << 12) | (rm << 8) | (1 << 7) | (1 << 4) | rn);
}

// NOTE: Work only with udiv and sdiv
static inline void rvjit_a32_md(rvjit_block_t *block, a32_md_opcs_t op, a32_cc_t cc, regid_t rd, regid_t ra, regid_t rn, regid_t rm)
{
    rvjit_a32_assert((rd & ~15) == 0);
    rvjit_a32_assert((rn & ~15) == 0);
    rvjit_a32_assert((rm & ~15) == 0);
    rvjit_a32_insn32(block, (cc << 28) | op | (rd << 16) | (ra << 12) | (rm << 8) | (1 << 4) | rn);
}

static inline void rvjit_a32_blx_reg(rvjit_block_t* block, a32_cc_t cc, regid_t rm)
{
    rvjit_a32_dp(block, A32_BX, cc, A32_PC, A32_PC, rvjit_a32_shifter_reg_reg(rm, A32_LSR, A32_PC));
}

static inline void rvjit_a32_bx_reg(rvjit_block_t* block, a32_cc_t cc, regid_t rm)
{
    rvjit_a32_dp(block, A32_BX, cc, A32_PC, A32_PC, rvjit_a32_shifter_reg_reg(rm, A32_LSL, A32_PC));
}

enum a32_mem_opcs
{
    A32_STR   = (1 << 26) | (0 << 20) | (0 << 22),
    A32_STRB  = (1 << 26) | (0 << 20) | (1 << 22),
    A32_LDR   = (1 << 26) | (1 << 20) | (0 << 22),
    A32_LDRB  = (1 << 26) | (1 << 20) | (1 << 22),
    A32_LDRSB = (0 << 26) | (1 << 20) | (1 << 6) | (0 << 5) | (1 << 7) | (1 << 4),
    A32_LDRSH = (0 << 26) | (1 << 20) | (1 << 6) | (1 << 5) | (1 << 7) | (1 << 4),
    A32_LDRH  = (0 << 26) | (1 << 20) | (0 << 6) | (1 << 5) | (1 << 7) | (1 << 4),
    A32_STRH  = (0 << 26) | (0 << 20) | (0 << 6) | (1 << 5) | (1 << 7) | (1 << 4),
    A32_STRM  = (1 << 27) | (0 << 20) | (0 << 22),
    A32_LDRM  = (1 << 27) | (1 << 20) | (0 << 22)
};
typedef uint32_t a32_mem_opcs_t;

enum a32_addrmode
{
    A32_POSTINDEX = (0 << 24) | (0 << 21), // Use value, modify it and write it to the register
    A32_OFFSET    = (1 << 24) | (0 << 21), // Just use the value
    A32_PREINDEX  = (1 << 24) | (1 << 21)  // Modify the value, write it to the register and use it
};
typedef uint32_t a32_addrmode_t;

static inline uint32_t rvjit_a32_addrmode_imm(int32_t imm, a32_addrmode_t am)
{
    rvjit_a32_assert(check_imm_bits(imm, 13));
    return (0 << 25) | am | ((imm >= 0) << 23) | (((imm >= 0) ? imm : -imm) & bit_mask(12));
}

static inline uint32_t rvjit_a32_addrmode_reg(bool add, regid_t rm, a32_shtype_t shtype, uint8_t shimm, a32_addrmode_t am)
{
    rvjit_a32_assert((rm & ~15) == 0);
    rvjit_a32_assert((shimm & ~31) == 0);
    return (1 << 25) | am | (add << 23) | (shimm << 7) | (shtype << 5) | rm;
}

static inline uint32_t rvjit_a32_addrmode3_imm(int32_t imm, a32_addrmode_t am)
{
    rvjit_a32_assert(check_imm_bits(imm, 9));
    bool add = imm >= 0;
    if (imm < 0) imm = -imm;
    return (1 << 22) | am | (add << 23) | ((imm & 0xf0) << 4) | (imm & 0x0f);
}

static inline uint32_t rvjit_a32_addrmode3_reg(bool add, regid_t rm, a32_addrmode_t am)
{
    rvjit_a32_assert((rm & ~15) == 0);
    return (0 << 22) | am | (add << 23) | rm;
}

static inline uint32_t rvjit_a32_addrmode_multiple_reg(a32_addrmode_t am, uint16_t regsmask)
{
    rvjit_a32_assert(bit_popcnt32(regsmask) > 1); // One register in mask is undefined behavor
    rvjit_a32_assert((regsmask & (1u << 13)) == 0);
    rvjit_a32_assert((regsmask & (1u << 15)) == 0);
    return (0 << 22) | am | regsmask;
}

static inline void rvjit_a32_mem_op(rvjit_block_t* block, a32_mem_opcs_t op, a32_cc_t cc, regid_t rd, regid_t rn, a32_addrmode_t addrmode)
{
    rvjit_a32_assert((rd & ~15) == 0);
    rvjit_a32_assert((rn & ~15) == 0);
    rvjit_a32_insn32(block, (cc << 28) | op | addrmode | (rn << 16) | (rd << 12));
}

/*
static inline void rvjit_a32_push_multiple(rvjit_block_t *block, a32_mem_opcs_t op, a32_cc_t cc, regid_t rn, uint16_t regmask)
{
    rvjit_a32_assert((rn & ~15) == 0);

    rvjit_a32_insn32(block, (cc << 28) | op | addrmode | (rn << 16) | (rd << 12));
}
*/

static inline void rvjit_native_push(rvjit_block_t* block, regid_t reg)
{
    rvjit_a32_mem_op(block, A32_STR, A32_AL, reg, A32_SP, rvjit_a32_addrmode_imm(-4, A32_PREINDEX));
}

static inline void rvjit_native_pop(rvjit_block_t* block, regid_t reg)
{
    rvjit_a32_mem_op(block, A32_LDR, A32_AL, reg, A32_SP, rvjit_a32_addrmode_imm(4, A32_POSTINDEX));
}

static inline void rvjit_native_ret(rvjit_block_t* block)
{
    // bx lr
    rvjit_a32_bx_reg(block, A32_AL, A32_LR);
}

static inline void rvjit_native_zero_reg(rvjit_block_t* block, regid_t reg)
{
    rvjit_a32_dp(block, A32_MOV, A32_AL, reg, 0, rvjit_a32_shifter_imm(0, 0));
}

// Set native register reg to zero-extended 32-bit imm
static inline void rvjit_native_setreg32(rvjit_block_t* block, regid_t reg, uint32_t imm)
{
    uint8_t pimm, prot = 32;
    if (rvjit_a32_encode_imm(imm, &pimm, &prot)) {
        rvjit_a32_dp(block, A32_MOV, A32_AL, reg, 0, rvjit_a32_shifter_imm(pimm, prot));
        return;
    } else if (rvjit_a32_encode_imm(~imm, &pimm, &prot)) {
        rvjit_a32_dp(block, A32_MVN, A32_AL, reg, 0, rvjit_a32_shifter_imm(pimm, prot));
        return;
    }

    // XXX: This can be optimized with popcnt if count of 1's is greater that 0's
    bool wasneg = (int32_t)imm < 0;
    if (wasneg) {
        imm = ~imm;
    }
    a32_dp_opcs_t op = wasneg ? A32_MVN : A32_MOV;
    regid_t rn = 0;
    while (imm != 0)
    {
        pimm = imm & 0xff;
        imm >>= 8;
        if (pimm != 0) {
            rvjit_a32_dp(block, op, A32_AL, reg, rn, rvjit_a32_shifter_imm(pimm, prot & 31));
            op = wasneg ? A32_EOR : A32_ORR;
            rn = reg;
        }
        prot -= 8;

        uint8_t prot2;
        if (rvjit_a32_encode_imm(imm, &pimm, &prot2)) {
            rvjit_a32_dp(block, op, A32_AL, reg, rn, rvjit_a32_shifter_imm(pimm, (prot + prot2) & 31));
            return;
        }
    }
}

#define RVJIT_NATIVE_ZEROEXT 1
static inline void rvjit_native_signext(rvjit_block_t* block, regid_t reg)
{
    /* not needed */
    UNUSED(block);
    UNUSED(reg);
}

// Set native register reg to sign-extended 32-bit imm
static inline void rvjit_native_setreg32s(rvjit_block_t* block, regid_t reg, int32_t imm)
{
    rvjit_native_setreg32(block, reg, imm);
}

static inline void rvjit_a32_b_reloc(void *addr, bool link, a32_cc_t cond, uint32_t offset)
{
    // ARM PC is offseted by 8
    offset -= 8;
    rvjit_a32_assert((offset & 3) == 0);
    rvjit_a32_assert(check_imm_bits(offset, 26));
    uint32_t mask = bit_mask(24);
    uint32_t insn = (cond << 28) | (0x5 << 25) | (link << 24) | ((offset >> 2) & mask);
    write_uint32_le_m(addr, insn);
}

static inline void rvjit_a32_b(rvjit_block_t* block, bool link, a32_cc_t cond, uint32_t offset)
{
    uint32_t insn;
    rvjit_a32_b_reloc((void*) &insn, link, cond, offset);
    rvjit_a32_insn32(block, insn);
}

static inline branch_t rvjit_native_jmp(rvjit_block_t* block, branch_t handle, bool label)
{
    if (label) {
        // We want to set a label for a branch
        if (handle) {
            // We have an instruction handle - this is a forward jump, relocate the address.
            rvjit_a32_b_reloc(block->code + handle, false, A32_AL, block->size - handle);
            return BRANCH_NEW;
        } else {
            // We don't have a handle - just set the label. This is a backward jump.
            return block->size;
        }
    } else {
        // We want to emit a branch instruction
        if (handle) {
            // We have a branch address - emit a full instruction. This is a backward jump.
            rvjit_a32_b(block, false, A32_AL, handle - block->size);
            return BRANCH_NEW;
        } else {
            // We don't have an address - it will be patched in the future. This is a forward jump.
            branch_t tmp = block->size;
            rvjit_a32_b(block, false, A32_AL, 0);
            return tmp;
        }
    }
}

static inline void rvjit32_native_add(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a32_dp(block, A32_ADD, A32_AL, hrds, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0));
}

static inline void rvjit32_native_sub(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a32_dp(block, A32_SUB, A32_AL, hrds, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0));
}

static inline void rvjit32_native_or(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a32_dp(block, A32_ORR, A32_AL, hrds, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0));
}

static inline void rvjit32_native_and(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a32_dp(block, A32_AND, A32_AL, hrds, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0));
}

static inline void rvjit32_native_xor(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a32_dp(block, A32_EOR, A32_AL, hrds, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0));
}

static inline void rvjit32_a32_native_shift_op(rvjit_block_t* block, a32_shtype_t sh, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    if (hrds == hrs2 && hrs1 != hrs2) {
        rvjit_a32_dp(block, A32_AND, A32_AL, hrds, hrs2, rvjit_a32_shifter_imm(31, 0));
        rvjit_a32_dp(block, A32_MOV, A32_AL, hrds, 0, rvjit_a32_shifter_reg_reg(hrs1, sh, hrs2));
    } else {
        regid_t rtmp = rvjit_claim_hreg(block);
        rvjit_a32_dp(block, A32_AND, A32_AL, rtmp, hrs2, rvjit_a32_shifter_imm(31, 0));
        rvjit_a32_dp(block, A32_MOV, A32_AL, hrds, 0, rvjit_a32_shifter_reg_reg(hrs1, sh, rtmp));
        rvjit_free_hreg(block, rtmp);
    }
}

static inline void rvjit32_native_sra(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit32_a32_native_shift_op(block, A32_ASR, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_srl(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit32_a32_native_shift_op(block, A32_LSR, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_sll(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit32_a32_native_shift_op(block, A32_LSL, hrds, hrs1, hrs2);
}

static inline void rvjit_a32_native_imm_op(rvjit_block_t* block, a32_dp_opcs_t op, regid_t hrds, regid_t hrs1, int32_t imm)
{
    uint8_t pimm, prot = 0;
    if (rvjit_a32_encode_imm(imm, &pimm, &prot)) {
        rvjit_a32_dp(block, op, A32_AL, hrds, hrs1, rvjit_a32_shifter_imm(pimm, prot));
    } else {
        regid_t rtmp = rvjit_claim_hreg(block);
        rvjit_native_setreg32s(block, rtmp, imm);
        rvjit_a32_dp(block, op, A32_AL, hrds, hrs1, rvjit_a32_shifter_reg_imm(rtmp, A32_LSL, 0));
        rvjit_free_hreg(block, rtmp);
    }
}

static inline void rvjit32_native_addi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    uint8_t pimm, prot = 0;
    if (rvjit_a32_encode_imm(imm, &pimm, &prot)) {
        rvjit_a32_dp(block, A32_ADD, A32_AL, hrds, hrs1, rvjit_a32_shifter_imm(pimm, prot));
    } else if (rvjit_a32_encode_imm(-imm, &pimm, &prot)) {
        rvjit_a32_dp(block, A32_SUB, A32_AL, hrds, hrs1, rvjit_a32_shifter_imm(pimm, prot));
    } else {
        regid_t rtmp = rvjit_claim_hreg(block);
        rvjit_native_setreg32s(block, rtmp, imm);
        rvjit_a32_dp(block, A32_ADD, A32_AL, hrds, hrs1, rvjit_a32_shifter_reg_imm(rtmp, A32_LSL, 0));
        rvjit_free_hreg(block, rtmp);
    }
}

// Set native register reg to wide imm
static inline void rvjit_native_setregw(rvjit_block_t* block, regid_t reg, uintptr_t imm)
{
    rvjit_native_setreg32(block, reg, imm);
}

static inline void rvjit32_native_ori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a32_native_imm_op(block, A32_ORR, hrds, hrs1, imm);
}

static inline void rvjit32_native_andi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a32_native_imm_op(block, A32_AND, hrds, hrs1, imm);
}

static inline void rvjit32_native_xori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a32_native_imm_op(block, A32_EOR, hrds, hrs1, imm);
}

static inline void rvjit32_native_srai(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_a32_dp(block, A32_MOV, A32_AL, hrds, 0, rvjit_a32_shifter_reg_imm(hrs1, A32_ASR, imm));
}

static inline void rvjit32_native_srli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_a32_dp(block, A32_MOV, A32_AL, hrds, 0, rvjit_a32_shifter_reg_imm(hrs1, A32_LSR, imm));
}

static inline void rvjit32_native_slli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_a32_dp(block, A32_MOV, A32_AL, hrds, 0, rvjit_a32_shifter_reg_imm(hrs1, A32_LSL, imm));
}

static inline void rvjit32_native_slti(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a32_native_imm_op(block, A32_CMP, 0, hrs1, imm);
    rvjit_native_zero_reg(block, hrds);
    rvjit_a32_dp(block, A32_MOV, A32_LT, hrds, 0, rvjit_a32_shifter_imm(1, 0));
}

static inline void rvjit32_native_sltiu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_a32_native_imm_op(block, A32_CMP, 0, hrs1, imm);
    rvjit_native_zero_reg(block, hrds);
    rvjit_a32_dp(block, A32_MOV, A32_CC, hrds, 0, rvjit_a32_shifter_imm(1, 0));
}

static inline void rvjit32_native_slt(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a32_dp(block, A32_CMP, A32_AL, 0, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0));
    rvjit_native_zero_reg(block, hrds);
    rvjit_a32_dp(block, A32_MOV, A32_LT, hrds, 0, rvjit_a32_shifter_imm(1, 0));
}

static inline void rvjit32_native_sltu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a32_dp(block, A32_CMP, A32_AL, 0, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0));
    rvjit_native_zero_reg(block, hrds);
    rvjit_a32_dp(block, A32_MOV, A32_CC, hrds, 0, rvjit_a32_shifter_imm(1, 0));
}

static inline void rvjit_a32_native_mem_op(rvjit_block_t* block, a32_mem_opcs_t op, regid_t dest, regid_t addr, int32_t off)
{
    if (bit_check(op, 26)) {
        if (check_imm_bits(off, 13)) {
            rvjit_a32_mem_op(block, op, A32_AL, dest, addr, rvjit_a32_addrmode_imm(off, A32_OFFSET));
        } else {
            regid_t rtmp = rvjit_claim_hreg(block);
            rvjit_native_setreg32s(block, rtmp, off >= 0 ? off : -off);
            rvjit_a32_mem_op(block, op, A32_AL, dest, addr, rvjit_a32_addrmode_reg(off >= 0, rtmp, A32_LSL, 0, A32_OFFSET));
            rvjit_free_hreg(block, rtmp);
        }
    } else {
        if (check_imm_bits(off, 9)) {
            rvjit_a32_mem_op(block, op, A32_AL, dest, addr, rvjit_a32_addrmode3_imm(off, A32_OFFSET));
        } else {
            regid_t rtmp = rvjit_claim_hreg(block);
            rvjit_native_setreg32s(block, rtmp, off >= 0 ? off : -off);
            rvjit_a32_mem_op(block, op, A32_AL, dest, addr, rvjit_a32_addrmode3_reg(off >= 0, rtmp, A32_OFFSET));
            rvjit_free_hreg(block, rtmp);
        }
    }

}

static inline void rvjit32_native_lb(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a32_native_mem_op(block, A32_LDRSB, dest, addr, off);
}

static inline void rvjit32_native_lbu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a32_native_mem_op(block, A32_LDRB, dest, addr, off);
}

static inline void rvjit32_native_lh(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a32_native_mem_op(block, A32_LDRSH, dest, addr, off);
}

static inline void rvjit32_native_lhu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a32_native_mem_op(block, A32_LDRH, dest, addr, off);
}

static inline void rvjit32_native_lw(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_a32_native_mem_op(block, A32_LDR, dest, addr, off);
}

static inline void rvjit32_native_sb(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_a32_native_mem_op(block, A32_STRB, src, addr, off);
}

static inline void rvjit32_native_sh(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_a32_native_mem_op(block, A32_STRH, src, addr, off);
}

static inline void rvjit32_native_sw(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_a32_native_mem_op(block, A32_STR, src, addr, off);
}

static inline branch_t rvjit_a32_bcc(rvjit_block_t* block, a32_cc_t cc, regid_t hrs1, uint32_t shifter, branch_t handle, bool label)
{
    if (label) {
        // We want to set a label for a branch
        if (handle == BRANCH_NEW) {
            // We don't have a handle - just set the label. This is a backward jump.
            return block->size;
        } else {
            // We have an instruction handle - this is a forward jump, relocate the address.
            rvjit_a32_b_reloc(block->code + handle, false, (read_uint8(block->code + handle + 3) & 0xf0) >> 4, block->size - handle);
            return BRANCH_NEW;
        }
    } else {
        // We want to emit a branch instruction
        if (handle == BRANCH_NEW) {
            // We don't have an address - it will be patched in the future. This is a forward jump.
            rvjit_a32_dp(block, A32_CMP, A32_AL, 0, hrs1, shifter);
            branch_t tmp = block->size;
            rvjit_a32_b(block, false, cc, 0);
            return tmp;
        } else {
            // We have a branch address - emit a full instruction. This is a backward jump.
            rvjit_a32_dp(block, A32_CMP, A32_AL, 0, hrs1, shifter);
            rvjit_a32_b(block, false, cc, handle - block->size);
            return BRANCH_NEW;
        }
    }
}

static inline branch_t rvjit32_native_beq(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_a32_bcc(block, A32_EQ, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0), handle, target);
}

static inline branch_t rvjit32_native_bne(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_a32_bcc(block, A32_NE, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0), handle, target);
}

static inline branch_t rvjit32_native_beqz(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_a32_bcc(block, A32_EQ, hrs1, rvjit_a32_shifter_imm(0, 0), handle, target);
}

static inline branch_t rvjit32_native_bnez(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_a32_bcc(block, A32_NE, hrs1, rvjit_a32_shifter_imm(0, 0), handle, target);
}

static inline branch_t rvjit32_native_blt(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_a32_bcc(block, A32_LT, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0), handle, target);
}

static inline branch_t rvjit32_native_bge(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_a32_bcc(block, A32_GE, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0), handle, target);
}

static inline branch_t rvjit32_native_bltu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_a32_bcc(block, A32_CC, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0), handle, target);
}

static inline branch_t rvjit32_native_bgeu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_a32_bcc(block, A32_CS, hrs1, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0), handle, target);
}

static inline void rvjit32_native_mul(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_a32_ma(block, A32_MUL, A32_AL, 0, hrds, hrs1, hrs2);
}

static inline void rvjit32_native_mulh(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    regid_t trash = rvjit_claim_hreg(block);
    rvjit_a32_ma(block, A32_SMULL, A32_AL, trash, hrds, hrs1, hrs2);
    rvjit_free_hreg(block, trash);
}

static inline void rvjit32_native_mulhu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    regid_t trash = rvjit_claim_hreg(block);
    rvjit_a32_ma(block, A32_UMULL, A32_AL, trash, hrds, hrs1, hrs2);
    rvjit_free_hreg(block, trash);
}

static inline void rvjit32_native_mulhsu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    regid_t sign = rvjit_claim_hreg(block);
    regid_t rdhi = rvjit_claim_hreg(block);
    regid_t rdlo = rvjit_claim_hreg(block);
    rvjit_a32_dp(block, A32_MOV, A32_AL, sign, 0, rvjit_a32_shifter_reg_imm(hrs1, A32_ASR, 31)); // extract flag we get -1 if its negative
    rvjit_a32_ma(block, A32_UMULL, A32_AL, rdlo, rdhi, hrs1, hrs2); // rdlo:rdhi = (hrs1 * hrs2)
    rvjit_a32_ma2(block, A32_MLA, A32_AL, hrds, rdhi, hrs2, sign); // hrds = rdhi + (hrs2 * sign)
    rvjit_free_hreg(block, sign);
    rvjit_free_hreg(block, rdhi);
    rvjit_free_hreg(block, rdlo);
}

static inline void rvjit_a32_soft_div_divu(rvjit_block_t* block, a32_md_opcs_t op, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    bool badreg  = (hrds <= 3);

    if (unlikely(badreg))
        rvjit_native_push(block, 4); // lazy to find free reg

    //rvjit_a32_op_bkpt(block);
    rvjit_native_push(block, 0);
    rvjit_native_push(block, 1);
    rvjit_native_push(block, 2);
    rvjit_native_push(block, 3);
    rvjit_native_push(block, A32_IP);

    rvjit_a32_dp(block, A32_MOV, A32_AL, 0, 0, rvjit_a32_shifter_reg_imm(hrs1, A32_LSL, 0));
    if (unlikely(hrs2 != 1))
        rvjit_a32_dp(block, A32_MOV, A32_AL, 1, 0, rvjit_a32_shifter_reg_imm(hrs2, A32_LSL, 0));

    if (likely(op == A32_SDIV)) {
        rvjit_native_setreg32(block, A32_IP, (unsigned)rvjit_a32_soft_idiv);
    } else if (likely(op == A32_UDIV)) {
        rvjit_native_setreg32(block, A32_IP, (unsigned)rvjit_a32_soft_uidiv);
    }

    rvjit_native_push(block, A32_LR);
    rvjit_a32_blx_reg(block, A32_AL, A32_IP);
    rvjit_native_pop(block, A32_LR);

    // god save me
    if (unlikely(badreg)) {
        rvjit_a32_dp(block, A32_MOV, A32_AL, 4, 0, rvjit_a32_shifter_reg_imm(0, A32_LSL, 0));
    } else {
        rvjit_a32_dp(block, A32_MOV, A32_AL, hrds, 0, rvjit_a32_shifter_reg_imm(0, A32_LSL, 0));
    }

    rvjit_native_pop(block, A32_IP);
    rvjit_native_pop(block, 3);
    rvjit_native_pop(block, 2);
    rvjit_native_pop(block, 1);
    rvjit_native_pop(block, 0);

    if (unlikely(badreg)) {
        rvjit_a32_dp(block, A32_MOV, A32_AL, hrds, 0, rvjit_a32_shifter_reg_imm(4, A32_LSL, 0));
        rvjit_native_pop(block, 4);
    }
}

// NOTE: aarch32 generate for overflow same behavor just need to check for divide by zero
static inline void rvjit32_native_div(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    bool need_allocate = (hrds == hrs1 || hrds == hrs2);
    regid_t tmphrds = need_allocate ? rvjit_claim_hreg(block) : hrds;

    rvjit_native_setreg32s(block, tmphrds, -1);

    if (likely(rvjit_a32_check_div())) {
        rvjit_a32_dp(block, A32_CMP, A32_AL, hrs2, 0x0, rvjit_a32_shifter_imm(0,0));
        rvjit_a32_md(block, A32_SDIV, A32_NE, tmphrds, 0xf, hrs1, hrs2);
    } else {
        branch_t zerocheck = rvjit32_native_beqz(block, hrs2, BRANCH_NEW, BRANCH_ENTRY);
        rvjit_a32_soft_div_divu(block, A32_SDIV, tmphrds, hrs1, hrs2);
        rvjit32_native_beqz(block, hrs2, zerocheck, BRANCH_TARGET);
    }

    if (need_allocate) {
        rvjit_a32_dp(block, A32_MOV, A32_AL, hrds, 0, rvjit_a32_shifter_reg_imm(tmphrds, A32_LSL, 0));
        rvjit_free_hreg(block, tmphrds);
    }
}

static inline void rvjit32_native_divu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    bool need_allocate = (hrds == hrs1 || hrds == hrs2);
    regid_t tmphrds = need_allocate ? rvjit_claim_hreg(block) : hrds;

    rvjit_native_setreg32s(block, tmphrds, -1);

    if (likely(rvjit_a32_check_div())) {
        rvjit_a32_dp(block, A32_CMP, A32_AL, hrs2, 0x0, rvjit_a32_shifter_imm(0,0));
        rvjit_a32_md(block, A32_UDIV, A32_NE, tmphrds, 0xf, hrs1, hrs2);
    } else {
        branch_t zerocheck = rvjit32_native_beqz(block, hrs2, BRANCH_NEW, BRANCH_ENTRY);
        rvjit_a32_soft_div_divu(block, A32_UDIV, tmphrds, hrs1, hrs2);
        rvjit32_native_beqz(block, hrs2, zerocheck, BRANCH_TARGET);
    }

    if (need_allocate) {
        rvjit_a32_dp(block, A32_MOV, A32_AL, hrds, 0, rvjit_a32_shifter_reg_imm(tmphrds, A32_LSL, 0));
        rvjit_free_hreg(block, tmphrds);
    }
}

// need to hrds = hrs1 - ((hrs1 / hrs2) * hrs2)
// its cool we need just check for zero and after then for INT_MIN in div result
static inline void rvjit32_native_rem(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    bool need_allocate = (hrds == hrs1 || hrds == hrs2);
    regid_t tmphrds = need_allocate ? rvjit_claim_hreg(block) : hrds;
    regid_t tmp = rvjit_claim_hreg(block);

    rvjit_a32_dp(block, A32_MOV, A32_AL, tmphrds, 0, rvjit_a32_shifter_reg_imm(hrs1, A32_LSL, 0));
    branch_t zerocheck = rvjit32_native_beqz(block, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    if (likely(rvjit_a32_check_div())) {
        rvjit_a32_md(block, A32_SDIV, A32_AL, tmp, 0xf, hrs1, hrs2); // tmp = (hrs1 sdiv hrs2)
    } else {
        rvjit_a32_soft_div_divu(block, A32_SDIV, tmp, hrs1, hrs2);
    }

    rvjit_a32_dp(block, A32_CMP, A32_AL, 0, hrs1, rvjit_a32_shifter_reg_imm(tmp, A32_LSL, 0)); // cmp(hrs1, tmp)
    rvjit_a32_dp(block, A32_MOV, A32_EQ, tmphrds, 0, rvjit_a32_shifter_imm(0, 0)); // if (tmp == cmpreg) { hrds = 0; }
    rvjit_a32_ma2(block, A32_MLS, A32_NE, tmphrds, hrs1, tmp, hrs2); // else { hrds = (hrs1 - (tmp * hrs2)); }
    rvjit32_native_beqz(block, hrs2, zerocheck, BRANCH_TARGET);

    if (need_allocate) {
        rvjit_a32_dp(block, A32_MOV, A32_AL, hrds, 0, rvjit_a32_shifter_reg_imm(tmphrds, A32_LSL, 0));
        rvjit_free_hreg(block, tmphrds);
    }

    rvjit_free_hreg(block, tmp);
}

static inline void rvjit32_native_remu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    bool need_allocate = (hrds == hrs1 || hrds == hrs2);
    regid_t tmphrds = need_allocate ? rvjit_claim_hreg(block) : hrds;
    regid_t tmp = rvjit_claim_hreg(block);

    rvjit_a32_dp(block, A32_MOV, A32_AL, tmphrds, 0, rvjit_a32_shifter_reg_imm(hrs1, A32_LSL, 0));
    branch_t zerocheck = rvjit32_native_beqz(block, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    if (likely(rvjit_a32_check_div())) {
        rvjit_a32_md(block, A32_UDIV, A32_AL, tmp, 0xf, hrs1, hrs2); // tmp = (hrs1 divu hrs2)
    } else {
        rvjit_a32_soft_div_divu(block, A32_UDIV, tmp, hrs1, hrs2);
    }

    rvjit_a32_ma2(block, A32_MLS, A32_AL, tmphrds, hrs1, tmp, hrs2); // hrds = hrs1 - (tmp * hrs2)
    rvjit32_native_beqz(block, hrs2, zerocheck, BRANCH_TARGET);

    if (need_allocate) {
        rvjit_a32_dp(block, A32_MOV, A32_AL, hrds, 0, rvjit_a32_shifter_reg_imm(tmphrds, A32_LSL, 0));
        rvjit_free_hreg(block, tmphrds);
    }

    rvjit_free_hreg(block, tmp);
}

#endif
