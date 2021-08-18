/*
riscv_mmu.h - RISC-V Memory Mapping Unit
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

#ifndef RISCV_MMU_H
#define RISCV_MMU_H

#include "rvvm.h"
#include "compiler.h"
#include "mem_ops.h"

#define MMU_VALID_PTE     0x1
#define MMU_READ          0x2
#define MMU_WRITE         0x4
#define MMU_EXEC          0x8
#define MMU_LEAF_PTE      0xA
#define MMU_USER_USABLE   0x10
#define MMU_GLOBAL_MAP    0x20
#define MMU_PAGE_ACCESSED 0x40
#define MMU_PAGE_DIRTY    0x80

#define PAGE_SHIFT        12
#define PAGE_MASK         0xFFF
#define PAGE_SIZE         0x1000
#define PAGE_PNMASK       (~0xFFFULL)

#define TLB_MASK          (TLB_SIZE-1)
#define TLB_VADDR(vaddr)  vaddr
//#define TLB_VADDR(vaddr)  (vaddr & PAGE_MASK) // we may remove vaddr offset if needed

// Init physical memory (be careful to not overlap MMIO regions!)
bool riscv_init_ram(rvvm_ram_t* mem, paddr_t begin, paddr_t size);
void riscv_free_ram(rvvm_ram_t* mem);

// Flush the TLB (on context switch, SFENCE.VMA, etc)
void riscv_tlb_flush(rvvm_hart_t* vm);
void riscv_tlb_flush_page(rvvm_hart_t* vm, vaddr_t addr);

/*
 * Non-inlined slow memory operations, perform MMU translation,
 * call MMIO handlers if needed.
 */

// Translate virtual address into VM pointer (only for physical memory)
NOINLINE vmptr_t riscv_mmu_vma_translate(rvvm_hart_t* vm, vaddr_t addr, uint8_t access);

// Fetch instruction from virtual address
NOINLINE bool riscv_mmu_fetch_inst(rvvm_hart_t* vm, vaddr_t addr, uint32_t* inst);

// Load/store operations on virtual address (also used by JIT)
NOINLINE void riscv_mmu_load_u64(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
NOINLINE void riscv_mmu_load_u32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
NOINLINE void riscv_mmu_load_s32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
NOINLINE void riscv_mmu_load_u16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
NOINLINE void riscv_mmu_load_s16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
NOINLINE void riscv_mmu_load_u8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
NOINLINE void riscv_mmu_load_s8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);

NOINLINE void riscv_mmu_store_u64(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
NOINLINE void riscv_mmu_store_u32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
NOINLINE void riscv_mmu_store_u16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
NOINLINE void riscv_mmu_store_u8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);

#ifdef USE_FPU
NOINLINE void riscv_mmu_load_double(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
NOINLINE void riscv_mmu_load_float(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);

NOINLINE void riscv_mmu_store_double(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
NOINLINE void riscv_mmu_store_float(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
#endif

// Alignment checks / fixup

static inline bool riscv_block_in_page(addr_t addr, size_t size)
{
    return (addr & PAGE_MASK) + size <= PAGE_SIZE;
}

static inline bool riscv_block_aligned(addr_t addr, size_t size)
{
    return (addr & (size - 1)) == 0;
}

static inline addr_t riscv_align_addr(addr_t addr, size_t size)
{
    return addr & (~(addr_t)(size - 1));
}

/*
 * Inlined TLB-cached memory operations (used for performance)
 * Fall back to MMU functions if:
 *     Address is not TLB-cached (TLB miss/protection fault)
 *     Address misalign (optimized on hosts without misalign)
 *     MMIO is accessed (since MMIO regions aren't memory)
 */

static inline bool riscv_fetch_inst(rvvm_hart_t* vm, vaddr_t addr, uint32_t* inst)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].e == vpn)) {
        *inst = read_uint16_le_m(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr));
        if ((*inst & 0x3) == 0x3) {
            // This is a 4-byte instruction, force tlb lookup again
            vpn = (addr + 2) >> PAGE_SHIFT;
            if (likely(vm->tlb[vpn & TLB_MASK].e == vpn)) {
                *inst |= ((uint32_t)read_uint16_le_m(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr + 2))) << 16;
                return true;
            }
        } else return true;
    }
    return riscv_mmu_fetch_inst(vm, addr, inst);
}

// VM Address translation (translated within single page bounds)
// May transparently swap pages to persistent storage

static inline vmptr_t riscv_vma_translate_r(rvvm_hart_t* vm, vaddr_t addr)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn)) {
        return vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr);
    }
    return riscv_mmu_vma_translate(vm, addr, MMU_READ);
}

static inline vmptr_t riscv_vma_translate_w(rvvm_hart_t* vm, vaddr_t addr)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn)) {
        return vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr);
    }
    return riscv_mmu_vma_translate(vm, addr, MMU_WRITE);
}

static inline vmptr_t riscv_vma_translate_e(rvvm_hart_t* vm, vaddr_t addr)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].e == vpn)) {
        return vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr);
    }
    return riscv_mmu_vma_translate(vm, addr, MMU_EXEC);
}

#ifdef USE_VMSWAP
vmptr_t riscv_phys_translate(rvvm_hart_t* vm, paddr_t addr)
#else
static inline vmptr_t riscv_phys_translate(rvvm_hart_t* vm, paddr_t addr)
{
    if (likely(addr >= vm->mem.begin && (addr - vm->mem.begin) < vm->mem.size)) {
        return vm->mem.data + (addr - vm->mem.begin);
    }
    return NULL;
}
#endif

// Integer load operations

static inline void riscv_load_u64(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 7) == 0)) {
        vm->registers[reg] = read_uint64_le(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr));
        return;
    }
    riscv_mmu_load_u64(vm, addr, reg);
}

static inline void riscv_load_u32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 3) == 0)) {
        vm->registers[reg] = read_uint32_le(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr));
        return;
    }
    riscv_mmu_load_u32(vm, addr, reg);
}

static inline void riscv_load_s32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 3) == 0)) {
        vm->registers[reg] = (int32_t)read_uint32_le(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr));
        return;
    }
    riscv_mmu_load_s32(vm, addr, reg);
}

static inline void riscv_load_u16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 1) == 0)) {
        vm->registers[reg] = read_uint16_le(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr));
        return;
    }
    riscv_mmu_load_u16(vm, addr, reg);
}

static inline void riscv_load_s16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 1) == 0)) {
        vm->registers[reg] = (int16_t)read_uint16_le(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr));
        return;
    }
    riscv_mmu_load_s16(vm, addr, reg);
}

static inline void riscv_load_u8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn)) {
        vm->registers[reg] = read_uint8(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr));
        return;
    }
    riscv_mmu_load_u8(vm, addr, reg);
}

static inline void riscv_load_s8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn)) {
        vm->registers[reg] = (int8_t)read_uint8(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr));
        return;
    }
    riscv_mmu_load_s8(vm, addr, reg);
}

// Integer store operations

static inline void riscv_store_u64(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn && (addr & 7) == 0)) {
        write_uint64_le(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr), vm->registers[reg]);
        return;
    }
    riscv_mmu_store_u64(vm, addr, reg);
}

static inline void riscv_store_u32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn && (addr & 3) == 0)) {
        write_uint32_le(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr), vm->registers[reg]);
        return;
    }
    riscv_mmu_store_u32(vm, addr, reg);
}

static inline void riscv_store_u16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn && (addr & 1) == 0)) {
        write_uint16_le(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr), vm->registers[reg]);
        return;
    }
    riscv_mmu_store_u16(vm, addr, reg);
}

static inline void riscv_store_u8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn)) {
        write_uint8(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr), vm->registers[reg]);
        return;
    }
    riscv_mmu_store_u8(vm, addr, reg);
}

// FPU load operations

#ifdef USE_FPU

static inline void riscv_load_double(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 7) == 0)) {
        vm->fpu_registers[reg] = read_double_le(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr));
        return;
    }
    riscv_mmu_load_double(vm, addr, reg);
}

static inline void riscv_load_float(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 3) == 0)) {
        write_float_nanbox(&vm->fpu_registers[reg], read_float_le(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)));
        return;
    }
    riscv_mmu_load_float(vm, addr, reg);
}

// FPU store operations

static inline void riscv_store_double(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn && (addr & 7) == 0)) {
        write_double_le(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr), vm->fpu_registers[reg]);
        return;
    }
    riscv_mmu_store_double(vm, addr, reg);
}

static inline void riscv_store_float(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn && (addr & 3) == 0)) {
        write_float_le(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr), read_float_nanbox(&vm->fpu_registers[reg]));
        return;
    }
    riscv_mmu_store_float(vm, addr, reg);
}

#endif

#endif
