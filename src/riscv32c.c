/*
riscv32c.c - RISC-V C instructions extension emulator
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

#include "riscv.h"
#include "riscv32.h"
#include "riscv32c.h"

// traslate compressed register encoding into normal
static uint32_t riscv32c_translate_register_interger(uint32_t reg)
{
    assert( reg < 0x08);
/*
    switch (reg) {
    case 0x00: return REGISTER_X8;
    case 0x01: return REGISTER_X9;
    case 0x02: return REGISTER_X10;
    case 0x03: return REGISTER_X11;
    case 0x04: return REGISTER_X12;
    case 0x05: return REGISTER_X13;
    case 0x06: return REGISTER_X14;
    case 0x07: return REGISTER_X15;
    }
*/
    return REGISTER_X8 + reg;
}

void riscv32c_emulate_C0(risc32_vm_state_t *vm, uint16_t instruction)
{

}

void riscv32c_emulate_C1(risc32_vm_state_t *vm, uint16_t instruction)
{

}
void riscv32c_emulate_C2(risc32_vm_state_t *vm, uint16_t instruction)
{

}

// We already check instruction for correct code
void riscv32c_emulate(risc32_vm_state_t *vm, uint16_t instruction)
{
    uint16_t opcode = (instruction & RISCV32C_OPCODE_MASK);

    switch (opcode) {
    case 0x00:
    {
        riscv32c_emulate_C0(vm, instruction);
        break;
    }
    case 0x01:
    {
        riscv32c_emulate_C1(vm, instruction);
        break;
    }
    case 0x02:
    {
        riscv32c_emulate_C2(vm, instruction);
        break;
    }
    }
}
