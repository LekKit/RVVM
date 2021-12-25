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
#include "rvvm.h"

#ifdef RVJIT_X86
#include "rvjit_x86.h"
#elif  RVJIT_RISCV
#include "rvjit_riscv.h"
#endif

#define REG_SRC    0x1
#define REG_DST    0x2

#define REG_LOADED REG_SRC
#define REG_DIRTY  REG_DST

// RVVM-specific configuration

#define VM_REG_OFFSET(reg) offsetof(rvvm_hart_t, registers[reg])
#define VM_TLB_OFFSET      offsetof(rvvm_hart_t, tlb)
#define VM_TLB_MASK        (TLB_SIZE-1)
#define VM_TLB_R           offsetof(rvvm_tlb_entry_t, r)
#define VM_TLB_W           offsetof(rvvm_tlb_entry_t, w)
#define VM_TLB_E           offsetof(rvvm_tlb_entry_t, e)

#if defined(USE_RV64) || defined(RVJIT_NATIVE_64BIT)
#define VM_TLB_SHIFT 5
#else
#define VM_TLB_SHIFT 4
#endif

void rvjit_emit_init(rvjit_block_t* block)
{
    block->hreg_mask = rvjit_native_default_hregmask();
    block->abireclaim_mask = 0;
    for (regid_t i=0; i<RVJIT_REGISTERS; ++i) {
        block->regs[i].hreg = REG_ILL;
        block->regs[i].last_used = 0;
        block->regs[i].flags = 0;
    }
}

static void rvjit_load_reg(rvjit_block_t* block, regid_t reg)
{
    if (block->regs[reg].hreg != REG_ILL) {
#ifdef RVJIT_NATIVE_64BIT
        if (block->rv64) {
            rvjit64_native_ld(block, block->regs[reg].hreg, VM_PTR_REG, VM_REG_OFFSET(reg));
        } else {
            rvjit32_native_lw(block, block->regs[reg].hreg, VM_PTR_REG, VM_REG_OFFSET(reg));
        }
#else
        rvjit32_native_lw(block, block->regs[reg].hreg, VM_PTR_REG, VM_REG_OFFSET(reg));
#endif
    }
}

static void rvjit_save_reg(rvjit_block_t* block, regid_t reg)
{
    if (block->regs[reg].hreg != REG_ILL) {
        if (block->regs[reg].flags & REG_DIRTY) {
            if (reg != RVJIT_REGISTER_ZERO) {
#ifdef RVJIT_NATIVE_64BIT
                if (block->rv64) {
                    rvjit64_native_sd(block, block->regs[reg].hreg, VM_PTR_REG, VM_REG_OFFSET(reg));
                } else {
                    rvjit32_native_sw(block, block->regs[reg].hreg, VM_PTR_REG, VM_REG_OFFSET(reg));
                }
#else
                rvjit32_native_sw(block, block->regs[reg].hreg, VM_PTR_REG, VM_REG_OFFSET(reg));
#endif
            }
        }
    }
}

static void rvjit_free_reg(rvjit_block_t* block, regid_t reg)
{
    if (block->regs[reg].hreg != REG_ILL) {
        rvjit_save_reg(block, reg);
        rvjit_free_hreg(block, block->regs[reg].hreg);
        block->regs[reg].hreg = REG_ILL;
    }
}

regid_t rvjit_reclaim_hreg(rvjit_block_t* block)
{
    // If we have any registers clobbered by ABI we can reuse them
    if (block->abireclaim_mask != rvjit_native_abireclaim_hregmask()) {
        for (regid_t i=0; i<RVJIT_REGISTERS; ++i) {
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
    for (regid_t i=0; i<RVJIT_REGISTERS; ++i) {
        if (block->regs[i].hreg != REG_ILL && block->regs[i].last_used < lru) {
            lru = block->regs[i].last_used;
            greg = i;
        }
    }
    if (unlikely(lru == (size_t)-1)) {
        rvvm_fatal("No reclaimable RVJIT registers!");
    }
    hreg = block->regs[greg].hreg;
    rvjit_free_reg(block, greg);
    block->hreg_mask &= ~rvjit_hreg_mask(hreg);
    return hreg;
}

// Maps virtual register to hardware register
static regid_t rvjit_map_reg(rvjit_block_t* block, regid_t greg, regflags_t flags)
{
    if (unlikely(greg >= RVJIT_REGISTERS)) {
        rvvm_fatal("Mapped RVJIT register is out of range!");
    }
    if (block->regs[greg].hreg == REG_ILL) {
        regid_t hreg = rvjit_claim_hreg(block);
        block->regs[greg].hreg = hreg;
        block->regs[greg].flags = 0;
    }
    block->regs[greg].last_used = block->size;

    if (greg == RVJIT_REGISTER_ZERO) {
        if (!(block->regs[greg].flags & REG_LOADED) || (block->regs[greg].flags & REG_DIRTY)) {
            rvjit_native_zero_reg(block, block->regs[greg].hreg);
        }
        block->regs[greg].flags = REG_LOADED;
    }

    if (flags & REG_DST) block->regs[greg].flags |= REG_DIRTY;
    if ((flags & REG_SRC) && !(block->regs[greg].flags & (REG_LOADED | REG_DIRTY))) {
        block->regs[greg].flags |= REG_LOADED;
        rvjit_load_reg(block, greg);
    }
    return block->regs[greg].hreg;
}

static void rvjit_update_vm_pc(rvjit_block_t* block)
{
    if (block->pc_off == 0) return;
    regid_t pc = rvjit_claim_hreg(block);
#ifdef RVJIT_NATIVE_64BIT
    if (block->rv64) {
        rvjit64_native_ld(block, pc, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
        rvjit64_native_addi(block, pc, pc, block->pc_off);
        rvjit64_native_sd(block, pc, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
    } else
#endif
    {
        rvjit32_native_lw(block, pc, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
        rvjit32_native_addi(block, pc, pc, block->pc_off);
        rvjit32_native_sw(block, pc, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
    }
    rvjit_free_hreg(block, pc);
}

void rvjit_emit_end(rvjit_block_t* block)
{
    size_t hreg_mask = block->hreg_mask;
    size_t abireclaim_mask = block->abireclaim_mask;
    // Save allocated native registers into VM context
    for (regid_t i=0; i<RVJIT_REGISTERS; ++i) {
        rvjit_save_reg(block, i);
    }
    block->hreg_mask = rvjit_native_default_hregmask();
    rvjit_update_vm_pc(block);
    // Recover clobbered registers
    for (regid_t i=RVJIT_REGISTERS; i>0; --i) {
        if (block->abireclaim_mask & rvjit_hreg_mask(i-1)) {
            rvjit_native_pop(block, i-1);
        }
    }
    rvjit_native_ret(block);
    block->hreg_mask = hreg_mask;
    block->abireclaim_mask = abireclaim_mask;
}

/*
 * Important: REG_DST (destination) registers should be mapped at the end,
 * otherwise nasty errors occur, this simplifies register remapping
 */

/* TODO: Optimize OP3 rA, rA, rB */

#define RVJIT_3REG_OP(native_func, rds, rs1, rs2) { \
    if (rds == RVJIT_REGISTER_ZERO) return; \
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC); \
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC); \
    regid_t hrds = rvjit_map_reg(block, rds, REG_DST); \
    native_func(block, hrds, hrs1, hrs2); }

#define RVJIT_2REG_IMM_OP(native_func, rds, rs1, imm) { \
    if (rds == RVJIT_REGISTER_ZERO) return; \
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC); \
    regid_t hrds = rvjit_map_reg(block, rds, REG_DST); \
    native_func(block, hrds, hrs1, imm); }

// Peephole optimization for cases like addi reg, zero, imm
#define RVJIT_2REG_IMM_OPTIMIZE(rds, rs1, imm) \
    if (rds && rs1 == RVJIT_REGISTER_ZERO) { \
        regid_t hrds = rvjit_map_reg(block, rds, REG_DST); \
        rvjit_native_setreg32(block, hrds, imm); \
        return; \
    }

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
    RVJIT_2REG_IMM_OPTIMIZE(rds, rs1, imm);
    RVJIT_2REG_IMM_OP(rvjit32_native_addi, rds, rs1, imm);
}

void rvjit32_ori(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OPTIMIZE(rds, rs1, imm);
    RVJIT_2REG_IMM_OP(rvjit32_native_ori, rds, rs1, imm);
}

void rvjit32_andi(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    if (rds && rs1 == RVJIT_REGISTER_ZERO) {
        regid_t hrds = rvjit_map_reg(block, rds, REG_DST);
        rvjit_native_setreg32(block, hrds, 0);
        return;
    }
    RVJIT_2REG_IMM_OP(rvjit32_native_andi, rds, rs1, imm);
}

void rvjit32_xori(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OPTIMIZE(rds, rs1, imm);
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

void rvjit32_li(rvjit_block_t* block, regid_t rds, int32_t imm)
{
    if (rds == RVJIT_REGISTER_ZERO) return;
    regid_t hrds = rvjit_map_reg(block, rds, REG_DST);
    rvjit_native_setreg32(block, hrds, imm);
}

void rvjit32_auipc(rvjit_block_t* block, regid_t rds, int32_t imm)
{
    if (rds == RVJIT_REGISTER_ZERO) return;
    regid_t hrds = rvjit_map_reg(block, rds, REG_DST);
    rvjit32_native_lw(block, hrds, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
    imm += block->pc_off;
    if (imm) {
        rvjit32_native_addi(block, hrds, hrds, imm);
    }
}

void rvjit32_jalr(rvjit_block_t* block, regid_t rds, regid_t rs, int32_t imm, uint8_t isize)
{
    regid_t hrs = rvjit_map_reg(block, rs, REG_SRC);
    regid_t hjmp = rvjit_claim_hreg(block);
    rvjit32_native_addi(block, hjmp, hrs, imm);
    rvjit32_auipc(block, rds, isize);
    rvjit32_native_sw(block, hjmp, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
    rvjit_free_hreg(block, hjmp);
    block->pc_off = 0;
    rvjit_emit_end(block);
}

void rvjit32_beq(rvjit_block_t* block, regid_t rs1, regid_t rs2)
{
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC);
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC);

    branch_t l1 = rvjit32_native_beq(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit32_native_beq(block, hrs1, hrs2, l1, BRANCH_TARGET);
}

void rvjit32_bne(rvjit_block_t* block, regid_t rs1, regid_t rs2)
{
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC);
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC);

    branch_t l1 = rvjit32_native_bne(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit32_native_bne(block, hrs1, hrs2, l1, BRANCH_TARGET);
}

void rvjit32_blt(rvjit_block_t* block, regid_t rs1, regid_t rs2)
{
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC);
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC);

    branch_t l1 = rvjit32_native_blt(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit32_native_blt(block, hrs1, hrs2, l1, BRANCH_TARGET);
}

void rvjit32_bge(rvjit_block_t* block, regid_t rs1, regid_t rs2)
{
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC);
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC);

    branch_t l1 = rvjit32_native_bge(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit32_native_bge(block, hrs1, hrs2, l1, BRANCH_TARGET);
}

void rvjit32_bltu(rvjit_block_t* block, regid_t rs1, regid_t rs2)
{
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC);
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC);

    branch_t l1 = rvjit32_native_bltu(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit32_native_bltu(block, hrs1, hrs2, l1, BRANCH_TARGET);
}

void rvjit32_bgeu(rvjit_block_t* block, regid_t rs1, regid_t rs2)
{
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC);
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC);

    branch_t l1 = rvjit32_native_bgeu(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit32_native_bgeu(block, hrs1, hrs2, l1, BRANCH_TARGET);
}

// RV64

#ifdef RVJIT_NATIVE_64BIT

// Peephole optimization for cases like addi reg, zero, imm
#define RVJIT_2REG_IMM_OPTIMIZE64(rds, rs1, imm) \
    if (rds && rs1 == RVJIT_REGISTER_ZERO) { \
        regid_t hrds = rvjit_map_reg(block, rds, REG_DST); \
        rvjit_native_setreg32s(block, hrds, imm); \
        return; \
    }

void rvjit64_add(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_add, rds, rs1, rs2);
}

void rvjit64_sub(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_sub, rds, rs1, rs2);
}

void rvjit64_or(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_or, rds, rs1, rs2);
}

void rvjit64_and(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_and, rds, rs1, rs2);
}

void rvjit64_xor(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_xor, rds, rs1, rs2);
}

void rvjit64_sra(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_sra, rds, rs1, rs2);
}

void rvjit64_srl(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_srl, rds, rs1, rs2);
}

void rvjit64_sll(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_sll, rds, rs1, rs2);
}

void rvjit64_addw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_addw, rds, rs1, rs2);
}

void rvjit64_subw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_subw, rds, rs1, rs2);
}

void rvjit64_sraw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_sraw, rds, rs1, rs2);
}

void rvjit64_srlw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_srlw, rds, rs1, rs2);
}

void rvjit64_sllw(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_sllw, rds, rs1, rs2);
}

void rvjit64_addi(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OPTIMIZE64(rds, rs1, imm);
    RVJIT_2REG_IMM_OP(rvjit64_native_addi, rds, rs1, imm);
}

void rvjit64_ori(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OPTIMIZE64(rds, rs1, imm);
    RVJIT_2REG_IMM_OP(rvjit64_native_ori, rds, rs1, imm);
}

void rvjit64_andi(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    if (rds && rs1 == RVJIT_REGISTER_ZERO) {
        regid_t hrds = rvjit_map_reg(block, rds, REG_DST);
        rvjit_native_setreg32(block, hrds, 0);
        return;
    }
    RVJIT_2REG_IMM_OP(rvjit64_native_andi, rds, rs1, imm);
}

void rvjit64_xori(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OPTIMIZE64(rds, rs1, imm);
    RVJIT_2REG_IMM_OP(rvjit64_native_xori, rds, rs1, imm);
}

void rvjit64_srai(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit64_native_srai, rds, rs1, imm);
}

void rvjit64_srli(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit64_native_srli, rds, rs1, imm);
}

void rvjit64_slli(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit64_native_slli, rds, rs1, imm);
}

void rvjit64_slti(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit64_native_slti, rds, rs1, imm);
}

void rvjit64_sltiu(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit64_native_sltiu, rds, rs1, imm);
}

void rvjit64_addiw(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OPTIMIZE64(rds, rs1, imm);
    RVJIT_2REG_IMM_OP(rvjit64_native_addiw, rds, rs1, imm);
}

void rvjit64_sraiw(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit64_native_sraiw, rds, rs1, imm);
}

void rvjit64_srliw(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit64_native_srliw, rds, rs1, imm);
}

void rvjit64_slliw(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm)
{
    RVJIT_2REG_IMM_OP(rvjit64_native_slliw, rds, rs1, imm);
}

void rvjit64_slt(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_slt, rds, rs1, rs2);
}

void rvjit64_sltu(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2)
{
    RVJIT_3REG_OP(rvjit64_native_sltu, rds, rs1, rs2);
}

void rvjit64_li(rvjit_block_t* block, regid_t rds, int32_t imm)
{
    if (rds == RVJIT_REGISTER_ZERO) return;
    regid_t hrds = rvjit_map_reg(block, rds, REG_DST);
    rvjit_native_setreg32s(block, hrds, imm);
}

void rvjit64_auipc(rvjit_block_t* block, regid_t rds, int32_t imm)
{
    if (rds == RVJIT_REGISTER_ZERO) return;
    regid_t hrds = rvjit_map_reg(block, rds, REG_DST);
    rvjit64_native_ld(block, hrds, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
    imm += block->pc_off;
    if (imm) {
        rvjit64_native_addi(block, hrds, hrds, imm);
    }
}

void rvjit64_jalr(rvjit_block_t* block, regid_t rds, regid_t rs, int32_t imm, uint8_t isize)
{
    regid_t hrs = rvjit_map_reg(block, rs, REG_SRC);
    regid_t hjmp = rvjit_claim_hreg(block);
    rvjit64_native_addi(block, hjmp, hrs, imm);
    rvjit64_auipc(block, rds, isize);
    rvjit64_native_sd(block, hjmp, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
    rvjit_free_hreg(block, hjmp);
    block->pc_off = 0;
    rvjit_emit_end(block);
}

void rvjit64_beq(rvjit_block_t* block, regid_t rs1, regid_t rs2)
{
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC);
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC);

    branch_t l1 = rvjit64_native_beq(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit64_native_beq(block, hrs1, hrs2, l1, BRANCH_TARGET);
}

void rvjit64_bne(rvjit_block_t* block, regid_t rs1, regid_t rs2)
{
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC);
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC);

    branch_t l1 = rvjit64_native_bne(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit64_native_bne(block, hrs1, hrs2, l1, BRANCH_TARGET);
}

void rvjit64_blt(rvjit_block_t* block, regid_t rs1, regid_t rs2)
{
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC);
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC);

    branch_t l1 = rvjit64_native_blt(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit64_native_blt(block, hrs1, hrs2, l1, BRANCH_TARGET);
}

void rvjit64_bge(rvjit_block_t* block, regid_t rs1, regid_t rs2)
{
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC);
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC);

    branch_t l1 = rvjit64_native_bge(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit64_native_bge(block, hrs1, hrs2, l1, BRANCH_TARGET);
}

void rvjit64_bltu(rvjit_block_t* block, regid_t rs1, regid_t rs2)
{
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC);
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC);

    branch_t l1 = rvjit64_native_bltu(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit64_native_bltu(block, hrs1, hrs2, l1, BRANCH_TARGET);
}

void rvjit64_bgeu(rvjit_block_t* block, regid_t rs1, regid_t rs2)
{
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC);
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC);

    branch_t l1 = rvjit64_native_bgeu(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit64_native_bgeu(block, hrs1, hrs2, l1, BRANCH_TARGET);
}

#endif

#if defined(RVJIT_NATIVE_64BIT) && defined(USE_RV64)

// Possibly may be optimized more
static void rvjit_tlb_lookup(rvjit_block_t* block, regid_t haddr, regid_t vaddr, int32_t offset, uint8_t moff, uint8_t align)
{
    regid_t a2 = rvjit_claim_hreg(block);
    regid_t a3 = rvjit_claim_hreg(block);
    regid_t hvaddr = rvjit_claim_hreg(block);
    regid_t hrs = rvjit_map_reg(block, vaddr, REG_SRC);

    rvjit64_native_addi(block, hvaddr, hrs, offset);
    rvjit64_native_srli(block, a3, hvaddr, 12);
    rvjit64_native_andi(block, a2, a3, VM_TLB_MASK);
    rvjit64_native_slli(block, a2, a2, VM_TLB_SHIFT);
    rvjit64_native_add(block, a2, a2, VM_PTR_REG);
    rvjit64_native_ld(block, haddr, a2, VM_TLB_OFFSET + moff);
    if (align > 1) {
        rvjit64_native_xor(block, haddr, haddr, a3);
        rvjit64_native_andi(block, a3, hvaddr, (align - 1));
        rvjit64_native_or(block, a3, a3, haddr);
    } else {
        rvjit64_native_xor(block, a3, a3, haddr);
    }
    branch_t l1 = rvjit64_native_beqz(block, a3, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit64_native_beqz(block, a3, l1, BRANCH_TARGET);
    rvjit64_native_ld(block, haddr, a2, VM_TLB_OFFSET);
    rvjit64_native_add(block, haddr, haddr, hvaddr);

    rvjit_free_hreg(block, a2);
    rvjit_free_hreg(block, a3);
    rvjit_free_hreg(block, hvaddr);
}

#else

static void rvjit_tlb_lookup(rvjit_block_t* block, regid_t haddr, regid_t vaddr, int32_t offset, uint8_t moff, uint8_t align)
{
    regid_t a2 = rvjit_claim_hreg(block);
    regid_t a3 = rvjit_claim_hreg(block);
    regid_t hvaddr = rvjit_claim_hreg(block);
    regid_t hrs = rvjit_map_reg(block, vaddr, REG_SRC);

    rvjit32_native_addi(block, hvaddr, hrs, offset);
    rvjit32_native_srli(block, a3, hvaddr, 12);
    rvjit32_native_andi(block, a2, a3, VM_TLB_MASK);
    rvjit32_native_slli(block, a2, a2, VM_TLB_SHIFT);
#ifdef RVJIT_NATIVE_64BIT
    rvjit64_native_add(block, a2, a2, VM_PTR_REG);
#else
    rvjit32_native_add(block, a2, a2, VM_PTR_REG);
#endif
    rvjit32_native_lw(block, haddr, a2, VM_TLB_OFFSET + moff);
    if (align > 1) {
        rvjit32_native_xor(block, haddr, haddr, a3);
        rvjit32_native_andi(block, a3, hvaddr, (align - 1));
        rvjit32_native_or(block, a3, a3, haddr);
    } else {
        rvjit32_native_xor(block, a3, a3, haddr);
    }
    branch_t l1 = rvjit32_native_beqz(block, a3, BRANCH_NEW, BRANCH_ENTRY);

    rvjit_emit_end(block);

    rvjit32_native_beqz(block, a3, l1, BRANCH_TARGET);
#ifdef RVJIT_NATIVE_64BIT
    rvjit64_native_ld(block, haddr, a2, VM_TLB_OFFSET);
    rvjit64_native_add(block, haddr, haddr, hvaddr);
#else
    rvjit32_native_lw(block, haddr, a2, VM_TLB_OFFSET);
    rvjit32_native_add(block, haddr, haddr, hvaddr);
#endif

    rvjit_free_hreg(block, a2);
    rvjit_free_hreg(block, a3);
    rvjit_free_hreg(block, hvaddr);
}

#endif

void rvjit32_sb(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_W, 1);
    regid_t hdest = rvjit_map_reg(block, src, REG_SRC);
    rvjit32_native_sb(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit32_lb(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_R, 1);
    regid_t hdest = rvjit_map_reg(block, dest, REG_DST);
    rvjit32_native_lb(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit32_lbu(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_R, 1);
    regid_t hdest = rvjit_map_reg(block, dest, REG_DST);
    rvjit32_native_lbu(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit32_sh(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_W, 2);
    regid_t hdest = rvjit_map_reg(block, src, REG_SRC);
    rvjit32_native_sh(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit32_lh(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_R, 2);
    regid_t hdest = rvjit_map_reg(block, dest, REG_DST);
    rvjit32_native_lh(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit32_lhu(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_R, 2);
    regid_t hdest = rvjit_map_reg(block, dest, REG_DST);
    rvjit32_native_lhu(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit32_sw(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_W, 4);
    regid_t hdest = rvjit_map_reg(block, src, REG_SRC);
    rvjit32_native_sw(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit32_lw(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_R, 4);
    regid_t hdest = rvjit_map_reg(block, dest, REG_DST);
    rvjit32_native_lw(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

#ifdef RVJIT_NATIVE_64BIT

void rvjit64_sb(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_W, 1);
    regid_t hdest = rvjit_map_reg(block, src, REG_SRC);
    rvjit64_native_sb(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit64_lb(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_R, 1);
    regid_t hdest = rvjit_map_reg(block, dest, REG_DST);
    rvjit64_native_lb(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit64_lbu(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_R, 1);
    regid_t hdest = rvjit_map_reg(block, dest, REG_DST);
    rvjit64_native_lbu(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit64_sh(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_W, 2);
    regid_t hdest = rvjit_map_reg(block, src, REG_SRC);
    rvjit64_native_sh(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit64_lh(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_R, 2);
    regid_t hdest = rvjit_map_reg(block, dest, REG_DST);
    rvjit64_native_lh(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit64_lhu(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_R, 2);
    regid_t hdest = rvjit_map_reg(block, dest, REG_DST);
    rvjit64_native_lhu(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit64_sw(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_W, 4);
    regid_t hdest = rvjit_map_reg(block, src, REG_SRC);
    rvjit64_native_sw(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit64_lw(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_R, 4);
    regid_t hdest = rvjit_map_reg(block, dest, REG_DST);
    rvjit64_native_lw(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit64_lwu(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_R, 4);
    regid_t hdest = rvjit_map_reg(block, dest, REG_DST);
    rvjit64_native_lwu(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit64_sd(rvjit_block_t* block, regid_t src, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_W, 8);
    regid_t hdest = rvjit_map_reg(block, src, REG_SRC);
    rvjit64_native_sd(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

void rvjit64_ld(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset)
{
    regid_t haddr = rvjit_claim_hreg(block);
    rvjit_tlb_lookup(block, haddr, vaddr, offset, VM_TLB_R, 8);
    regid_t hdest = rvjit_map_reg(block, dest, REG_DST);
    rvjit64_native_ld(block, hdest, haddr, 0);
    rvjit_free_hreg(block, haddr);
}

#endif
