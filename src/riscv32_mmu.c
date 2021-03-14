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

#include <inttypes.h>

#include "bit_ops.h"
#include "riscv32.h"
#include "riscv32_mmu.h"
#include "riscv32_csr.h"

struct bit_range
{
	unsigned begin;
	unsigned size;
};

struct mmu {
	size_t levels;
	struct bit_range *vaddr_ranges;
	struct bit_range *paddr_ranges;
	size_t ptesize;
};


static const struct mmu mmu_list[] =
{
	[MMU_SV32] = {
		.levels = 2,
		.vaddr_ranges = (struct bit_range[]) { { 12, 10 }, { 22, 10 } },
		.paddr_ranges = (struct bit_range[]) { { 10, 10 }, { 20, 12 } },
		.ptesize = 4,
	},
	[MMU_SV39] = {
		.levels = 3,
		.vaddr_ranges = (struct bit_range[]) { { 12, 9 }, { 21, 9 }, { 30, 9 } },
		.paddr_ranges = (struct bit_range[]) { { 12, 9 }, { 21, 9 }, { 30, 26 } },
		.ptesize = 8,
	},
	[MMU_SV48] = {
		.levels = 4,
		.vaddr_ranges = (struct bit_range[]) { { 12, 9 }, { 21, 9 }, { 30, 9 }, { 39, 9 } },
		.paddr_ranges = (struct bit_range[]) { { 12, 9 }, { 21, 9 }, { 30, 9 }, { 39, 17 } },
		.ptesize = 8,
	},
};

void riscv32_mmu_dump(riscv32_vm_state_t *vm)
{
	// TODO: rewrite using new MMU struct
	if (vm->mmu_virtual != MMU_SV32)
	{
		printf("unsupported MMU to dump: %d\n", vm->mmu_virtual);
		return;
	}

	printf("root page table at: 0x%"PRIxpaddr"\n", vm->root_page_table);

	if (!vm->root_page_table || vm->root_page_table < vm->mem.begin || vm->root_page_table >= vm->mem.begin + vm->mem.size)
	{
		printf("page table is not in physical memory bounds\n");
		return;
	}

	for (uint32_t i = 0; i < 4096; i += 4)
	{
		uint32_t pte = read_uint32_le(vm->mem.data + vm->root_page_table + i);
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
			pte = read_uint32_le(vm->mem.data + pte0_base_addr + j);
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
static inline bool phys_addr_in_mem(riscv32_phys_mem_t mem, physaddr_t page_addr)
{
    return page_addr >= mem.begin && (page_addr - mem.begin) < mem.size;
}

// Put address translation into TLB
static void tlb_put(riscv32_vm_state_t* vm, virtaddr_t addr, physaddr_t page_addr, uint8_t access)
{
    if (phys_addr_in_mem(vm->mem, page_addr)) {
        addr &= ~gen_mask(12);
        page_addr &= ~gen_mask(12);
        virtaddr_t key = tlb_hash(addr);
        /*
        * Add only requested access bits for correct access/dirty flags
        * implementation. Assume the software does not clear A/D bits without
        * calling SFENCE.VMA
        */
        if ((vm->tlb[key].pte & ~gen_mask(12)) == addr)
            vm->tlb[key].pte |= access;
        else
            vm->tlb[key].pte = addr | access;
        vm->tlb[key].ptr = vm->mem.data + page_addr;
    }
}

// Virtual memory addressing mode (SV32)
bool riscv_mmu_translate(riscv32_vm_state_t *vm, const virtaddr_t va, physaddr_t *pa, const uint8_t access, bool update_pages)
{
	if (vm->mmu_virtual != MMU_SV32
			&& vm->mmu_virtual != MMU_SV39
			&& vm->mmu_virtual != MMU_SV48)
	{
		/* wut iz dat? */
		return false;
	}
	const struct mmu *mmu = &mmu_list[vm->mmu_virtual];
	physaddr_t a = vm->root_page_table;

	assert((access & (MMU_READ | MMU_WRITE | MMU_EXEC)) == access);
	assert((a & gen_mask(12)) == 0);

	size_t i = mmu->levels - 1;
	do
	{
		size_t vpn = cut_bits(va, mmu->vaddr_ranges[i].begin, mmu->vaddr_ranges[i].size);
		physaddr_t pte_addr = a + vpn * mmu->ptesize;

		physaddr_t pte = 0;
		for (size_t j = 0; j < mmu->ptesize; ++j)
		{
			/* TODO: update PTE atomically */
			if (!phys_addr_in_mem(vm->mem, pte_addr + j))
			{
				return false;
			}
			pte |= (physaddr_t)vm->mem.data[pte_addr + j] << 8 * j;
		}

		if (   !(pte & MMU_VALID_PTE)
		    || (!(pte & MMU_READ) && (pte & MMU_WRITE)))
		{
			/* invalid PTE */
			/* raise an exception corresponding to original access type */
			return false;
		}

		/* PTE is valid */

		if (!(pte & MMU_READ) && !(pte & MMU_WRITE))
		{
			/* Non-leaf PTE; go to the next one */
			a = 0;
			for (size_t j = 0; j < mmu->levels; ++j)
			{
				size_t ppn = cut_bits(pte, mmu->paddr_ranges[j].begin, mmu->paddr_ranges[j].size);
				a |= ppn << mmu->vaddr_ranges[j].begin;
			}

			continue;
		}

		/* This is a leaf PTE */

		/* MXR and permissions handling */
		if (   ((pte | (MMU_READ * !!(pte & MMU_EXEC && is_bit_set(vm->csr.status, CSR_STATUS_MXR)))) & access) != access
		    /* supervisor user page access */
		    || ((pte & MMU_USER_USABLE) && vm->priv_mode == PRIVILEGE_SUPERVISOR && !is_bit_set(vm->csr.status, CSR_STATUS_SUM)))
		{
			/* raise an exception corresponding to original access type */
			return false;
		}

		for (size_t j = 0; j < i; ++j)
		{
			if (cut_bits(pte, mmu->paddr_ranges[j].begin, mmu->paddr_ranges[j].size) != 0)
			{
				/* Page is misaligned */
				/* raise an exception corresponding to original access type */
				return false;
			}
		}

		if (update_pages && (!(pte & MMU_PAGE_ACCESSED) || ((access & MMU_WRITE) && (pte & MMU_PAGE_DIRTY))))
		{
			pte |= MMU_PAGE_ACCESSED;
			pte |= MMU_PAGE_DIRTY * !!(access & MMU_WRITE);

			/* TODO: update PTE atomically */
			physaddr_t val = pte;
			for (size_t j = 0; j < mmu->ptesize; ++j)
			{
				if (!phys_addr_in_mem(vm->mem, pte_addr + j))
				{
					return false;
				}

				/* XXX: need to check bounds here? */
				vm->mem.data[pte_addr + j] = (val >> 8 * j) & 0xFF;
			}
		}

		/* The translation is successful */
		if (pa != NULL)
		{
			*pa = va & gen_mask(12);

			for (size_t j = 0; j < mmu->levels; ++j)
			{
				if (j >= i)
				{
					/* This is a normal translation */
					*pa |= cut_bits(pte, mmu->paddr_ranges[j].begin, mmu->paddr_ranges[j].size)
						<< mmu->vaddr_ranges[j].begin;
				}
				else
				{
					/* This is a superpage translation */
					*pa |= cut_bits(va, mmu->vaddr_ranges[j].begin, mmu->vaddr_ranges[j].size)
						<< mmu->vaddr_ranges[j].begin;
				}
			}

			tlb_put(vm, va, *pa, access);
		}

		return true;
	}
	while (i-- != 0);

	/* top level PTE reached */
	return false;
}


// Flat 32-bit physical addressing mode (Mbare)
static bool riscv32_mmu_translate_bare(riscv32_vm_state_t* vm, virtaddr_t addr, uint8_t access, physaddr_t* dest_addr)
{
    tlb_put(vm, addr, addr, access);
    *dest_addr = addr;
    return true;
}

// Receives any operation on physical address space out of RAM region
static bool riscv32_mmio_op(riscv32_vm_state_t* vm, physaddr_t addr, void* dest, uint32_t size, uint8_t access)
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

bool riscv32_init_phys_mem(riscv32_phys_mem_t* mem, physaddr_t begin, physaddr_t pages)
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
    free(mem->data + mem->begin);
    mem->data = NULL;
    mem->begin = 0;
    mem->size = 0;
}

void riscv32_mmio_add_device(riscv32_vm_state_t* vm, physaddr_t base_addr, physaddr_t end_addr, riscv32_mmio_handler_t handler, void* data)
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

void riscv32_mmio_remove_device(riscv32_vm_state_t* vm, physaddr_t addr)
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

bool riscv32_mmu_translate(riscv32_vm_state_t* vm, virtaddr_t addr, uint8_t access, physaddr_t* dest_addr)
{
    if (vm->mmu_virtual && vm->priv_mode <= PRIVILEGE_SUPERVISOR)
        return riscv_mmu_translate(vm, addr, dest_addr, access, true);
	//return riscv32_mmu_translate_sv32(vm, addr, access, dest_addr);
    else
        return riscv32_mmu_translate_bare(vm, addr, access, dest_addr);
}

bool riscv32_mmu_op(riscv32_vm_state_t* vm, virtaddr_t addr, void* dest, uint32_t size, uint8_t access)
{
    if (!block_inside_page(addr, size)) {
        // Handle misalign between 2 pages
        if (access == MMU_EXEC) {
            /*
            * If we are fetching a 2-byte instruction at the end of page,
            * do not fetch other 2 bytes to prevent spurious pagefaults
            */
            physaddr_t inst_addr;
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
    physaddr_t phys_addr;
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
