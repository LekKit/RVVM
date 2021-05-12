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

#include <string.h>
#include <inttypes.h>

#include "mem_ops.h"
#include "riscv.h"
#include "riscv32.h"
#include "riscv32i_registers.h"
#include "riscv32_mmu.h"
#include "riscv32_csr.h"
#include "elf_load.h"
#include "devices/ata.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif

typedef struct {
    const char* bootrom;
    const char* dtb;
    const char* flash_image;
    bool is_linux;
} vm_args_t;

size_t load_file_to_ram(riscv32_vm_state_t* vm, uint32_t addr, const char* filename)
{
    FILE* file = fopen(filename, "rb");
    size_t ret = 0;

    if (file == NULL) {
        printf("ERROR: Cannot open file %s.\n", filename);
        return ret;
    }

    char *buf = malloc(BUFSIZ);
    if (!buf)
    {
	    printf("ERROR: Unable to allocate buffer for file %s\n", filename);
	    goto err_fclose;
    }

    size_t to_end = vm->mem.begin + vm->mem.size - addr;
    size_t buffer_size = to_end < BUFSIZ ? to_end : BUFSIZ;

    while (addr >= vm->mem.begin && addr + buffer_size <= vm->mem.begin + vm->mem.size)
    {
	    size_t bytes_read = fread(vm->mem.data + addr, 1, buffer_size, file);
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
    free(buf);
err_fclose:
    fclose(file);
    return ret;
}

void parse_args(int argc, char** argv, vm_args_t* args)
{
    for (int i=1; i<argc; ++i) {
        if (strncmp(argv[i], "-dtb=", 5) == 0) {
    		args->dtb = argv[i] + 5;
    	} else if (strncmp(argv[i], "-image=", 7) == 0) {
    		args->flash_image = argv[i] + 7;
        } else if (strcmp(argv[i], "--linux") == 0) {
    		args->is_linux = true;
        } else {
            args->bootrom = argv[i];
        }
    }
}

// Temporary implementation, we probably need some kind of abstraction over mmap/MapViewOfFile/VirtualAlloc
#ifdef USE_FLASH
static bool flash_mmio_handler(riscv32_vm_state_t* vm, riscv32_mmio_device_t* device, uint32_t offset, void* data, uint32_t size, uint8_t op)
{
    uint8_t* devptr = ((uint8_t*)device->data) + offset;
    uint8_t* dataptr = (uint8_t*)data;
    UNUSED(vm);
    if (op == MMU_WRITE) {
        for (size_t i=0; i<size; ++i) devptr[i] = dataptr[i];
    } else {
        for (size_t i=0; i<size; ++i) dataptr[i] = devptr[i];
    }
    return true;
}

#ifdef _WIN32
static void init_flash(riscv32_vm_state_t* vm, uint32_t addr, const char* filename)
{
    HANDLE hf = CreateFileA(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL, 3, FILE_ATTRIBUTE_NORMAL, NULL);
    if (!hf) {
        printf("ERROR: Failed to open image %s\n", filename);
        return;
    }
    HANDLE hm = CreateFileMappingA(hf, NULL, PAGE_READWRITE, 0, 0, NULL);
    void* tmp = MapViewOfFile(hm, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    riscv32_mmio_add_device(vm, addr, addr + GetFileSize(hf, NULL) - 1, flash_mmio_handler, tmp);
}
#else
static void init_flash(riscv32_vm_state_t* vm, uint32_t addr, const char* filename)
{
    int fd = open(filename, O_RDWR);
    if (fd == -1) {
        printf("ERROR: Failed to open image %s\n", filename);
        return;
    }
    struct stat filestat;
    fstat(fd, &filestat);
    size_t filesize = filestat.st_size & ~(sysconf(_SC_PAGE_SIZE) - 1);
    void* tmp = mmap(NULL, filesize, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    riscv32_mmio_add_device(vm, addr, addr + filesize - 1, flash_mmio_handler, tmp);
}
#endif
#endif

int main(int argc, char** argv)
{
    vm_args_t args = {0};
#ifdef _WIN32
    // let the vm be run by simple double-click, heh
    args.dtb = "rvvm.dtb";
    args.bootrom = "fw_payload.bin";
    args.flash_image = "rootfs.img";
#endif
    parse_args(argc, argv, &args);
    if (args.bootrom == NULL)
    {
        printf("Usage: %s <bootrom> [--linux] [-dtb=<device.dtb>]\n", argv[0]);
        return 0;
    }

    riscv32_vm_state_t* vm = riscv32_create_vm();
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
			    void* addr = vm->mem.data + vm->mem.begin + vm->mem.size - 0x200000 + dts;
			    write_uint32_le(addr, 0x4942534F); // magic
			    write_uint32_le(addr+4, 0x2); // version
			    write_uint32_le(addr+8, 0x0); // next_addr
			    write_uint32_le(addr+12, 0x1); // next_mode
			    write_uint32_le(addr+16, 0x1); // options
			    write_uint32_le(addr+20, 0x0); // boot_hart
			    riscv32i_write_register_u(vm, REGISTER_X12, vm->mem.begin + vm->mem.size - 0x200000 + dts);
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

    if (args.flash_image) {
#ifdef USE_FLASH
	    init_flash(vm, 0x40000000, args.flash_image);
#else
	    FILE *fp = fopen(args.flash_image, "rw+");
	    if (fp == NULL) {
		    printf("Unable to open image file %s\n", args.flash_image);
	    } else {
		    ata_init(vm, 0x40000000, 0x40001000, fp, NULL);
	    }
#endif
    }

    riscv32_run(vm);

    riscv32_destroy_vm(vm);
    return 0;
}
