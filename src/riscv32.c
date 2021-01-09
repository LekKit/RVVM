/*
riscv32.c - Very stupid and slow RISC-V emulator code
Copyright (C) 2021  Mr0maks <mr.maks0443@gmail.com>
                    LekKit <github.com/LekKit>

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

#include <stdarg.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#include "riscv.h"
#include "riscv32.h"
#include "riscv32i.h"
#include "riscv32c.h"
#include "mem_ops.h"

void (*riscv32_opcodes[1024])(risc32_vm_state_t *vm, const uint32_t instruction);

void riscv32_illegal_insn(risc32_vm_state_t *vm, const uint32_t instruction)
{
    riscv32_error(vm, "RV32: illegal instruction 0x%x in VM %p\n", instruction, vm);
}

void smudge_opcode_func3(uint32_t opcode, void (*func)(risc32_vm_state_t*, const uint32_t))
{
    for (uint32_t f3=0; f3<8; ++f3)
        riscv32_opcodes[(f3 << 7) | opcode] = func;
}

risc32_vm_state_t *riscv32_create_vm()
{
    static bool global_init = false;
    if (!global_init) {
        for (uint32_t i=0; i<1024; ++i) riscv32_opcodes[i] = riscv32_illegal_insn;
        riscv32i_init();
        riscv32m_init();
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

void riscv32_dump_registers(risc32_vm_state_t *vm)
{
    for ( int i = 0; i < REGISTERS_MAX - 1; i++ ) {
        printf("%-5s: 0x%08X  ", riscv32i_translate_register(i), riscv32i_read_register_u(vm, i));

        if (((i + 1) % 4) == 0)
            printf("\n");
    }
    printf("%-5s: 0x%08X\n", riscv32i_translate_register(32), riscv32i_read_register_u(vm, 32));
}

void riscv32_error(risc32_vm_state_t *vm, const char *fmt, ...)
{
    assert(vm);
    assert(fmt);

    vm->error = true;

    va_list va;
    va_start(va, fmt);
    int len = vsnprintf(vm->error_string, sizeof (vm->error_string), fmt, va);
    va_end(va);

    if (len < 0)
    {
        err(EXIT_FAILURE, "vsnprintf");
    }

    riscv32_dump_registers(vm);

    longjmp(vm->jump_buff, 1);
}

void riscv32_exec_instruction(risc32_vm_state_t *vm)
{
    // FYI: Any jump instruction implementation should take care of PC increment
    // TODO: proper error handling (maybe not here)
    if ((vm->code[vm->registers[REGISTER_PC]] & RISCV32I_OPCODE_MASK) != RISCV32I_OPCODE_MASK) {
        // 16-bit opcode
        riscv32c_emulate(vm, read_uint16_le(vm->code+vm->registers[REGISTER_PC]));
        vm->registers[REGISTER_PC] += 2;
    } else {
        riscv32i_emulate(vm, read_uint32_le(vm->code+vm->registers[REGISTER_PC]));
        vm->registers[REGISTER_PC] += 4;
    }

    riscv32_dump_registers(vm);
}

void riscv32_run(risc32_vm_state_t *vm)
{
    assert(vm);
    assert(vm->code);

    switch (setjmp(vm->jump_buff)) {
    case 0: break;
    case 1: printf("vm error: %s\n", vm->error_string);
    default: return;
    }

    while (vm->registers[REGISTER_PC] != vm->code_len) {
        riscv32_exec_instruction(vm);
    }
}
