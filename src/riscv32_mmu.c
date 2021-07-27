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

#include "compiler.h"
#include "mem_ops.h"
#include "rvvm_types.h"
#include <inttypes.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "riscv32.h"
#include "riscv32_mmu.h"

#ifdef USE_VMSWAP

// size of the buffer (in pages) to use for swap
#define VMSWAP_SIZE (TLB_SIZE)

vmptr_t riscv32_get_physmem_page(riscv32_phys_mem_t *mem, riscv32_tlb_t *tlb, paddr_t ppn)
{
    if (mem->ptrmap[ppn].ptr == NULL) {
        // address is not mapped, map it
        //printf("vmswap: addr %x\n", ppn << 12);

        // do not allocate new blocks, use previous ones
        vmswap_entry_t *pptr;
        ringbuf_get(&mem->ptrbuf, &pptr, sizeof(pptr));
        assert(pptr != NULL);
        paddr_t prev_ppn = pptr - &mem->ptrmap[0];
        // TODO: fix tlb address flush. Store the address in ptrbuf/ptrmap
        for (size_t i = 0; i < TLB_SIZE; ++i) {
            if (tlb[i].ptr == pptr->ptr) {
                tlb[i].ptr = NULL;
            }
        }
        mem->ptrmap[ppn].ptr = pptr->ptr;
        pptr->ptr = NULL;

        // save the stored block to buffer, so we can use
        // the allocated buffer later
        pptr = &mem->ptrmap[ppn];
        ringbuf_put(&mem->ptrbuf, &pptr, sizeof(pptr));

        // flush old page data, read new
#ifndef USE_VMSWAP_SPLIT
        paddr_t start_ppn = (mem->begin >> 12);
        if (fseek(mem->fp, (prev_ppn - start_ppn) << 12, SEEK_SET) < 0) {
            printf("vmswap: fseek error\n");
        }
        if (fwrite(mem->ptrmap[ppn].ptr, 1 << 12, 1, mem->fp) != 1) {
            printf("vmswap: fwrite error\n");
        }
        if (fseek(mem->fp, (ppn - start_ppn) << 12, SEEK_SET) < 0) {
            printf("vmswap: fseek error\n");
        }
        if (fread(mem->ptrmap[ppn].ptr, 1 << 12, 1, mem->fp) != 1) {
            printf("vmswap: fread error: 0x%08x\n", (ppn - start_ppn) << 12);
            goto out;
        }
#else
        FILE *fp_prev = fopen(mem->ptrmap[prev_ppn].fpath, "wb");
        if (fp_prev == NULL) {
            printf("vmswap: fp_prev open error\n");
            goto out;
        }
        if (fseek(fp_prev, 0, SEEK_SET) < 0) {
            printf("vmswap: fseek error\n");
        }
        if (fwrite(mem->ptrmap[ppn].ptr, 1 << 12, 1, fp_prev) != 1) {
            printf("vmswap: fwrite error\n");
        }
        fclose(fp_prev);

        FILE *fp = fopen(mem->ptrmap[ppn].fpath, "rb");
        if (fp == NULL) {
            /* no writes were done; return zeroes */
            memset(mem->ptrmap[ppn].ptr, '\0', 1 << 12);
            goto out;
        }
        if (fseek(fp, 0, SEEK_SET) < 0) {
            printf("vmswap: fseek error\n");
        }
        size_t read = fread(mem->ptrmap[ppn].ptr, 1 << 12, 1, fp);
        if (read != 1) {
            printf("vmswap: fread error %d\n", read);
        }
        fclose(fp);
#endif
    }

out:
    return mem->ptrmap[ppn].ptr;
}

void riscv32_physmem_op(rvvm_hart_t *vm, paddr_t addr, vmptr_t dest, size_t size, uint8_t access)
{
    paddr_t off = addr & ((1 << 12) - 1);
    paddr_t part_size = ((1 << 12) - off) & ((1 << 12) - 1);
    vmptr_t ptr;

    if (unlikely(part_size >= size)) {
        part_size = size;
    }

    spin_lock(&vm->mem.lock);

    // copy first part
    if (likely(part_size != 0)) { // check for zero size to avoid unnecessary swap page lookups
        ptr = riscv32_get_physmem_page(&vm->mem, vm->tlb, addr >> 12);
        if (access == MMU_WRITE) {
            memcpy(ptr + off, dest, part_size);
        } else {
            memcpy(dest, ptr + off, part_size);
        }
        dest += part_size;
        addr += part_size;
        size -= part_size;
    }

    if (likely(size == 0)) {
        // this is the case most of the time since memory operations
        // coming from MMU unit are tiny
        spin_unlock(&vm->mem.lock);
        return;
    }

    // copy middle pages
    part_size = size >> 12; // size in pages
    size -= (1 << 12) * part_size;
    while (part_size--) {
        ptr = riscv32_get_physmem_page(&vm->mem, vm->tlb, addr >> 12);
        if (access == MMU_WRITE) {
            memcpy(ptr, dest, (1 << 12));
        } else {
            memcpy(dest, ptr, (1 << 12));
        }
        dest += (1 << 12);
        addr += (1 << 12);
    }

    // and the rest
    if (size != 0) {
        ptr = riscv32_get_physmem_page(&vm->mem, vm->tlb, addr >> 12);
        if (access == MMU_WRITE) {
            memcpy(ptr, dest, size);
        } else {
            memcpy(dest, ptr, size);
        }
    }
    spin_unlock(&vm->mem.lock);
}
#else
vmptr_t riscv32_get_physmem_page(riscv32_phys_mem_t *mem, riscv32_tlb_t *tlb, paddr_t ppn)
{
    UNUSED(tlb);
    return mem->data + (ppn << 12);
}

void riscv32_physmem_op(rvvm_hart_t *vm, paddr_t addr, vmptr_t dest, size_t size, uint8_t access)
{
    if (access == MMU_WRITE) {
        memcpy(vm->mem.data + addr, dest, size);
    } else {
        memcpy(dest, vm->mem.data + addr, size);
    }
}
#endif

void riscv32_mmu_dump(rvvm_hart_t *vm)
{
    printf("root page table at: 0x%"PRIxXLEN"\n", vm->root_page_table);

    if (!vm->root_page_table || vm->root_page_table < vm->mem.begin || vm->root_page_table >= vm->mem.begin + vm->mem.size)
    {
        printf("page table is not in physical memory bounds\n");
        return;
    }

    for (uint32_t i = 0; i < 4096; i += 4)
    {
        uint32_t pte;
        riscv32_physmem_op(vm, vm->root_page_table + i, (vmptr_t)&pte, sizeof(pte), MMU_READ);
        pte = read_uint32_le(&pte);
        if (!(pte & MMU_VALID_PTE))
        {
            continue;
        }

        uint32_t pte0_base_addr = GET_PHYS_ADDR(pte);
        printf("0x%08"PRIx32": 0x%08"PRIx32" %c%c%c%c%c%c%c\n", i << 20, pte0_base_addr,
                pte & MMU_READ ? 'R': '.',
                pte & MMU_WRITE ? 'W': '.',
                pte & MMU_EXEC ? 'X': '.',
                pte & MMU_USER_USABLE ? 'U': '.',
                pte & MMU_GLOBAL_MAP ? 'G': '.',
                pte & MMU_PAGE_ACCESSED ? 'A': '.',
                pte & MMU_PAGE_DIRTY ? 'D': '.');

        if (pte & MMU_LEAF_PTE)
        {
            continue;
        }

        for (uint32_t j = 0; j < 4096; j += 4)
        {
            riscv32_physmem_op(vm, pte0_base_addr + i, (vmptr_t)&pte, sizeof(pte), MMU_READ);
            pte = read_uint32_le(&pte);
            if (!(pte & MMU_VALID_PTE))
            {
                continue;
            }

            uint32_t page_addr = GET_PHYS_ADDR(pte);
            printf("\t0x%08"PRIx32": 0x%08"PRIx32" %c%c%c%c%c%c%c\n", (i << 20) + (j << 10), page_addr,
                    pte & MMU_READ ? 'R': '.',
                    pte & MMU_WRITE ? 'W': '.',
                    pte & MMU_EXEC ? 'X': '.',
                    pte & MMU_USER_USABLE ? 'U': '.',
                    pte & MMU_GLOBAL_MAP ? 'G': '.',
                    pte & MMU_PAGE_ACCESSED ? 'A': '.',
                    pte & MMU_PAGE_DIRTY ? 'D': '.');
        }
    }
}

// Check that specific physical address belongs to RAM
static inline bool phys_addr_in_mem(riscv32_phys_mem_t mem, uint32_t page_addr)
{
    return page_addr >= mem.begin && (page_addr - mem.begin) < mem.size;
}

// Put address translation into TLB
static void tlb_put(rvvm_hart_t* vm, uint32_t addr, uint32_t page_addr, uint8_t access)
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
        vm->tlb[key].ptr = riscv32_get_physmem_page(&vm->mem, vm->tlb, page_addr >> 12);
    }
}

// Virtual memory addressing mode (SV32)
static bool riscv32_mmu_translate_sv32(rvvm_hart_t* vm, uint32_t addr, uint8_t access, uint32_t* dest_addr)
{
    uint32_t pte_addr = vm->root_page_table | ((addr >> 20) & 0xFFC);
    uint32_t pte;

    if (phys_addr_in_mem(vm->mem, pte_addr)) {
        riscv32_physmem_op(vm, pte_addr, (vmptr_t)&pte, sizeof(pte), MMU_READ);
        pte = read_uint32_le(&pte);
        if (pte & MMU_VALID_PTE) {
            if (pte & MMU_LEAF_PTE) {
                // PGT entry is a leaf, hugepage mapped
                // Check that PPN[0] is 0, otherwise the page is misaligned
                if ((pte & (access | 0xFFC00)) == access) {
                    // TODO: A/D flag updates should be atomic
                    pte |= MMU_PAGE_ACCESSED;
                    if (access & MMU_WRITE) pte |= MMU_PAGE_DIRTY;
                    uint32_t temp_pte;
                    write_uint32_le(&temp_pte, pte);
                    riscv32_physmem_op(vm, pte_addr, (vmptr_t)&temp_pte, sizeof(temp_pte), MMU_WRITE);
                    pte_addr = ((pte & 0xFFF00000) << 2) | (addr & 0x3FFFFF);
                    tlb_put(vm, addr, pte_addr, access);
                    *dest_addr = pte_addr;
                    return true;
                }
            } else {
                // PGT entry is a pointer to next pagetable
                pte_addr = ((pte & 0xFFFFFC00) << 2) | ((addr >> 10) & 0xFFC);
                if (phys_addr_in_mem(vm->mem, pte_addr)) {
                    riscv32_physmem_op(vm, pte_addr, (vmptr_t)&pte, sizeof(pte), MMU_READ);
                    if ((pte & MMU_VALID_PTE) && (pte & access)) {
                        pte |= MMU_PAGE_ACCESSED;
                        if (access & MMU_WRITE) pte |= MMU_PAGE_DIRTY;
                        uint32_t temp_pte;
                        write_uint32_le(&temp_pte, pte);
                        riscv32_physmem_op(vm, pte_addr, (vmptr_t)&temp_pte, sizeof(temp_pte), MMU_WRITE);
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
static bool riscv32_mmu_translate_bare(rvvm_hart_t* vm, uint32_t addr, uint8_t access, uint32_t* dest_addr)
{
    tlb_put(vm, addr, addr, access);
    *dest_addr = addr;
    return true;
}

// Receives any operation on physical address space out of RAM region
static bool riscv32_mmio_op(rvvm_hart_t* vm, uint32_t addr, void* dest, uint32_t size, uint8_t access)
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
    mem->begin = begin;
    mem->size = pages * 4096;

#ifndef USE_VMSWAP
    void* tmp = calloc(pages, 4096);
    mem->data = tmp - begin;
    if (!tmp) return false;
#else

#ifndef USE_VMSWAP_SPLIT
    mem->fp = tmpfile();
    if (!mem->fp) {
        return false;
    }
#endif

    spin_init(&mem->lock);

    // use TLB size by default
    ringbuf_create(&mem->ptrbuf, TLB_SIZE * sizeof(vmptr_t));
    paddr_t first_page = mem->begin >> 12;
    mem->ptrmap = (vmswap_entry_t*)calloc(pages, sizeof(mem->ptrmap[0])) - first_page;

    char *buf = calloc(1, 1 << 12);
    if (buf == NULL) {
        return false;
    }
    for (size_t i = 0; i < pages; ++i) {
#ifndef USE_VMSWAP_SPLIT
        if (fwrite(buf, 1 << 12, 1, mem->fp) != 1) {
            printf("vmswap: init: fwrite failed\n");
        }
#else
        char *fpath;
        fpath = malloc(L_tmpnam);
        if (fpath == NULL) {
            printf("vmswap: init: malloc failed\n");
            return false;
        }
        tmpnam(fpath);
        vmswap_entry_t *entry = mem->ptrmap + first_page + i;
        entry->fpath = fpath;
        /* ptr is already zero because of calloc */
#endif
    }
    free(buf);

    for (size_t i = 0; i < VMSWAP_SIZE; ++i) {
        mem->ptrmap[first_page + i].ptr = (vmptr_t)malloc(1 << 12);
        vmswap_entry_t *pptr = &mem->ptrmap[first_page + i];
        ringbuf_put(&mem->ptrbuf, &pptr, sizeof(pptr));
    }
#endif
    return true;
}

void riscv32_destroy_phys_mem(riscv32_phys_mem_t* mem)
{
#ifndef USE_VMSWAP
    if (mem->data + mem->begin) free(mem->data + mem->begin);
    mem->data = NULL;
#else
    for (size_t i = 0; i < mem->size; i += (1 << 12)) {
        vmswap_entry_t *entry = &mem->ptrmap[(mem->begin >> 12) + i];
        free(entry->ptr);
#ifdef USE_VMSWAP_SPLIT
        free(entry->fpath);
#endif
    }

    free(mem->ptrmap + (mem->begin >> 12) * sizeof(mem->ptrmap[0]));
    ringbuf_destroy(&mem->ptrbuf);
#ifndef USE_VMSWAP_SPLIT
    fclose(mem->fp);
#endif
#endif
    mem->begin = 0;
    mem->size = 0;
}

void riscv32_mmio_add_device(rvvm_hart_t* vm, uint32_t base_addr, uint32_t end_addr, riscv32_mmio_handler_t handler, void* data)
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

void riscv32_mmio_remove_device(rvvm_hart_t* vm, uint32_t addr)
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

void riscv32_tlb_flush(rvvm_hart_t* vm)
{
    // No ASID support as of now (TLB is quite small, there are no benefits)
    memset(vm->tlb, 0, sizeof(vm->tlb));
    // Flush dispatch loop page
    vm->wait_event = 0;
}

bool riscv32_mmu_translate(rvvm_hart_t* vm, uint32_t addr, uint8_t access, uint32_t* dest_addr)
{
    if (vm->mmu_virtual && vm->priv_mode <= PRIVILEGE_SUPERVISOR)
        return riscv32_mmu_translate_sv32(vm, addr, access, dest_addr);
    else
        return riscv32_mmu_translate_bare(vm, addr, access, dest_addr);
}

bool riscv32_mmu_op(rvvm_hart_t* vm, uint32_t addr, void* dest, uint32_t size, uint8_t access)
{
    if (!block_inside_page(addr, size)) {
        // Handle misalign between 2 pages
        if (access == MMU_EXEC) {
            /*
            * If we are fetching a 2-byte instruction at the end of page,
            * do not fetch other 2 bytes to prevent spurious pagefaults
            */
            uint32_t inst_addr;
            if (   riscv32_mmu_translate(vm, addr, access, &inst_addr)
                && phys_addr_in_mem(vm->mem, inst_addr)) {
                uint8_t ibyte;
                riscv32_physmem_op(vm, inst_addr, (vmptr_t)&ibyte, sizeof(ibyte), MMU_EXEC);
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
            riscv32_physmem_op(vm, phys_addr, dest, size, access);
            return true;
        }
        // Physical address not in memory region, check MMIO
        if (riscv32_mmio_op(vm, phys_addr, dest, size, access)) {
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
