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
#include <string.h>

#include "riscv.h"
#include "riscv32.h"
#include "riscv32i.h"
#include "riscv32i_registers.h"
#include "riscv32c.h"
#include "mem_ops.h"

void (*riscv32_opcodes[1024])(risc32_vm_state_t *vm, uint32_t instruction);

void riscv32c_illegal_insn(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("RV32: illegal instruction 0x%x in VM %p\n", instruction, vm);
}

void smudge_opcode_func3(uint32_t opcode, void (*func)(risc32_vm_state_t*, uint32_t))
{
    for (uint32_t f3=0; f3<8; ++f3)
        riscv32_opcodes[(f3 << 7) | opcode] = func;
}

risc32_vm_state_t *riscv32_create_vm()
{
    static bool global_init = false;
    if (!global_init) {
        for (uint32_t i=0; i<1024; ++i) riscv32_opcodes[i] = riscv32c_illegal_insn;
        riscv32i_init();
        riscv32c_init();
        global_init = true;
    }

    risc32_vm_state_t *vm = (risc32_vm_state_t*)malloc(sizeof(risc32_vm_state_t));
    memset(vm, 0, sizeof(risc32_vm_state_t));
    // Put other stuff here

    return vm;
}

void riscv32_destroy_vm(risc32_vm_state_t *vm)
{
    free(vm);
}

void riscv32_exec_instruction(risc32_vm_state_t *vm)
{
    // FYI: Any jump instruction implementation should take care of PC increment
    // TODO: proper error handling (maybe not here)
    if ((vm->code[vm->code_pointer] & RISCV32I_OPCODE_MASK) != RISCV32I_OPCODE_MASK) {
        // 16-bit opcode
        riscv32c_emulate(vm, read_uint16_le(vm->code+vm->code_pointer));
        vm->code_pointer += 2;
    } else {
        riscv32i_emulate(vm, read_uint32_le(vm->code+vm->code_pointer));
        vm->code_pointer += 4;
    }

    int i = 0;
    int k = 0;

    for( i = 0; i < REGISTERS_MAX; i++ )
    {
        for(k = 0; k < 2; i++,k++)
        {
            printf("%s: (0x%X %i) ", riscv32i_translate_register(i), riscv32i_read_register_u(vm, i), riscv32i_read_register_s(vm, i));
        }
        printf("%s: (0x%X %i)\n", riscv32i_translate_register(i), riscv32i_read_register_u(vm, i), riscv32i_read_register_s(vm, i));
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
        riscv32_exec_instruction(vm);
    }
}
