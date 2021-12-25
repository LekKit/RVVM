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
#include "mem_ops.h"

#ifndef RVJIT_EMIT_H
#define RVJIT_EMIT_H

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

#endif
