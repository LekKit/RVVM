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
#include "bit_ops.h"

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

#define VM_REG_OFFSET(reg) (offsetof(rvvm_hart_t, registers) + (sizeof(maxlen_t) * reg))
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

static regid_t rvjit_reclaim_hreg(rvjit_block_t* block)
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

static inline regid_t rvjit_try_claim_hreg(rvjit_block_t* block)
{
    if (block->hreg_mask) {
        regid_t reg = bit_clz32(block->hreg_mask) ^ 31;
        block->hreg_mask &= ~rvjit_hreg_mask(reg);
        return reg;
    }
    return REG_ILL;
}

regid_t rvjit_claim_hreg(rvjit_block_t* block)
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
    if (unlikely(greg >= RVJIT_REGISTERS)) {
        rvvm_fatal("Mapped RVJIT register is out of range!");
        return REG_ILL;
    }
#if defined(RVJIT_RISCV)
    if (greg == RVJIT_REGISTER_ZERO) return 0;
#elif defined(RVJIT_ARM64)
    if (greg == RVJIT_REGISTER_ZERO) return 31;
#endif
    if (block->regs[greg].hreg == REG_ILL) {
        regid_t hreg = rvjit_claim_hreg(block);
        block->regs[greg].hreg = hreg;
        block->regs[greg].flags = 0;
    }
    block->regs[greg].last_used = block->size;
#if !defined(RVJIT_RISCV) && !defined(RVJIT_ARM64)
    if (greg == RVJIT_REGISTER_ZERO) {
        if (!(block->regs[greg].flags & REG_LOADED) || (block->regs[greg].flags & REG_DIRTY)) {
            rvjit_native_zero_reg(block, block->regs[greg].hreg);
        }
        block->regs[greg].flags = REG_LOADED;
    }
#endif

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
#ifdef RVJIT_X86
    rvjit_x86_memref_addi(block, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]), block->pc_off, block->rv64);
#else
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
#endif
}

//#define RVJIT_LOOKUP_TAILCALL

#ifdef RVJIT_LOOKUP_TAILCALL

#include "riscv_mmu.h"
#include "riscv_cpu.h"

/*
 * It's possible to offload the lookup to C code and call it from JIT.
 * This is shorter as well (~1% of JIT cache used for lookups vs ~5%),
 * and allows hashmap lookups which might be beneficial,
 * but it's slightly slower for hot calls.
 *
 * ! Experimental stuff !
 */

RVJIT_CALL static void rvjit_tail_lookup(rvvm_hart_t* vm)
{
    size_t pc, tpc, entry, phys_pc;
    rvjit_func_t block;
    pc = vm->registers[REGISTER_PC];
    entry = (pc >> 1) & (TLB_SIZE - 1);
    tpc = vm->jtlb[entry].pc;
    if (likely(vm->wait_event)) {
        if (false && likely(pc == tpc)) {
            // This *should* optimize into tail call,
            // but that's not guaranteed...
            vm->jtlb[entry].block(vm);
        } else {
            vmptr_t ptr = riscv_vma_translate_e(vm, pc);
            if (ptr) {
                phys_pc = (size_t)(ptr - vm->mem.data) + vm->mem.begin;
                block = rvjit_block_lookup(&vm->jit, phys_pc);
                if (block) {
                    vm->jtlb[entry].pc = pc;
                    vm->jtlb[entry].block = block;
                    block(vm);
                }
            }
        }
    }
}

#endif

static void rvjit_lookup_block(rvjit_block_t* block)
{
#ifdef RVJIT_NATIVE_LINKER

#ifdef RVJIT_LOOKUP_TAILCALL
    regid_t reg = rvjit_claim_hreg(block);
    rvjit_native_setregw(block, reg, (size_t)rvjit_tail_lookup);
    rvjit_jmp_reg(block, reg);
    rvjit_free_hreg(block, reg);
#elif defined(RVJIT_X86) && defined(RVJIT_NATIVE_64BIT)
/*
 * For future reference:
 * Hand-optimized inline lookup for x86_64 and i386, uses memrefs and other CISC stuff.
 * Since i386 doesn't have enough registers for RVJIT IR lookup, this is the only way there.
 *
    mov rdx, QWORD PTR [rdi+0x108]
    mov eax, edx
    sal eax, 3
    and eax, 0xff0
    add rax, rdi
    cmp QWORD PTR [rax+0x2220], rdx
    jne L1
    cmp DWORD PTR [rdi], 0
    je  L1
    jmp QWORD PTR [rax+0x2218]
    L1:
    ret
 * While RVJIT would compile the IR to:
    mov rax, QWORD PTR [rdi+0x108]
    mov ecx, eax
    shl ecx, 0x3
    and ecx, 0xff0
    add rcx, rdi
    mov rdx, QWORD PTR [rcx+0x2220]
    cmp rdx, rax
    jne L1
    mov edx, DWORD PTR [rdi]
    cmp edx, 0x0
    je  L1
    mov rax, QWORD PTR [rcx+0x2218]
    jmp rax
    L1:
    ret
 */
    uint8_t code[41] = {0x48, 0x8B, 0x90, 0x08, 0x01, 0x00, 0x00, 0x89,
        0xD0, 0xC1, 0xE0, 0x03, 0x25, 0xF0, 0x0F, 0x00, 0x00, 0x48, 0x01,
        0xC0, 0x48, 0x39, 0x90, 0x20, 0x22, 0x00, 0x00, 0x75, 0x0B, 0x83,
        0x38, 0x00, 0x74, 0x06, 0xFF, 0xA0, 0x18, 0x22, 0x00, 0x00, 0xC3};
    code[2] |= VM_PTR_REG;
    code[19] |= (VM_PTR_REG << 3);
    code[30] |= VM_PTR_REG;
    write_uint32_le_m(code + 3, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
    code[11] = VM_TLB_SHIFT - 2;
    write_uint32_le_m(code + 13, VM_TLB_MASK << (VM_TLB_SHIFT - 1));
    write_uint32_le_m(code + 23, offsetof(rvvm_hart_t, jtlb) + offsetof(rvvm_jtlb_entry_t, pc));
    write_uint32_le_m(code + 36, offsetof(rvvm_hart_t, jtlb) + offsetof(rvvm_jtlb_entry_t, block));
    rvjit_put_code(block, code, sizeof(code));
#elif defined(RVJIT_X86) && defined(RVJIT_ABI_FASTCALL)
    uint8_t code[38] = {0x8B, 0x91, 0x04, 0x01, 0x00, 0x00, 0x89, 0xD0,
        0xC1, 0xE0, 0x03, 0x25, 0xF0, 0x0F, 0x00, 0x00, 0x01, 0xC8, 0x39,
        0x90, 0x1C, 0x22, 0x00, 0x00, 0x75, 0x0B, 0x83, 0x39, 0x00, 0x74,
        0x06, 0xFF, 0xA0, 0x14, 0x22, 0x00, 0x00, 0xC3};
    write_uint32_le_m(code + 2, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
    code[10] = VM_TLB_SHIFT - 2;
    write_uint32_le_m(code + 12, VM_TLB_MASK << (VM_TLB_SHIFT - 1));
    write_uint32_le_m(code + 20, offsetof(rvvm_hart_t, jtlb) + offsetof(rvvm_jtlb_entry_t, pc));
    write_uint32_le_m(code + 33, offsetof(rvvm_hart_t, jtlb) + offsetof(rvvm_jtlb_entry_t, block));
    rvjit_put_code(block, code, sizeof(code));
#else
    regid_t pc = rvjit_try_claim_hreg(block);
    regid_t tpc = rvjit_try_claim_hreg(block);
    regid_t cpc = rvjit_try_claim_hreg(block);

    static bool allow_ir_lookup = true;
    if (!allow_ir_lookup || pc == REG_ILL || tpc == REG_ILL || cpc == REG_ILL) {
        if (allow_ir_lookup) {
            allow_ir_lookup = false;
            // This is usually the case on i386
            rvvm_warn("Insufficient RVJIT registers for IR-based block lookup");
        }
        rvjit_native_ret(block);
        return;
    }

#if defined(RVJIT_NATIVE_64BIT) && defined(USE_RV64)
    rvjit64_native_ld(block, pc, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
#else
    rvjit32_native_lw(block, pc, VM_PTR_REG, offsetof(rvvm_hart_t, registers[REGISTER_PC]));
#endif

#if defined(RVJIT_X86) || defined(RVJIT_ARM64)
    // x86 & ARM64 can carry big mask immediate without spilling
    rvjit32_native_slli(block, tpc, pc, VM_TLB_SHIFT - 2);
    rvjit32_native_andi(block, tpc, tpc, VM_TLB_MASK << (VM_TLB_SHIFT - 1));
#else
    rvjit32_native_srli(block, tpc, pc, 1);
    rvjit32_native_andi(block, tpc, tpc, VM_TLB_MASK);
    rvjit32_native_slli(block, tpc, tpc, VM_TLB_SHIFT - 1);
#endif

#ifdef RVJIT_NATIVE_64BIT
    rvjit64_native_add(block, tpc, tpc, VM_PTR_REG);
#else
    rvjit32_native_add(block, tpc, tpc, VM_PTR_REG);
#endif
#if defined(RVJIT_NATIVE_64BIT) && defined(USE_RV64)
    rvjit64_native_ld(block, cpc, tpc, offsetof(rvvm_hart_t, jtlb) + offsetof(rvvm_jtlb_entry_t, pc));
    branch_t l1 = rvjit64_native_bne(block, cpc, pc, BRANCH_NEW, false);
#else
    rvjit32_native_lw(block, cpc, tpc, offsetof(rvvm_hart_t, jtlb) + offsetof(rvvm_jtlb_entry_t, pc));
    branch_t l1 = rvjit32_native_bne(block, cpc, pc, BRANCH_NEW, false);
#endif
    rvjit32_native_lw(block, cpc, VM_PTR_REG, 0);
    branch_t l2 = rvjit32_native_beqz(block, cpc, BRANCH_NEW, false);
#ifdef RVJIT_NATIVE_64BIT
    rvjit64_native_ld(block, pc, tpc, offsetof(rvvm_hart_t, jtlb) + offsetof(rvvm_jtlb_entry_t, block));
#else
    rvjit32_native_lw(block, pc, tpc, offsetof(rvvm_hart_t, jtlb) + offsetof(rvvm_jtlb_entry_t, block));
#endif
    rvjit_jmp_reg(block, pc);
#if defined(RVJIT_NATIVE_64BIT) && defined(USE_RV64)
    rvjit64_native_bne(block, cpc, pc, l1, true);
#else
    rvjit32_native_bne(block, cpc, pc, l1, true);
#endif
    rvjit32_native_beqz(block, cpc, l2, true);
    rvjit_native_ret(block);

    rvjit_free_hreg(block, pc);
    rvjit_free_hreg(block, tpc);
    rvjit_free_hreg(block, cpc);
#endif

#else
    rvjit_native_ret(block);
#endif
}

static void rvjit_link_block(rvjit_block_t* block)
{
#ifdef RVJIT_NATIVE_LINKER
    phys_addr_t next_pc = block->phys_pc + block->pc_off;
    size_t exit_ptr = (size_t)(block->heap.data + block->heap.curr + block->size);
    size_t next_block;
    if (next_pc == block->phys_pc) {
        next_block = (size_t)(block->heap.data + block->heap.curr);
    } else {
        next_block = hashmap_get(&block->heap.blocks, next_pc);
        if (next_block && block->heap.code) {
            next_block += (size_t)(block->heap.data) - (size_t)(block->heap.code);
        }
    }

    if ((next_pc >> 12) == (block->phys_pc >> 12)) {
        if (next_block) {
            rvjit_tail_bnez(block, VM_PTR_REG, next_block - exit_ptr);
            //rvjit_tail_jmp(block, next_block - exit_ptr);
        } else {
            rvjit_patchable_ret(block);
            vector_emplace_back(block->links);
            vector_at(block->links, vector_size(block->links) - 1).dest = next_pc;
            vector_at(block->links, vector_size(block->links) - 1).ptr = exit_ptr;
            return;
        }
    } else {
        rvjit_lookup_block(block);
        return;
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

void rvjit_emit_end(rvjit_block_t* block, uint8_t linkage)
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

    switch (linkage) {
        case LINKAGE_JMP:
            rvjit_link_block(block);
            break;
        case LINKAGE_TAIL:
            rvjit_lookup_block(block);
            break;
        default:
            rvjit_native_ret(block);
            break;
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
    rvjit_emit_end(block, LINKAGE_JMP); \
    rvjit32_native_##instr(block, hrs1, hrs2, l1, BRANCH_TARGET); \
}

#ifdef RVJIT_NATIVE_64BIT
#define RVJIT64_BRANCH(instr) \
void rvjit64_##instr(rvjit_block_t* block, regid_t rs1, regid_t rs2) \
{ \
    regid_t hrs1 = rvjit_map_reg(block, rs1, REG_SRC); \
    regid_t hrs2 = rvjit_map_reg(block, rs2, REG_SRC); \
    branch_t l1 = rvjit64_native_##instr(block, hrs1, hrs2, BRANCH_NEW, BRANCH_ENTRY); \
    rvjit_emit_end(block, LINKAGE_JMP); \
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
        block->linkage = LINKAGE_JMP;
    } else {
        block->pc_off = 0;
        block->linkage = LINKAGE_TAIL;
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
        block->linkage = LINKAGE_JMP;
    } else {
        block->pc_off = 0;
        block->linkage = LINKAGE_TAIL;
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
    rvjit32_native_slli(block, a2, a2, VM_TLB_SHIFT);
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

    rvjit_emit_end(block, LINKAGE_NONE);

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

    rvjit_emit_end(block, LINKAGE_NONE);

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
    if (block->native_ptrs) { \
        regid_t haddr = rvjit_map_reg(block, vaddr, REG_SRC); \
        regid_t hdest = rvjit_map_reg(block, dest, store ? REG_SRC : REG_DST); \
        rvjit32_native_##instr(block, hdest, haddr, offset); \
    } else { \
        regid_t haddr = rvjit_claim_hreg(block); \
        rvjit_tlb_lookup(block, haddr, vaddr, offset, store ? VM_TLB_W : VM_TLB_R, align); \
        regid_t hdest = rvjit_map_reg(block, dest, store ? REG_SRC : REG_DST); \
        rvjit32_native_##instr(block, hdest, haddr, 0); \
        rvjit_free_hreg(block, haddr); \
    } \
}

#ifdef RVJIT_NATIVE_64BIT
#define RVJIT64_LDST(instr, align, store) \
void rvjit64_##instr(rvjit_block_t* block, regid_t dest, regid_t vaddr, int32_t offset) \
{ \
    if (block->native_ptrs) { \
        regid_t haddr = rvjit_map_reg(block, vaddr, REG_SRC); \
        regid_t hdest = rvjit_map_reg(block, dest, store ? REG_SRC : REG_DST); \
        rvjit64_native_##instr(block, hdest, haddr, offset); \
    } else { \
        regid_t haddr = rvjit_claim_hreg(block); \
        rvjit_tlb_lookup(block, haddr, vaddr, offset, store ? VM_TLB_W : VM_TLB_R, align); \
        regid_t hdest = rvjit_map_reg(block, dest, store ? REG_SRC : REG_DST); \
        rvjit64_native_##instr(block, hdest, haddr, 0); \
        rvjit_free_hreg(block, haddr); \
    } \
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
