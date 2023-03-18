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
#include "riscv_csr.h"

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
#define TLB_VADDR(vaddr)  (vaddr)
//#define TLB_VADDR(vaddr)  ((vaddr) & PAGE_MASK) // we may remove vaddr offset if needed

// Init physical memory (be careful to not overlap MMIO regions!)
bool riscv_init_ram(rvvm_ram_t* mem, paddr_t begin, paddr_t size);
void riscv_free_ram(rvvm_ram_t* mem);

// Flush the TLB (on context switch, SFENCE.VMA, etc)
void riscv_tlb_flush(rvvm_hart_t* vm);
void riscv_tlb_flush_page(rvvm_hart_t* vm, vaddr_t addr);

#ifdef USE_JIT
void riscv_jit_tlb_flush(rvvm_hart_t* vm);
#endif

/*
 * Non-inlined slow memory operations, perform MMU translation,
 * call MMIO handlers if needed.
 */

// Translate virtual address to physical with respect to current CPU mode
bool riscv_mmu_translate(rvvm_hart_t* vm, vaddr_t vaddr, paddr_t* paddr, uint8_t access);
// Translate virtual address into VM pointer OR buff in case of RMW MMIO operation
vmptr_t riscv_mmu_vma_translate(rvvm_hart_t* vm, vaddr_t addr, void* buff, size_t size, uint8_t access);
// Commit changes back to MMIO
void riscv_mmu_vma_mmio_write(rvvm_hart_t* vm, vaddr_t addr, void* buff, size_t size);

// Fetch instruction from virtual address
bool riscv_mmu_fetch_inst(rvvm_hart_t* vm, vaddr_t addr, uint32_t* inst);

// Load/store operations on virtual address (uses MMU translation)
void riscv_mmu_load_u64(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
void riscv_mmu_load_u32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
void riscv_mmu_load_s32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
void riscv_mmu_load_u16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
void riscv_mmu_load_s16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
void riscv_mmu_load_u8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
void riscv_mmu_load_s8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);

void riscv_mmu_store_u64(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
void riscv_mmu_store_u32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
void riscv_mmu_store_u16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
void riscv_mmu_store_u8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);

#ifdef USE_FPU
void riscv_mmu_load_double(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
void riscv_mmu_load_float(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);

void riscv_mmu_store_double(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
void riscv_mmu_store_float(rvvm_hart_t* vm, vaddr_t addr, regid_t reg);
#endif

// Alignment checks / fixup

static inline bool riscv_block_in_page(vaddr_t addr, size_t size)
{
    return (addr & PAGE_MASK) + size <= PAGE_SIZE;
}

static inline bool riscv_block_aligned(vaddr_t addr, size_t size)
{
    return (addr & (size - 1)) == 0;
}

static inline vaddr_t riscv_align_addr(vaddr_t addr, size_t size)
{
    return addr & (~(vaddr_t)(size - 1));
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
        *inst = read_uint16_le_m((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)));
        if ((*inst & 0x3) == 0x3) {
            // This is a 4-byte instruction, force tlb lookup again
            vpn = (addr + 2) >> PAGE_SHIFT;
            if (likely(vm->tlb[vpn & TLB_MASK].e == vpn)) {
                *inst |= ((uint32_t)read_uint16_le_m((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr + 2)))) << 16;
                return true;
            }
        } else return true;
    }
    return riscv_mmu_fetch_inst(vm, addr, inst);
}

// VM Address translation (translated within single page bounds)

static inline bool riscv_virt_translate_r(rvvm_hart_t* vm, vaddr_t vaddr, paddr_t* paddr)
{
    vaddr_t vpn = vaddr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn)) {
        *paddr = vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(vaddr) - (size_t)vm->mem.data + vm->mem.begin;
        return true;
    }
    return riscv_mmu_translate(vm, vaddr, paddr, MMU_READ);
}

static inline bool riscv_virt_translate_w(rvvm_hart_t* vm, vaddr_t vaddr, paddr_t* paddr)
{
    vaddr_t vpn = vaddr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn)) {
        *paddr = vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(vaddr) - (size_t)vm->mem.data + vm->mem.begin;
        return true;
    }
    return riscv_mmu_translate(vm, vaddr, paddr, MMU_WRITE);
}

static inline bool riscv_virt_translate_e(rvvm_hart_t* vm, vaddr_t vaddr, paddr_t* paddr)
{
    vaddr_t vpn = vaddr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].e == vpn)) {
        *paddr = vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(vaddr) - (size_t)vm->mem.data + vm->mem.begin;
        return true;
    }
    return riscv_mmu_translate(vm, vaddr, paddr, MMU_EXEC);
}

static inline vmptr_t riscv_vma_translate_r(rvvm_hart_t* vm, vaddr_t addr, void* buff, size_t size)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn)) {
        return (vmptr_t)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr));
    }
    return riscv_mmu_vma_translate(vm, addr, buff, size, MMU_READ);
}

static inline vmptr_t riscv_vma_translate_w(rvvm_hart_t* vm, vaddr_t addr, void* buff, size_t size)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn)) {
        return (vmptr_t)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr));
    }
    return riscv_mmu_vma_translate(vm, addr, buff, size, MMU_WRITE);
}

static inline vmptr_t riscv_vma_translate_e(rvvm_hart_t* vm, vaddr_t addr, void* buff, size_t size)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].e == vpn)) {
        return (vmptr_t)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr));
    }
    return riscv_mmu_vma_translate(vm, addr, buff, size, MMU_EXEC);
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
        vm->registers[reg] = read_uint64_le((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)));
        return;
    }
    riscv_mmu_load_u64(vm, addr, reg);
}

static inline void riscv_load_u32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 3) == 0)) {
        vm->registers[reg] = read_uint32_le((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)));
        return;
    }
    riscv_mmu_load_u32(vm, addr, reg);
}

static inline void riscv_load_s32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 3) == 0)) {
        vm->registers[reg] = (int32_t)read_uint32_le((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)));
        return;
    }
    riscv_mmu_load_s32(vm, addr, reg);
}

static inline void riscv_load_u16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 1) == 0)) {
        vm->registers[reg] = read_uint16_le((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)));
        return;
    }
    riscv_mmu_load_u16(vm, addr, reg);
}

static inline void riscv_load_s16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 1) == 0)) {
        vm->registers[reg] = (int16_t)read_uint16_le((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)));
        return;
    }
    riscv_mmu_load_s16(vm, addr, reg);
}

static inline void riscv_load_u8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn)) {
        vm->registers[reg] = read_uint8((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)));
        return;
    }
    riscv_mmu_load_u8(vm, addr, reg);
}

static inline void riscv_load_s8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn)) {
        vm->registers[reg] = (int8_t)read_uint8((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)));
        return;
    }
    riscv_mmu_load_s8(vm, addr, reg);
}

// Integer store operations

static inline void riscv_store_u64(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn && (addr & 7) == 0)) {
        write_uint64_le((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)), vm->registers[reg]);
        return;
    }
    riscv_mmu_store_u64(vm, addr, reg);
}

static inline void riscv_store_u32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn && (addr & 3) == 0)) {
        write_uint32_le((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)), vm->registers[reg]);
        return;
    }
    riscv_mmu_store_u32(vm, addr, reg);
}

static inline void riscv_store_u16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn && (addr & 1) == 0)) {
        write_uint16_le((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)), vm->registers[reg]);
        return;
    }
    riscv_mmu_store_u16(vm, addr, reg);
}

static inline void riscv_store_u8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn)) {
        write_uint8((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)), vm->registers[reg]);
        return;
    }
    riscv_mmu_store_u8(vm, addr, reg);
}

#ifdef USE_FPU

// FPU load operations

static inline void riscv_load_double(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 7) == 0)) {
        vm->fpu_registers[reg] = read_double_le((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)));
        fpu_set_fs(vm, FS_DIRTY);
        return;
    }
    riscv_mmu_load_double(vm, addr, reg);
}

static inline void riscv_load_float(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].r == vpn && (addr & 3) == 0)) {
        write_float_nanbox(&vm->fpu_registers[reg], read_float_le((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr))));
        fpu_set_fs(vm, FS_DIRTY);
        return;
    }
    riscv_mmu_load_float(vm, addr, reg);
}

// FPU store operations

static inline void riscv_store_double(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn && (addr & 7) == 0)) {
        write_double_le((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)), vm->fpu_registers[reg]);
        return;
    }
    riscv_mmu_store_double(vm, addr, reg);
}

static inline void riscv_store_float(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    vaddr_t vpn = addr >> PAGE_SHIFT;
    if (likely(vm->tlb[vpn & TLB_MASK].w == vpn && (addr & 3) == 0)) {
        write_float_le((void*)(size_t)(vm->tlb[vpn & TLB_MASK].ptr + TLB_VADDR(addr)), read_float_nanbox(&vm->fpu_registers[reg]));
        return;
    }
    riscv_mmu_store_float(vm, addr, reg);
}

#endif

#endif
