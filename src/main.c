/*
main.c - Entry point
Copyright (C) 2021  Mr0maks <mr.maks0443@gmail.com>

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
#include <string.h>

#include "mem_ops.h"
#include "riscv.h"
#include "riscv32.h"
#include "riscv32i_registers.h"
#include "riscv32_mmu.h"
#include "riscv32_csr.h"
#include "elf_load.h"
#include "devices/ata.h"

typedef struct {
    const char* bootrom;
    const char* dtb;
    const char* image;
    bool is_linux;
} vm_args_t;

size_t load_file_to_ram(rvvm_hart_t* vm, uint32_t addr, const char* filename)
{
    FILE* file = fopen(filename, "rb");
    size_t ret = 0;

    if (file == NULL) {
        printf("ERROR: Cannot open file %s.\n", filename);
        return ret;
    }

#ifdef USE_VMSWAP
    char *buf = malloc(BUFSIZ);
    if (!buf)
    {
	    printf("ERROR: Unable to allocate buffer for file %s\n", filename);
	    goto err_fclose;
    }
#endif

    size_t to_end = vm->mem.begin + vm->mem.size - addr;
    size_t buffer_size = to_end < BUFSIZ ? to_end : BUFSIZ;

    while (addr >= vm->mem.begin && addr + buffer_size <= vm->mem.begin + vm->mem.size)
    {
#ifdef USE_VMSWAP
	    size_t bytes_read = fread(buf, 1, buffer_size, file);
	    riscv32_physmem_op(vm, addr, (vmptr_t)buf, buffer_size, MMU_WRITE);
#else
	    size_t bytes_read = fread(vm->mem.data + addr, 1, buffer_size, file);
#endif
	    ret += bytes_read;
	    addr += bytes_read;

	    if (buffer_size != bytes_read)
	    {
		    break;
	    }
    }

    if (!feof(file))
    {
        printf("ERROR: File %s does not fit in VM RAM. Bytes read: 0x%zx\n", filename, ret);
        ret = 0;
        goto err_free;
    }

err_free:
#ifdef USE_VMSWAP
    free(buf);
err_fclose:
#endif
    fclose(file);
    return ret;
}

void parse_args(int argc, char** argv, vm_args_t* args)
{
    for (int i=1; i<argc; ++i) {
        if (strncmp(argv[i], "-dtb=", 5) == 0) {
    		args->dtb = argv[i] + 5;
    	} else if (strncmp(argv[i], "-image=", 7) == 0) {
    		args->image = argv[i] + 7;
        } else if (strcmp(argv[i], "--linux") == 0) {
    		args->is_linux = true;
        } else {
            args->bootrom = argv[i];
        }
    }
}

int rvvm_run_with_args(vm_args_t args)
{
    rvvm_hart_t* vm = riscv32_create_vm();
    if (vm == NULL)
    {
        printf("ERROR: VM creation failed.\n");
        return 1;
    }

    if (args.is_linux)
    {
	    if (!riscv32_elf_load_by_path(vm, args.bootrom, true, 0))
	    {
		printf("ERROR: Failed to load vmlinux ELF file.\n");
		return 1;
	    }
    }
    else if (load_file_to_ram(vm, vm->mem.begin, args.bootrom) == 0)
    {
        printf("ERROR: Failed to load bootrom.\n");
        return 1;
    }

    if (args.dtb)
    {
	    size_t dtb_addr = vm->mem.begin + vm->mem.size - 0x8000000;

	    // Explicitly set a0 to 0 as boot hart
	    riscv32i_write_register_u(vm, REGISTER_X10, 0);

	    // DTB is aligned by 2MB
	    uint32_t dts = load_file_to_ram(vm, dtb_addr, args.dtb);
	    if (dts == 0)
	    {
		    printf("ERROR: Failed to load DTB.\n");
		    return 1;
	    }

	    printf("DTB loaded at: 0x%zx size: %"PRId32"\n", dtb_addr, dts);

	    // pass DTB address in a1 register
	    riscv32i_write_register_u(vm, REGISTER_X11, dtb_addr);

	    if (args.is_linux)
	    {
		    uint32_t medeleg = -1;
		    riscv32_csr_op(vm, 0x302, &medeleg, CSR_SWAP);

		    vm->priv_mode = PRIVILEGE_SUPERVISOR;
	    }
	    else
	    {
		    // OpenSBI FW_DYNAMIC struct passed in a2 register
		    if (vm->mem.size - dtb_addr + dts >= 24)
		    {
			    paddr_t paddr = vm->mem.begin + vm->mem.size - 0x200000 + dts;
#ifdef USE_VMSWAP
			    uint32_t addr[24];
#else
			    void* addr = vm->mem.data + paddr;
#endif
			    write_uint32_le(addr, 0x4942534F); // magic
			    write_uint32_le(addr+4, 0x2); // version
			    write_uint32_le(addr+8, 0x0); // next_addr
			    write_uint32_le(addr+12, 0x1); // next_mode
			    write_uint32_le(addr+16, 0x1); // options
			    write_uint32_le(addr+20, 0x0); // boot_hart
#ifdef USE_VMSWAP
			    riscv32_physmem_op(vm, paddr, (vmptr_t)addr, sizeof(addr), MMU_WRITE);
#endif
			    riscv32i_write_register_u(vm, REGISTER_X12, paddr);
		    }
		    else
		    {
			    printf("WARN: No space for FW_DYNAMIC struct\n");
		    }

#if 0
		    //XXX - Linux raw image
		    uint32_t medeleg = -1;
		    riscv32_csr_op(vm, 0x302, &medeleg, CSR_SWAP);
		    vm->priv_mode = PRIVILEGE_SUPERVISOR;
#endif
	    }
    }

    if (args.image) {
	    FILE *fp = fopen(args.image, "rb+");
	    if (fp == NULL) {
		    printf("Unable to open image file %s\n", args.image);
	    } else {
		    ata_init(vm, 0x40000000, 0x40001000, fp, NULL);
	    }
    }


    riscv32_run(vm);

    riscv32_destroy_vm(vm);
    return 0;
}

int main(int argc, char** argv)
{
    vm_args_t args = {0};
#ifdef _WIN32
    // let the vm be run by simple double-click, heh
    args.dtb = "rvvm.dtb";
    args.bootrom = "fw_payload.bin";
    args.image = "rootfs.img";
#endif
    parse_args(argc, argv, &args);
    if (args.bootrom == NULL)
    {
        printf("Usage: %s <bootrom> [--linux] [-dtb=<device.dtb>]\n", argv[0]);
        return 0;
    }

    rvvm_run_with_args(args);
    return 0;
}

