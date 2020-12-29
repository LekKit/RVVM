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

static inline uint32_t riscv32_fetch_insn32(risc32_vm_state_t *vm)
{
    // Read 32-bit width instruction
    return ((uint32_t)vm->code[vm->code_pointer]) << 16 | vm->code[vm->code_pointer+1];
}

void riscv32_exec_instuction(risc32_vm_state_t *vm)
{
    uint32_t instruction = riscv32_fetch_insn32(vm);

    // FYI: Any jump instruction implementation should take care of PC increment
    // TODO: proper error handling (maybe not here)
    if ((instruction & RISCV32I_OPCODE_MASK) != RISCV32I_OPCODE_MASK) {
        // 16-bit opcode
        riscv32c_emulate(vm, vm->code[vm->code_pointer]);
        vm->code_pointer++;
    } else {
        riscv32i_emulate(vm, instruction);
        vm->code_pointer += 2;
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
