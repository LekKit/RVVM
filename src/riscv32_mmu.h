/*
riscv32_mmu.h - RISC-V Memory Mapping Unit
Copyright (C) 2021  LekKit <github.com/LekKit>
                    Mr0maks <mr.maks0443@gmail.com>

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

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "mem_ops.h"
#include "bit_ops.h"
#include "riscv32.h"

#define MMU_VALID_PTE     0x1
#define MMU_READ          0x2
#define MMU_WRITE         0x4
#define MMU_EXEC          0x8
#define MMU_LEAF_PTE      0xA
#define MMU_USER_USABLE   0x10
#define MMU_GLOBAL_MAP    0x20
#define MMU_PAGE_ACCESSED 0x40
#define MMU_PAGE_DIRTY    0x80

#define GET_VPN1(addr) ((addr) >> 22)
#define GET_VPN2(addr) (((addr) >> 12) & bit_mask(10))

#define GET_PHYS_PAGE(pte) (bit_cut(pte, 10, 22))
#define SET_PHYS_PAGE(pte, pgnum) bit_replace(pte, 10, 22, pgnum)

#define GET_PHYS_ADDR(pte) (GET_PHYS_PAGE(pte) << 12)
#define SET_PHYS_ADDR(pte, addr) SET_PHYS_PAGE(pte, (addr) >> 12)

// Hash-function for the TLB, this is subject to further optimisations
static inline uint32_t tlb_hash(uint32_t addr)
{
    #ifdef RISCV_TLB_DIRECT_MAP
        return (addr >> 12) & (TLB_SIZE - 1); // direct-mapped
    #else
        return ((addr >> 12) + (addr >> 22)) & (TLB_SIZE - 1); // associative
    #endif
}

// Validate TLB entry
static inline bool tlb_check(riscv32_tlb_t tlb, uint32_t addr, uint8_t access)
{
    return (tlb.pte & 0xFFFFF000) == (addr & 0xFFFFF000) && (tlb.pte & access) && tlb.ptr;
}

// Check that memory block doesn't cross page boundaries
static inline bool block_inside_page(uint32_t addr, uint32_t size)
{
    return (addr & 0xFFF) + size <= 4096;
}

// Init VM physical memory (be careful to not overlap MMIO regions!)
bool riscv32_init_phys_mem(riscv32_phys_mem_t* mem, uint32_t begin, uint32_t pages);

// Free emulator memory and VM physical addrspace
void riscv32_destroy_phys_mem(riscv32_phys_mem_t* mem);

// Register MMIO device in the physical address space
void riscv32_mmio_add_device(rvvm_hart_t* vm, uint32_t base_addr, uint32_t end_addr, riscv32_mmio_handler_t handler, void* data);

// Remove MMIO device (whatever addr in the range will do)
// Frees device->data as well if not NULL
void riscv32_mmio_remove_device(rvvm_hart_t* vm, uint32_t addr);

// Flush the TLB (on context switch, SFENCE.VMA, etc)
void riscv32_tlb_flush(rvvm_hart_t* vm);

// Performs translation corresponding to current CSR satp[MODE]
bool riscv32_mmu_translate(rvvm_hart_t* vm, uint32_t addr, uint8_t access, uint32_t* dest_addr);

// Parse the MMU, perform memory operation and cache address translation in TLB
bool riscv32_mmu_op(rvvm_hart_t* vm, uint32_t addr, void* dest, uint32_t size, uint8_t access);

/*
* Inlined TLB-cached function (used for performance)
* Falls back to riscv32_mmu_op() if:
*     Address is not TLB-cached
*     Protection flags do not match
*     Operation crosses pages boundaries
*     MMIO is accessed
* Why is static used? GCC prevents inlining otherwise, i assume it's a bug
*/
inline static bool riscv32_mem_op(rvvm_hart_t* vm, uint32_t addr, void* dest, uint32_t size, uint8_t access)
{
    // Check for TLB cached address translation and cross-page alignment
    uint32_t key = tlb_hash(addr);
    if (tlb_check(vm->tlb[key], addr, access) && block_inside_page(addr, size)) {
        if (access == MMU_WRITE) {
            memcpy(vm->tlb[key].ptr + (addr & 0xFFF), dest, size);
        } else {
            memcpy(dest, vm->tlb[key].ptr + (addr & 0xFFF), size);
        }
        return true;
    }

    // TLB miss, misaligned access or protection fault - perform non-TLB access
    return riscv32_mmu_op(vm, addr, dest, size, access);
}

void riscv32_mmu_dump(rvvm_hart_t *vm);

#endif
