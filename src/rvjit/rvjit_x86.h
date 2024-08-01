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

#ifndef RVJIT_X86_H
#define RVJIT_X86_H

#include "rvjit.h"
#include "mem_ops.h"
#include "compiler.h"
#include "utils.h"

#if defined(RVJIT_NATIVE_64BIT) && defined(_MSC_VER)
#include <intrin.h> // For cpuid
#endif

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

#define SSE_XMM0  0x0
#define SSE_XMM1  0x1
#define SSE_XMM2  0x2
#define SSE_XMM3  0x3
#define SSE_XMM4  0x4
#define SSE_XMM5  0x5
#define SSE_XMM6  0x6
#define SSE_XMM7  0x7
#define SSE_XMM8  0x8
#define SSE_XMM9  0x9
#define SSE_XMM10 0xA
#define SSE_XMM11 0xB
#define SSE_XMM12 0xC
#define SSE_XMM13 0xD
#define SSE_XMM14 0xE
#define SSE_XMM15 0xF

#ifdef RVJIT_ABI_SYSV
#define VM_PTR_REG X64_RDI
#elif RVJIT_ABI_WIN64
#define VM_PTR_REG X64_RCX
#elif RVJIT_ABI_FASTCALL
#define VM_PTR_REG X86_ECX
#endif

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
#else
    return 0;
#endif
}

static inline size_t rvjit_native_abireclaim_hregmask()
{
#ifdef RVJIT_NATIVE_64BIT
    return rvjit_hreg_mask(X64_RBX) |
           rvjit_hreg_mask(X64_RBP) |
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
           rvjit_hreg_mask(X86_EBP) |
           rvjit_hreg_mask(X86_ESI) |
           rvjit_hreg_mask(X86_EDI);
#else
    return 0;
#endif
}

static inline size_t rvjit_native_default_fpu_regmask()
{
#ifdef RVJIT_ABI_SYSV
    return 0xFFFF; // All XMM registers are caller-saved
#elif RVJIT_ABI_WIN64
    return 0x1F;  // XMM0 - XMM5 are caller-saved
#elif RVJIT_ABI_FASTCALL
    return 0x1F;  // TODO
#else
    return 0;
#endif
}

// REX prefixes
#define X64_REX_W     0x48  // Operands are 64-bit wide
#define X64_REX_R     0x44  // Second (destination) register is >= R8
#define X64_REX_X     0x42
#define X64_REX_B     0x41  // First (source) register is >= R8

static inline bool x86_is_byte_imm(int32_t imm)
{
    return imm == (int32_t)(int8_t)imm;
}

// Valid 1-byte 1-reg opcodes
#define X86_PUSH 0x50
#define X86_POP  0x58

static inline void rvjit_x86_1byte_1reg_op(rvjit_block_t* block, uint8_t opcode, regid_t reg)
{
    uint8_t code[2] = { X64_REX_B, opcode | (reg & 0x7), };
    size_t size = (reg < X64_R8) ? 1 : 2;
    size_t off = 2 - size;
    rvjit_put_code(block, code + off, size);
}

static inline void rvjit_native_push(rvjit_block_t* block, regid_t reg)
{
    rvjit_x86_1byte_1reg_op(block, X86_PUSH, reg);
}

static inline void rvjit_native_pop(rvjit_block_t* block, regid_t reg)
{
    rvjit_x86_1byte_1reg_op(block, X86_POP, reg);
}

// Valid 2-reg opcodes
#define X86_ADD     0x01
#define X86_SUB     0x29
#define X86_OR      0x09
#define X86_AND     0x21
#define X86_XOR     0x31
#define X86_CMP     0x39
#define X86_MOV_R_M 0x89 // Move between registers
#define X86_MOVSXD  0x63 // Sign-extend 32 to 64 bits, x86_64 only!
#define X86_XCHG    0x87 // This has a shorter encoding..

#define X86_2_REGS  0xC0

static inline void rvjit_x86_2reg_op(rvjit_block_t* block, uint8_t opcode, regid_t dst, regid_t src, bool bits_64)
{
    uint8_t code[3] = { 0, opcode, X86_2_REGS | ((src & 0x7) << 3) | (dst & 0x7), };
    // If we are operating on 64 bit values set wide prefix
    if (bits_64) code[0] = X64_REX_W;
    if (src >= X64_R8) code[0] |= X64_REX_R;
    if (dst >= X64_R8) code[0] |= X64_REX_B;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 3 : 2);
}

// Valid 2-reg 0f prefix opcodes
#define X86_IMUL_2REG 0xAF
#define X86_BTS       0xAB // Set bit
#define X86_BTR       0xB3 // Clear bit
#define X86_BTC       0xBB // Invert bit
#define X86_BSF       0xBC // Bit scan forward
#define X86_BSR       0xBD // Bit scan reverse
#define X86_MOVSXW    0xBF // Sign-extend 16-bit
#define X86_MOVZXW    0xB7 // Zero-extend 16-bit
#define X86_CMOVE     0x44 // Conditional move if equal, Pentium Pro+
#define X86_CMOVNE    0x45 // Conditional move if not equal, Pentium Pro+

static inline void rvjit_x86_0f_2reg_op(rvjit_block_t* block, uint8_t opcode, regid_t dst, regid_t src, bool bits_64)
{
    uint8_t code[4] = { 0, 0x0F, opcode, X86_2_REGS | ((dst & 0x7) << 3) | (src & 0x7), };
    if (bits_64) code[0] = X64_REX_W;
    if (src >= X64_R8) code[0] |= X64_REX_B;
    if (dst >= X64_R8) code[0] |= X64_REX_R;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 4 : 3);
}

#define X86_MOVSXB    0xBE // Sign-extend 8-bit
#define X86_MOVZXB    0xB6 // Zero-extend 8-bit

// Zero/sign extend data from 8-bit src to full register
// Careful: not all 8-bit registers are accessible on i386
static inline void rvjit_x86_movxb(rvjit_block_t* block, uint8_t opcode, regid_t dst, regid_t src, bool bits_64)
{
    uint8_t code[4] = { 0, 0x0F, opcode, X86_2_REGS | ((dst & 0x7) << 3) | (src & 0x7), };
    if (bits_64) code[0] = X64_REX_W;
    if (src >= X64_R8) code[0] |= X64_REX_B;
    if (dst >= X64_R8) code[0] |= X64_REX_R;
    // REX prefix for using sil, dil, r8b... registers
    if (src > X86_EBX) code[0] |= 0x40;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 4 : 3);
}

// Valid reg + imm opcodes
#define X86_ADD_IMM 0xC0
#define X86_OR_IMM  0xC8
#define X86_AND_IMM 0xE0
#define X86_XOR_IMM 0xF0
#define X86_CMP_IMM 0xF8

#define X86_IMM_OP  0x81

// 1 register operand + 32-bit sign-extended immediate instruction
static inline void rvjit_x86_r_imm_op(rvjit_block_t* block, uint8_t opcode, regid_t reg, int32_t imm, bool bits_64)
{
    uint8_t code[7] = { 0, X86_IMM_OP, opcode | (reg & 0x7),  };
    if (bits_64) code[0] = X64_REX_W;
    if (reg >= X64_R8) code[0] |= X64_REX_B;
    size_t insn_size = code[0] ? 4 : 3;
    if (x86_is_byte_imm(imm)) {
        code[1] |= 0x02; // IMM length override
        write_uint8(code + 3, imm);
    } else {
        write_uint32_le_m(code + 3, imm);
        insn_size += 3;
    }
    rvjit_put_code(block, code + (code[0] ? 0 : 1), insn_size);
}

#define X86_BTS_IMM 0xE8 // Set bit
#define X86_BTR_IMM 0xF0 // Clear bit
#define X86_BTC_IMM 0xF8 // Invert bit

// Single-bit imm operations
static inline void rvjit_x86_s_bit_imm(rvjit_block_t* block, uint8_t opcode, regid_t reg, uint8_t imm, bool bits_64)
{
    uint8_t code[5] = { 0, 0x0F, 0xBA, opcode | (reg & 0x7), imm, };
    if (bits_64) code[0] = X64_REX_W;
    if (reg >= X64_R8) code[0] |= X64_REX_B;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 5 : 4);
}

#define X86_SLL 0xE0
#define X86_SRL 0xE8
#define X86_SRA 0xF8
#define X86_ROL 0xC0
#define X86_ROR 0xC8

// 1 register operand, cl(ecx) register is used as shift amount
// For whatever stupid reason we cannot use any register as shift amount, needs workarounds
static inline void rvjit_x86_shift_op(rvjit_block_t* block, uint8_t opcode, regid_t reg, bool bits_64)
{
    uint8_t code[3] = { 0, 0xD3, opcode | (reg & 0x7), };
    if (bits_64) code[0] = X64_REX_W;
    if (reg >= X64_R8) code[0] |= X64_REX_B;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 3 : 2);
}

static inline void rvjit_x86_imm_shift_op(rvjit_block_t* block, uint8_t opcode, regid_t reg, uint8_t imm, bool bits_64)
{
    uint8_t code[4] = { 0, 0xC1, opcode | (reg & 0x7), imm, };
    if (bits_64) code[0] = X64_REX_W;
    if (reg >= X64_R8) code[0] |= X64_REX_B;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 4 : 3);
}

#define X86_NEG  0xD8
#define X86_NOT  0xD0
#define X86_MUL  0xE0
#define X86_IMUL 0xE8
#define X86_DIV  0xF0
#define X86_IDIV 0xF8

// mul/imul EDX:EAX = EAX * reg, used for mulh
// div/idiv EAX = EDX:EAX / reg; EDX = EDX:EAX % reg, used for div
// neg, not
static inline void rvjit_x86_1reg_op(rvjit_block_t* block, uint8_t opcode, regid_t reg, bool bits_64)
{
    uint8_t code[3] = { 0, 0xF7, opcode | (reg & 0x7), };
    if (bits_64) code[0] = X64_REX_W;
    if (reg >= X64_R8) code[0] |= X64_REX_B;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 3 : 2);
}

// Negate a register
static inline void rvjit_x86_neg(rvjit_block_t* block, regid_t reg, bool bits_64)
{
    rvjit_x86_1reg_op(block, X86_NEG, reg, bits_64);
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

// Memory operand offset size
#define X86_MEM_OFFB  0x40
#define X86_MEM_OFFW  0x80

// Emit memory-addressing part of the instruction
static inline void rvjit_x86_memory_ref(rvjit_block_t* block, regid_t dst, regid_t addr, int32_t off)
{
    uint8_t code[6] = { (addr & 0x7) | ((dst & 0x7) << 3), };
    uint8_t insn_size = 1;
    if ((addr & 0x7) == X86_ESP) {
        // SIB byte (edge case)
        code[1] = 0x24;
        insn_size++;
    }
    if (!x86_is_byte_imm(off)) {
        // Huge offset
        code[0] |= X86_MEM_OFFW;
        write_uint32_le_m(code + insn_size, off);
        insn_size += 4;
    } else if (off || (addr & 0x7) == X86_EBP) {
        // 1-byte offset
        code[0] |= X86_MEM_OFFB;
        code[insn_size] = off;
        insn_size++;
    }
    rvjit_put_code(block, code, insn_size);
}

// x86 substitute for addi instruction
static inline void rvjit_x86_lea_addi(rvjit_block_t* block, regid_t dst, regid_t src, int32_t imm, bool bits_64)
{
    uint8_t code[2] = { 0, 0x8D, };
    if (bits_64) code[0] = X64_REX_W;
    if (src >= X64_R8) code[0] |= X64_REX_B;
    if (dst >= X64_R8) code[0] |= X64_REX_R;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 2 : 1);
    rvjit_x86_memory_ref(block, dst, src, imm);
}

// x86 substitute for 3-operand add instruction (With second operand shift)
static inline void rvjit_x86_lea_add(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2, uint8_t shift, bool bits_64)
{
    uint8_t code[5] = { 0x00, 0x8D, 0x04 | ((hrds & 0x7) << 3), (hrs1 & 0x7) | ((hrs2 & 0x7) << 3) | (shift << 6), 0x00, };
    uint8_t inst_size = 3;
    if (bits_64) code[0] = X64_REX_W;
    if (hrds >= X64_R8) code[0] |= X64_REX_R;
    if (hrs1 >= X64_R8) code[0] |= X64_REX_B;
    if (hrs2 >= X64_R8) code[0] |= X64_REX_X;
    if ((hrs1 & 0x7) == X86_EBP) {
        code[2] |= X86_MEM_OFFB;
        inst_size++;
    }
    rvjit_put_code(block, code + (code[0] ? 0 : 1), inst_size + (code[0] ? 1 : 0));
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
        if (opcode == X86_ADD) {
            // add r1, r2, r3 -> lea r1, [r2 + r3]
            rvjit_x86_lea_add(block, hrds, hrs1, hrs2, 0, bits_64);
            return;
        }
        rvjit_x86_mov(block, hrds, hrs1, bits_64);
        rvjit_x86_2reg_op(block, opcode, hrds, hrs2, bits_64);
    }
}

static inline void rvjit_x86_2reg_imm_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, int32_t imm, bool bits_64)
{
    if (opcode == X86_AND_IMM) {
        if (imm == 0) {
            // Optimize andi r1, r2, 0 -> xor r1, r1
            rvjit_x86_2reg_op(block, X86_XOR, hrds, hrds, false);
            return;
        } else if (imm == 0xFF && x86_byte_reg_usable(hrs1)) {
            // Optimize andi r1, r2, 0xFF -> movzxb r1, r2
            rvjit_x86_movxb(block, X86_MOVZXB, hrds, hrs1, false);
            return;
        } else if (imm > 0) {
            // Remove REX.W prefix for unsigned andi imm
            bits_64 = false;
        }
    } else if (opcode == X86_ADD_IMM && imm && hrds != hrs1) {
        // addi r1, r2, imm -> lea r1, [r2 + imm]
        rvjit_x86_lea_addi(block, hrds, hrs1, imm, bits_64);
        return;
    }
    if (hrds != hrs1) rvjit_x86_mov(block, hrds, hrs1, bits_64);
    if (opcode == X86_XOR_IMM && imm == -1) {
        // xor r1, r2, -1 -> not r1, r2
        rvjit_x86_1reg_op(block, X86_NOT, hrds, bits_64);
    } else if (imm) rvjit_x86_r_imm_op(block, opcode, hrds, imm, bits_64);
}

static inline void rvjit_x86_2reg_imm_shift_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, uint8_t imm, bool bits_64)
{
    if (hrds != hrs1) rvjit_x86_mov(block, hrds, hrs1, bits_64);
    if (imm) rvjit_x86_imm_shift_op(block, opcode, hrds, imm, bits_64);
}

#define X86_VEX_RI 0x80
#define X86_VEX_BI 0x20
#define X86_VEX_W  0x80

// Orthogonal 3-operand shlx/shrx/sarx from BMI2 extension
static inline void rvjit_x86_vex_shift_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, regid_t hrs2, bool bits_64)
{
    uint8_t code[5] = { 0xC4, 0x42, ((~hrs2) & 0xF) << 3, 0xF7, 0xC0 | (hrs1 & 0x7) | ((hrds & 0x7) << 3), };
    if (bits_64) code[2] |= X86_VEX_W;
    if (hrds < X64_R8) code[1] |= X86_VEX_RI;
    if (hrs1 < X64_R8) code[1] |= X86_VEX_BI;
    switch (opcode) {
        case X86_SLL: code[2] |= 0x1; break;
        case X86_SRL: code[2] |= 0x3; break;
        case X86_SRA: code[2] |= 0x2; break;
    }
    rvjit_put_code(block, code, 5);
}

static void rvjit_x86_cpuid_internal(uint32_t eax, uint32_t ecx, uint32_t* regs)
{
#if defined(RVJIT_NATIVE_64BIT) && defined(GNU_EXTS)
    __asm__ __volatile__ (
        "cpuid"
        : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
        : "a"(eax), "c"(ecx));
#elif defined(RVJIT_NATIVE_64BIT) && defined(_MSC_VER)
    __cpuidex(regs, eax, ecx);
#else
    // Don't bother checking fancy extensions on i386 or exotic compilers
    UNUSED(eax);
    UNUSED(ecx);
    memset(regs, 0, sizeof(uint32_t) * 4);
#endif
}

static void rvjit_x86_cpuid(uint32_t eax, uint32_t ecx, uint32_t* regs)
{
    // Check maximum allowed EAX value for cpuid
    uint32_t tmp_regs[4] = {0};
    rvjit_x86_cpuid_internal(0, 0, tmp_regs);

    if (eax <= tmp_regs[0]) {
        rvjit_x86_cpuid_internal(eax, ecx, regs);
    } else {
        memset(regs, 0, sizeof(uint32_t) * 4);
    }
}

static inline bool rvjit_x86_has_bmi2()
{
    static bool bmi2 = false;
    DO_ONCE ({
        if (rvvm_has_arg("rvjit_force_bmi2")) {
            bmi2 = rvvm_getarg_bool("rvjit_force_bmi2");
        } else {
            uint32_t regs[4] = {0};
            rvjit_x86_cpuid(7, 0, regs);
            bmi2 = !!(regs[1] & 0x100);
        }
        if (bmi2) rvvm_info("RVJIT detected x86 BMI2 extension");
    });
    return bmi2;
}

static inline void rvjit_x86_3reg_shift_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, regid_t hrs2, bool bits_64)
{
    /* Shift by register is insane on i386, practically a 1-operand instruction,
     * with CL hardcoded as shift amount reg.
     * This function implements a proper 3-operand intrinsic.
     */
    if (rvjit_x86_has_bmi2()) {
        // On BMI2 hardware, we have 1:1 instruction mappings into shlx/shrx/sarx
        rvjit_x86_vex_shift_op(block, opcode, hrds, hrs1, hrs2, bits_64);
        return;
    }

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
            rvjit_x86_shift_op(block, opcode, hrds, bits_64);
            rvjit_x86_xchg(block, X86_ECX, hrs2);
        } else {
            rvjit_x86_shift_op(block, opcode, hrds, bits_64);
        }
    } else if (hrds == hrs2) {
        // Cursed...
        rvjit_native_push(block, hrs1);
        if (hrs1 == X86_ECX) {
            rvjit_x86_xchg(block, X86_ECX, hrds);
            rvjit_x86_shift_op(block, opcode, hrds, bits_64);
            rvjit_x86_xchg(block, X86_ECX, hrds);
        } else if (hrds != X86_ECX) {
            rvjit_x86_xchg(block, X86_ECX, hrds);
            rvjit_x86_shift_op(block, opcode, hrs1, bits_64);
            rvjit_x86_xchg(block, X86_ECX, hrds);
        } else {
            rvjit_x86_shift_op(block, opcode, hrs1, bits_64);
        }
        rvjit_x86_mov(block, hrds, hrs1, bits_64);
        rvjit_native_pop(block, hrs1);
    } else {
        rvjit_x86_mov(block, hrds, hrs1, bits_64);
        if (hrds == X86_ECX) {
            rvjit_x86_xchg(block, X86_ECX, hrs2);
            rvjit_x86_shift_op(block, opcode, hrs2, bits_64);
            rvjit_x86_xchg(block, X86_ECX, hrs2);
        } else if (hrs2 != X86_ECX) {
            rvjit_x86_xchg(block, X86_ECX, hrs2);
            rvjit_x86_shift_op(block, opcode, hrds, bits_64);
            rvjit_x86_xchg(block, X86_ECX, hrs2);
        } else {
            rvjit_x86_shift_op(block, opcode, hrds, bits_64);
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
    uint8_t code[4] = { 0, 0x0F, opcode, X86_2_REGS | (reg & 0x07), };
    if (reg >= X64_R8) code[0] |= X64_REX_B;
    // REX prefix for using sil, dil, r8b... registers
    if (reg > X86_EBX) code[0] |= 0x40;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 4 : 3);
}

#define X86_SETB 0x92
#define X86_SETL 0x9C

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

#define X86_MOV_IMM 0xB8

// Set native register reg to zero-extended 32-bit imm
static inline void rvjit_native_setreg32(rvjit_block_t* block, regid_t reg, uint32_t imm)
{
    if (imm == 0) {
        rvjit_native_zero_reg(block, reg);
    } else {
        uint8_t code[6] = { 0, X86_MOV_IMM | (reg & 0x7), };
        if (reg >= X64_R8) code[0] |= X64_REX_B;
        write_uint32_le_m(code + 2, imm);
        rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 6 : 5);
    }
}

// Set native register reg to sign-extended 32-bit imm
static inline void rvjit_native_setreg32s(rvjit_block_t* block, regid_t reg, int32_t imm)
{
    if (imm == 0) {
        rvjit_native_zero_reg(block, reg);
    } else {
#ifdef RVJIT_NATIVE_64BIT
        uint8_t code[7] = { X64_REX_W, 0xC7, X86_2_REGS | (reg & 0x7), };
        if (reg >= X64_R8) code[0] |= X64_REX_B;
        write_uint32_le_m(code + 3, imm);
        rvjit_put_code(block, code, 7);
#else
        rvjit_native_setreg32(block, reg, imm);
#endif
    }
}

// Set native register reg to wide imm
static inline void rvjit_native_setregw(rvjit_block_t* block, regid_t reg, uintptr_t imm)
{
#ifdef RVJIT_NATIVE_64BIT
    // movabsq
    uint8_t code[10] = { X64_REX_W, X86_MOV_IMM | (reg & 0x7), };
    if (reg >= X64_R8) code[0] |= X64_REX_B;
    write_uint64_le_m(code + 2, imm);
    rvjit_put_code(block, code, 10);
#else
    rvjit_native_setreg32(block, reg, imm);
#endif
}

// Call a function pointed to by native register
static inline void rvjit_native_callreg(rvjit_block_t* block, regid_t reg)
{
    uint8_t code[3] = { 0, 0xFF, 0xD0 | (reg & 0x7), };
    if (reg >= X64_R8) code[0] = X64_REX_B;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 3 : 2);
}

#define X86_LB  0xBE
#define X86_LBU 0xB6
#define X86_LH  0xBF
#define X86_LHU 0xB7

// For lb/lbu/lh/lhu; bits_64 means signext to full 64-bit reg, not needed for unsigned
static inline void rvjit_x86_lbhu(rvjit_block_t* block, uint8_t opcode, regid_t dest, regid_t addr, int32_t off, bool bits_64)
{
    uint8_t code[3] = { 0, 0x0F, opcode, };
    if (bits_64) code[0] = X64_REX_W;
    if (addr >= X64_R8) code[0] |= X64_REX_B;
    if (dest >= X64_R8) code[0] |= X64_REX_R;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 3 : 2);
    rvjit_x86_memory_ref(block, dest, addr, off);
}

#define X86_LWU_LD  0x8B
#define X86_LW      X86_MOVSXD
#define X86_SB      0x88
#define X86_SW_SD   X86_MOV_R_M

// For lwu/ld bits_64 ? ld : lwu, for lw bits_64 = true!
// For sw/sd bits_64 ? sd : sw, for sb bits_64 = false!
static inline void rvjit_x86_lwdu_sbwd(rvjit_block_t* block, uint8_t opcode, regid_t dest, regid_t addr, int32_t off, bool bits_64)
{
    uint8_t code[2] = { 0, opcode, };
    if (bits_64) code[0] = X64_REX_W;
    if (addr >= X64_R8) code[0] |= X64_REX_B;
    if (dest >= X64_R8) code[0] |= X64_REX_R;
    if (opcode == X86_SB && dest > X86_EBX) code[0] |= 0x40;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 2 : 1);
    rvjit_x86_memory_ref(block, dest, addr, off);
}

static inline void rvjit_x86_sb(rvjit_block_t* block, regid_t src, regid_t addr, int32_t off)
{
    if (x86_byte_reg_usable(src)) {
        rvjit_x86_lwdu_sbwd(block, X86_SB, src, addr, off, false);
    } else {
        if (addr == src) {
            addr = X86_EAX;
        } else if (addr == X86_EAX) {
            addr = src;
        }
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
        if (handle == BRANCH_NEW) {
            branch_t tmp = block->size;
            rvjit_put_code(block, "\xE9\xFB\xFF\xFF\xFF", 5);
            return tmp;
        } else {
            uint8_t code[5] = { 0xE9, };
            write_uint32_le_m(code + 1, handle - block->size - 5);
            rvjit_put_code(block, code, 5);
            return BRANCH_NEW;
        }
    }
}

/*
 * Forward branches are dynamically resized,
 * however this may introduce problems with cross-branching code.
 * RVVM currently generates linear code with no branch intersections,
 * so this isn't a concern, but might be revised.
 */

#define X86_JB   0x72
#define X86_JNB  0x73
#define X86_JE   0x74
#define X86_JNE  0x75
#define X86_JL   0x7C
#define X86_JGE  0x7D

#define X86_BEQ  X86_JE
#define X86_BNE  X86_JNE
#define X86_BLT  X86_JL
#define X86_BGE  X86_JGE
#define X86_BLTU X86_JB
#define X86_BGEU X86_JNB

#define X86_FAR_BRANCH 0x0F
#define X86_FAR_BRANCH_MASK 0x10

static inline branch_t rvjit_x86_branch_entry(rvjit_block_t* block, uint8_t opcode, branch_t handle)
{
    uint8_t code[6] = {0};
    if (handle == BRANCH_NEW) {
        // Forward branch, migh relocate code after it
        branch_t tmp = block->size;
        code[0] = opcode;
        code[1] = 0xFE;
        rvjit_put_code(block, code, 2);
        return tmp;
    } else {
        // Backward branch, no need for relocations
        int32_t offset = handle - block->size - 2;
        if (x86_is_byte_imm(offset)) {
            code[0] = opcode;
            code[1] = offset;
            rvjit_put_code(block, code, 2);
        } else {
            // Far branch
            code[0] = X86_FAR_BRANCH;
            code[1] = opcode + X86_FAR_BRANCH_MASK;
            write_uint32_le_m(code + 2, offset - 4);
            rvjit_put_code(block, code, 6);
        }
        return BRANCH_NEW;
    }
}

static inline branch_t rvjit_x86_branch_target(rvjit_block_t* block, branch_t handle)
{
    if (handle == BRANCH_NEW) {
        return block->size;
    } else {
        int32_t offset = block->size - handle - 2;
        // Patch jump offset
        if (x86_is_byte_imm(offset)) {
            // Offset fits into 1 byte
            block->code[handle + 1] = offset;
        } else {
            // Far branch required, reserve space & relocate the code
            rvjit_put_code(block, "\xCC\xCC\xCC\xCC", 4);
            memmove(block->code + handle + 6, block->code + handle + 2, offset);
            block->code[handle + 1] = block->code[handle] + X86_FAR_BRANCH_MASK;
            block->code[handle] = X86_FAR_BRANCH;
            write_uint32_le_m(block->code + handle + 2, offset);
        }
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
 * Multiply/divide internal functions
 */

// Sign-extend EAX to EDX:EAX
static inline void rvjit_x86_cdq(rvjit_block_t* block, bool bits_64)
{
    if (bits_64) rvjit_put_code(block, "\x48", 1);
    rvjit_put_code(block, "\x99", 1);
}

static inline void rvjit_x86_mul(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2, bool bits_64)
{
    if (hrds == hrs1) {
        rvjit_x86_0f_2reg_op(block, X86_IMUL_2REG, hrds, hrs2, bits_64);
    } else if (hrds == hrs2) {
        rvjit_x86_0f_2reg_op(block, X86_IMUL_2REG, hrds, hrs1, bits_64);
    } else {
        rvjit_x86_mov(block, hrds, hrs1, bits_64);
        rvjit_x86_0f_2reg_op(block, X86_IMUL_2REG, hrds, hrs2, bits_64);
    }
}

// mulh:  X86_IMUL, rem = true
// mulhu: X86_MUL,  rem = true
static inline void rvjit_x86_mulh_div_rem(rvjit_block_t* block, uint8_t opcode, bool rem, regid_t hrds, regid_t hrs1, regid_t hrs2, bool bits_64)
{
    regid_t output_reg = rem ? X86_EDX : X86_EAX;
    regid_t second_reg = rem ? X86_EAX : X86_EDX;
    regid_t s2_reg = hrs2;

    if (hrds != output_reg) rvjit_native_push(block, output_reg);
    if (hrds != second_reg) rvjit_native_push(block, second_reg);

    if (hrs2 == X86_EAX || hrs2 == X86_EDX) {
        // Search for any non-clobbering register
        s2_reg = X86_ECX;
        while (s2_reg == X86_EAX || s2_reg == X86_EDX || s2_reg == hrs1 || s2_reg == hrs2) s2_reg++;
        rvjit_native_push(block, s2_reg);
        rvjit_x86_mov(block, s2_reg, hrs2, bits_64);
    }

    if (hrs1 != X86_EAX) {
        rvjit_x86_mov(block, X86_EAX, hrs1, bits_64);
    }

    if (opcode == X86_DIV) {
        // On unsigned division, EDX input is zero
        rvjit_native_zero_reg(block, X86_EDX);
    } else if (opcode == X86_IDIV) {
        // On signed division, EDX input is a sign-extension of EAX
        rvjit_x86_cdq(block, bits_64);
    }

    rvjit_x86_1reg_op(block, opcode, s2_reg, bits_64);

    if (s2_reg != hrs2) rvjit_native_pop(block, s2_reg);
    if (hrds != second_reg) rvjit_native_pop(block, second_reg);
    if (hrds != output_reg) {
        rvjit_x86_mov(block, hrds, output_reg, bits_64);
        rvjit_native_pop(block, output_reg);
    }
}

static inline void rvjit_x86_mulhsu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2, bool bits_64)
{
    regid_t second_reg = X86_EAX;
    rvjit_x86_mulh_div_rem(block, X86_MUL, true, hrds, hrs1, hrs2, bits_64);
    // Search for any non-clobbering register
    while (second_reg == hrds || second_reg == hrs1 || second_reg == hrs2) second_reg++;
    rvjit_native_push(block, second_reg);
    rvjit_x86_2reg_imm_shift_op(block, X86_SRA, second_reg, hrs1, bits_64 ? 63 : 31, bits_64);
    rvjit_x86_0f_2reg_op(block, X86_IMUL_2REG, second_reg, hrs2, bits_64);
    rvjit_x86_3reg_op(block, X86_ADD, hrds, hrds, second_reg, bits_64);
    rvjit_native_pop(block, second_reg);
}

// div:   X86_IDIV, rem = false
// divu:  X86_DIV,  rem = false
// rem:   X86_IDIV, rem = true
// remu:  X86_DIV,  rem = true
static inline void rvjit_x86_divu_remu(rvjit_block_t* block, bool rem, regid_t hrds, regid_t hrs1, regid_t hrs2, bool bits_64)
{
    branch_t l1 = rvjit_x86_branch_imm(block, X86_BNE, hrs2, 0, BRANCH_NEW, false, bits_64);
    if (rem) {
        if (hrds != hrs1) rvjit_x86_mov(block, hrds, hrs1, bits_64);
    } else {
        rvjit_native_setreg32s(block, hrds, -1);
    }
    branch_t l2 = rvjit_native_jmp(block, BRANCH_NEW, false);
    rvjit_x86_branch_imm(block, X86_BNE, hrs1, 0, l1, true, bits_64);
    rvjit_x86_mulh_div_rem(block, X86_DIV, rem, hrds, hrs1, hrs2, bits_64);
    rvjit_native_jmp(block, l2, true);
}

static inline void rvjit_x86_div_rem(rvjit_block_t* block, bool rem, regid_t hrds, regid_t hrs1, regid_t hrs2, bool bits_64)
{
    regid_t cmp_reg = X86_EAX;
    branch_t l1;

    // Overflow check (rs1 != 0x80000000... && rs2 != -1)
    if (bits_64) {
        // Search for any non-clobbering register
        //while (cmp_reg == hrds || cmp_reg == hrs1 || cmp_reg == hrs2) cmp_reg++;
        //rvjit_native_push(block, cmp_reg);
        cmp_reg = rvjit_claim_hreg(block);
        rvjit_native_setregw(block, cmp_reg, (size_t)0x8000000000000000ULL);
        l1 = rvjit_x86_branch(block, X86_BNE, hrs2, cmp_reg, BRANCH_NEW, false, bits_64);
    } else {
        l1 = rvjit_x86_branch_imm(block, X86_BNE, hrs2, 0x80000000U, BRANCH_NEW, false, bits_64);
    }

    branch_t l2 = rvjit_x86_branch_imm(block, X86_BNE, hrs2, -1, BRANCH_NEW, false, bits_64);

    // Overflow check fallthrough
    if (rem) {
        rvjit_native_setreg32(block, hrds, 0);
    } else {
        if (bits_64) {
            rvjit_x86_mov(block, hrds, cmp_reg, bits_64);
        } else {
            rvjit_native_setreg32(block, hrds, 0x80000000U);
        }
    }
    branch_t l3 = rvjit_native_jmp(block, BRANCH_NEW, false); // goto exit

    // Overflow check pass
    rvjit_x86_branch(block, X86_BNE, cmp_reg, hrs2, l1, true, bits_64);
    rvjit_x86_branch_imm(block, X86_BNE, hrs2, -1, l2, true, bits_64);

    // Division by zero check
    branch_t l4 = rvjit_x86_branch_imm(block, X86_BNE, hrs2, 0, BRANCH_NEW, false, bits_64);

    // Division by zero fallthrough
    if (rem) {
        if (hrds != hrs1) rvjit_x86_mov(block, hrds, hrs1, bits_64);
    } else {
        rvjit_native_setreg32s(block, hrds, -1);
    }
    branch_t l5 = rvjit_native_jmp(block, BRANCH_NEW, false); // goto exit

    // Division by zero check pass
    rvjit_x86_branch_imm(block, X86_BNE, hrs2, 0, l4, true, bits_64);

    rvjit_x86_mulh_div_rem(block, X86_IDIV, rem, hrds, hrs1, hrs2, bits_64);

    // Exit label
    rvjit_native_jmp(block, l3, true);
    rvjit_native_jmp(block, l5, true);

    //if (bits_64) rvjit_native_pop(block, cmp_reg);
    if (bits_64) rvjit_free_hreg(block, cmp_reg);
}

/*
 * Linker routines
 */

static inline size_t rvjit_x86_cmp_bnez_mem(rvjit_block_t* block, regid_t addr, bool bits_64)
{
    uint8_t code[4] = { 0, 0x83, 0x38 | (addr & 0x7), 0, };
    if (bits_64) code[0] |= X64_REX_W;
    if (addr >= X64_R8) code[0] |= X64_REX_B;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 4 : 3);
    return code[0] ? 4 : 3;
}

// Emit jump instruction (may return false if offset cannot be encoded)
static inline bool rvjit_tail_jmp(rvjit_block_t* block, int32_t offset)
{
    uint8_t code[5] = { 0xE9, };
    write_uint32_le_m(code + 1, ((uint32_t)offset) - 5);
    rvjit_put_code(block, code, 5);
    return true;
}

// Emit patchable ret instruction
static inline void rvjit_patchable_ret(rvjit_block_t* block)
{
    uint8_t code[5] = { 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, };
    rvjit_put_code(block, code, 5);
}

// Jump if word pointed to by addr is nonzero (may emit nothing if the offset cannot be encoded)
// Used to check interrupts in block linkage
static inline void rvjit_tail_bnez(rvjit_block_t* block, regid_t addr, int32_t offset)
{
    size_t cmp_size = rvjit_x86_cmp_bnez_mem(block, addr, false);
    uint8_t code[6] = { 0x0F, 0x85, };
    write_uint32_le_m(code + 2, ((uint32_t)offset) - (6 + cmp_size));
    rvjit_put_code(block, code, 6);
}

// Patch instruction at addr into ret
static inline void rvjit_patch_ret(void* addr)
{
    *(uint8_t*)addr = 0xC3;
}

// Patch jump instruction at addr
static inline bool rvjit_patch_jmp(void* addr, int32_t offset)
{
    uint8_t* code = (uint8_t*)addr;
    code[0] = 0xE9;
    write_uint32_le_m(code + 1, ((uint32_t)offset) - 5);
    return true;
}

// Indirect jump by register
static inline void rvjit_jmp_reg(rvjit_block_t* block, regid_t reg)
{
    uint8_t code[3] = { 0, 0xFF, 0xE0 | (reg & 0x7), };
    if (reg >= X64_R8) code[0] |= X64_REX_B;
    rvjit_put_code(block, code + (code[0] ? 0 : 1), code[0] ? 3 : 2);
}

/*
 * For shorter block PC updates in RVVM.
 * Theoretically, this could be done by optimizing the IR into memrefs,
 * but that's too expensive & complicated for now
 */
static inline void rvjit_x86_memref_addi(rvjit_block_t* block, regid_t addr, int32_t offset, int32_t imm, bool bits_64)
{
    size_t inst_size = 3;
    uint8_t code[11] = {0x00, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    code[2] = addr & 0x7;
    if (bits_64) code[0] |= X64_REX_W;
    if (addr >= X64_R8) code[0] |= X64_REX_B;
    if (offset) {
        if (x86_is_byte_imm(offset)) {
            code[inst_size] = offset;
            code[2] |= 0x40;
            inst_size++;
        } else {
            write_uint32_le_m(code + inst_size, offset);
            code[2] |= 0x80;
            inst_size += 4;
        }
    }
    if (x86_is_byte_imm(imm)) {
        code[inst_size] = imm;
        code[1] |= 0x2;
        inst_size++;
    } else {
        write_uint32_le_m(code + inst_size, imm);
        inst_size += 4;
    }
    rvjit_put_code(block, code + (code[0] ? 0 : 1), inst_size - (code[0] ? 0 : 1));
}

/*
 * RV32
 */
static inline void rvjit32_native_neg(rvjit_block_t* block, regid_t hrds, regid_t hrs1)
{
    if (hrds != hrs1) rvjit_x86_mov(block, hrds, hrs1, false);
    rvjit_x86_neg(block, hrds, false);
}

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

static inline void rvjit32_native_mul(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_mul(block, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_mulh(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_mulh_div_rem(block, X86_IMUL, true, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_mulhu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_mulh_div_rem(block, X86_MUL, true, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_mulhsu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_mulhsu(block, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_div(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_div_rem(block, false, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_divu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_divu_remu(block, false, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_rem(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_div_rem(block, true, hrds, hrs1, hrs2, false);
}

static inline void rvjit32_native_remu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_divu_remu(block, true, hrds, hrs1, hrs2, false);
}

/*
 * RV64
 */
#ifdef RVJIT_NATIVE_64BIT
static inline void rvjit64_native_neg(rvjit_block_t* block, regid_t hrds, regid_t hrs1)
{
    if (hrds != hrs1) rvjit_x86_mov(block, hrds, hrs1, true);
    rvjit_x86_neg(block, hrds, true);
}

static inline void rvjit64_native_sextw(rvjit_block_t* block, regid_t hrds, regid_t hrs1)
{
    rvjit_x86_movsxd(block, hrds, hrs1);
}

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

static inline void rvjit64_native_mul(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_mul(block, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_mulh(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_mulh_div_rem(block, X86_IMUL, true, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_mulhu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_mulh_div_rem(block, X86_MUL, true, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_mulhsu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_mulhsu(block, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_div(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_div_rem(block, false, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_divu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_divu_remu(block, false, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_rem(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_div_rem(block, true, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_remu(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_divu_remu(block, true, hrds, hrs1, hrs2, true);
}

static inline void rvjit64_native_mulw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_mul(block, hrds, hrs1, hrs2, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

static inline void rvjit64_native_divw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_div_rem(block, false, hrds, hrs1, hrs2, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

static inline void rvjit64_native_divuw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_divu_remu(block, false, hrds, hrs1, hrs2, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

static inline void rvjit64_native_remw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_div_rem(block, true, hrds, hrs1, hrs2, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

static inline void rvjit64_native_remuw(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_x86_divu_remu(block, true, hrds, hrs1, hrs2, false);
    rvjit_x86_movsxd(block, hrds, hrds);
}

#endif

#ifdef RVJIT_NATIVE_FPU

#define SSE2_MOVAPSD  0x28
#define SSE2_UCOMISSD 0x2E
#define SSE2_COMISSD  0x2F
#define SSE2_ANDPS    0x54
#define SSE2_ANDNPS   0x55
#define SSE2_ORPS     0x56
#define SSE2_XORPS    0x57

static inline void rvjit_sse2_scalar_2reg_op(rvjit_block_t* block, uint8_t opcode, regid_t dst, regid_t src, bool fpu_d)
{
    if (fpu_d) rvjit_put_code(block, "\x66", 1); // SSE2 double-precision prefix
    rvjit_x86_0f_2reg_op(block, opcode, dst, src, false);
}

#define SSE2_PCMPEQB 0x74
#define SSE2_PAND    0xDB
#define SSE2_PANDN   0xDF
#define SSE2_POR     0xEB
#define SSE2_PXOR    0xEF

static inline void rvjit_sse2_simd_2reg_op(rvjit_block_t* block, uint8_t opcode, regid_t dst, regid_t src)
{
    rvjit_sse2_scalar_2reg_op(block, opcode, dst, src, true);
}

#define SSE2_MOV_XMM_GPR 0x6E
#define SSE2_MOV_GPR_XMM 0x7E

// Bitcast integer register to xmm register
static inline void rvjit_sse2_mov_xmm_gpr(rvjit_block_t* block, regid_t dst, regid_t src, bool bits_64)
{
    rvjit_put_code(block, "\x66", 1); // XMM prefix
    rvjit_x86_0f_2reg_op(block, SSE2_MOV_XMM_GPR, dst, src, bits_64);
}

// Bitcast xmm register to integer register
static inline void rvjit_sse2_mov_gpr_xmm(rvjit_block_t* block, regid_t dst, regid_t src, bool bits_64)
{
    rvjit_put_code(block, "\x66", 1); // XMM prefix
    rvjit_x86_0f_2reg_op(block, SSE2_MOV_GPR_XMM, dst, src, bits_64);
}

static inline void rvjit_sse2_movapsd(rvjit_block_t* block, regid_t dst, regid_t src, bool fpu_d)
{
    rvjit_sse2_scalar_2reg_op(block, SSE2_MOVAPSD, dst, src, fpu_d);
}

#define SSE2_FP_CVTSI2S 0x2A
#define SSE2_FP_CVTS2SI 0x2C

#define SSE2_FP_ADD     0x58
#define SSE2_FP_SUB     0x5C
#define SSE2_FP_MUL     0x59
#define SSE2_FP_DIV     0x5E
#define SSE2_FP_SQRT    0x51

static inline void rvjit_sse2_fp_2reg_op(rvjit_block_t* block, uint8_t opcode, regid_t dst, regid_t src, bool fpu_d)
{
    if (fpu_d) {
        rvjit_put_code(block, "\xF2", 1); // SSE2 Double-precision prefix
    } else {
        rvjit_put_code(block, "\xF3", 1); // SSE2 Single-precision prefix
    }
    rvjit_x86_0f_2reg_op(block, opcode, dst, src, false);
}

static inline void rvjit_sse2_fp_3reg_op(rvjit_block_t* block, uint8_t opcode, regid_t hrds, regid_t hrs1, regid_t hrs2, bool fpu_d)
{
    if (hrds == hrs1) {
        rvjit_sse2_fp_2reg_op(block, opcode, hrds, hrs2, fpu_d);
    } else if (hrds == hrs2) {
        if (opcode == SSE2_FP_ADD || opcode == SSE2_FP_MUL) {
            // Reversible operands
            rvjit_sse2_fp_2reg_op(block, opcode, hrds, hrs1, fpu_d);
        } else {
            // TODO Non-reversible operands, need regalloc
            rvvm_fatal("Unimplemented non-reversible SSE2 FP 3-reg operands");
        }
    } else {
        rvjit_sse2_movapsd(block, hrds, hrs1, fpu_d);
        rvjit_sse2_fp_2reg_op(block, opcode, hrds, hrs2, fpu_d);
    }
}

#define SSE2_FP_LOAD  0x10
#define SSE2_FP_STORE 0x11

static inline void rvjit_sse2_fp_loadstore(rvjit_block_t* block, uint8_t opcode, regid_t dst, regid_t addr, int32_t off, bool fpu_d)
{
    uint8_t code[3] = { 0xF3, 0x0f, opcode, };
    if (fpu_d) code[0] = 0xF2;
    rvjit_put_code(block, code, 3);
    rvjit_x86_memory_ref(block, dst, addr, off);
}

// FPU intrinsics

static inline void rvjit_native_fadd_s(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_sse2_fp_3reg_op(block, SSE2_FP_ADD, hrds, hrs1, hrs2, false);
}

static inline void rvjit_native_fsub_s(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_sse2_fp_3reg_op(block, SSE2_FP_SUB, hrds, hrs1, hrs2, false);
}

static inline void rvjit_native_fmul_s(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_sse2_fp_3reg_op(block, SSE2_FP_MUL, hrds, hrs1, hrs2, false);
}

static inline void rvjit_native_fdiv_s(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_sse2_fp_3reg_op(block, SSE2_FP_DIV, hrds, hrs1, hrs2, false);
}

static inline void rvjit_native_fsqrt_s(rvjit_block_t* block, regid_t hrds, regid_t hrs1)
{
    rvjit_sse2_fp_2reg_op(block, SSE2_FP_SQRT, hrds, hrs1, false);
}

// TODO fsgnj, fmin/fmax, fcvt, fcmp, fma

static inline void rvjit_native_fmv_w_x(rvjit_block_t* block, regid_t hrds, regid_t hrs1)
{
    rvjit_sse2_mov_xmm_gpr(block, hrds, hrs1, false);
}

static inline void rvjit_native_fmv_x_w(rvjit_block_t* block, regid_t hrds, regid_t hrs1)
{
    rvjit_sse2_mov_gpr_xmm(block, hrds, hrs1, false);
}



static inline void rvjit_native_fadd_d(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_sse2_fp_3reg_op(block, SSE2_FP_ADD, hrds, hrs1, hrs2, true);
}

static inline void rvjit_native_fsub_d(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_sse2_fp_3reg_op(block, SSE2_FP_SUB, hrds, hrs1, hrs2, true);
}

static inline void rvjit_native_fmul_d(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_sse2_fp_3reg_op(block, SSE2_FP_MUL, hrds, hrs1, hrs2, true);
}

static inline void rvjit_native_fdiv_d(rvjit_block_t* block, regid_t hrds, regid_t hrs1, regid_t hrs2)
{
    rvjit_sse2_fp_3reg_op(block, SSE2_FP_DIV, hrds, hrs1, hrs2, true);
}

static inline void rvjit_native_fsqrt_d(rvjit_block_t* block, regid_t hrds, regid_t hrs1)
{
    rvjit_sse2_fp_2reg_op(block, SSE2_FP_SQRT, hrds, hrs1, true);
}

// TODO fsgnj, fmin/fmax, fcvt, fcmp, fma

#ifdef RVJIT_NATIVE_64BIT

static inline void rvjit_native_fmv_d_x(rvjit_block_t* block, regid_t hrds, regid_t hrs1)
{
    rvjit_sse2_mov_xmm_gpr(block, hrds, hrs1, true);
}

static inline void rvjit_native_fmv_x_d(rvjit_block_t* block, regid_t hrds, regid_t hrs1)
{
    rvjit_sse2_mov_gpr_xmm(block, hrds, hrs1, true);
}

#endif

#endif

#endif
