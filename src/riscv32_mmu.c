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

#include "riscv32.h"
#include "riscv32_mmu.h"

// Check that specific physical address belongs to RAM
inline bool phys_addr_in_mem(riscv32_phys_mem_t mem, uint32_t page_addr)
{
    return page_addr >= mem.begin && (page_addr - mem.begin) < mem.size;
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

// Virtual memory addressing mode (SV32)
static bool riscv32_mmu_translate_sv32(riscv32_vm_state_t* vm, uint32_t addr, uint8_t access, uint32_t* dest_addr)
{
    uint32_t pte_addr = vm->root_page_table | ((addr >> 20) & 0xFFC);
    uint32_t pte;

    if (phys_addr_in_mem(vm->mem, pte_addr)) {
        pte = read_uint32_le(vm->mem.data + pte_addr);
        if (pte & MMU_VALID_PTE) {
            if (pte & MMU_LEAF_PTE) {
                // PGT entry is a leaf, hugepage mapped
                // Check that PPN[0] is 0, otherwise the page is misaligned
                if ((pte & (access | 0xFFC00)) == access) {
                    // TODO: A/D flag updates should be atomic
                    pte |= MMU_PAGE_ACCESSED;
                    if (access & MMU_WRITE) pte |= MMU_PAGE_DIRTY;
                    write_uint32_le(vm->mem.data + pte_addr, pte);
                    pte_addr = ((pte & 0xFFF00000) << 2) | (addr & 0x3FFFFF);
                    tlb_put(vm, addr, pte_addr, access);
                    *dest_addr = pte_addr;
                    return true;
                }
            } else {
                // PGT entry is a pointer to next pagetable
                pte_addr = ((pte & 0xFFFFFC00) << 2) | ((addr >> 10) & 0xFFC);
                if (phys_addr_in_mem(vm->mem, pte_addr)) {
                    pte = read_uint32_le(vm->mem.data + pte_addr);
                    if ((pte & MMU_VALID_PTE) && (pte & access)) {
                        pte |= MMU_PAGE_ACCESSED;
                        if (access & MMU_WRITE) pte |= MMU_PAGE_DIRTY;
                        write_uint32_le(vm->mem.data + pte_addr, pte);
                        pte_addr = ((pte & 0xFFFFFC00) << 2) | (addr & 0xFFF);
                        tlb_put(vm, addr, pte_addr, access);
                        *dest_addr = pte_addr;
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
static bool riscv32_mmio_op(riscv32_vm_state_t* vm, uint32_t addr, void* dest, uint32_t size, uint8_t access)
{
    riscv32_mmio_device_t* device;
    for (uint32_t i=0; i<vm->mmio.count; ++i) {
        device = &vm->mmio.regions[i];
        if (addr >= device->base_addr && addr <= device->end_addr) {
            return device->handler(vm, device, addr - device->base_addr, dest, size, access);
        }
    }
    return false;
}

bool riscv32_init_phys_mem(riscv32_phys_mem_t* mem, uint32_t begin, uint32_t pages)
{
    if (begin & 0xFFF) return false;
    void* tmp = calloc(pages, 4096);
    if (!tmp) return false;
    mem->data = tmp - begin;
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

void riscv32_mmio_add_device(riscv32_vm_state_t* vm, uint32_t base_addr, uint32_t end_addr, riscv32_mmio_handler_t handler, void* data)
{
    if (vm->mmio.count > 255) {
        printf("ERROR: Too much MMIO zones!\n");
        exit(0);
    }
    riscv32_mmio_device_t* device = &vm->mmio.regions[vm->mmio.count];
    device->base_addr = base_addr;
    device->end_addr = end_addr;
    device->handler = handler;
    device->data = data;
    vm->mmio.count++;
}

void riscv32_mmio_remove_device(riscv32_vm_state_t* vm, uint32_t addr)
{
    riscv32_mmio_device_t* device;
    for (uint32_t i=0; i<vm->mmio.count; ++i) {
        device = &vm->mmio.regions[i];
        if (addr >= device->base_addr && addr <= device->end_addr) {
            if (device->data) free(device->data);
            vm->mmio.count--;
            for (uint32_t j=i; j<vm->mmio.count; ++j) {
                vm->mmio.regions[j] = vm->mmio.regions[j+1];
            }
            return;
        }
    }
}

void riscv32_tlb_flush(riscv32_vm_state_t* vm)
{
    // No ASID support as of now (TLB is quite small, there are no benefits)
    memset(vm->tlb, 0, sizeof(vm->tlb));
}

bool riscv32_mmu_translate(riscv32_vm_state_t* vm, uint32_t addr, uint8_t access, uint32_t* dest_addr)
{
    if (vm->mmu_virtual && vm->priv_mode <= PRIVILEGE_SUPERVISOR)
        return riscv32_mmu_translate_sv32(vm, addr, access, dest_addr);
    else
        return riscv32_mmu_translate_bare(vm, addr, access, dest_addr);
}

bool riscv32_mmu_op(riscv32_vm_state_t* vm, uint32_t addr, void* dest, uint32_t size, uint8_t access)
{
    if (!block_inside_page(addr, size)) {
        // Handle misalign between 2 pages
        if (access == MMU_EXEC) {
            /*
            * If we are fetching a 2-byte instruction at the end of page,
            * do not fetch other 2 bytes to prevent spurious pagefaults
            */
            uint32_t inst_addr;
            if (riscv32_mmu_translate(vm, addr, access, &inst_addr)
            && phys_addr_in_mem(vm->mem, inst_addr)) {
                uint8_t ibyte = *(uint8_t*)(vm->mem.data + inst_addr);
                if ((ibyte & RISCV32I_OPCODE_MASK) != RISCV32I_OPCODE_MASK)
                    return riscv32_mmu_op(vm, addr, dest, 2, MMU_EXEC);
            }
        }
        uint8_t part_size = 4096 - (addr & 0xFFF);
        return riscv32_mmu_op(vm, addr, dest, part_size, access) &&
               riscv32_mmu_op(vm, addr + part_size, dest + part_size, size - part_size, access);
    }
    uint32_t phys_addr;
    uint32_t trap_cause = 0;
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
        if (riscv32_mmio_op(vm, addr, dest, size, access)) {
            return true;
        }
        // Physical memory access fault (bad physical address)
        switch (access) {
        case MMU_WRITE:
            trap_cause = TRAP_STORE_FAULT;
            break;
        case MMU_READ:
            trap_cause = TRAP_LOAD_FAULT;
            break;
        case MMU_EXEC:
            trap_cause = TRAP_INSTR_FETCH;
            break;
        }
    } else {
        // Pagefault (no translation for address or protection fault)
        switch (access) {
        case MMU_WRITE:
            trap_cause = TRAP_STORE_PAGEFAULT;
            break;
        case MMU_READ:
            trap_cause = TRAP_LOAD_PAGEFAULT;
            break;
        case MMU_EXEC:
            trap_cause = TRAP_INSTR_PAGEFAULT;
            break;
        }
    }
    // Trap the CPU & instruct the caller to discard operation
    riscv32_trap(vm, trap_cause, addr);
    return false;
}
