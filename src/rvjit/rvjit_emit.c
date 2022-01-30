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
#elif  RVJIT_ARM64
#include "rvjit_arm64.h"
#elif  RVJIT_ARM
#include "rvjit_arm.h"
#endif

#define REG_SRC    0x1
#define REG_DST    0x2
#define REG_AUIPC  0x4

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

    if (flags & REG_DST) {
        block->regs[greg].flags |= REG_DIRTY;
        block->regs[greg].flags &= ~REG_AUIPC;
    }
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

static void rvjit_link_block(rvjit_block_t* block)
{
#ifdef RVJIT_NATIVE_LINKER
    paddr_t dest = block->phys_pc + block->pc_off;
    size_t exit_ptr = (size_t)(block->heap.data + block->heap.curr + block->size);
    size_t dest_block;
    if (dest == block->phys_pc) {
        dest_block = (size_t)(block->heap.data + block->heap.curr);
    } else {
        dest_block = hashmap_get(&block->heap.blocks, dest);
    }

    if ((dest >> 12) == (block->phys_pc >> 12)) {
        if (dest_block) {
            rvjit_tail_bnez(block, VM_PTR_REG, dest_block - exit_ptr);
            //rvjit_tail_jmp(block, dest_block - exit_ptr);
        } else if (dest) {
            rvjit_patchable_ret(block);
            vector_emplace_back(block->links);
            vector_at(block->links, vector_size(block->links) - 1).dest = dest;
            vector_at(block->links, vector_size(block->links) - 1).ptr = exit_ptr;
            return;
        }
    }
#endif
    rvjit_native_ret(block);
}

void rvjit_linker_patch_jmp(void* addr, int32_t offset)
{
#ifdef RVJIT_NATIVE_LINKER
    rvjit_patch_jmp(addr, offset);
#else
    UNUSED(addr);
    UNUSED(offset);
#endif
}

void rvjit_linker_patch_ret(void* addr)
{
#ifdef RVJIT_NATIVE_LINKER
    rvjit_patch_ret(addr);
#else
    UNUSED(addr);
#endif
}

void rvjit_emit_end(rvjit_block_t* block, bool link)
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

    if (link) {
        rvjit_link_block(block);
    } else {
        rvjit_native_ret(block);
    }

    block->hreg_mask = hreg_mask;
    block->abireclaim_mask = abireclaim_mask;
}

/*
 * Important: REG_DST (destination) registers should be mapped at the end,
 * otherwise nasty errors occur, this simplifies register remapping
 */

/*
 * ALU Register-Register intrinsics
 */

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
#define RVJIT32_IMM_INC_OPTIMIZE(rds, rs1, imm) \
    if (rds != RVJIT_REGISTER_ZERO && rs1 == RVJIT_REGISTER_ZERO) { \
        regid_t hrds = rvjit_map_reg(block, rds, REG_DST); \
        rvjit_native_setreg32(block, hrds, imm); \
        return; \
    }

#define RVJIT64_IMM_INC_OPTIMIZE(rds, rs1, imm) \
    if (rds != RVJIT_REGISTER_ZERO && rs1 == RVJIT_REGISTER_ZERO) { \
        regid_t hrds = rvjit_map_reg(block, rds, REG_DST); \
        rvjit_native_setreg32s(block, hrds, imm); \
        return; \
    }

#define RVJIT_IMM_ZERO_OPTIMIZE(rds, rs1, imm) \
    if (rds != RVJIT_REGISTER_ZERO && rs1 == RVJIT_REGISTER_ZERO) { \
        regid_t hrds = rvjit_map_reg(block, rds, REG_DST); \
        rvjit_native_zero_reg(block, hrds); \
        return; \
    }

#define RVJIT32_3REG(instr) \
void rvjit32_##instr(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2) \
{ \
    RVJIT_3REG_OP(rvjit32_native_##instr, rds, rs1, rs2); \
}

#ifdef RVJIT_NATIVE_64BIT
#define RVJIT64_3REG(instr) \
void rvjit64_##instr(rvjit_block_t* block, regid_t rds, regid_t rs1, regid_t rs2) \
{ \
    RVJIT_3REG_OP(rvjit64_native_##instr, rds, rs1, rs2); \
}
#else
#define RVJIT64_3REG(instr)
#endif

#define RVJIT_3REG(instr) \
RVJIT32_3REG(instr) \
RVJIT64_3REG(instr)

/*
 * ALU Register-Immediate intrinsics
 */

#define RVJIT32_IMM_INC(instr) \
void rvjit32_##instr(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm) \
{ \
    RVJIT32_IMM_INC_OPTIMIZE(rds, rs1, imm); \
    RVJIT_2REG_IMM_OP(rvjit32_native_##instr, rds, rs1, imm); \
}

#ifdef RVJIT_NATIVE_64BIT
#define RVJIT64_IMM_INC(instr) \
void rvjit64_##instr(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm) \
{ \
    RVJIT64_IMM_INC_OPTIMIZE(rds, rs1, imm); \
    RVJIT_2REG_IMM_OP(rvjit64_native_##instr, rds, rs1, imm); \
}
#else
#define RVJIT64_IMM_INC(instr)
#endif

#define RVJIT_IMM_INC(instr) \
RVJIT32_IMM_INC(instr) \
RVJIT64_IMM_INC(instr)

#define RVJIT32_IMM(instr) \
void rvjit32_##instr(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm) \
{ \
    RVJIT_IMM_ZERO_OPTIMIZE(rds, rs1, imm); \
    RVJIT_2REG_IMM_OP(rvjit32_native_##instr, rds, rs1, imm); \
}

#ifdef RVJIT_NATIVE_64BIT
#define RVJIT64_IMM(instr) \
void rvjit64_##instr(rvjit_block_t* block, regid_t rds, regid_t rs1, int32_t imm) \
{ \
    RVJIT_IMM_ZERO_OPTIMIZE(rds, rs1, imm); \
    RVJIT_2REG_IMM_OP(rvjit64_native_##instr, rds, rs1, imm); \
}
#else
#define RVJIT64_IMM(instr)
#endif

#define RVJIT_IMM(instr) \
RVJIT32_IMM(instr) \
RVJIT64_IMM(instr)

/*
 * Branch intrinsics
 */

#define RVJIT32_BRANCH(instr) \
void rvjit32_##instr(rvjit_block_t* block, regid_t rs1, regid_t rs2) \
{ \
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC); \
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC); \
    branch_t l1 = rvjit32_native_##instr(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY); \
    rvjit_emit_end(block, true); \
    rvjit32_native_##instr(block, hrs1, hrs2, l1, BRANCH_TARGET); \
}

#ifdef RVJIT_NATIVE_64BIT
#define RVJIT64_BRANCH(instr) \
void rvjit64_##instr(rvjit_block_t* block, regid_t rs1, regid_t rs2) \
{ \
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC); \
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC); \
    branch_t l1 = rvjit64_native_##instr(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY); \
    rvjit_emit_end(block, true); \
    rvjit64_native_##instr(block, hrs1, hrs2, l1, BRANCH_TARGET); \
}
#else
#define RVJIT64_BRANCH(instr)
#endif

#define RVJIT_BRANCH(instr) \
RVJIT32_BRANCH(instr) \
RVJIT64_BRANCH(instr)

RVJIT_3REG(add)
RVJIT_3REG(sub)
RVJIT_3REG(or)
RVJIT_3REG(and)
RVJIT_3REG(xor)
RVJIT_3REG(sra)
RVJIT_3REG(srl)
RVJIT_3REG(sll)
RVJIT_3REG(slt)
RVJIT_3REG(sltu)
RVJIT_3REG(mul)
RVJIT_3REG(mulh)
RVJIT_3REG(mulhu)
RVJIT_3REG(mulhsu)
RVJIT_3REG(div)
RVJIT_3REG(divu)
RVJIT_3REG(rem)
RVJIT_3REG(remu)

RVJIT_IMM_INC(addi)
RVJIT_IMM_INC(ori)
RVJIT_IMM_INC(xori)

RVJIT_IMM(andi)
RVJIT_IMM(srai)
RVJIT_IMM(srli)
RVJIT_IMM(slli)
RVJIT_IMM(slti)
RVJIT_IMM(sltiu)

RVJIT64_3REG(addw)
RVJIT64_3REG(subw)
RVJIT64_3REG(sraw)
RVJIT64_3REG(srlw)
RVJIT64_3REG(sllw)
RVJIT64_3REG(mulw)
RVJIT64_3REG(divw)
RVJIT64_3REG(divuw)
RVJIT64_3REG(remw)
RVJIT64_3REG(remuw)

RVJIT64_IMM_INC(addiw)
RVJIT64_IMM(sraiw)
RVJIT64_IMM(srliw)
RVJIT64_IMM(slliw)

RVJIT_BRANCH(beq)
RVJIT_BRANCH(bne)
RVJIT_BRANCH(blt)
RVJIT_BRANCH(bge)
RVJIT_BRANCH(bltu)
RVJIT_BRANCH(bgeu)

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
    block->regs[rds].flags |= REG_AUIPC;
    block->regs[rds].auipc_off = imm;
}

void rvjit32_jalr(rvjit_block_t* block, regid_t rds, regid_t rs, int32_t imm, uint8_t isize)
{
    regid_t hrs = rvjit_map_reg(block, rs, REG_SRC);
    regid_t hjmp = rvjit_claim_hreg(block);
    rvjit32_native_addi(block, hjmp, hrs, imm);
    if (rds != RVJIT_REGISTER_ZERO) {
        int32_t new_imm = block->pc_off + isize;
        regid_t hrds = rvjit_map_reg(block, rds, REG_DST);
        rvjit32_native_lw(block, hrds, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
        if (new_imm) {
            rvjit32_native_addi(block, hrds, hrds, new_imm);
        }
    }

    if (block->regs[rs].flags & REG_AUIPC) {
        block->pc_off = block->regs[rs].auipc_off + imm;
        block->linkage = true;
    } else {
        block->pc_off = 0;
        block->linkage = false;
        rvjit32_native_sw(block, hjmp, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
    }

    rvjit_free_hreg(block, hjmp);
}

// RV64

#ifdef RVJIT_NATIVE_64BIT

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
    block->regs[rds].flags |= REG_AUIPC;
    block->regs[rds].auipc_off = imm;
}

void rvjit64_jalr(rvjit_block_t* block, regid_t rds, regid_t rs, int32_t imm, uint8_t isize)
{
    regid_t hrs = rvjit_map_reg(block, rs, REG_SRC);
    regid_t hjmp = rvjit_claim_hreg(block);
    rvjit64_native_addi(block, hjmp, hrs, imm);
    if (rds != RVJIT_REGISTER_ZERO) {
        int32_t new_imm = block->pc_off + isize;
        regid_t hrds = rvjit_map_reg(block, rds, REG_DST);
        rvjit64_native_ld(block, hrds, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
        if (new_imm) {
            rvjit64_native_addi(block, hrds, hrds, new_imm);
        }
    }

    if (block->regs[rs].flags & REG_AUIPC) {
        block->pc_off = block->regs[rs].auipc_off + imm;
        block->linkage = true;
    } else {
        block->pc_off = 0;
        block->linkage = false;
        rvjit64_native_sd(block, hjmp, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
    }

    rvjit_free_hreg(block, hjmp);
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

    rvjit_emit_end(block, false);

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

    rvjit_emit_end(block, false);

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

/*
 * Load/store intrinsics
 */

#define RVJIT32_LDST(instr, align, store) \
void rvjit32_##instr(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset) \
{ \
    regid_t haddr = rvjit_claim_hreg(block); \
    rvjit_tlb_lookup(block, haddr, vaddr, offset, store ? VM_TLB_W : VM_TLB_R, align); \
    regid_t hdest = rvjit_map_reg(block, dest, store ? REG_SRC : REG_DST); \
    rvjit32_native_##instr(block, hdest, haddr, 0); \
    rvjit_free_hreg(block, haddr); \
}

#ifdef RVJIT_NATIVE_64BIT
#define RVJIT64_LDST(instr, align, store) \
void rvjit64_##instr(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset) \
{ \
    regid_t haddr = rvjit_claim_hreg(block); \
    rvjit_tlb_lookup(block, haddr, vaddr, offset, store ? VM_TLB_W : VM_TLB_R, align); \
    regid_t hdest = rvjit_map_reg(block, dest, store ? REG_SRC : REG_DST); \
    rvjit64_native_##instr(block, hdest, haddr, 0); \
    rvjit_free_hreg(block, haddr); \
}
#else
#define RVJIT64_LDST(instr, align, store)
#endif

#define RVJIT_LDST(instr, align, store) \
RVJIT32_LDST(instr, align, store) \
RVJIT64_LDST(instr, align, store)

RVJIT_LDST(lb,    1, false)
RVJIT_LDST(lbu,   1, false)
RVJIT_LDST(lh,    2, false)
RVJIT_LDST(lhu,   2, false)
RVJIT_LDST(lw,    4, false)
RVJIT64_LDST(lwu, 4, false)
RVJIT64_LDST(ld,  8, false)

RVJIT_LDST(sb,   1, true)
RVJIT_LDST(sh,   2, true)
RVJIT_LDST(sw,   4, true)
RVJIT64_LDST(sd, 8, true)
