/*
rvjit_emit.h - Retargetable Versatile JIT Compiler
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

#ifndef RVJIT_EMIT_H
#define RVJIT_EMIT_H

void rvjit_linker_patch_jmp(void* addr, int32_t offset);
void rvjit_linker_patch_ret(void* addr);

// RV32IC
void rvjit32_add(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_sub(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_or(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_and(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_xor(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_sra(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_srl(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_sll(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_addi(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_ori(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_andi(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_xori(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_srai(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_srli(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_slli(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_slti(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_sltiu(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_slt(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_sltu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_li(rvjit_block_t* block, regid_t rds, int32_t imm);
void rvjit32_auipc(rvjit_block_t* block, regid_t rds, int32_t imm);
void rvjit32_jalr(rvjit_block_t* block, regid_t rds, regid_t rs, int32_t imm, uint8_t isize);

void rvjit32_sb(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset);
void rvjit32_lb(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit32_lbu(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit32_sh(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset);
void rvjit32_lh(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit32_lhu(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit32_sw(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset);
void rvjit32_lw(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);

void rvjit32_beq(rvjit_block_t* block, regid_t rs1, regid_t rs2);
void rvjit32_bne(rvjit_block_t* block, regid_t rs1, regid_t rs2);
void rvjit32_blt(rvjit_block_t* block, regid_t rs1, regid_t rs2);
void rvjit32_bge(rvjit_block_t* block, regid_t rs1, regid_t rs2);
void rvjit32_bltu(rvjit_block_t* block, regid_t rs1, regid_t rs2);
void rvjit32_bgeu(rvjit_block_t* block, regid_t rs1, regid_t rs2);

// RV32M
void rvjit32_mul(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_mulh(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_mulhu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_mulhsu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);

void rvjit32_div(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_divu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_rem(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_remu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);

// RV32 Zba
void rvjit32_shadd(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2, uint8_t shift);

// RV32 Zbb
#ifdef RVJIT_NATIVE_BITMANIP
void rvjit32_orc_b(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit32_clz(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit32_ctz(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit32_cpop(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit32_rev8(rvjit_block_t* block, regid_t rds, regid_t rs1);
#endif
void rvjit32_rol(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_ror(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_rori(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_andn(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_orn(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_xnor(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_max(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_maxu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_min(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_minu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_sext_b(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit32_sext_h(rvjit_block_t* block, regid_t rds, regid_t rs1);

// RV32 Zbs
void rvjit32_bext(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_bexti(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_bclr(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_bclri(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_bset(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_bseti(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit32_binv(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit32_binvi(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);

#ifdef RVJIT_NATIVE_64BIT

/*
 * RV64-only intrinsics
 */

// RV64IC
void rvjit64_add(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_sub(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_or(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_and(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_xor(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_sra(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_srl(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_sll(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_addi(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_ori(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_andi(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_xori(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_srai(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_srli(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_slli(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_slti(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_sltiu(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_slt(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_sltu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_li(rvjit_block_t* block, regid_t rds, int32_t imm);
void rvjit64_auipc(rvjit_block_t* block, regid_t rds, int32_t imm);
void rvjit64_jalr(rvjit_block_t* block, regid_t rds, regid_t rs, int32_t imm, uint8_t isize);

void rvjit64_addw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_subw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_sraw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_srlw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_sllw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_addiw(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_sraiw(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_srliw(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_slliw(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);

void rvjit64_sb(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset);
void rvjit64_lb(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit64_lbu(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit64_sh(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit64_lh(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit64_lhu(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit64_sw(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset);
void rvjit64_lw(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit64_lwu(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit64_sd(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit64_ld(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);

void rvjit64_beq(rvjit_block_t* block, regid_t rs1, regid_t rs2);
void rvjit64_bne(rvjit_block_t* block, regid_t rs1, regid_t rs2);
void rvjit64_blt(rvjit_block_t* block, regid_t rs1, regid_t rs2);
void rvjit64_bge(rvjit_block_t* block, regid_t rs1, regid_t rs2);
void rvjit64_bltu(rvjit_block_t* block, regid_t rs1, regid_t rs2);
void rvjit64_bgeu(rvjit_block_t* block, regid_t rs1, regid_t rs2);

// RV64M
void rvjit64_mul(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_mulh(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_mulhu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_mulhsu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_mulw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);

void rvjit64_div(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_divu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_rem(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_remu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_divw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_divuw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_remw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_remuw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);

// RV64 Zba
void rvjit64_shadd(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2, uint8_t shift);
void rvjit64_shadd_uw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2, uint8_t shift);
void rvjit64_slli_uw(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);

// RV64 Zbb
#ifdef RVJIT_NATIVE_BITMANIP
void rvjit64_orc_b(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_clz(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_clzw(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_ctz(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_ctzw(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_cpop(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_cpopw(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_rev8(rvjit_block_t* block, regid_t rds, regid_t rs1);
#endif
void rvjit64_rol(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_rolw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_ror(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_rorw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_rori(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_roriw(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_andn(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_orn(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_xnor(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_max(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_maxu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_min(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_minu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_sext_b(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_sext_h(rvjit_block_t* block, regid_t rds, regid_t rs1);

// RV64 Zbs
void rvjit64_bext(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_bexti(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_bclr(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_bclri(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_bset(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_bseti(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);
void rvjit64_binv(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit64_binvi(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm);

#endif

#ifdef RVJIT_NATIVE_FPU

/*
 * FPU intrinsics
 */

void rvjit_fsw(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset);
void rvjit_flw(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit_fsd(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);
void rvjit_fld(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset);

void rvjit_fadd_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fadd_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fsub_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fsub_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fmul_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fmul_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fdiv_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fdiv_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fsqrt_s(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit_fsqrt_d(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit_fmadd_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2, regid_t rs3);
void rvjit_fmadd_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2, regid_t rs3);
void rvjit_fmsub_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2, regid_t rs3);
void rvjit_fmsub_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2, regid_t rs3);
void rvjit_fnmadd_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2, regid_t rs3);
void rvjit_fnmadd_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2, regid_t rs3);
void rvjit_fnmsub_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2, regid_t rs3);
void rvjit_fnmsub_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2, regid_t rs3);
void rvjit_fsgnj_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fsgnj_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fsgnjn_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fsgnjn_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fsgnjx_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fsgnjx_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fmin_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fmin_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fmax_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fmax_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);

void rvjit_feq_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_feq_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_flt_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_flt_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fle_s(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);
void rvjit_fle_d(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2);

void rvjit_fclass_s(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit_fclass_d(rvjit_block_t* block, regid_t rds, regid_t rs1);

void rvjit_fcvt_s_d(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit_fcvt_d_s(rvjit_block_t* block, regid_t rds, regid_t rs1);

void rvjit_fmv_w_x(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit_fmv_x_w(rvjit_block_t* block, regid_t rds, regid_t rs1);

void rvjit_fcvt_w_s(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit_fcvt_wu_s(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit_fcvt_s_w(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit_fcvt_s_wu(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit_fcvt_w_d(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit_fcvt_wu_d(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit_fcvt_d_w(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit_fcvt_d_wu(rvjit_block_t* block, regid_t rds, regid_t rs1);

#ifdef RVJIT_NATIVE_64BIT

// RV64-only FPU intrinsics

void rvjit64_fmv_d_x(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_fmv_x_d(rvjit_block_t* block, regid_t rds, regid_t rs1);

void rvjit64_fcvt_l_s(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_fcvt_lu_s(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_fcvt_s_l(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_fcvt_s_lu(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_fcvt_l_d(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_fcvt_lu_d(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_fcvt_d_l(rvjit_block_t* block, regid_t rds, regid_t rs1);
void rvjit64_fcvt_d_lu(rvjit_block_t* block, regid_t rds, regid_t rs1);

#endif

#endif

#endif
