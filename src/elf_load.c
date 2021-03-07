#include <elf.h>
#include <stdio.h>
#include <string.h>

#include "bit_ops.h"
#include "riscv32.h"
#include "elf_load.h"
#include "mem_ops.h"
#include "riscv32_csr.h"
#include "riscv32_mmu.h"

bool riscv32_elf_load_by_path(riscv32_vm_state_t *vm, const char *path, bool use_mmu, ssize_t offset)
{
	FILE *fp = fopen(path, "rb");
	if (!fp)
	{
		printf("Unable to open ELF file %s\n", path);
		return false;
	}

	bool status = true;
	Elf32_Ehdr ehdr;
	if (1 != fread(&ehdr, sizeof(ehdr), 1, fp))
	{
		printf("Unable to read ELF file header of %s\n", path);
		status = false;
		goto err_fclose;
		
	}

	size_t phdr_num = ehdr.e_phnum;
	if (phdr_num == PN_XNUM)
	{
		if (ehdr.e_shnum <= 0)
		{
			printf("Unable to load ELF section header of %s - section count is 0\n", path);
			status = false;
			goto err_fclose;
		}

		if (fseek(fp, ehdr.e_shoff, SEEK_SET))
		{
			printf("Unable to load ELF section header of %s - unable to seek to section offset\n", path);
			status = false;
			goto err_fclose;
		}

		Elf32_Shdr shdr;
		if (1 != fread(&shdr, sizeof(shdr), 1, fp))
		{
			printf("Unable to load ELF section header of %s\n", path);
			status = false;
			goto err_fclose;
		}

		phdr_num = shdr.sh_info;
	}

	size_t pgd = 0;
	size_t load_addr = offset;
	if (use_mmu)
	{
		/* set up page table */
		/* XXX: need to restore this on error exit? */

		/* put memory mappings at the end of physical memory to avoid overlap with program */
		pgd = vm->mem.begin + vm->mem.size - 4096;
		uint32_t satp = (pgd >> 12) | (1 << 31);
		riscv32_csr_op(vm, 0x180, &satp, CSR_SWAP);
	}

	for (size_t phdr_start = ehdr.e_phoff;
			phdr_start < ehdr.e_phoff + phdr_num * ehdr.e_phentsize;
			phdr_start += ehdr.e_phentsize)
	{
		if (fseek(fp, phdr_start, SEEK_SET))
		{
			printf("Unable to load ELF program header of %s - unable to seek to header offset 0x%lx\n", path, phdr_start);
			status = false;
			goto err_fclose;
		}

		Elf32_Phdr phdr;
		if (1 != fread(&phdr, sizeof(phdr), 1, fp))
		{
			printf("Unable to load ELF program header of %s - unable to read header offset 0x%lx\n", path, phdr_start);
			status = false;
			goto err_fclose;
		}

		if (phdr.p_type != PT_LOAD)
		{
			continue;
		}

		size_t dest_addr = load_addr + phdr.p_vaddr;
		if (dest_addr < vm->mem.begin || dest_addr + phdr.p_memsz >= vm->mem.begin + vm->mem.size)
		{
			printf("Unable to load ELF segment at offset 0x%lx of file %s - segment doesnt't fit in memory\n", phdr_start, path);
			printf("load addr: 0x%lx p_memsz: 0x%x end: 0x%lx\n", dest_addr, phdr.p_memsz, dest_addr + phdr.p_memsz);
			printf("mem begin: 0x%x size: 0x%x end: 0x%x\n", vm->mem.begin, vm->mem.size, vm->mem.begin + vm->mem.size);
			status = false;
			goto err_fclose;
		}

		if (fseek(fp, phdr.p_offset, SEEK_SET))
		{
			printf("Unable to load ELF segment at offset 0x%lx of file %s - unable to seek to start of the segment\n", phdr_start, path);
			status = false;
			goto err_fclose;
		}

		if (1 != fread(vm->mem.data + dest_addr, phdr.p_filesz, 1, fp))
		{
			printf("Unable to read ELF segment at offset 0x%lx of file %s\n", phdr_start, path);
			status = false;
			goto err_fclose;
		}

		/* fill .bss */
		memset(vm->mem.data + dest_addr + phdr.p_filesz, '\0', phdr.p_memsz - phdr.p_filesz);

		if (!use_mmu)
		{
			/* adjust entry point offset */
			if (ehdr.e_entry >= phdr.p_vaddr && ehdr.e_entry < phdr.p_vaddr + phdr.p_memsz)
			{
				ehdr.e_entry = ehdr.e_entry + load_addr;
			}

			continue;
		}

		/* fill page table */
		size_t segment_start_addr = dest_addr;

		size_t pte1_start = vm->root_page_table + GET_VPN1(phdr.p_vaddr) * 4;
		size_t pte1_count = GET_VPN1(phdr.p_memsz + gen_mask(22));//(page_count + gen_mask(10)) >> 10;
		size_t page_count = (phdr.p_memsz + gen_mask(12)) >> 12;
	
		size_t pte = MMU_VALID_PTE
			| MMU_PAGE_ACCESSED | MMU_PAGE_DIRTY /* optimize */
			| MMU_READ * !!(phdr.p_flags & PF_R)
			| MMU_WRITE * !!(phdr.p_flags & PF_W)
			| MMU_EXEC * !!(phdr.p_flags & PF_X);

		for (size_t i = 0; i < pte1_count; ++i)
		{
			size_t pte1_addr = pte1_start + i * 4;
			uint32_t pte1 = read_uint32_le(vm->mem.data + pte1_addr);

			size_t pte0_start;
			if (pte1 & MMU_VALID_PTE)
			{
				pte0_start = GET_PHYS_ADDR(pte1);
			}
			else
			{
				pgd -= 4096;
				pte0_start = pgd;
				assert((pte0_start & gen_mask(12)) == 0);

				write_uint32_le(vm->mem.data + pte1_addr,
						SET_PHYS_ADDR(MMU_VALID_PTE, pte0_start));
			}
			pte0_start += GET_VPN2(phdr.p_vaddr) * 4;

			size_t pte0_count = page_count > (1 << 10) ? (1 << 10) : page_count;

			for (size_t j = 0; j < pte0_count; ++j)
			{
				size_t pte0_addr = pte0_start + j * 4;

				write_uint32_le(vm->mem.data + pte0_addr,
						SET_PHYS_ADDR(pte, segment_start_addr + j * 4096));
			}

			page_count -= pte0_count;
			segment_start_addr += pte0_count * 4096;
		}

		assert(page_count == 0);
	}

	vm->registers[REGISTER_PC] = ehdr.e_entry;

#ifdef RV_DEBUG
	if (use_mmu)
	{
		riscv32_mmu_dump(vm);
	}
#endif

err_fclose:
	fclose(fp);
	return status;
}
