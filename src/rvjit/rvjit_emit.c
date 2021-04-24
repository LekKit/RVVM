/*
rvjit_emit.c - Retargetable Versatile JIT Compiler
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

#include "rvjit_emit.h"

#ifdef RVJIT_X86
#include "rvjit_x86.h"
#endif

// Frees explicitly claimed hardware register
static inline void rvjit_free_hreg(rvjit_block_t* block, regid_t hreg)
{
    block->hreg_mask |= rvjit_hreg_mask(hreg);
}

static void rvjit_load_reg(rvjit_block_t* block, regid_t reg)
{
    if (block->regs[reg].hreg != REG_ILL) {
        rvjit_native_load(block, block->regs[reg].hreg, VM_PTR_REG, sizeof(size_t) * reg);
    }
}

static void rvjit_save_reg(rvjit_block_t* block, regid_t reg)
{
    if (block->regs[reg].hreg != REG_ILL) {
        if (block->regs[reg].flags & REG_DIRTY) {
#ifdef REGISTER_ZERO_ENABLED
        if (reg != 0)
#endif
            rvjit_native_store(block, block->regs[reg].hreg, VM_PTR_REG, sizeof(size_t) * reg);
        }
        rvjit_free_hreg(block, block->regs[reg].hreg);
        block->regs[reg].hreg = REG_ILL;
    }
}

static void rvjit_save_all_regs(rvjit_block_t* block)
{
    for (regid_t i=0; i<REGISTERS_MAX; ++i)
        rvjit_save_reg(block, i);
}

static regid_t rvjit_reclaim_hreg(rvjit_block_t* block)
{
    // If we have any registers clobbered by ABI we can reuse them
    if (block->abireclaim_mask != rvjit_native_abireclaim_hregmask()) {
        for (regid_t i=0; i<REGISTERS_MAX; ++i) {
            if ((block->abireclaim_mask & rvjit_hreg_mask(i)) !=
            (rvjit_native_abireclaim_hregmask() & rvjit_hreg_mask(i))) {
                block->abireclaim_mask |= rvjit_hreg_mask(i);
                rvjit_native_push(block, i);
                return i;
            }
        }
    }
    // Reclaim least recently used register mapping
    regid_t greg = 0, hreg;
    size_t lru = (size_t)-1;
    for (regid_t i=0; i<REGISTERS_MAX; ++i) {
        if (block->regs[i].hreg != REG_ILL && block->regs[i].last_used < lru) {
            lru = block->regs[i].last_used;
            greg = i;
        }
    }
    assert(lru != (size_t)-1); // None of the registers can be reclaimed
    hreg = block->regs[greg].hreg;
    rvjit_save_reg(block, greg);
    block->hreg_mask &= ~rvjit_hreg_mask(hreg);
    return hreg;
}

static inline regid_t rvjit_try_claim_hreg(rvjit_block_t* block)
{
    for (regid_t i=0; i<REGISTERS_MAX; ++i) {
        if (block->hreg_mask & rvjit_hreg_mask(i)) {
            block->hreg_mask &= ~rvjit_hreg_mask(i);
            return i;
        }
    }
    return REG_ILL;
}

// Claims any free hardware register, or reclaims mapped register preserving it's value in VM
static inline regid_t rvjit_claim_hreg(rvjit_block_t* block)
{
    regid_t hreg = rvjit_try_claim_hreg(block);
    // No free host registers
    if (hreg == REG_ILL) {
        hreg = rvjit_reclaim_hreg(block);
    }
    return hreg;
}

// Maps virtual register to hardware register
static regid_t rvjit_map_reg(rvjit_block_t* block, regid_t greg, regflags_t flags)
{
    assert(greg < REGISTERS_MAX);
    if (block->regs[greg].hreg == REG_ILL) {
        regid_t hreg = rvjit_claim_hreg(block);
        block->regs[greg].hreg = hreg;
        block->regs[greg].flags = 0;
    }
    block->regs[greg].last_used = block->size;
#ifdef REGISTER_ZERO_ENABLED
    if (greg == 0) {
        if (!(block->regs[greg].flags & REG_LOADED) || (block->regs[greg].flags & REG_DIRTY)) {
            rvjit_native_zero_reg(block, block->regs[greg].hreg);
        }
        block->regs[greg].flags = REG_LOADED;
    }
#endif
    if (flags & REG_DST) block->regs[greg].flags |= REG_DIRTY;
    if ((flags & REG_SRC) && !(block->regs[greg].flags & (REG_LOADED | REG_DIRTY))) {
        block->regs[greg].flags |= REG_LOADED;
        rvjit_load_reg(block, greg);
    }
    return block->regs[greg].hreg;
}

void rvjit_emit_end(rvjit_block_t* block)
{
    // Save allocated native registers into VM context
    rvjit_save_all_regs(block);
    // Recover clobbered registers
    for (regid_t i=REGISTERS_MAX; i>0; --i) {
        if (block->abireclaim_mask & rvjit_hreg_mask(i-1)) {
            rvjit_native_pop(block, i-1);
        }
    }
    rvjit_native_ret(block);
}

#ifdef REGISTER_ZERO_ENABLED
#define REGZERO_COND 1
#else
#define REGZERO_COND 0
#endif

/*
 * Important: REG_DST (destination) registers should be mapped at the end,
 * otherwise nasty errors occur, this simplifies register remapping
 */

#define RVJIT_3REG_OP(native_func, rds, rs1, rs2) { \
    if (rds == 0 && REGZERO_COND) return; \
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC); \
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC); \
    regid_t hrds = rvjit_map_reg(block, rds, REG_DST); \
    native_func(block, hrds, hrs1, hrs2); }

#define RVJIT_2REG_IMM_OP(native_func, rds, rs1, imm) { \
    if (rds == 0 && REGZERO_COND) return; \
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC); \
    regid_t hrds = rvjit_map_reg(block, rds, REG_DST); \
    native_func(block, hrds, hrs1, imm); }

void rvjit32_add(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit32_native_add, rds, rs1, rs2);
}

void rvjit32_sub(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit32_native_sub, rds, rs1, rs2);
}

void rvjit32_or(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit32_native_or, rds, rs1, rs2);
}

void rvjit32_and(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit32_native_and, rds, rs1, rs2);
}

void rvjit32_xor(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit32_native_xor, rds, rs1, rs2);
}

void rvjit32_sra(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit32_native_sra, rds, rs1, rs2);
}

void rvjit32_srl(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit32_native_srl, rds, rs1, rs2);
}

void rvjit32_sll(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit32_native_sll, rds, rs1, rs2);
}

void rvjit32_addi(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit32_native_addi, rds, rs1, imm);
}

void rvjit32_ori(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit32_native_ori, rds, rs1, imm);
}

void rvjit32_andi(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit32_native_andi, rds, rs1, imm);
}

void rvjit32_xori(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit32_native_xori, rds, rs1, imm);
}

void rvjit32_srai(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit32_native_srai, rds, rs1, imm);
}

void rvjit32_srli(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit32_native_srli, rds, rs1, imm);
}

void rvjit32_slli(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit32_native_slli, rds, rs1, imm);
}

void rvjit32_slti(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit32_native_slti, rds, rs1, imm);
}

void rvjit32_sltiu(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit32_native_sltiu, rds, rs1, imm);
}

void rvjit32_slt(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit32_native_slt, rds, rs1, rs2);
}

void rvjit32_sltu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit32_native_sltu, rds, rs1, rs2);
}

void rvjit_emit_call(rvjit_block_t* block, const void* funcaddr)
{
    rvjit_save_all_regs(block);
    regid_t tmp_reg = rvjit_claim_hreg(block);
    rvjit_native_push(block, VM_PTR_REG);
    rvjit_native_setreg(block, tmp_reg, (uintptr_t)funcaddr);
    rvjit_native_callreg(block, tmp_reg);
    rvjit_native_pop(block, VM_PTR_REG);
    rvjit_free_hreg(block, tmp_reg);
}
