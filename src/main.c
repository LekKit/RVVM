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

#include <stdio.h>
#include <stdio.h>
#include <malloc.h>
#include <stdint.h>
#include <string.h>

#include "mem_ops.h"
#include "riscv.h"
#include "riscv32.h"
#include "riscv32i.h"

typedef struct {
    const char* bootrom;
    const char* dtb;
} vm_args_t;

uint32_t load_file_to_ram(riscv32_vm_state_t* vm, uint32_t addr, const char* filename)
{
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        printf("ERROR: Cannot open file %s.\n", filename);
        return 0;
    }
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (vm->mem.begin - addr < file_size) {
        printf("ERROR: File %s does not fit in VM RAM.\n", filename);
        fclose(file);
        return 0;
    }
    fread(vm->mem.data + vm->mem.begin + addr, 1, file_size, file);
    fclose(file);
    return file_size;
}

void parse_args(int argc, char** argv, vm_args_t* args)
{

    for (int i=1; i<argc; ++i) {
        if (strncmp(argv[i], "-dtb=", 5) == 0) {
            args->dtb = argv[i] + 5;
        } else args->bootrom = argv[i];
    }
}

int main(int argc, char** argv)
{
    vm_args_t args = {NULL};
    parse_args(argc, argv, &args);
    if (args.bootrom == NULL) {
        printf("Usage: %s [bootrom] -dtb=[device.dtb]\n", argv[0]);
        return 0;
    }

    riscv32_vm_state_t* vm = riscv32_create_vm();
    if (vm == NULL) {
        printf("ERROR: VM creation failed.\n");
        return 1;
    }

    if (load_file_to_ram(vm, 0, args.bootrom) == 0) {
        printf("ERROR: Failed to load bootrom.\n");
        return 1;
    }

    if (args.dtb) {
        // Explicitly set a0 to 0 as boot hart
        riscv32i_write_register_u(vm, REGISTER_X10, 0);
        // DTB is aligned by 2MB
        uint32_t dts = load_file_to_ram(vm, vm->mem.size - 0x200000, args.dtb);
        if (dts == 0) {
           printf("ERROR: Failed to load DTB.\n");
           return 1;
       }
       // pass DTB address in a1 register
       riscv32i_write_register_u(vm, REGISTER_X11, vm->mem.begin + vm->mem.size - 0x200000);

       // OpenSBI FW_DYNAMIC struct passed in a2 register
       if (0x200000 - dts >= 24) {
           void* addr = vm->mem.data + vm->mem.begin + vm->mem.size - 0x200000 + dts;
           write_uint32_le(addr, 0x4942534F); // magic
           write_uint32_le(addr+4, 0x2); // version
           write_uint32_le(addr+8, 0x0); // next_addr
           write_uint32_le(addr+12, 0x1); // next_mode
           write_uint32_le(addr+16, 0x0); // options
           write_uint32_le(addr+20, 0x0); // boot_hart
           riscv32i_write_register_u(vm, REGISTER_X12, vm->mem.begin + vm->mem.size - 0x200000 + dts);
       } else printf("WARN: No space for FW_DYNAMIC struct\n");
    }

    riscv32_run(vm);

    riscv32_destroy_vm(vm);
    return 0;
}
