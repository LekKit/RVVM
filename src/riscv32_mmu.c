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

#include "riscv32_mmu.h"

// Check that specific physical address belongs to RAM
inline bool phys_addr_in_mem(riscv32_phys_mem_t mem, uint32_t page_addr)
{
    return page_addr >= mem.begin && (page_addr - mem.begin) < mem.size;
}

// Memory traps fall here
static void riscv32_mmu_trap(riscv32_vm_state_t* vm, uint32_t addr)
{
    printf("MMU trap at %p in VM %p\n", (void*)(uintptr_t)addr, vm);
}

// Put address translation into TLB
static void tlb_put(riscv32_vm_state_t* vm, uint32_t addr, uint32_t page_addr, uint8_t access)
{
    if (phys_addr_in_mem(vm->mem, page_addr)) {
        addr &= 0xFFFFF000;
        page_addr &= 0xFFFFF000;
        uint32_t key = tlb_hash(addr);
        /*
        * Add only requested access bits for correct access/dirty flags
        * implementation. Assume the software does not clear A/D bits without
        * calling SFENCE.VMA
        */
        if ((vm->tlb[key].pte & 0xFFFFF000) == addr)
            vm->tlb[key].pte |= access;
        else
            vm->tlb[key].pte = addr | access;
        vm->tlb[key].ptr = vm->mem.data + page_addr;
    }
}

// Virtual memory addressing mode (SV32) - TODO a conforming implementation
static bool riscv32_mmu_translate_sv32(riscv32_vm_state_t* vm, uint32_t addr, uint8_t access, uint32_t* dest_addr)
{
    // Shift addr by 10 instead of 12, and cut lower 2 bits to multiply it by 4
    if (phys_addr_in_mem(vm->mem, vm->root_page_table)) {
        uint32_t pte = read_uint32_le(vm->mem.data + vm->root_page_table + ((addr >> 10) & 0xFFC));
        uint32_t page_addr;
        if (pte & MMU_VALID_PTE) {
            if (pte & MMU_LEAF_PTE) {
                // PGT entry was a leaf, 4 MiB hugepage mapped
                if (pte & access) {
                    page_addr = ((addr & 0xFFC00000) | (pte & 0x3FF000));
                    tlb_put(vm, addr, page_addr, access);
                    *dest_addr = page_addr + (addr & 0xFFF);
                    return true;
                }
            } else {
                page_addr = (pte & 0xFFFFF000);
                if (phys_addr_in_mem(vm->mem, page_addr)) {
                    // Use PPN as nested PTE address
                    pte = read_uint32_le(vm->mem.data + page_addr + ((addr >> 20) & 0xFFC));
                    if ((pte & MMU_VALID_PTE) && (pte & access)) {
                        page_addr = (pte & 0xFFFFF000);
                        tlb_put(vm, addr, page_addr, access);
                        *dest_addr = page_addr + (addr & 0xFFF);
                        return true;
                    }
                }
            }
        }
    }
    // No valid address translation can be done (invalid PTE or protection fault)
    return false;
}

// Flat 32-bit physical addressing mode (Mbare)
static bool riscv32_mmu_translate_bare(riscv32_vm_state_t* vm, uint32_t addr, uint8_t access, uint32_t* dest_addr)
{
    tlb_put(vm, addr, addr, access);
    *dest_addr = addr;
    return true;
}

// Receives any operation on physical address space out of RAM region
static bool riscv32_mmio_check(riscv32_vm_state_t* vm, uint32_t addr, void* dest, uint32_t size, uint8_t access)
{
    // TODO: Check MMIO regions, call appropriate handlers
    printf("Non-RAM range physical access %d:%p at addr %p size %d in vm %p\n", access, dest, (void*)(uintptr_t)addr, size, vm);
    return false;
}

bool riscv32_init_phys_mem(riscv32_phys_mem_t* mem, uint32_t begin, uint32_t pages)
{
    if (begin & 0xFFF) return false;
    void* tmp = calloc(pages, 4096);
    if (!tmp) return false;
    mem->data = tmp + begin;
    mem->begin = begin;
    mem->size = pages * 4096;
    return true;
}

void riscv32_destroy_phys_mem(riscv32_phys_mem_t* mem)
{
    if (mem->data + mem->begin) free(mem->data + mem->begin);
    mem->data = NULL;
    mem->begin = 0;
    mem->size = 0;
}

void riscv32_tlb_flush(riscv32_vm_state_t* vm)
{
    // No ASID support as of now (TLB is quite small, there are no benefits)
    memset(vm->tlb, 0, sizeof(vm->tlb));
}

bool riscv32_mmu_translate(riscv32_vm_state_t* vm, uint32_t addr, uint8_t access, uint32_t* dest_addr)
{
    if (vm->mmu_virtual)
        return riscv32_mmu_translate_sv32(vm, addr, access, dest_addr);
    else
        return riscv32_mmu_translate_bare(vm, addr, access, dest_addr);
}

bool riscv32_mmu_op(riscv32_vm_state_t* vm, uint32_t addr, void* dest, uint32_t size, uint8_t access)
{
    if (!block_inside_page(addr, size)) {
        // Handle misalign between 2 pages
        uint8_t part_size = 4096 - (addr & 0xFFF);
        return riscv32_mmu_op(vm, addr, dest, part_size, access) &&
               riscv32_mmu_op(vm, addr, dest + part_size, size - part_size, access);
    }
    uint32_t phys_addr;
    // Translation function also checks access rights and caches addr translation in TLB
    if (riscv32_mmu_translate(vm, addr, access, &phys_addr)) {
        if (phys_addr_in_mem(vm->mem, phys_addr)) {
            if (access == MMU_WRITE) {
                memcpy(vm->mem.data + phys_addr, dest, size);
            } else {
                memcpy(dest, vm->mem.data + phys_addr, size);
            }
            return true;
        }
        // Physical address not in memory region, check MMIO
        return riscv32_mmio_check(vm, addr, dest, size, access);
    }
    // Memory access fault (address translation fault, bad physical address)
    // Trap the CPU & tell the caller to discard operation
    riscv32_mmu_trap(vm, addr);
    return false;
}
