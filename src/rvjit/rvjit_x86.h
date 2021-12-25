/*
rvjit_x86.h - RVJIT x86 Backend
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

#ifndef RVJIT_X86_H
#define RVJIT_X86_H

#define X86_EAX 0x0
#define X86_ECX 0x1
#define X86_EDX 0x2
#define X86_EBX 0x3
#define X86_ESP 0x4
#define X86_EBP 0x5
#define X86_ESI 0x6
#define X86_EDI 0x7

#define X64_RAX X86_EAX
#define X64_RCX X86_ECX
#define X64_RDX X86_EDX
#define X64_RBX X86_EBX
#define X64_RSP X86_ESP
#define X64_RBP X86_EBP
#define X64_RSI X86_ESI
#define X64_RDI X86_EDI
#define X64_R8  0x8
#define X64_R9  0x9
#define X64_R10 0xA
#define X64_R11 0xB
#define X64_R12 0xC
#define X64_R13 0xD
#define X64_R14 0xE
#define X64_R15 0xF

#ifdef RVJIT_ABI_SYSV
#define VM_PTR_REG X64_RDI
#elif RVJIT_ABI_WIN64
#define VM_PTR_REG X64_RCX
#elif RVJIT_ABI_FASTCALL
#define VM_PTR_REG X86_ECX
#endif

#define X86_MAX_OFFB  0x7F  // Maximum value for 1-byte offset

#define X64_REX_W     0x48  // Operands are 64-bit wide
#define X64_REX_R     0x44  // Second (destination) register is >= R8
#define X64_REX_X     0x42
#define X64_REX_B     0x41  // First (source) register is >= R8

#define X86_MEM_OFFB  0x40
#define X86_MEM_OFFW  0x80

#define X86_2_REGS    0xC0
#define X86_IMM_OP    0x81

#define X86_PUSH      0x50
#define X86_POP       0x58
#define X86_MOV_IMM   0xB8
#define X86_MOV_R_M   0x89
#define X86_MOV_M_R   0x8B
#define X86_XCHG      0x87
#define X86_MOVSXD    0x63  // x86_64 only!!!

#define X86_ADD       0x01
#define X86_SUB       0x29
#define X86_OR        0x09
#define X86_AND       0x21
#define X86_XOR       0x31

#define X86_ADD_IMM   0xC0
#define X86_OR_IMM    0xC8
#define X86_AND_IMM   0xE0
#define X86_XOR_IMM   0xF0

#define X86_SLL       0xE0
#define X86_SRL       0xE8
#define X86_SRA       0xF8

#define X86_CMP_IMM   0xF8
#define X86_CMP       0x39

#define X86_SETB      0x92
#define X86_SETL      0x9C

static inline size_t rvjit_native_default_hregmask()
{
#ifdef RVJIT_NATIVE_64BIT
    return rvjit_hreg_mask(X64_RAX) |
#ifndef RVJIT_ABI_WIN64
           rvjit_hreg_mask(X64_RCX) |
           rvjit_hreg_mask(X64_RSI) |
#endif
           rvjit_hreg_mask(X64_RDX) |
           rvjit_hreg_mask(X64_R8)  |
           rvjit_hreg_mask(X64_R9)  |
           rvjit_hreg_mask(X64_R10) |
           rvjit_hreg_mask(X64_R11);
#elif RVJIT_ABI_FASTCALL
    // Pretty much useless without abireclaim
    return rvjit_hreg_mask(X86_EAX) |
           rvjit_hreg_mask(X86_EDX);
#endif
}

static inline size_t rvjit_native_abireclaim_hregmask()
{
#ifdef RVJIT_NATIVE_64BIT
    return rvjit_hreg_mask(X64_RBX) |
#ifdef RVJIT_ABI_WIN64
           rvjit_hreg_mask(X64_RSI) |
           rvjit_hreg_mask(X64_RDI) |
#endif
           rvjit_hreg_mask(X64_R12) |
           rvjit_hreg_mask(X64_R13) |
           rvjit_hreg_mask(X64_R14) |
           rvjit_hreg_mask(X64_R15);
#elif RVJIT_ABI_FASTCALL
    return rvjit_hreg_mask(X86_EBX) |
           rvjit_hreg_mask(X86_ESI) |
           rvjit_hreg_mask(X86_EDI);
#endif
}

static inline bool x86_is_byte_imm(int32_t imm)
{
    return imm > -129 && imm < 128;
}

static inline void rvjit_native_push(rvjit_block_t* block, regid_t reg)
{
    uint8_t code[2];
    if (reg < X64_R8) {
        code[0] = X86_PUSH + reg;
        rvjit_put_code(block, code, 1);
    } else {
        code[0] = X64_REX_B;
        code[1] = X86_PUSH + reg - X64_R8;
        rvjit_put_code(block, code, 2);
    }
}

static inline void rvjit_native_pop(rvjit_block_t* block, regid_t reg)
{
    uint8_t code[2];
    if (reg < X64_R8) {
        code[0] = X86_POP + reg;
        rvjit_put_code(block, code, 1);
    } else {
        code[0] = X64_REX_B;
        code[1] = X86_POP + reg - X64_R8;
        rvjit_put_code(block, code, 2);
    }
}

// 2 register operands instruction
static inline void rvjit_x86_2reg_op(rvjit_block_t* block, uint8_t opcode, regid_t dest, regid_t src, bool bits_64)
{
    uint8_t code[3];
    // If we are operating on 64 bit values set wide prefix
    code[0] = bits_64 ? X64_REX_W : 0;
    code[1] = opcode;
    code[2] = X86_2_REGS;
    if (src >= X64_R8) {
        code[0] |= X64_REX_R;
        code[2] += (src - X64_R8) << 3;
    } else {
        code[2] += src << 3;
    }
    if (dest >= X64_R8) {
        code[0] |= X64_REX_B;
        code[2] += dest - X64_R8;
    } else {
        code[2] += dest;
    }
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 3 : 2);
}

// 1 register operand + 32-bit sign-extended immediate instruction
static inline void rvjit_x86_r_imm_op(rvjit_block_t* block, uint8_t opcode, regid_t reg, int32_t imm, bool bits_64)
{
    uint8_t code[7];
    uint8_t inst_size;
    code[0] = bits_64 ? X64_REX_W : 0;
    code[1] = X86_IMM_OP;
    code[2] = opcode;
    if (reg >= X64_R8) {
        code[0] |= X64_REX_B;
        code[2] += reg - X64_R8;
    } else {
        code[2] += reg;
    }
    if (x86_is_byte_imm(imm)) {
        code[1] |= 0x02; // IMM length override
        code[3] = (int8_t)imm;
        inst_size = 3;
    } else {
        write_uint32_le_m(code + 3, imm);
        inst_size = 6;
    }
    if (code[0]) inst_size++;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), inst_size);
}

// 1 register operand + shift amount immediate (If imm is 0, cl(ecx) register is used as shift amount)
// For whatever stupid reason we cannot use any register as shift amount, needs workarounds
static inline void rvjit_x86_shift_op(rvjit_block_t* block, uint8_t opcode, regid_t reg, uint8_t imm, bool bits_64)
{
    uint8_t code[4];
    code[0] = bits_64 ? X64_REX_W : 0;
    code[1] = imm ? 0xC1 : 0xD3;
    code[2] = opcode;
    code[3] = imm;
    if (reg >= X64_R8) {
        code[0] |= X64_REX_B;
        code[2] += reg - X64_R8;
    } else {
        code[2] += reg;
    }
    rvjit_put_code(block, code + (code[0] ? 0 : 1), 2 + (code[0] ? 1 : 0) + (imm ? 1 : 0));
}

// Negate a register
static inline void rvjit_x86_neg(rvjit_block_t* block, regid_t reg, bool bits_64)
{
    uint8_t code[3];
    code[0] = bits_64 ? X64_REX_W : 0;
    code[1] = 0xF7;
    if (reg >= X64_R8) {
        code[0] |= X64_REX_B;
        code[2] = 0xD8 + reg - X64_R8;
    } else {
        code[2] = 0xD8 + reg;
    }
    rvjit_put_code(block, code + (code[0] ? 0 : 1), 2 + (code[0] ? 1 : 0));
}

// Copy data from native register src to dest
static inline void rvjit_x86_mov(rvjit_block_t* block, regid_t dest, regid_t src, bool bits_64)
{
    rvjit_x86_2reg_op(block, X86_MOV_R_M, dest, src, bits_64);
}

// Swap data between 2 registers
static inline void rvjit_x86_xchg(rvjit_block_t* block, regid_t dest, regid_t src)
{
#ifdef RVJIT_NATIVE_64BIT
    rvjit_x86_2reg_op(block, X86_XCHG, dest, src, true);
#else
    rvjit_x86_2reg_op(block, X86_XCHG, dest, src, false);
#endif
}

// Sign-extend data from 32-bit src to 64-bit dest
static inline void rvjit_x86_movsxd(rvjit_block_t* block, regid_t dest, regid_t src)
{
    rvjit_x86_2reg_op(block, X86_MOVSXD, src, dest, true);
}

static bool x86_byte_reg_usable(regid_t reg)
{
#ifdef RVJIT_NATIVE_64BIT
    return reg <= X64_R15;
#else
    return reg <= X86_EBX;
#endif
}

// Emit memory-addressing part of the instruction
static inline void rvjit_x86_memory_ref(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    uint8_t code[6];
    uint8_t inst_size = 1;
    code[0] = (addr & 0x7) | ((dest & 0x7) << 3);
    if (addr == X64_R12) {
        // SIB byte (edge case)
        code[1] = 0x24;
        inst_size++;
    }
    if (!x86_is_byte_imm(off) || addr == X64_R13) {
        // Huge offset
        code[0] |= X86_MEM_OFFW;
        write_uint32_le_m(code + inst_size, off);
        inst_size += 4;
    } else if (off) {
        // 1-byte offset
        code[0] |= X86_MEM_OFFB;
        code[inst_size] = off;
        inst_size++;
    }
    rvjit_put_code(block, code, inst_size);
}

// x86 substitute for addi instruction
static inline void rvjit_x86_lea_addi(rvjit_block_t* block, regid_t dest, regid_t src, int32_t imm, bool bits_64)
{
    uint8_t code[2];
    code[0] = bits_64 ? X64_REX_W : 0;
    code[1] = 0x8d;
    if (src >= X64_R8) {
        code[0] |= X64_REX_B;
    }
    if (dest >= X64_R8) {
        code[0] |= X64_REX_R;
    }
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 2 : 1);
    rvjit_x86_memory_ref(block, dest, src, imm);
}

// Zero-extend data from 8-bit src to full register
// Careful: not all 8-bit registers are accessible on i386
static inline void rvjit_x86_movzxb(rvjit_block_t* block, regid_t dest, regid_t src)
{
    uint8_t code[4];
    code[0] = 0;
    code[1] = 0x0F;
    code[2] = 0xB6;
    code[3] = 0xC0;
    if (dest >= X64_R8) {
        code[0] |= X64_REX_R;
        code[3] += (dest - X64_R8) << 3;
    } else {
        code[3] += dest << 3;
    }
    if (src >= X64_R8) {
        code[0] |= X64_REX_B;
        code[3] += src - X64_R8;
    } else {
        code[3] += src;
    }
    if (src > X86_EBX) {
        // REX prefix for using sil, dil registers
        code[0] |= 0x40;
    }
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 4 : 3);
}

static inline void rvjit_x86_3reg_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, regid_t hrs2, bool bits_64)
{
    if (hrds == hrs1) {
        rvjit_x86_2reg_op(block, opcode, hrds, hrs2, bits_64);
    } else if (hrds == hrs2) {
        if (opcode == X86_SUB) {
            // Edge case: subtracted operand is destination, lower to neg + add
            rvjit_x86_neg(block, hrs2, bits_64);
            rvjit_x86_2reg_op(block, X86_ADD, hrds, hrs1, bits_64);
        } else {
            rvjit_x86_2reg_op(block, opcode, hrds, hrs1, bits_64);
        }
    } else {
        rvjit_x86_mov(block, hrds, hrs1, bits_64);
        rvjit_x86_2reg_op(block, opcode, hrds, hrs2, bits_64);
    }
}

static inline void rvjit_x86_2reg_imm_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, int32_t imm, bool bits_64)
{
    if (opcode == X86_AND_IMM && imm == 0xFF && x86_byte_reg_usable(hrs1)) {
        // Optimize andi r1, r2, 0xFF -> movzxb r1, r2
        rvjit_x86_movzxb(block, hrds, hrs1);
        return;
    }
    if (opcode == X86_ADD_IMM && imm && hrds != hrs1) {
        // addi r1, r2, imm -> lea r1, [r2 + imm]
        rvjit_x86_lea_addi(block, hrds, hrs1, imm, bits_64);
        return;
    }
    if (hrds != hrs1) rvjit_x86_mov(block, hrds, hrs1, bits_64);
    if (imm || opcode == X86_AND_IMM) rvjit_x86_r_imm_op(block, opcode, hrds, imm, bits_64);
}

static inline void rvjit_x86_2reg_imm_shift_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, uint8_t imm, bool bits_64)
{
    if (hrds != hrs1) rvjit_x86_mov(block, hrds, hrs1, bits_64);
    if (imm) rvjit_x86_shift_op(block, opcode, hrds, imm, bits_64);
}

static inline void rvjit_x86_3reg_shift_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, regid_t hrs2, bool bits_64)
{
    /* Shift by register is insane on x86, practically a 1-operand instruction,
     * with CL hardcoded as shift amount reg.
     * This function implements a proper 3-operand intrinsic,
     * register allocator will support us in generating efficient code.
     */
    if (hrds == hrs1) {
        if (hrs2 != X86_ECX) {
            rvjit_x86_xchg(block, X86_ECX, hrs2);
            if (hrds == X86_ECX) {
                // We exchanged rds with ECX
                hrds = hrs2;
            } else if (hrds == hrs2) {
                // Everything is in ECX now
                hrds = X86_ECX;
            }
            rvjit_x86_shift_op(block, opcode, hrds, 0, bits_64);
            rvjit_x86_xchg(block, X86_ECX, hrs2);
        } else {
            rvjit_x86_shift_op(block, opcode, hrds, 0, bits_64);
        }
    } else if (hrds == hrs2) {
        // Cursed...
        rvjit_native_push(block, hrs1);
        if (hrs1 == X86_ECX) {
            rvjit_x86_xchg(block, X86_ECX, hrds);
            rvjit_x86_shift_op(block, opcode, hrds, 0, bits_64);
            rvjit_x86_xchg(block, X86_ECX, hrds);
        } else if (hrds != X86_ECX) {
            rvjit_x86_xchg(block, X86_ECX, hrds);
            rvjit_x86_shift_op(block, opcode, hrs1, 0, bits_64);
            rvjit_x86_xchg(block, X86_ECX, hrds);
        } else {
            rvjit_x86_shift_op(block, opcode, hrs1, 0, bits_64);
        }
        rvjit_x86_mov(block, hrds, hrs1, bits_64);
        rvjit_native_pop(block, hrs1);
    } else {
        rvjit_x86_mov(block, hrds, hrs1, bits_64);
        if (hrds == X86_ECX) {
            rvjit_x86_xchg(block, X86_ECX, hrs2);
            rvjit_x86_shift_op(block, opcode, hrs2, 0, bits_64);
            rvjit_x86_xchg(block, X86_ECX, hrs2);
        } else if (hrs2 != X86_ECX) {
            rvjit_x86_xchg(block, X86_ECX, hrs2);
            rvjit_x86_shift_op(block, opcode, hrds, 0, bits_64);
            rvjit_x86_xchg(block, X86_ECX, hrs2);
        } else {
            rvjit_x86_shift_op(block, opcode, hrds, 0, bits_64);
        }
    }
}

static inline void rvjit_native_zero_reg(rvjit_block_t* block, regid_t reg)
{
    rvjit_x86_3reg_op(block, X86_XOR, reg, reg, reg, false);
}

// Set lower 8 bits of native register to specific cmp result
static inline void rvjit_x86_setcc_internal(rvjit_block_t* block, uint8_t opcode, regid_t reg)
{
    uint8_t code[4];
    code[0] = 0;
    code[1] = 0x0F;
    code[2] = opcode;
    code[3] = 0xC0;
    if (reg >= X64_R8) {
        code[0] |= X64_REX_B;
        code[3] += reg - X64_R8;
    } else {
        code[3] += reg;
    }
    if (reg > X86_EBX) {
        // REX prefix for using sil, dil registers
        code[0] |= 0x40;
    }
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 4 : 3);
}

// Orthogonal version of rvjit_x86_setcc_internal()
static inline void rvjit_x86_setcc(rvjit_block_t* block, uint8_t opcode, regid_t reg)
{
    if (x86_byte_reg_usable(reg)) {
        rvjit_x86_setcc_internal(block, opcode, reg);
    } else {
        // surprise!!!
        rvjit_x86_xchg(block, X86_EAX, reg);
        rvjit_x86_setcc_internal(block, opcode, X86_EAX);
        rvjit_x86_xchg(block, X86_EAX, reg);
    }
}

static inline void rvjit_x86_3reg_slt_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, regid_t hrs2, bool bits_64)
{
    if (hrds != hrs1 && hrds != hrs2) rvjit_native_zero_reg(block, hrds);
    rvjit_x86_2reg_op(block, X86_CMP, hrs1, hrs2, bits_64);
    rvjit_x86_setcc(block, opcode, hrds);
    if (hrds == hrs1 || hrds == hrs2) rvjit_x86_2reg_imm_op(block, X86_AND_IMM, hrds, hrds, 0xFF, false);
}

static inline void rvjit_x86_2reg_imm_slt_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, int32_t imm, bool bits_64)
{
    if (hrds != hrs1) rvjit_native_zero_reg(block, hrds);
    rvjit_x86_r_imm_op(block, X86_CMP_IMM, hrs1, imm, bits_64);
    rvjit_x86_setcc(block, opcode, hrds);
    if (hrds == hrs1) rvjit_x86_2reg_imm_op(block, X86_AND_IMM, hrds, hrds, 0xFF, false);
}

static inline void rvjit_native_ret(rvjit_block_t* block)
{
    rvjit_put_code(block, "\xC3", 1);
}

// Set native register reg to zero-extended 32-bit imm
static inline void rvjit_native_setreg32(rvjit_block_t* block, regid_t reg, uint32_t imm)
{
    if (imm == 0) {
        rvjit_native_zero_reg(block, reg);
        return;
    }
    uint8_t code[6];
    code[0] = 0;
    code[1] = X86_MOV_IMM;
    if (reg >= X64_R8) {
        code[0] |= X64_REX_B;
        code[1] += reg - X64_R8;
    } else {
        code[1] += reg;
    }
    write_uint32_le_m(code + 2, imm);
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 6 : 5);
}

// Set native register reg to sign-extended 32-bit imm
static inline void rvjit_native_setreg32s(rvjit_block_t* block, regid_t reg, int32_t imm)
{
#ifdef RVJIT_NATIVE_64BIT
    if (imm == 0) {
        rvjit_native_zero_reg(block, reg);
        return;
    }
    uint8_t code[7];
    code[0] = X64_REX_W;
    code[1] = 0xC7;
    code[2] = 0xC0;
    if (reg >= X64_R8) {
        code[0] |= X64_REX_B;
        code[2] += reg - X64_R8;
    } else {
        code[2] += reg;
    }
    write_uint32_le_m(code + 3, imm);
    rvjit_put_code(block, code, 7);
#else
    rvjit_native_setreg32(block, reg, imm);
#endif
}

// Set native register reg to wide imm
static inline void rvjit_native_setregw(rvjit_block_t* block, regid_t reg, uintptr_t imm)
{
#ifdef RVJIT_NATIVE_64BIT
    uint8_t code[10];
    code[0] = X64_REX_W; // movabsq
    code[1] = X86_MOV_IMM + reg;
    if (reg >= X64_R8) {
        code[0] |= X64_REX_B;
        code[1] -= X64_R8;
    }
    write_uint64_le_m(code + 2, imm);
    rvjit_put_code(block, code, 10);
#else
    rvjit_native_setreg32(block, reg, imm);
#endif
}

// Call a function pointed to by native register
static inline void rvjit_native_callreg(rvjit_block_t* block, regid_t reg)
{
    uint8_t code[3];
    if (reg < X64_R8) {
        code[0] = 0xFF;
        code[1] = 0xD0 + reg;
        rvjit_put_code(block, code, 2);
    } else {
        code[0] = X64_REX_B;
        code[1] = 0xFF;
        code[2] = 0xD0 + reg - X64_R8;
        rvjit_put_code(block, code, 3);
    }
}

#define X86_LB  0xBE
#define X86_LBU 0xB6
#define X86_LH  0xBF
#define X86_LHU 0xB7

// For lb/lbu/lh/lhu; bits_64 means signext to full 64-bit reg, not needed for unsigned
static inline void rvjit_x86_lbhu(rvjit_block_t* block, uint8_t opcode, regid_t dest, regid_t addr, int32_t off, bool bits_64)
{
    uint8_t code[3];
    code[0] = bits_64 ? X64_REX_W : 0;
    code[1] = 0x0F;
    code[2] = opcode;
    if (addr >= X64_R8) {
        code[0] |= X64_REX_B;
    }
    if (dest >= X64_R8) {
        code[0] |= X64_REX_R;
    }
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 3 : 2);
    rvjit_x86_memory_ref(block, dest, addr, off);
}

#define X86_LWU_LD  X86_MOV_M_R
#define X86_LW      X86_MOVSXD
#define X86_SB      0x88
#define X86_SW_SD   X86_MOV_R_M

// For lwu/ld bits_64 ? ld : lwu, for lw bits_64 = true!
// For sw/sd bits_64 ? sd : sw, for sb bits_64 = false!
static inline void rvjit_x86_lwdu_sbwd(rvjit_block_t* block, uint8_t opcode, regid_t dest, regid_t addr, int32_t off, bool bits_64)
{
    uint8_t code[2];
    code[0] = bits_64 ? X64_REX_W : 0;
    code[1] = opcode;
    if (opcode == X86_SB && dest > X86_EBX) {
        code[0] |= 0x40;
    }
    if (addr >= X64_R8) {
        code[0] |= X64_REX_B;
    }
    if (dest >= X64_R8) {
        code[0] |= X64_REX_R;
    }
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 2 : 1);
    rvjit_x86_memory_ref(block, dest, addr, off);
}

static inline void rvjit_x86_sb(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    if (x86_byte_reg_usable(src)) {
        rvjit_x86_lwdu_sbwd(block, X86_SB, src, addr, off, false);
    } else {
        rvjit_x86_xchg(block, X86_EAX, src);
        rvjit_x86_lwdu_sbwd(block, X86_SB, X86_EAX, addr, off, false);
        rvjit_x86_xchg(block, X86_EAX, src);
    }
}

static inline void rvjit_x86_sh(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_put_code(block, "\x66", 1); // operand override
    rvjit_x86_lwdu_sbwd(block, X86_MOV_R_M, src, addr, off, false);
}

static inline branch_t rvjit_native_jmp(rvjit_block_t* block, branch_t handle, bool target)
{
    if (target) {
        if (handle == BRANCH_NEW) {
            return block->size;
        } else {
            // Patch jump offset
            write_uint32_le_m(block->code + handle + 1, block->size - handle - 5);
            return BRANCH_NEW;
        }
    } else {
        if (handle) {
            uint8_t code[5];
            code[0] = 0xE9;
            write_uint32_le_m(code + 1, handle - block->size - 5);
            rvjit_put_code(block, code, 5);
            return BRANCH_NEW;
        } else {
            branch_t tmp = block->size;
            rvjit_put_code(block, "\xE9\xFB\xFF\xFF\xFF", 5);
            return tmp;
        }
    }
}

//#define RVJIT_FAR_BRANCHES

// Needs testing if we aren't violating offset constraints
#ifdef RVJIT_FAR_BRANCHES
#define X86_JB   0x82
#define X86_JNB  0x83
#define X86_JE   0x84
#define X86_JNE  0x85
#define X86_JL   0x8C
#define X86_JGE  0x8D
#else
#define X86_JB   0x72
#define X86_JNB  0x73
#define X86_JE   0x74
#define X86_JNE  0x75
#define X86_JL   0x7C
#define X86_JGE  0x7D
#endif

#define X86_BEQ  X86_JE
#define X86_BNE  X86_JNE
#define X86_BLT  X86_JL
#define X86_BGE  X86_JGE
#define X86_BLTU X86_JB
#define X86_BGEU X86_JNB


static branch_t rvjit_x86_branch_entry(rvjit_block_t* block, uint8_t opcode, branch_t handle)
{
#ifdef RVJIT_FAR_BRANCHES
    uint8_t code[6];
    code[0] = 0x0F;
    code[1] = opcode;
    if (handle == BRANCH_NEW) {
        branch_t tmp = block->size;
        write_uint32_le_m(code + 2, 0xFFFFFFFC);
        rvjit_put_code(block, code, 6);
        return tmp;
    } else {
        write_uint32_le_m(code + 2, handle - block->size - 6);
        rvjit_put_code(block, code, 6);
        return BRANCH_NEW;
    }
#else
    uint8_t code[2];
    code[0] = opcode;
    if (handle == BRANCH_NEW) {
        branch_t tmp = block->size;
        code[1] = 0xFE;
        rvjit_put_code(block, code, 2);
        return tmp;
    } else {
        code[1] = handle - block->size - 2;
        rvjit_put_code(block, code, 2);
        return BRANCH_NEW;
    }
#endif
}

static branch_t rvjit_x86_branch_target(rvjit_block_t* block, branch_t handle)
{
    if (handle == BRANCH_NEW) {
        return block->size;
    } else {
        // Patch jump offset
#ifdef RVJIT_FAR_BRANCHES
        write_uint32_le_m(block->code + handle + 2, block->size - handle - 6);
#else
        block->code[handle + 1] = block->size - handle - 2;
#endif
        return BRANCH_NEW;
    }
}

static inline branch_t rvjit_x86_branch(rvjit_block_t* block, uint8_t opcode, regid_t hrs1, regid_t hrs2, branch_t handle, bool target, bool bits_64)
{
    if (target) {
        return rvjit_x86_branch_target(block, handle);
    } else {
        rvjit_x86_2reg_op(block, X86_CMP, hrs1, hrs2, bits_64);
        return rvjit_x86_branch_entry(block, opcode, handle);
    }
}

static inline branch_t rvjit_x86_branch_imm(rvjit_block_t* block, uint8_t opcode, regid_t hrs1, int32_t imm, branch_t handle, bool target, bool bits_64)
{
    if (target) {
        return rvjit_x86_branch_target(block, handle);
    } else {
        rvjit_x86_r_imm_op(block, X86_CMP_IMM, hrs1, imm, bits_64);
        return rvjit_x86_branch_entry(block, opcode, handle);
    }
}

/*
 * RV32
 */
static inline void rvjit32_native_add(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_op(block, X86_ADD, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_sub(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_op(block, X86_SUB, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_or(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_op(block, X86_OR, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_and(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_op(block, X86_AND, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_xor(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_op(block, X86_XOR, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_sra(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_shift_op(block, X86_SRA, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_srl(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_shift_op(block, X86_SRL, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_sll(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_shift_op(block, X86_SLL, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_addi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_x86_2reg_imm_op(block, X86_ADD_IMM, hrds, hrs1, imm, false);
}

static inline void rvjit32_native_ori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_x86_2reg_imm_op(block, X86_OR_IMM, hrds, hrs1, imm, false);
}

static inline void rvjit32_native_andi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_x86_2reg_imm_op(block, X86_AND_IMM, hrds, hrs1, imm, false);
}

static inline void rvjit32_native_xori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_x86_2reg_imm_op(block, X86_XOR_IMM, hrds, hrs1, imm, false);
}

static inline void rvjit32_native_srai(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_x86_2reg_imm_shift_op(block, X86_SRA, hrds, hrs1, imm, false);
}

static inline void rvjit32_native_srli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_x86_2reg_imm_shift_op(block, X86_SRL, hrds, hrs1, imm, false);
}

static inline void rvjit32_native_slli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_x86_2reg_imm_shift_op(block, X86_SLL, hrds, hrs1, imm, false);
}

static inline void rvjit32_native_slti(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_x86_2reg_imm_slt_op(block, X86_SETL, hrds, hrs1, imm, false);
}

static inline void rvjit32_native_sltiu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_x86_2reg_imm_slt_op(block, X86_SETB, hrds, hrs1, imm, false);
}

static inline void rvjit32_native_slt(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_slt_op(block, X86_SETL, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_sltu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_slt_op(block, X86_SETB, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_lb(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_x86_lbhu(block, X86_LB, dest, addr, off, false);
}

static inline void rvjit32_native_lbu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_x86_lbhu(block, X86_LBU, dest, addr, off, false);
}

static inline void rvjit32_native_lh(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_x86_lbhu(block, X86_LH, dest, addr, off, false);
}

static inline void rvjit32_native_lhu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_x86_lbhu(block, X86_LHU, dest, addr, off, false);
}

static inline void rvjit32_native_lw(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_x86_lwdu_sbwd(block, X86_LWU_LD, dest, addr, off, false);
}

static inline void rvjit32_native_sb(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_x86_sb(block, src, addr, off);
}

static inline void rvjit32_native_sh(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_x86_sh(block, src, addr, off);
}

static inline void rvjit32_native_sw(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_x86_lwdu_sbwd(block, X86_SW_SD, src, addr, off, false);
}

static inline branch_t rvjit32_native_bne(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_x86_branch(block, X86_BNE, hrs1, hrs2, handle, target, false);
}

static inline branch_t rvjit32_native_beq(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_x86_branch(block, X86_BEQ, hrs1, hrs2, handle, target, false);
}

static inline branch_t rvjit32_native_beqz(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_x86_branch_imm(block, X86_BEQ, hrs1, 0, handle, target, false);
}

static inline branch_t rvjit32_native_bnez(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_x86_branch_imm(block, X86_BNE, hrs1, 0, handle, target, false);
}

static inline branch_t rvjit32_native_blt(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_x86_branch(block, X86_BLT, hrs1, hrs2, handle, target, false);
}

static inline branch_t rvjit32_native_bge(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_x86_branch(block, X86_BGE, hrs1, hrs2, handle, target, false);
}

static inline branch_t rvjit32_native_bltu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_x86_branch(block, X86_BLTU, hrs1, hrs2, handle, target, false);
}

static inline branch_t rvjit32_native_bgeu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_x86_branch(block, X86_BGEU, hrs1, hrs2, handle, target, false);
}

/*
 * RV64
 */
#ifdef RVJIT_NATIVE_64BIT
static inline void rvjit64_native_add(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_op(block, X86_ADD, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_addw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_op(block, X86_ADD, hrds, hrs1, hrs2, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

static inline void rvjit64_native_sub(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_op(block, X86_SUB, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_subw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_op(block, X86_SUB, hrds, hrs1, hrs2, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

static inline void rvjit64_native_or(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_op(block, X86_OR, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_and(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_op(block, X86_AND, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_xor(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_op(block, X86_XOR, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_sra(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_shift_op(block, X86_SRA, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_sraw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_shift_op(block, X86_SRA, hrds, hrs1, hrs2, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

static inline void rvjit64_native_srl(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_shift_op(block, X86_SRL, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_srlw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_shift_op(block, X86_SRL, hrds, hrs1, hrs2, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

static inline void rvjit64_native_sll(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_shift_op(block, X86_SLL, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_sllw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_shift_op(block, X86_SLL, hrds, hrs1, hrs2, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

static inline void rvjit64_native_addi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_x86_2reg_imm_op(block, X86_ADD_IMM, hrds, hrs1, imm, true);
}

static inline void rvjit64_native_addiw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    if (imm) {
        rvjit_x86_2reg_imm_op(block, X86_ADD_IMM, hrds, hrs1, imm, false);
        rvjit_x86_movsxd(block, hrds, hrds);
    } else {
        rvjit_x86_movsxd(block, hrds, hrs1);
    }
}

static inline void rvjit64_native_ori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_x86_2reg_imm_op(block, X86_OR_IMM, hrds, hrs1, imm, true);
}

static inline void rvjit64_native_andi(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_x86_2reg_imm_op(block, X86_AND_IMM, hrds, hrs1, imm, true);
}

static inline void rvjit64_native_xori(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_x86_2reg_imm_op(block, X86_XOR_IMM, hrds, hrs1, imm, true);
}

static inline void rvjit64_native_srli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_x86_2reg_imm_shift_op(block, X86_SRL, hrds, hrs1, imm, true);
}

static inline void rvjit64_native_srliw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    if (imm) {
        rvjit_x86_2reg_imm_shift_op(block, X86_SRL, hrds, hrs1, imm, false);
        rvjit_x86_movsxd(block, hrds, hrds);
    } else {
        rvjit_x86_movsxd(block, hrds, hrs1);
    }
}

static inline void rvjit64_native_srai(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_x86_2reg_imm_shift_op(block, X86_SRA, hrds, hrs1, imm, true);
}

static inline void rvjit64_native_sraiw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    if (imm) {
        rvjit_x86_2reg_imm_shift_op(block, X86_SRA, hrds, hrs1, imm, false);
        rvjit_x86_movsxd(block, hrds, hrds);
    } else {
        rvjit_x86_movsxd(block, hrds, hrs1);
    }
}

static inline void rvjit64_native_slli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_x86_2reg_imm_shift_op(block, X86_SLL, hrds, hrs1, imm, true);
}

static inline void rvjit64_native_slliw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    if (imm) {
        rvjit_x86_2reg_imm_shift_op(block, X86_SLL, hrds, hrs1, imm, false);
        rvjit_x86_movsxd(block, hrds, hrds);
    } else {
        rvjit_x86_movsxd(block, hrds, hrs1);
    }
}

static inline void rvjit64_native_slti(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_x86_2reg_imm_slt_op(block, X86_SETL, hrds, hrs1, imm, true);
}

static inline void rvjit64_native_sltiu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    rvjit_x86_2reg_imm_slt_op(block, X86_SETB, hrds, hrs1, imm, true);
}

static inline void rvjit64_native_slt(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_slt_op(block, X86_SETL, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_sltu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_3reg_slt_op(block, X86_SETB, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_lb(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_x86_lbhu(block, X86_LB, dest, addr, off, true);
}

static inline void rvjit64_native_lbu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_x86_lbhu(block, X86_LBU, dest, addr, off, false);
}

static inline void rvjit64_native_lh(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_x86_lbhu(block, X86_LH, dest, addr, off, true);
}

static inline void rvjit64_native_lhu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_x86_lbhu(block, X86_LHU, dest, addr, off, false);
}

static inline void rvjit64_native_lw(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_x86_lwdu_sbwd(block, X86_LW, dest, addr, off, true);
}

static inline void rvjit64_native_lwu(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_x86_lwdu_sbwd(block, X86_LWU_LD, dest, addr, off, false);
}

static inline void rvjit64_native_ld(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    rvjit_x86_lwdu_sbwd(block, X86_LWU_LD, dest, addr, off, true);
}

static inline void rvjit64_native_sb(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_x86_sb(block, src, addr, off);
}

static inline void rvjit64_native_sh(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_x86_sh(block, src, addr, off);
}

static inline void rvjit64_native_sw(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_x86_lwdu_sbwd(block, X86_SW_SD, src, addr, off, false);
}

static inline void rvjit64_native_sd(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    rvjit_x86_lwdu_sbwd(block, X86_SW_SD, src, addr, off, true);
}

static inline branch_t rvjit64_native_bne(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_x86_branch(block, X86_BNE, hrs1, hrs2, handle, target, true);
}

static inline branch_t rvjit64_native_beq(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_x86_branch(block, X86_BEQ, hrs1, hrs2, handle, target, true);
}

static inline branch_t rvjit64_native_beqz(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_x86_branch_imm(block, X86_BEQ, hrs1, 0, handle, target, true);
}

static inline branch_t rvjit64_native_bnez(rvjit_block_t* block, regid_t hrs1, branch_t handle, bool target)
{
    return rvjit_x86_branch_imm(block, X86_BNE, hrs1, 0, handle, target, true);
}

static inline branch_t rvjit64_native_blt(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_x86_branch(block, X86_BLT, hrs1, hrs2, handle, target, true);
}

static inline branch_t rvjit64_native_bge(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_x86_branch(block, X86_BGE, hrs1, hrs2, handle, target, true);
}

static inline branch_t rvjit64_native_bltu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_x86_branch(block, X86_BLTU, hrs1, hrs2, handle, target, true);
}

static inline branch_t rvjit64_native_bgeu(rvjit_block_t* block, regid_t hrs1, regid_t hrs2, branch_t handle, bool target)
{
    return rvjit_x86_branch(block, X86_BGEU, hrs1, hrs2, handle, target, true);
}

#endif

#endif
