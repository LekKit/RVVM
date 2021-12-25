/*
riscv_cpu.h - RISC-V CPU Definitions
Copyright (C) 2021  LekKit <github.com/LekKit>
                    cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#ifndef RISCV_CPU_H
#define RISCV_CPU_H

#include "rvvm.h"
#include "riscv_hart.h"
#include "riscv_csr.h"
#include "mem_ops.h"
#include "assert.h"

void riscv_install_opcode_R(rvvm_hart_t* vm, uint32_t opcode, riscv_inst_t func);
void riscv_install_opcode_UJ(rvvm_hart_t* vm, uint32_t opcode, riscv_inst_t func);
void riscv_install_opcode_ISB(rvvm_hart_t* vm, uint32_t opcode, riscv_inst_t func);
void riscv_install_opcode_C(rvvm_hart_t* vm, uint32_t opcode, riscv_inst_c_t func);

void riscv_decoder_init_rv64(rvvm_hart_t* vm);
void riscv_decoder_init_rv32(rvvm_hart_t* vm);
void riscv_decoder_enable_fpu(rvvm_hart_t* vm, bool enable);
void riscv_run_till_event(rvvm_hart_t* vm);

void riscv32i_init(rvvm_hart_t* vm);
void riscv32c_init(rvvm_hart_t* vm);
void riscv32m_init(rvvm_hart_t* vm);
void riscv32a_init(rvvm_hart_t* vm);

void riscv64i_init(rvvm_hart_t* vm);
void riscv64c_init(rvvm_hart_t* vm);
void riscv64m_init(rvvm_hart_t* vm);
void riscv64a_init(rvvm_hart_t* vm);

void riscv32f_enable(rvvm_hart_t* vm, bool enable);
void riscv32d_enable(rvvm_hart_t* vm, bool enable);

void riscv64f_enable(rvvm_hart_t* vm, bool enable);
void riscv64d_enable(rvvm_hart_t* vm, bool enable);

void riscv_illegal_insn(rvvm_hart_t* vm, const uint32_t instruction);
void riscv_c_illegal_insn(rvvm_hart_t* vm, const uint16_t instruction);

static inline void riscv_jit_discard(rvvm_hart_t* vm)
{
#ifdef USE_JIT
    vm->jit_compiling = false;
#else
    UNUSED(vm);
#endif
}

static inline void riscv_jit_compile(rvvm_hart_t* vm)
{
#ifdef USE_JIT
    vm->block_ends = true;
#else
    UNUSED(vm);
#endif
}

// Private CPU implementation definitions
#ifdef RISCV_CPU_SOURCE

#ifdef USE_JIT
#include "rvjit/rvjit_emit.h"

NOINLINE bool riscv_jit_lookup(rvvm_hart_t* vm);

static inline bool riscv_jit_tlb_lookup(rvvm_hart_t* vm)
{
    vaddr_t pc, tpc, entry;
    size_t tries = 0;

    if (unlikely(!vm->jit_enabled)) return false;

    // Try to find & execute a block
    trace:
    pc = vm->registers[REGISTER_PC];
    entry = (pc >> 1) & (TLB_SIZE - 1);
    tpc = vm->jtlb[entry].pc;
    if (likely(pc == tpc)) {
        vm->jtlb[entry].block(vm);
        if (likely(tries++ < 10)) goto trace;
        return true;
    } else if (tries == 0) {
        return riscv_jit_lookup(vm);
    } else return true;
}

// Block unrolling configuration
#define BRANCH_MAX_BLOCK_SIZE 4096

// Wraps trace-compile-trace-execute
#define RVVM_RVJIT_TRACE(intrinsic, inst_size) \
do { \
    if (!vm->jit_compiling && riscv_jit_tlb_lookup(vm)) { \
        vm->registers[REGISTER_PC] -= inst_size; \
        return; \
    } \
    if (vm->jit_compiling) { \
        intrinsic; \
        vm->jit.pc_off += inst_size; \
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
#define RVVM_RVJIT_TRACE_LDST(intrinsic, inst_size) \
do { \
    vaddr_t pc = vm->registers[REGISTER_PC]; \
    if (!vm->jit_compiling && vm->ldst_trace && riscv_jit_tlb_lookup(vm)) { \
        vm->ldst_trace = pc != vm->registers[REGISTER_PC]; \
        vm->registers[REGISTER_PC] -= inst_size; \
        return; \
    } \
    vm->ldst_trace = true; \
    if (vm->jit_compiling) { \
        intrinsic; \
        vm->jit.pc_off += inst_size; \
        vm->block_ends = false; \
    } \
} while (0)

// JAL instruction applies jump offset to pc_off
// We already check page cross in riscv_emulate()
#define RVVM_RVJIT_TRACE_JAL(intrinsic, offset, inst_size) \
do { \
    if (!vm->jit_compiling && riscv_jit_tlb_lookup(vm)) { \
        vm->registers[REGISTER_PC] -= inst_size; \
        return; \
    } \
    if (vm->jit_compiling) { \
        intrinsic; \
        vm->jit.pc_off += offset; \
        vm->block_ends = vm->jit.size > BRANCH_MAX_BLOCK_SIZE; \
    } \
} while (0)

// Blocks immediately ends upon indirect jump (thus no need to trace it)
#define RVVM_RVJIT_COMPILE_JALR(intrinsic) \
do { \
    if (vm->jit_compiling) { \
        intrinsic; \
    } \
} while (0)

// Branches taken in interpreter are treated as likely branches and inlined
#define RVVM_RVJIT_BRANCH(intrinsic, target_off, falthrough_off, inst_size) \
do { \
    if (!vm->jit_compiling && riscv_jit_tlb_lookup(vm)) { \
        vm->registers[REGISTER_PC] -= inst_size; \
        return; \
    } \
    if (vm->jit_compiling) { \
        vm->jit.pc_off += falthrough_off; \
        intrinsic; \
        vm->jit.pc_off += (target_off - falthrough_off); \
        vm->block_ends = vm->jit.size > BRANCH_MAX_BLOCK_SIZE; \
    } \
} while (0)

#if defined(RV64) && defined(RVJIT_NATIVE_64BIT)

#define rvjit_add(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_add(&vm->jit, rds, rs1, rs2), size)
#define rvjit_sub(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_sub(&vm->jit, rds, rs1, rs2), size)
#define rvjit_or(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit64_or(&vm->jit, rds, rs1, rs2), size)
#define rvjit_and(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_and(&vm->jit, rds, rs1, rs2), size)
#define rvjit_xor(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_xor(&vm->jit, rds, rs1, rs2), size)
#define rvjit_sra(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_sra(&vm->jit, rds, rs1, rs2), size)
#define rvjit_srl(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_srl(&vm->jit, rds, rs1, rs2), size)
#define rvjit_sll(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_sll(&vm->jit, rds, rs1, rs2), size)
#define rvjit_addi(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_addi(&vm->jit, rds, rs1, imm), size)
#define rvjit_ori(rds, rs1, imm, size)   RVVM_RVJIT_TRACE(rvjit64_ori(&vm->jit, rds, rs1, imm), size)
#define rvjit_andi(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_andi(&vm->jit, rds, rs1, imm), size)
#define rvjit_xori(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_xori(&vm->jit, rds, rs1, imm), size)
#define rvjit_srai(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_srai(&vm->jit, rds, rs1, imm), size)
#define rvjit_srli(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_srli(&vm->jit, rds, rs1, imm), size)
#define rvjit_slli(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_slli(&vm->jit, rds, rs1, imm), size)
#define rvjit_slti(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit64_slti(&vm->jit, rds, rs1, imm), size)
#define rvjit_sltiu(rds, rs1, imm, size) RVVM_RVJIT_TRACE(rvjit64_sltiu(&vm->jit, rds, rs1, imm), size)
#define rvjit_slt(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_slt(&vm->jit, rds, rs1, rs2), size)
#define rvjit_sltu(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_sltu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_li(rds, imm, size)         RVVM_RVJIT_TRACE(rvjit64_li(&vm->jit, rds, imm), size)
#define rvjit_auipc(rds, imm, size)      RVVM_RVJIT_TRACE(rvjit64_auipc(&vm->jit, rds, imm), size)
#define rvjit_jal(rds, imm, size)        RVVM_RVJIT_TRACE_JAL(rvjit64_auipc(&vm->jit, rds, size), imm, size)
#define rvjit_jalr(rds, rs, imm, size)   RVVM_RVJIT_COMPILE_JALR(rvjit64_jalr(&vm->jit, rds, rs, imm, size))

#define rvjit_addw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_addw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_subw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_subw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_sraw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_sraw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_srlw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_srlw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_sllw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_sllw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_addiw(rds, rs1, imm, size) RVVM_RVJIT_TRACE(rvjit64_addiw(&vm->jit, rds, rs1, imm), size)
#define rvjit_sraiw(rds, rs1, imm, size) RVVM_RVJIT_TRACE(rvjit64_sraiw(&vm->jit, rds, rs1, imm), size)
#define rvjit_srliw(rds, rs1, imm, size) RVVM_RVJIT_TRACE(rvjit64_srliw(&vm->jit, rds, rs1, imm), size)
#define rvjit_slliw(rds, rs1, imm, size) RVVM_RVJIT_TRACE(rvjit64_slliw(&vm->jit, rds, rs1, imm), size)

#define rvjit_sb(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_sb(&vm->jit, rds, rs1, off), size)
#define rvjit_lb(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_lb(&vm->jit, rds, rs1, off), size)
#define rvjit_lbu(rds, rs1, off, size)   RVVM_RVJIT_TRACE_LDST(rvjit64_lbu(&vm->jit, rds, rs1, off), size)
#define rvjit_sh(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_sh(&vm->jit, rds, rs1, off), size)
#define rvjit_lh(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_lh(&vm->jit, rds, rs1, off), size)
#define rvjit_lhu(rds, rs1, off, size)   RVVM_RVJIT_TRACE_LDST(rvjit64_lhu(&vm->jit, rds, rs1, off), size)
#define rvjit_sw(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_sw(&vm->jit, rds, rs1, off), size)
#define rvjit_lw(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_lw(&vm->jit, rds, rs1, off), size)
#define rvjit_lwu(rds, rs1, off, size)   RVVM_RVJIT_TRACE_LDST(rvjit64_lwu(&vm->jit, rds, rs1, off), size)
#define rvjit_sd(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_sd(&vm->jit, rds, rs1, off), size)
#define rvjit_ld(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit64_ld(&vm->jit, rds, rs1, off), size)

#define rvjit_beq(rs1, rs2, t, f, i)     RVVM_RVJIT_BRANCH(rvjit64_beq(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_bne(rs1, rs2, t, f, i)     RVVM_RVJIT_BRANCH(rvjit64_bne(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_blt(rs1, rs2, t, f, i)     RVVM_RVJIT_BRANCH(rvjit64_blt(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_bge(rs1, rs2, t, f, i)     RVVM_RVJIT_BRANCH(rvjit64_bge(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_bltu(rs1, rs2, t, f, i)    RVVM_RVJIT_BRANCH(rvjit64_bltu(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_bgeu(rs1, rs2, t, f, i)    RVVM_RVJIT_BRANCH(rvjit64_bgeu(&vm->jit, rs1, rs2), t, f, i)

#elif !defined(RV64)

#define rvjit_add(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_add(&vm->jit, rds, rs1, rs2), size)
#define rvjit_sub(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_sub(&vm->jit, rds, rs1, rs2), size)
#define rvjit_or(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit32_or(&vm->jit, rds, rs1, rs2), size)
#define rvjit_and(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_and(&vm->jit, rds, rs1, rs2), size)
#define rvjit_xor(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_xor(&vm->jit, rds, rs1, rs2), size)
#define rvjit_sra(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_sra(&vm->jit, rds, rs1, rs2), size)
#define rvjit_srl(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_srl(&vm->jit, rds, rs1, rs2), size)
#define rvjit_sll(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_sll(&vm->jit, rds, rs1, rs2), size)
#define rvjit_addi(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_addi(&vm->jit, rds, rs1, imm), size)
#define rvjit_ori(rds, rs1, imm, size)   RVVM_RVJIT_TRACE(rvjit32_ori(&vm->jit, rds, rs1, imm), size)
#define rvjit_andi(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_andi(&vm->jit, rds, rs1, imm), size)
#define rvjit_xori(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_xori(&vm->jit, rds, rs1, imm), size)
#define rvjit_srai(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_srai(&vm->jit, rds, rs1, imm), size)
#define rvjit_srli(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_srli(&vm->jit, rds, rs1, imm), size)
#define rvjit_slli(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_slli(&vm->jit, rds, rs1, imm), size)
#define rvjit_slti(rds, rs1, imm, size)  RVVM_RVJIT_TRACE(rvjit32_slti(&vm->jit, rds, rs1, imm), size)
#define rvjit_sltiu(rds, rs1, imm, size) RVVM_RVJIT_TRACE(rvjit32_sltiu(&vm->jit, rds, rs1, imm), size)
#define rvjit_slt(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_slt(&vm->jit, rds, rs1, rs2), size)
#define rvjit_sltu(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit32_sltu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_li(rds, imm, size)         RVVM_RVJIT_TRACE(rvjit32_li(&vm->jit, rds, imm), size)
#define rvjit_auipc(rds, imm, size)      RVVM_RVJIT_TRACE(rvjit32_auipc(&vm->jit, rds, imm), size)
#define rvjit_jal(rds, imm, size)        RVVM_RVJIT_TRACE_JAL(rvjit32_auipc(&vm->jit, rds, size), imm, size)
#define rvjit_jalr(rds, rs, imm, size)   RVVM_RVJIT_COMPILE_JALR(rvjit32_jalr(&vm->jit, rds, rs, imm, size))

#define rvjit_sb(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit32_sb(&vm->jit, rds, rs1, off), size)
#define rvjit_lb(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit32_lb(&vm->jit, rds, rs1, off), size)
#define rvjit_lbu(rds, rs1, off, size)   RVVM_RVJIT_TRACE_LDST(rvjit32_lbu(&vm->jit, rds, rs1, off), size)
#define rvjit_sh(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit32_sh(&vm->jit, rds, rs1, off), size)
#define rvjit_lh(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit32_lh(&vm->jit, rds, rs1, off), size)
#define rvjit_lhu(rds, rs1, off, size)   RVVM_RVJIT_TRACE_LDST(rvjit32_lhu(&vm->jit, rds, rs1, off), size)
#define rvjit_sw(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit32_sw(&vm->jit, rds, rs1, off), size)
#define rvjit_lw(rds, rs1, off, size)    RVVM_RVJIT_TRACE_LDST(rvjit32_lw(&vm->jit, rds, rs1, off), size)

#define rvjit_beq(rs1, rs2, t, f, i)     RVVM_RVJIT_BRANCH(rvjit32_beq(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_bne(rs1, rs2, t, f, i)     RVVM_RVJIT_BRANCH(rvjit32_bne(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_blt(rs1, rs2, t, f, i)     RVVM_RVJIT_BRANCH(rvjit32_blt(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_bge(rs1, rs2, t, f, i)     RVVM_RVJIT_BRANCH(rvjit32_bge(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_bltu(rs1, rs2, t, f, i)    RVVM_RVJIT_BRANCH(rvjit32_bltu(&vm->jit, rs1, rs2), t, f, i)
#define rvjit_bgeu(rs1, rs2, t, f, i)    RVVM_RVJIT_BRANCH(rvjit32_bgeu(&vm->jit, rs1, rs2), t, f, i)

#endif

#endif

#if !defined(USE_JIT) || defined(RV64) && !defined(RVJIT_NATIVE_64BIT)

#define rvjit_add(rds, rs1, rs2, size)
#define rvjit_sub(rds, rs1, rs2, size)
#define rvjit_or(rds, rs1, rs2, size)
#define rvjit_and(rds, rs1, rs2, size)
#define rvjit_xor(rds, rs1, rs2, size)
#define rvjit_sra(rds, rs1, rs2, size)
#define rvjit_srl(rds, rs1, rs2, size)
#define rvjit_sll(rds, rs1, rs2, size)
#define rvjit_addi(rds, rs1, imm, size)
#define rvjit_ori(rds, rs1, imm, size)
#define rvjit_andi(rds, rs1, imm, size)
#define rvjit_xori(rds, rs1, imm, size)
#define rvjit_srai(rds, rs1, imm, size)
#define rvjit_srli(rds, rs1, imm, size)
#define rvjit_slli(rds, rs1, imm, size)
#define rvjit_slti(rds, rs1, imm, size)
#define rvjit_sltiu(rds, rs1, imm, size)
#define rvjit_slt(rds, rs1, rs2, size)
#define rvjit_sltu(rds, rs1, rs2, size)
#define rvjit_li(rds, imm, size)
#define rvjit_auipc(rds, imm, size)
#define rvjit_jal(rds, imm, size)
#define rvjit_jalr(rds, rs, imm, size)

#define rvjit_addw(rds, rs1, rs2, size)
#define rvjit_subw(rds, rs1, rs2, size)
#define rvjit_sraw(rds, rs1, rs2, size)
#define rvjit_srlw(rds, rs1, rs2, size)
#define rvjit_sllw(rds, rs1, rs2, size)
#define rvjit_addiw(rds, rs1, imm, size)
#define rvjit_sraiw(rds, rs1, imm, size)
#define rvjit_srliw(rds, rs1, imm, size)
#define rvjit_slliw(rds, rs1, imm, size)

#define rvjit_sb(rds, rs1, off, size)
#define rvjit_lb(rds, rs1, off, size)
#define rvjit_lbu(rds, rs1, off, size)
#define rvjit_sh(rds, rs1, off, size)
#define rvjit_lh(rds, rs1, off, size)
#define rvjit_lhu(rds, rs1, off, size)
#define rvjit_sw(rds, rs1, off, size)
#define rvjit_lw(rds, rs1, off, size)
#define rvjit_lwu(rds, rs1, off, size)
#define rvjit_sd(rds, rs1, off, size)
#define rvjit_ld(rds, rs1, off, size)

#define rvjit_beq(rs1, rs2, t, f, i)
#define rvjit_bne(rs1, rs2, t, f, i)
#define rvjit_beq(rs1, rs2, t, f, i)
#define rvjit_bne(rs1, rs2, t, f, i)
#define rvjit_blt(rs1, rs2, t, f, i)
#define rvjit_bge(rs1, rs2, t, f, i)
#define rvjit_bltu(rs1, rs2, t, f, i)
#define rvjit_bgeu(rs1, rs2, t, f, i)

#endif

#ifdef RV64
    typedef uint64_t xlen_t;
    typedef int64_t sxlen_t;
    typedef uint64_t xaddr_t;
    #define SHAMT_BITS 6
    #define DIV_OVERFLOW_RS1 ((sxlen_t)0x8000000000000000ULL)
    #define riscv_i_init riscv64i_init
    #define riscv_c_init riscv64c_init
    #define riscv_m_init riscv64m_init
    #define riscv_a_init riscv64a_init
    #define riscv_f_enable riscv64f_enable
    #define riscv_d_enable riscv64d_enable
#else
    typedef uint32_t xlen_t;
    typedef int32_t sxlen_t;
    typedef uint32_t xaddr_t;
    #define SHAMT_BITS 5
    #define DIV_OVERFLOW_RS1 ((sxlen_t)0x80000000U)
    #define riscv_i_init riscv32i_init
    #define riscv_c_init riscv32c_init
    #define riscv_m_init riscv32m_init
    #define riscv_a_init riscv32a_init
    #define riscv_f_enable riscv32f_enable
    #define riscv_d_enable riscv32d_enable
#endif

static inline xlen_t riscv_read_register(rvvm_hart_t *vm, regid_t reg)
{
    return vm->registers[reg];
}

static inline sxlen_t riscv_read_register_s(rvvm_hart_t *vm, regid_t reg)
{
    return vm->registers[reg];
}

static inline void riscv_write_register(rvvm_hart_t *vm, regid_t reg, xlen_t data)
{
    vm->registers[reg] = data;
}

#ifdef USE_FPU
static inline float fpu_read_register32(rvvm_hart_t *vm, regid_t reg)
{
    assert(reg < FPU_REGISTERS_MAX);
    return read_float_nanbox(&vm->fpu_registers[reg]);
}

static inline void fpu_write_register32(rvvm_hart_t *vm, regid_t reg, float val)
{
    assert(reg < FPU_REGISTERS_MAX);
    // NOTE: for performance reasons/smaller JIT footprint, maybe
    // we should hardcode the FPU state to dirty?
    fpu_set_fs(vm, FS_DIRTY);
    write_float_nanbox(&vm->fpu_registers[reg], val);
}

static inline double fpu_read_register64(rvvm_hart_t *vm, regid_t reg)
{
    assert(reg < FPU_REGISTERS_MAX);
    return vm->fpu_registers[reg];
}

static inline void fpu_write_register64(rvvm_hart_t *vm, regid_t reg, double val)
{
    assert(reg < FPU_REGISTERS_MAX);
    fpu_set_fs(vm, FS_DIRTY);
    vm->fpu_registers[reg] = val;
}
#endif

/* translate compressed register encoding into normal */
static inline regid_t riscv_c_reg(regid_t reg)
{
    //NOTE: register index is hard limited to 8, since encoding is 3 bits
    return REGISTER_X8 + reg;
}

/*
 * For normal 32-bit length instructions, identifier is func7[25] func3[14:12] opcode[6:2]
 * For compressed 16-bit length instructions, identifier is func3[15:13] opcode[1:0]
 *
 * This is tricky for non-R type instructions since there's no func3 or func7,
 * so we will simply smudge function pointers for those all over the jumptable.
 */

/*
 * RVI Base ISA
 */

// U/J type instructions
#define RVI_LUI          0xD
#define RVI_AUIPC        0x5
#define RVI_JAL          0x1B
// R-type instructions
#define RVI_SLLI         0x24
#define RVI_SRLI_SRAI    0xA4
#define RVI_ADD_SUB      0xC
#define RVI_SLL          0x2C
#define RVI_SLT          0x4C
#define RVI_SLTU         0x6C
#define RVI_XOR          0x8C
#define RVI_SRL_SRA      0xAC
#define RVI_OR           0xCC
#define RVI_AND          0xEC
// I/S/B type instructions
#define RVI_JALR         0x19
#define RVI_BEQ          0x18
#define RVI_BNE          0x38
#define RVI_BLT          0x98
#define RVI_BGE          0xB8
#define RVI_BLTU         0xD8
#define RVI_BGEU         0xF8
#define RVI_LB           0x0
#define RVI_LH           0x20
#define RVI_LW           0x40
#define RVI_LBU          0x80
#define RVI_LHU          0xA0
#define RVI_SB           0x8
#define RVI_SH           0x28
#define RVI_SW           0x48
#define RVI_ADDI         0x4
#define RVI_SLTI         0x44
#define RVI_SLTIU        0x64
#define RVI_XORI         0x84
#define RVI_ORI          0xC4
#define RVI_ANDI         0xE4

/*
 * RV64I-only instructions
 */

// R-type instructions
#define RV64I_ADDIW       0x6
#define RV64I_SLLIW       0x26
#define RV64I_SRLIW_SRAIW 0xA6
#define RV64I_ADDW_SUBW   0xE
#define RV64I_SLLW        0x2E
#define RV64I_SRLW_SRAW   0xAE
// I/S/B type instructions
#define RV64I_LWU         0xC0
#define RV64I_LD          0x60
#define RV64I_SD          0x68

/*
 * RVC Compressed instructions
 */

/* opcode 0 */
#define RVC_ADDI4SPN     0x0
#define RVC_FLD          0x4
#define RVC_LW           0x8
#define RVC_FLW          0xC  // only exists on RV32!
#define RVC_RESERVED1    0x10
#define RVC_FSD          0x14
#define RVC_SW           0x18
#define RVC_FSW          0x1C // only exists on RV32!
#define RV64C_SD         0x1C // Replaces FSW on RV64
#define RV64C_LD         0xC  // Replaces FLW on RV64
/* opcode 1 */
#define RVC_ADDI         0x1  // this is also NOP when rs/rd == 0
#define RVC_JAL          0x5  // only exists on RV32!
#define RVC_LI           0x9
#define RVC_ADDI16SP_LUI 0xD  // this is ADDI16SP when rd == 2 or LUI (rd!=0)
#define RVC_ALOPS1       0x11 // a lot of operations packed tightly, idk about performance
#define RVC_J            0x15
#define RVC_BEQZ         0x19
#define RVC_BNEZ         0x1D
#define RV64C_ADDIW      0x5  // Replaces JAL on RV64
/* opcode 2 */
#define RVC_SLLI         0x2
#define RVC_FLDSP        0x6
#define RVC_LWSP         0xA
#define RVC_FLWSP        0xE  // only exists on RV32!
#define RVC_ALOPS2       0x12 // same as RVC_ALOPS1
#define RVC_FSDSP        0x16
#define RVC_SWSP         0x1A
#define RVC_FSWSP        0x1E // only exists on RV32!
#define RV64C_LDSP       0xE  // Replaces FLWSP on RV64
#define RV64C_SDSP       0x1E // Replaces FSWSP on RV64

/*
 * RVM Math instructions
 */
// R-type instructions
#define RVM_MUL          0x10C
#define RVM_MULH         0x12C
#define RVM_MULHSU       0x14C
#define RVM_MULHU        0x16C
#define RVM_DIV          0x18C
#define RVM_DIVU         0x1AC
#define RVM_REM          0x1CC
#define RVM_REMU         0x1EC

/*
 * RV64M-only instructions
 */

// R-type instructions
#define RV64M_MULW       0x10E
#define RV64M_DIVW       0x18E
#define RV64M_DIVUW      0x1AE
#define RV64M_REMW       0x1CE
#define RV64M_REMUW      0x1EE

/*
 * RVA, RV64A Atomic instructions
 */

// I/S/B type instructions
#define RVA_ATOMIC_W     0x4B
#define RV64A_ATOMIC_D   0x6B

/*
 * RV32F instructions
 */
#define RVF_FLW          0x41 /* ISB */
#define RVF_FSW          0x49 /* ISB */
#define RVF_FMADD        0x10 /* R + funct3 */
#define RVF_FMSUB        0x11 /* R + funct3 */
#define RVF_FNMSUB       0x12 /* R + funct3 */
#define RVF_FNMADD       0x13 /* R + funct3 */
#define RVF_OTHER        0x14 /* R + funct3 + funct7 a bunch */

/*
 * RV32D instructions
 */
#define RVD_FLW          0x61 /* ISB */
#define RVD_FSW          0x69 /* ISB */
#define RVD_FMADD        0x110 /* R + funct3 */
#define RVD_FMSUB        0x111 /* R + funct3 */
#define RVD_FNMSUB       0x112 /* R + funct3 */
#define RVD_FNMADD       0x113 /* R + funct3 */
/* except FCVT.S.D */
#define RVD_OTHER        0x114 /* R + funct3 + funct7 a bunch */

#endif
#endif
