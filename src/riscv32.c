/*
riscv32.c - Very stupid and slow RISC-V emulator code
Copyright (C) 2020  Mr0maks <mr.maks0443@gmail.com>

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

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "riscv.h"
#include "riscv32.h"
#include "riscv32i.h"
#include "riscv32c.h"

uint16_t riscv32_fetch_code(risc32_vm_state_t *vm)
{
    // TODO: check for overflow here
    return vm->code[vm->code_pointer++];
}

void riscv32_exec_instuction(risc32_vm_state_t *vm)
{
    uint16_t chunk = riscv32_fetch_code(vm);
    uint32_t chunk2 = 0;
    uint32_t instruction = 0;

    if(chunk == 0xFFFF || chunk == 0x0000)
    {
        //printf("Illegal instruction\n");
        return; // TODO: fatal error here
    }

    // check for RISCV32_HAVE_ or any extension were 16 bit opcodes
    if((chunk & RISCV32I_OPCODE_MASK) != 0x3)
    {
        uint16_t encoding = (chunk & RISCV32C_OPCODE_MASK);

        // RISC32C opcode check
        if(encoding == 0x00 || encoding == 0x1 || encoding == 0x2)
        {
            riscv32c_emulate(vm, chunk);
        } else {
            printf("0x%x not a riscv32c instruction\n", chunk);
            return; // TODO: fatal error here
        }
    } else {
        chunk2 = riscv32_fetch_code(vm);
        instruction = ((uint32_t)(chunk2 << 16) | chunk);

        if( instruction == RISCV32I_ILLEGAL_OPCODE1 || instruction == RISCV32I_ILLEGAL_OPCODE2 )
            return; // TODO: fatal error here

        risv32i_emulate(vm, instruction);
    }
}

void riscv32_run(risc32_vm_state_t *vm)
{
    assert(vm);
    assert(vm->code);

    switch(setjmp(vm->jump_buff))
    {
    case 0: break;
    }

    while (vm->code_pointer != vm->code_len)
    {
        riscv32_exec_instuction(vm);
    }
}
