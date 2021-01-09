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

#include "riscv.h"
#include "riscv32.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf( "ERROR: No file provided.\n" );
        return 0;
    }

    FILE *fp;

    if ((fp=fopen(argv[1], "rb")) == NULL) {
        printf( "ERROR: Cannot open file %s.\n", argv[1] );
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size & 0x1) {
        printf("Misaligned file\n");
        fclose(fp);
        return 0;
    }

    if (file_size < sizeof (uint16_t)) {
        printf("File too small\n");
        fclose(fp);
        return 0;
    }

    uint8_t *bytecode = malloc(file_size);

    fread(bytecode, 1, file_size, fp);

    fclose(fp);

    risc32_vm_state_t *vm = riscv32_create_vm();

    if (!vm) {
        printf("ERROR: VM creation failed.\n");
        return 1;
    }

    vm->code = bytecode;
    vm->code_len = file_size;

    riscv32_run(vm);

    riscv32_destroy_vm(vm);
    free(bytecode);
    return 0;
}
