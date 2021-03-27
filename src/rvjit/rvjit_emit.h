/*
rvjit.h - Retargetable Versatile JIT Compiler
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

void rvjit_emit_end(rvjit_block_t* block);
void rvjit_emit_call(rvjit_block_t* block, const void* funcaddr);

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

#endif
