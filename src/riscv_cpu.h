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
#include "riscv_mmu.h"
#include "mem_ops.h"

void riscv_illegal_insn(rvvm_hart_t* vm, const uint32_t insn);

void riscv_run_till_event(rvvm_hart_t* vm);

void riscv32_run_interpreter(rvvm_hart_t* vm);
void riscv64_run_interpreter(rvvm_hart_t* vm);

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

static inline void riscv_jit_flush_cache(rvvm_hart_t* vm)
{
#ifdef USE_JIT
    if (vm->jit_enabled) {
        riscv_jit_discard(vm);
        riscv_jit_tlb_flush(vm);
        rvjit_flush_cache(&vm->jit);
    }
#else
    UNUSED(vm);
#endif
}

#ifdef USE_JIT
void riscv_jit_mark_dirty_mem(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);
#else
static inline void riscv_jit_mark_dirty_mem(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size) {
    UNUSED(machine);
    UNUSED(addr);
    UNUSED(size);
}
#endif

// Private CPU implementation definitions
#ifdef RISCV_CPU_SOURCE

#ifdef USE_JIT
#include "rvjit/rvjit_emit.h"

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

// Block unrolling configuration
#define BRANCH_MAX_BLOCK_SIZE 256

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
    virt_addr_t pc = vm->registers[REGISTER_PC]; \
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

#define rvjit_mul(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit64_mul(&vm->jit, rds, rs1, rs2), size)
#define rvjit_mulh(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_mulh(&vm->jit, rds, rs1, rs2), size)
#define rvjit_mulhu(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_mulhu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_mulhsu(rds, rs1, rs2, size) RVVM_RVJIT_TRACE(rvjit64_mulhsu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_div(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit64_div(&vm->jit, rds, rs1, rs2), size)
#define rvjit_divu(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_divu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_rem(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit64_rem(&vm->jit, rds, rs1, rs2), size)
#define rvjit_remu(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_remu(&vm->jit, rds, rs1, rs2), size)

#define rvjit_mulw(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_mulw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_divw(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_divw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_divuw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_divuw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_remw(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit64_remw(&vm->jit, rds, rs1, rs2), size)
#define rvjit_remuw(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit64_remuw(&vm->jit, rds, rs1, rs2), size)

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

#define rvjit_mul(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit32_mul(&vm->jit, rds, rs1, rs2), size)
#define rvjit_mulh(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_mulh(&vm->jit, rds, rs1, rs2), size)
#define rvjit_mulhu(rds, rs1, rs2, size)  RVVM_RVJIT_TRACE(rvjit32_mulhu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_mulhsu(rds, rs1, rs2, size) RVVM_RVJIT_TRACE(rvjit32_mulhsu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_div(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit32_div(&vm->jit, rds, rs1, rs2), size)
#define rvjit_divu(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_divu(&vm->jit, rds, rs1, rs2), size)
#define rvjit_rem(rds, rs1, rs2, size)    RVVM_RVJIT_TRACE(rvjit32_rem(&vm->jit, rds, rs1, rs2), size)
#define rvjit_remu(rds, rs1, rs2, size)   RVVM_RVJIT_TRACE(rvjit32_remu(&vm->jit, rds, rs1, rs2), size)

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

#define rvjit_mul(rds, rs1, rs2, size)
#define rvjit_mulh(rds, rs1, rs2, size)
#define rvjit_mulhu(rds, rs1, rs2, size)
#define rvjit_mulhsu(rds, rs1, rs2, size)
#define rvjit_div(rds, rs1, rs2, size)
#define rvjit_divu(rds, rs1, rs2, size)
#define rvjit_rem(rds, rs1, rs2, size)
#define rvjit_remu(rds, rs1, rs2, size)

#define rvjit_mulw(rds, rs1, rs2, size)
#define rvjit_divw(rds, rs1, rs2, size)
#define rvjit_divuw(rds, rs1, rs2, size)
#define rvjit_remw(rds, rs1, rs2, size)
#define rvjit_remuw(rds, rs1, rs2, size)

#endif

#ifdef RV64
    typedef uint64_t xlen_t;
    typedef int64_t sxlen_t;
    typedef uint64_t xaddr_t;
    #define SHAMT_BITS 6
    #define DIV_OVERFLOW_RS1 ((sxlen_t)0x8000000000000000ULL)
#else
    typedef uint32_t xlen_t;
    typedef int32_t sxlen_t;
    typedef uint32_t xaddr_t;
    #define SHAMT_BITS 5
    #define DIV_OVERFLOW_RS1 ((sxlen_t)0x80000000U)
#endif

static forceinline xlen_t riscv_read_reg(rvvm_hart_t* vm, regid_t reg)
{
    return vm->registers[reg];
}

static forceinline sxlen_t riscv_read_reg_s(rvvm_hart_t* vm, regid_t reg)
{
    return vm->registers[reg];
}

static forceinline void riscv_write_reg(rvvm_hart_t* vm, regid_t reg, sxlen_t data)
{
    vm->registers[reg] = data;
}

#endif
#endif
