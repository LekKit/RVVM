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

#define X64_REX_W     0x48  // Operands are 64-bit wide
#define X64_REX_R     0x44  // Second (destination) register is >= R8
#define X64_REX_X     0x42
#define X64_REX_B     0x41  // First (source) register is >= R8

#define X86_2OPERANDS 0xC0
#define X86_IMM_OP    0x81

#define X86_PUSH      0x50
#define X86_POP       0x58
#define X86_MOV_IMM   0xB8
#define X86_MOV_R_M   0x89
#define X86_MOV_M_R   0x8B
#define X86_XCHG      0x87
#define X86_MOVSXD    0x63

#define X86_ADD       0x01
#define X86_SUB       0x29
#define X86_OR        0x09
#define X86_AND       0x31
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
#else
    return 0;
#endif
}

// 2 register operands instruction
static inline void rvjit_x86_2reg_op(rvjit_block_t* block, uint8_t opcode, regid_t dest, regid_t src, bool bits_64)
{
#ifndef RVJIT_NATIVE_64BIT
    bits_64 = false;
#endif
    uint8_t code[3];
    // If we are operating on 64 bit values set wide prefix
    code[0] = bits_64 ? X64_REX_W : 0;
    code[1] = opcode;
    code[2] = X86_2OPERANDS;
    if (src >= X64_R8) {
        // Now the bits_64 flag means we have a prefix
        bits_64 = true;
        code[0] |= X64_REX_R;
        code[2] += (src - X64_R8) << 3;
    } else {
        code[2] += src << 3;
    }
    if (dest >= X64_R8) {
        // Same
        bits_64 = true;
        code[0] |= X64_REX_B;
        code[2] += dest - X64_R8;
    } else {
        code[2] += dest;
    }
    rvjit_put_code(block, code + (bits_64 ? 0 : 1), bits_64 ? 3 : 2);
}

// 1 register operand + 32-bit sign-extended immediate instruction
static inline void rvjit_x86_r_imm_op(rvjit_block_t* block, uint8_t opcode, regid_t reg, int32_t imm, bool bits_64)
{
#ifndef RVJIT_NATIVE_64BIT
    bits_64 = false;
#endif
    uint8_t code[7];
    code[0] = bits_64 ? X64_REX_W : 0;
    code[1] = X86_IMM_OP;
    code[2] = opcode;
    if (reg >= X64_R8) {
        bits_64 = true;
        code[0] |= X64_REX_B;
        code[2] += reg - X64_R8;
    } else {
        code[2] += reg;
    }
    write_uint32_le(code + 3, imm);
    rvjit_put_code(block, code + (bits_64 ? 0 : 1), bits_64 ? 7 : 6);
}

// 1 register operand + shift amount immediate (If imm is 0, cl(ecx) register is used as shift amount)
// For whatever stupid reason we cannot use any register as shift amount, needs workarounds
static inline void rvjit_x86_shift_op(rvjit_block_t* block, uint8_t opcode, regid_t reg, uint8_t imm, bool bits_64)
{
#ifndef RVJIT_NATIVE_64BIT
    bits_64 = false;
#endif
    uint8_t code[4];
    code[0] = bits_64 ? X64_REX_W : 0;
    code[1] = imm ? 0xC1 : 0xD3;
    code[2] = opcode;
    code[3] = imm;
    if (reg >= X64_R8) {
        bits_64 = true;
        code[0] |= X64_REX_B;
        code[2] += reg - X64_R8;
    } else {
        code[2] += reg;
    }
    rvjit_put_code(block, code + (bits_64 ? 0 : 1), 2 + (bits_64 ? 1 : 0) + (imm ? 1 : 0));
}

// Set lower 8 bits of native register to specific cmp result
static inline void rvjit_x86_setcc(rvjit_block_t* block, uint8_t opcode, regid_t reg)
{
    uint8_t code[4];
    code[0] = X64_REX_B;
    code[1] = 0x0F;
    code[2] = opcode;
    code[3] = 0xC0 + reg;
    if (reg < X64_R8)
        rvjit_put_code(block, code+1, 3);
    else
        rvjit_put_code(block, code, 4);
}

// Copy data from native register src to dest
static inline void rvjit_x86_mov(rvjit_block_t* block, regid_t dest, regid_t src, bool bits_64)
{
    rvjit_x86_2reg_op(block, X86_MOV_R_M, dest, src, bits_64);
}

// Swap data between 2 registers
static inline void rvjit_x86_xchg(rvjit_block_t* block, regid_t dest, regid_t src)
{
    rvjit_x86_2reg_op(block, X86_XCHG, dest, src, true);
}

// Sign-extend data from 32-bit src to 64-bit dest
static inline void rvjit_x86_movsxd(rvjit_block_t* block, regid_t dest, regid_t src)
{
    rvjit_x86_2reg_op(block, X86_MOVSXD, dest, src, true);
}

// Zero-extend data from 8-bit src to full register
static inline void rvjit_x86_movzxb(rvjit_block_t* block, regid_t dest, regid_t src)
{
    uint8_t code[4];
    code[0] = 0;
    code[1] = 0x0F;
    code[2] = 0xB6;
    code[3] = 0xC0;
    if (src >= X64_R8) {
        code[0] |= X64_REX_R;
        code[3] += (src - X64_R8) << 3;
    } else {
        code[3] += src << 3;
    }
    if (dest >= X64_R8) {
        code[0] |= X64_REX_B;
        code[3] += dest - X64_R8;
    } else {
        code[3] += dest;
    }
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 4 : 3);
}

static inline void rvjit_x86_3reg_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, regid_t hrs2, bool bits_64)
{
    if (hrds == hrs1) {
        rvjit_x86_2reg_op(block, opcode, hrds, hrs2, bits_64);
    } else if (opcode != X86_SUB && hrds == hrs2) {
        rvjit_x86_2reg_op(block, opcode, hrds, hrs1, bits_64);
    } else {
        rvjit_x86_mov(block, hrds, hrs1, bits_64);
        rvjit_x86_2reg_op(block, opcode, hrds, hrs2, bits_64);
    }
}

static inline void rvjit_x86_2reg_imm_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, int32_t imm, bool bits_64)
{
    if (hrds != hrs1) rvjit_x86_mov(block, hrds, hrs1, bits_64);
    if (opcode != X86_AND && imm) rvjit_x86_r_imm_op(block, opcode, hrds, imm, bits_64);
}

static inline void rvjit_x86_2reg_imm_shift_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, uint8_t imm, bool bits_64)
{
    if (hrds != hrs1) rvjit_x86_mov(block, hrds, hrs1, bits_64);
    if (imm) rvjit_x86_shift_op(block, opcode, hrds, imm, bits_64);
}

static inline void rvjit_x86_3reg_shift_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, regid_t hrs2, bool bits_64)
{
    if (hrds != hrs1) rvjit_x86_mov(block, hrds, hrs1, bits_64);
    if (hrs2 != X86_ECX) rvjit_x86_xchg(block, X86_ECX, hrs2); // I hate x86
    rvjit_x86_shift_op(block, opcode, hrds, 0, bits_64);
    if (hrs2 != X86_ECX) rvjit_x86_xchg(block, X86_ECX, hrs2);
}

/*
 * Basic functionality
 */
static inline void rvjit_native_zero_reg(rvjit_block_t* block, regid_t reg)
{
    rvjit_x86_3reg_op(block, X86_XOR, reg, reg, reg, false);
}

static inline void rvjit_native_ret(rvjit_block_t* block)
{
    rvjit_put_code(block, "\xC3", 1);
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

// Set native register reg to zero-extended 32-bit imm
static inline void rvjit_native_setreg32(rvjit_block_t* block, regid_t reg, uint32_t imm)
{
    uint8_t code[6];
    code[0] = 0;
    code[1] = X86_MOV_IMM;
    if (reg >= X64_R8) {
        code[0] |= X64_REX_B;
        code[1] += reg - X64_R8;
    } else {
        code[1] += reg;
    }
    write_uint32_le(code + 2, imm);
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 6 : 5);
}

// Set native register reg to sign-extended 32-bit imm
static inline void rvjit_native_setreg32s(rvjit_block_t* block, regid_t reg, int32_t imm)
{
#ifdef RVJIT_NATIVE_64BIT
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
    write_uint32_le(code + 3, imm);
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
    write_uint64_le(code + 2, imm);
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

// Store native register src to memory pointed to by (reg addr + offset)
static inline void rvjit_native_store(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    uint8_t code[7];
#ifdef RVJIT_NATIVE_64BIT
    code[0] = X64_REX_W;
    code[1] = X86_MOV_R_M;
    code[2] = 0x80;
    if (src >= X64_R8) {
        code[0] |= X64_REX_R;
        code[2] += (src - X64_R8) << 3;
    } else {
        code[2] += src << 3;
    }
    if (addr >= X64_R8) {
        code[0] |= X64_REX_B;
        code[2] += addr - X64_R8;
    } else {
        code[2] += addr;
    }
    write_uint32_le(code + 3, off);
    rvjit_put_code(block, code, 7);
#else
    code[0] = X86_MOV_R_M;
    code[1] = 0x80 + (src << 3) + addr;
    write_uint32_le(code + 2, off);
    rvjit_put_code(block, code, 6);
#endif
}

// Load from memory pointed to by (reg addr + offset) to native register dest
static inline void rvjit_native_load(rvjit_block_t* block, regid_t dest, regid_t addr, int32_t off)
{
    uint8_t code[7];
#ifdef RVJIT_NATIVE_64BIT
    code[0] = X64_REX_W;
    code[1] = X86_MOV_M_R;
    code[2] = 0x80;
    if (dest >= X64_R8) {
        code[0] |= X64_REX_R;
        code[2] += (dest - X64_R8) << 3;
    } else {
        code[2] += dest << 3;
    }
    if (addr >= X64_R8) {
        code[0] |= X64_REX_B;
        code[2] += addr - X64_R8;
    } else {
        code[2] += addr;
    }
    write_uint32_le(code + 3, off);
    rvjit_put_code(block, code, 7);
#else
    code[0] = X86_MOV_M_R;
    code[1] = 0x80 + (dest << 3) + addr;
    write_uint32_le(code + 2, off);
    rvjit_put_code(block, code, 6);
#endif
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
    if (hrds != hrs1) rvjit_native_zero_reg(block, hrds);
    rvjit_x86_r_imm_op(block, X86_CMP_IMM, hrs1, imm, false);
    rvjit_x86_setcc(block, X86_SETL, hrds);
    if (hrds == hrs1) rvjit_x86_movzxb(block, hrds, hrds);
}

static inline void rvjit32_native_sltiu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    if (hrds != hrs1) rvjit_native_zero_reg(block, hrds);
    rvjit_x86_r_imm_op(block, X86_CMP_IMM, hrs1, imm, false);
    rvjit_x86_setcc(block, X86_SETB, hrds);
    if (hrds == hrs1) rvjit_x86_movzxb(block, hrds, hrds);
}

static inline void rvjit32_native_slt(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    if (hrds != hrs1 && hrds != hrs2) rvjit_native_zero_reg(block, hrds);
    rvjit_x86_2reg_op(block, X86_CMP, hrs1, hrs2, false);
    rvjit_x86_setcc(block, X86_SETL, hrds);
    if (hrds == hrs1 || hrds == hrs2) rvjit_x86_movzxb(block, hrds, hrds);
}

static inline void rvjit32_native_sltu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    if (hrds != hrs1 && hrds != hrs2) rvjit_native_zero_reg(block, hrds);
    rvjit_x86_2reg_op(block, X86_CMP, hrs1, hrs2, false);
    rvjit_x86_setcc(block, X86_SETB, hrds);
    if (hrds == hrs1 || hrds == hrs2) rvjit_x86_movzxb(block, hrds, hrds);
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
    rvjit_x86_2reg_imm_op(block, X86_ADD_IMM, hrds, hrs1, imm, false);
    rvjit_x86_movsxd(block, hrds, hrds);
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
    rvjit_x86_2reg_imm_shift_op(block, X86_SRL, hrds, hrs1, imm, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

static inline void rvjit64_native_srai(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_x86_2reg_imm_shift_op(block, X86_SRA, hrds, hrs1, imm, true);
}

static inline void rvjit64_native_sraiw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_x86_2reg_imm_shift_op(block, X86_SRA, hrds, hrs1, imm, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

static inline void rvjit64_native_slli(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_x86_2reg_imm_shift_op(block, X86_SLL, hrds, hrs1, imm, true);
}

static inline void rvjit64_native_slliw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, uint8_t imm)
{
    rvjit_x86_2reg_imm_shift_op(block, X86_SLL, hrds, hrs1, imm, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

static inline void rvjit64_native_slti(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    if (hrds != hrs1) rvjit_native_zero_reg(block, hrds);
    rvjit_x86_r_imm_op(block, X86_CMP_IMM, hrs1, imm, true);
    rvjit_x86_setcc(block, X86_SETL, hrds);
    if (hrds == hrs1) rvjit_x86_movzxb(block, hrds, hrds);
}

static inline void rvjit64_native_sltiu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, int32_t imm)
{
    if (hrds != hrs1) rvjit_native_zero_reg(block, hrds);
    rvjit_x86_r_imm_op(block, X86_CMP_IMM, hrs1, imm, true);
    rvjit_x86_setcc(block, X86_SETB, hrds);
    if (hrds == hrs1) rvjit_x86_movzxb(block, hrds, hrds);
}

static inline void rvjit64_native_slt(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    if (hrds != hrs1 && hrds != hrs2) rvjit_native_zero_reg(block, hrds);
    rvjit_x86_2reg_op(block, X86_CMP, hrs1, hrs2, true);
    rvjit_x86_setcc(block, X86_SETL, hrds);
    if (hrds == hrs1 || hrds == hrs2) rvjit_x86_movzxb(block, hrds, hrds);
}

static inline void rvjit64_native_sltu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    if (hrds != hrs1 && hrds != hrs2) rvjit_native_zero_reg(block, hrds);
    rvjit_x86_2reg_op(block, X86_CMP, hrs1, hrs2, true);
    rvjit_x86_setcc(block, X86_SETB, hrds);
    if (hrds == hrs1 || hrds == hrs2) rvjit_x86_movzxb(block, hrds, hrds);
}
#endif

#endif
