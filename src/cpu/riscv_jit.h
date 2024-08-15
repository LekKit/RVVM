/*
riscv_jit.h - RVJIT Tracing integration
Copyright (C) 2024  LekKit <github.com/LekKit>

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

#ifndef RISCV_JIT_H
#define RISCV_JIT_H

#if defined(USE_JIT) && (!defined(RV64) || defined(RVJIT_NATIVE_64BIT))

#include "rvjit/rvjit_emit.h"

// Block unrolling configuration
#define UNROLL_MAX_BLOCK_SIZE 256

/*
 * Block lookup and linking internals
 */

NOINLINE bool riscv_jit_lookup(rvvm_hart_t* vm);

#ifndef RVJIT_NATIVE_LINKER

static inline bool riscv_jtlb_lookup(rvvm_hart_t* vm)
{
    // Try to find & execute a block
    virt_addr_t pc = vm->registers[REGISTER_PC];
    virt_addr_t entry = (pc >> 1) & (TLB_SIZE - 1);
    virt_addr_t tpc = vm->jtlb[entry].pc;
    if (likely(pc == tpc)) {
        vm->jtlb[entry].block(vm);
        return true;
    } else {
        return false;
    }
}

#endif

static inline bool riscv_jit_tlb_lookup(rvvm_hart_t* vm)
{
    if (unlikely(!vm->jit_enabled)) return false;

    virt_addr_t pc = vm->registers[REGISTER_PC];
    virt_addr_t entry = (pc >> 1) & (TLB_SIZE - 1);
    virt_addr_t tpc = vm->jtlb[entry].pc;
    if (likely(pc == tpc)) {
        vm->jtlb[entry].block(vm);
#ifndef RVJIT_NATIVE_LINKER
        // Try to execute more blocks if they aren't linked
        for (size_t i=0; i<10 && riscv_jtlb_lookup(vm); ++i);
#endif
        return true;
    } else {
        return riscv_jit_lookup(vm);
    }
}

/*
 * RVJIT tracing helpers
 */

// Wraps trace-compile-trace-execute
#define RVVM_RVJIT_TRACE(intrinsic, insn_size) \
do { \
    if (!vm->jit_compiling && riscv_jit_tlb_lookup(vm)) { \
        vm->registers[REGISTER_PC] -= insn_size; \
        return; \
    } \
    if (unlikely(vm->jit_compiling)) { \
        intrinsic; \
        vm->jit.pc_off += insn_size; \
        vm->block_ends = false; \
    } \
} while (0)

/*
 * Load/store instructions are not trivially traceable - they may trigger a TLB miss
 * exactly at the beggining of the block, thus failing to progress forward.
 * If the PC is unchanged after executing the block, load/store tracing is disabled
 * and instruction is interpreted, refilling the TLB.
 *
 * This may be also solved by resetting ldst_trace flag from JITed code upon TLB miss.
 */
#define RVVM_RVJIT_TRACE_LDST(intrinsic, insn_size) \
do { \
    virt_addr_t pc = vm->registers[REGISTER_PC]; \
    if (!vm->jit_compiling && vm->ldst_trace && riscv_jit_tlb_lookup(vm)) { \
        vm->ldst_trace = pc != vm->registers[REGISTER_PC]; \
        vm->registers[REGISTER_PC] -= insn_size; \
        return; \
    } \
    vm->ldst_trace = true; \
    if (unlikely(vm->jit_compiling)) { \
        intrinsic; \
        vm->jit.pc_off += insn_size; \
        vm->block_ends = false; \
    } \
} while (0)

// JAL instruction applies jump offset to pc_off
// We already check page cross in riscv_emulate()
#define RVVM_RVJIT_TRACE_JAL(intrinsic, offset, insn_size) \
do { \
    if (!vm->jit_compiling && riscv_jit_tlb_lookup(vm)) { \
        vm->registers[REGISTER_PC] -= insn_size; \
        return; \
    } \
    if (unlikely(vm->jit_compiling)) { \
        intrinsic; \
        vm->jit.pc_off += offset; \
        vm->block_ends = vm->jit.size > UNROLL_MAX_BLOCK_SIZE; \
    } \
} while (0)

// Blocks immediately ends upon indirect jump (thus no need to trace it)
#define RVVM_RVJIT_TRACE_JALR(intrinsic) \
do { \
    if (unlikely(vm->jit_compiling)) { \
        intrinsic; \
    } \
} while (0)

// Branches taken in interpreter are treated as likely branches and inlined
#define RVVM_RVJIT_TRACE_BRANCH(intrinsic, target_off, falthrough_off, insn_size) \
do { \
    if (!vm->jit_compiling && riscv_jit_tlb_lookup(vm)) { \
        vm->registers[REGISTER_PC] -= insn_size; \
        return; \
    } \
    if (unlikely(vm->jit_compiling)) { \
        vm->jit.pc_off += falthrough_off; \
        intrinsic; \
        vm->jit.pc_off += (target_off - falthrough_off); \
        vm->block_ends = vm->jit.size > UNROLL_MAX_BLOCK_SIZE; \
    } \
} while (0)

#else

#define RVVM_RVJIT_TRACE(intrinsic, insn_size)
#define RVVM_RVJIT_TRACE_LDST(intrinsic, insn_size)
#define RVVM_RVJIT_TRACE_JAL(intrinsic, imm, insn_size)
#define RVVM_RVJIT_TRACE_JALR(intrinsic)
#define RVVM_RVJIT_TRACE_BRANCH(intrinsic, target_off, fallthrough_off, insn_size)

#endif

/*
 * Bitmanip helpers
 */

#ifdef RVJIT_NATIVE_BITMANIP
#define RVVM_RVJIT_TRACE_BITMANIP(intrinsic, insn_size) RVVM_RVJIT_TRACE(intrinsic, insn_size)
#else
#define RVVM_RVJIT_TRACE_BITMANIP(intrinsic, insn_size)
#endif

/*
 * FPU tracing helpers
 */

#ifdef RVJIT_NATIVE_FPU
#define RVVM_RVJIT_TRACE_FPU(intrinsic, insn_size)      RVVM_RVJIT_TRACE(intrinsic, insn_size)
#define RVVM_RVJIT_TRACE_FPU_LDST(intrinsic, insn_size) RVVM_RVJIT_TRACE_LDST(intrinsic, insn_size)
#else
#define RVVM_RVJIT_TRACE_FPU(intrinsic, insn_size)
#define RVVM_RVJIT_TRACE_FPU_LDST(intrinsic, insn_size)
#endif

/*
 * RVJIT tracing intrinsics used by the interpreter
 */

#ifdef RV64

// RV64IC
#define rvjit_trace_add(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_add(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_sub(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_sub(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_or(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit64_or(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_and(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_and(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_xor(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_xor(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_sra(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_sra(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_srl(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_srl(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_sll(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_sll(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_addi(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_addi(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_ori(rds, rs1, imm, size)   RVVM_RVJIT_TRACE(rvjit64_ori(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_andi(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_andi(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_xori(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_xori(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_srai(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_srai(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_srli(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_srli(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_slli(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_slli(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_slti(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_slti(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_sltiu(rds, rs1, imm, size) RVVM_RVJIT_TRACE(rvjit64_sltiu(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_slt(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_slt(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_sltu(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_sltu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_li(rds, imm, size)         RVVM_RVJIT_TRACE(rvjit64_li(&vm->jit, rds, imm), size)
#define rvjit_trace_auipc(rds, imm, size)      RVVM_RVJIT_TRACE(rvjit64_auipc(&vm->jit, rds, imm), size)
#define rvjit_trace_jal(rds, imm, size)        RVVM_RVJIT_TRACE_JAL(rvjit64_auipc(&vm->jit, rds, size), imm, size)
#define rvjit_trace_jalr(rds, rs, imm, size)   RVVM_RVJIT_TRACE_JALR(rvjit64_jalr(&vm->jit, rds, rs, imm, size))

#define rvjit_trace_addw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_addw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_subw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_subw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_sraw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_sraw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_srlw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_srlw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_sllw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_sllw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_addiw(rds, rs1, imm, size) RVVM_RVJIT_TRACE(rvjit64_addiw(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_sraiw(rds, rs1, imm, size) RVVM_RVJIT_TRACE(rvjit64_sraiw(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_srliw(rds, rs1, imm, size) RVVM_RVJIT_TRACE(rvjit64_srliw(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_slliw(rds, rs1, imm, size) RVVM_RVJIT_TRACE(rvjit64_slliw(&vm->jit, rds, rs1, imm), size)

#define rvjit_trace_sb(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_sb(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_lb(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_lb(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_lbu(rds, rs1, off, size)   RVVM_RVJIT_TRACE_LDST(rvjit64_lbu(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_sh(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_sh(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_lh(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_lh(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_lhu(rds, rs1, off, size)   RVVM_RVJIT_TRACE_LDST(rvjit64_lhu(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_sw(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_sw(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_lw(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_lw(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_lwu(rds, rs1, off, size)   RVVM_RVJIT_TRACE_LDST(rvjit64_lwu(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_sd(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_sd(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_ld(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_ld(&vm->jit, rds, rs1, off), size)

#define rvjit_trace_beq(rs1, rs2, t, f, i)     RVVM_RVJIT_TRACE_BRANCH(rvjit64_beq(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_trace_bne(rs1, rs2, t, f, i)     RVVM_RVJIT_TRACE_BRANCH(rvjit64_bne(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_trace_blt(rs1, rs2, t, f, i)     RVVM_RVJIT_TRACE_BRANCH(rvjit64_blt(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_trace_bge(rs1, rs2, t, f, i)     RVVM_RVJIT_TRACE_BRANCH(rvjit64_bge(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_trace_bltu(rs1, rs2, t, f, i)    RVVM_RVJIT_TRACE_BRANCH(rvjit64_bltu(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_trace_bgeu(rs1, rs2, t, f, i)    RVVM_RVJIT_TRACE_BRANCH(rvjit64_bgeu(&vm->jit, rs1, rs2), t, f, i)

// RV64M
#define rvjit_trace_mul(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit64_mul(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_mulh(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_mulh(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_mulhu(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_mulhu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_mulhsu(rds, rs1, rs2, size) RVVM_RVJIT_TRACE(rvjit64_mulhsu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_mulw(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_mulw(&vm->jit, rds, rs1, rs2), size)

#define rvjit_trace_div(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit64_div(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_divu(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_divu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_rem(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit64_rem(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_remu(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_remu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_divw(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_divw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_divuw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_divuw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_remw(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_remw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_remuw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_remuw(&vm->jit, rds, rs1, rs2), size)

// RV64 Zba
#define rvjit_trace_shadd(rds, rs1, rs2, shift, size)    RVVM_RVJIT_TRACE_BITMANIP(rvjit64_shadd(&vm->jit, rds, rs1, rs2, shift), size)
#define rvjit_trace_shadd_uw(rds, rs1, rs2, shift, size) RVVM_RVJIT_TRACE_BITMANIP(rvjit64_shadd_uw(&vm->jit, rds, rs1, rs2, shift), size)
#define rvjit_trace_slli_uw(rds, rs1, imm, size)         RVVM_RVJIT_TRACE_BITMANIP(rvjit64_slli_uw(&vm->jit, rds, rs1, imm), size)

// RV64 Zbb
#define rvjit_trace_rol(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE_BITMANIP(rvjit64_rol(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_rolw(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit64_rolw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_ror(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE_BITMANIP(rvjit64_ror(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_rorw(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit64_rorw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_rori(rds, rs1, imm, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit64_rori(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_roriw(rds, rs1, imm, size)  RVVM_RVJIT_TRACE_BITMANIP(rvjit64_roriw(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_andn(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit64_andn(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_orn(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE_BITMANIP(rvjit64_orn(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_xnor(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit64_xnor(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_max(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE_BITMANIP(rvjit64_max(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_maxu(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit64_maxu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_min(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE_BITMANIP(rvjit64_min(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_minu(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit64_minu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_sext_b(rds, rs1, size)      RVVM_RVJIT_TRACE_BITMANIP(rvjit64_sext_b(&vm->jit, rds, rs1), size)
#define rvjit_trace_sext_h(rds, rs1, size)      RVVM_RVJIT_TRACE_BITMANIP(rvjit64_sext_h(&vm->jit, rds, rs1), size)

// RV64 Zbs
#define rvjit_trace_bext(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit64_bext(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_bexti(rds, rs1, imm, size)  RVVM_RVJIT_TRACE_BITMANIP(rvjit64_bexti(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_bclr(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit64_bclr(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_bclri(rds, rs1, imm, size)  RVVM_RVJIT_TRACE_BITMANIP(rvjit64_bclri(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_bset(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit64_bset(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_bseti(rds, rs1, imm, size)  RVVM_RVJIT_TRACE_BITMANIP(rvjit64_bseti(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_binv(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit64_binv(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_binvi(rds, rs1, imm, size)  RVVM_RVJIT_TRACE_BITMANIP(rvjit64_binvi(&vm->jit, rds, rs1, imm), size)

#else

// RV32IC
#define rvjit_trace_add(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_add(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_sub(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_sub(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_or(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit32_or(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_and(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_and(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_xor(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_xor(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_sra(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_sra(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_srl(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_srl(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_sll(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_sll(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_addi(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_addi(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_ori(rds, rs1, imm, size)   RVVM_RVJIT_TRACE(rvjit32_ori(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_andi(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_andi(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_xori(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_xori(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_srai(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_srai(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_srli(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_srli(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_slli(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_slli(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_slti(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_slti(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_sltiu(rds, rs1, imm, size) RVVM_RVJIT_TRACE(rvjit32_sltiu(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_slt(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_slt(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_sltu(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit32_sltu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_li(rds, imm, size)         RVVM_RVJIT_TRACE(rvjit32_li(&vm->jit, rds, imm), size)
#define rvjit_trace_auipc(rds, imm, size)      RVVM_RVJIT_TRACE(rvjit32_auipc(&vm->jit, rds, imm), size)
#define rvjit_trace_jal(rds, imm, size)        RVVM_RVJIT_TRACE_JAL(rvjit32_auipc(&vm->jit, rds, size), imm, size)
#define rvjit_trace_jalr(rds, rs, imm, size)   RVVM_RVJIT_TRACE_JALR(rvjit32_jalr(&vm->jit, rds, rs, imm, size))

#define rvjit_trace_sb(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit32_sb(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_lb(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit32_lb(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_lbu(rds, rs1, off, size)   RVVM_RVJIT_TRACE_LDST(rvjit32_lbu(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_sh(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit32_sh(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_lh(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit32_lh(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_lhu(rds, rs1, off, size)   RVVM_RVJIT_TRACE_LDST(rvjit32_lhu(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_sw(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit32_sw(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_lw(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit32_lw(&vm->jit, rds, rs1, off), size)

#define rvjit_trace_beq(rs1, rs2, t, f, i)     RVVM_RVJIT_TRACE_BRANCH(rvjit32_beq(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_trace_bne(rs1, rs2, t, f, i)     RVVM_RVJIT_TRACE_BRANCH(rvjit32_bne(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_trace_blt(rs1, rs2, t, f, i)     RVVM_RVJIT_TRACE_BRANCH(rvjit32_blt(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_trace_bge(rs1, rs2, t, f, i)     RVVM_RVJIT_TRACE_BRANCH(rvjit32_bge(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_trace_bltu(rs1, rs2, t, f, i)    RVVM_RVJIT_TRACE_BRANCH(rvjit32_bltu(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_trace_bgeu(rs1, rs2, t, f, i)    RVVM_RVJIT_TRACE_BRANCH(rvjit32_bgeu(&vm->jit, rs1, rs2), t, f, i)

// RV32M
#define rvjit_trace_mul(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit32_mul(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_mulh(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_mulh(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_mulhu(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit32_mulhu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_mulhsu(rds, rs1, rs2, size) RVVM_RVJIT_TRACE(rvjit32_mulhsu(&vm->jit, rds, rs1, rs2), size)

#define rvjit_trace_div(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit32_div(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_divu(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_divu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_rem(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit32_rem(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_remu(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_remu(&vm->jit, rds, rs1, rs2), size)

// RV32 Zba
#define rvjit_trace_shadd(rds, rs1, rs2, shift, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit32_shadd(&vm->jit, rds, rs1, rs2, shift), size)

// RV32 Zbb
#define rvjit_trace_rol(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE_BITMANIP(rvjit32_rol(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_ror(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE_BITMANIP(rvjit32_ror(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_rori(rds, rs1, imm, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit32_rori(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_andn(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit32_andn(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_orn(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE_BITMANIP(rvjit32_orn(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_xnor(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit32_xnor(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_max(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE_BITMANIP(rvjit32_max(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_maxu(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit32_maxu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_min(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE_BITMANIP(rvjit32_min(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_minu(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit32_minu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_sext_b(rds, rs1, size)      RVVM_RVJIT_TRACE_BITMANIP(rvjit32_sext_b(&vm->jit, rds, rs1), size)
#define rvjit_trace_sext_h(rds, rs1, size)      RVVM_RVJIT_TRACE_BITMANIP(rvjit32_sext_h(&vm->jit, rds, rs1), size)

// RV32 Zbs
#define rvjit_trace_bext(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit32_bext(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_bexti(rds, rs1, imm, size)  RVVM_RVJIT_TRACE_BITMANIP(rvjit32_bexti(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_bclr(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit32_bclr(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_bclri(rds, rs1, imm, size)  RVVM_RVJIT_TRACE_BITMANIP(rvjit32_bclri(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_bset(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit32_bset(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_bseti(rds, rs1, imm, size)  RVVM_RVJIT_TRACE_BITMANIP(rvjit32_bseti(&vm->jit, rds, rs1, imm), size)
#define rvjit_trace_binv(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE_BITMANIP(rvjit32_binv(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_binvi(rds, rs1, imm, size)  RVVM_RVJIT_TRACE_BITMANIP(rvjit32_binvi(&vm->jit, rds, rs1, imm), size)

#endif

// RISC-V FPU
#define rvjit_trace_fsw(rds, rs1, off, size) RVVM_RVJIT_TRACE_FPU_LDST(rvjit_fsw(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_flw(rds, rs1, off, size) RVVM_RVJIT_TRACE_FPU_LDST(rvjit_flw(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_fsd(rds, rs1, off, size) RVVM_RVJIT_TRACE_FPU_LDST(rvjit_fsd(&vm->jit, rds, rs1, off), size)
#define rvjit_trace_fld(rds, rs1, off, size) RVVM_RVJIT_TRACE_FPU_LDST(rvjit_fld(&vm->jit, rds, rs1, off), size)

#define rvjit_trace_fadd_s(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fadd_s(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fadd_d(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fadd_d(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fsub_s(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fsub_s(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fsub_d(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fsub_d(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fmul_s(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fmul_s(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fmul_d(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fmul_d(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fdiv_s(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fdiv_s(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fdiv_d(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fdiv_d(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fsqrt_s(rds, rs1, size)      RVVM_RVJIT_TRACE_FPU(rvjit_fsqrt_s(&vm->jit, rds, rs1), size)
#define rvjit_trace_fsqrt_d(rds, rs1, size)      RVVM_RVJIT_TRACE_FPU(rvjit_fsqrt_d(&vm->jit, rds, rs1), size)

#define rvjit_trace_fmadd_s(rds, rs1, rs2, rs3, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fmadd_s(&vm->jit, rds, rs1, rs2, rs3), size)
#define rvjit_trace_fmadd_d(rds, rs1, rs2, rs3, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fmadd_d(&vm->jit, rds, rs1, rs2, rs3), size)
#define rvjit_trace_fmsub_s(rds, rs1, rs2, rs3, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fmsub_s(&vm->jit, rds, rs1, rs2, rs3), size)
#define rvjit_trace_fmsub_d(rds, rs1, rs2, rs3, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fmsub_d(&vm->jit, rds, rs1, rs2, rs3), size)
#define rvjit_trace_fnmadd_s(rds, rs1, rs2, rs3, size) RVVM_RVJIT_TRACE_FPU(rvjit_fnmadd_s(&vm->jit, rds, rs1, rs2, rs3), size)
#define rvjit_trace_fnmadd_d(rds, rs1, rs2, rs3, size) RVVM_RVJIT_TRACE_FPU(rvjit_fnmadd_d(&vm->jit, rds, rs1, rs2, rs3), size)
#define rvjit_trace_fnmsub_s(rds, rs1, rs2, rs3, size) RVVM_RVJIT_TRACE_FPU(rvjit_fnmsub_s(&vm->jit, rds, rs1, rs2, rs3), size)
#define rvjit_trace_fnmsub_d(rds, rs1, rs2, rs3, size) RVVM_RVJIT_TRACE_FPU(rvjit_fnmsub_d(&vm->jit, rds, rs1, rs2, rs3), size)

#define rvjit_trace_fsgnj_s(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fsgnj_s(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fsgnj_d(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fsgnj_d(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fsgnjn_s(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_fsgnjn_s(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fsgnjn_d(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_fsgnjn_d(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fsgnjx_s(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_fsgnjx_s(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fsgnjx_d(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_fsgnjx_d(&vm->jit, rds, rs1, rs2), size)

#define rvjit_trace_fmin_s(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_fmin_s(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fmin_d(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_fmin_d(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fmax_s(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_fmax_s(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fmax_d(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_fmax_d(&vm->jit, rds, rs1, rs2), size)

#define rvjit_trace_feq_s(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_feq_s(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_feq_d(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_feq_d(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_flt_s(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_flt_s(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_flt_d(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_flt_d(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fle_s(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_fle_s(&vm->jit, rds, rs1, rs2), size)
#define rvjit_trace_fle_d(rds, rs1, rs2, size) RVVM_RVJIT_TRACE_FPU(rvjit_fle_d(&vm->jit, rds, rs1, rs2), size)

#define rvjit_trace_fclass_s(rds, rs1, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fclass_s(&vm->jit, rds, rs1), size)
#define rvjit_trace_fclass_d(rds, rs1, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fclass_d(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_s_d(rds, rs1, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_s_d(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_d_s(rds, rs1, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_d_s(&vm->jit, rds, rs1), size)
#define rvjit_trace_fmv_w_x(rds, rs1, size)   RVVM_RVJIT_TRACE_FPU(rvjit_fmv_w_x(&vm->jit, rds, rs1), size)
#define rvjit_trace_fmv_x_w(rds, rs1, size)   RVVM_RVJIT_TRACE_FPU(rvjit_fmv_x_w(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_w_s(rds, rs1, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_w_s(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_wu_s(rds, rs1, size) RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_wu_s(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_s_w(rds, rs1, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_s_w(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_s_wu(rds, rs1, size) RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_s_wu(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_w_d(rds, rs1, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_w_d(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_wu_d(rds, rs1, size) RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_wu_d(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_d_w(rds, rs1, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_d_w(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_d_wu(rds, rs1, size) RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_d_wu(&vm->jit, rds, rs1), size)

#ifdef RV64

// RV64-specific FPU
#define rvjit_trace_fmv_d_x(rds, rs1, size)   RVVM_RVJIT_TRACE_FPU(rvjit_fmv_d_x(&vm->jit, rds, rs1), size)
#define rvjit_trace_fmv_x_d(rds, rs1, size)   RVVM_RVJIT_TRACE_FPU(rvjit_fmv_x_d(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_l_s(rds, rs1, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_l_s(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_lu_s(rds, rs1, size) RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_lu_s(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_s_l(rds, rs1, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_s_l(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_s_lu(rds, rs1, size) RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_s_lu(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_l_d(rds, rs1, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_l_d(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_lu_d(rds, rs1, size) RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_lu_d(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_d_l(rds, rs1, size)  RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_d_l(&vm->jit, rds, rs1), size)
#define rvjit_trace_fcvt_d_lu(rds, rs1, size) RVVM_RVJIT_TRACE_FPU(rvjit_fcvt_d_lu(&vm->jit, rds, rs1), size)

#endif

#endif
