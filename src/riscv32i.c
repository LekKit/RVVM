/*
riscv32i.c - RISC-V Interger instruction emulator
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

#include "riscv.h"
#include "riscv32.h"

// translate register number into abi name
const char *riscv32i_translate_register_interger(uint32_t reg)
{
    assert( reg < REGISTERS_MAX );
    switch (reg) {
    case REGISTER_ZERO: return "zero";
    case REGISTER_X1: return "ra";
    case REGISTER_X2: return "sp";
    case REGISTER_X3: return "gp";
    case REGISTER_X4: return "tp";
    case REGISTER_X5: return "t0";
    case REGISTER_X6: return "t1";
    case REGISTER_X7: return "t2";
    case REGISTER_X8: return "s0/fp";
    case REGISTER_X9: return "s1";
    case REGISTER_X10: return "a0";
    case REGISTER_X11: return "a1";
    case REGISTER_X12: return "a2";
    case REGISTER_X13: return "a3";
    case REGISTER_X14: return "a4";
    case REGISTER_X15: return "a5";
    case REGISTER_X16: return "a6";
    case REGISTER_X17: return "a7";
    case REGISTER_X18: return "s2";
    case REGISTER_X19: return "a3";
    case REGISTER_X20: return "a4";
    case REGISTER_X21: return "a5";
    case REGISTER_X22: return "a6";
    case REGISTER_X23: return "a7";
    case REGISTER_X24: return "a8";
    case REGISTER_X25: return "a9";
    case REGISTER_X26: return "a10";
    case REGISTER_X27: return "a11";
    case REGISTER_X28: return "t3";
    case REGISTER_X29: return "t4";
    case REGISTER_X30: return "t5";
    case REGISTER_X31: return "t6";
    case REGISTER_PC: return "pc";
    default: return "unknown";
    }
}

void riscv32i_emulate_0x00(risc32_vm_state_t *vm, uint32_t instruction)
{
    uint32_t rds = ((instruction >> 7) & 0x1f);
    uint32_t func3 = ((instruction >> 12) & 0x3);
    uint32_t rs1 = ((instruction >> 15) & 0x1f);
    uint32_t imm = (instruction >> 20);

    printf("riscv32i 0x00 opcode\n");
}

void riscv32i_emulate_0x03(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("riscv32i 0x03 opcode\n");
}

void riscv32i_emulate_0x04(risc32_vm_state_t *vm, uint32_t instruction)
{
    uint32_t rds = ((instruction >> 7) & 0x1f);
    uint32_t func3 = ((instruction >> 12) & 0x3);
    uint32_t rs1 = ((instruction >> 15) & 0x1f);
    uint32_t imm = (instruction >> 20);

    switch (func3) {
    case 0x00:
    {
        printf("addi %u,%u,%u\n", rds, rs1, imm);
        break;
    }
    case 0x01:
    {
        if( imm == 0 )
            printf("slli %u,%u,%u\n", rds, rs1, imm);
        break;
    }
    case 0x02:
    {
        printf("slti %u,%u,%u\n", rds, rs1, imm);
        break;
    }
    case 0x03:
    {
        printf("sltiu %u,%u,%u\n", rds, rs1, imm);
        break;
    }
    case 0x04:
    {
        printf("xori %u,%u,%u\n", rds, rs1, imm);
        break;
    }
    case 0x05:
    {
        if( imm == 0 ) {
            printf("srli %u,%u,%u\n", rds, rs1, imm);
        } else if( imm == 16 )
        {
            printf("srai %u,%u,%u\n", rds, rs1, imm);
        }
        break;
    }
    case 0x06:
    {
        printf("ori %u,%u,%u\n", rds, rs1, imm);
        break;
    }
    case 0x07:
    {
        printf("andi %u,%u,%u\n", rds, rs1, imm);
        break;
    }

    }
}

void riscv32i_emulate_0x05(risc32_vm_state_t *vm, uint32_t instruction)
{
    uint32_t rds = ((instruction >> 7) & 0x1f);
    uint32_t imm = (instruction >> 12);
    printf("auipc %u, %i\n", rds, imm);
}

void riscv32i_emulate_0x08(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("riscv32i 0x08 opcode\n");
}


void riscv32i_emulate_0x0C(risc32_vm_state_t *vm, uint32_t instruction)
{
    uint32_t rds = ((instruction >> 7) & 0x1f);
    uint32_t func3 = ((instruction >> 12) & 0x3);
    uint32_t rs1 = ((instruction >> 15) & 0x1f);
    uint32_t rs2 = ((instruction >> 20) & 0x1f);
    uint32_t func7 = (instruction >> 25);

    switch (func3) {
    case 0:
    {
        if(func7 == 0x20)
            printf("sub %u,%u,%u\n", rds, rs1, rs2);
        else
            printf("add %u,%u,%u\n", rds, rs1, rs2);
        break;
    }
    case 1:
    {
        printf("sll %u,%u,%u\n", rds, rs1, rs2);
        break;
    }
    case 2:
    {
        printf("slt %u,%u,%u\n", rds, rs1, rs2);
        break;
    }
    case 3:
    {
        printf("sltu %u,%u,%u\n", rds, rs1, rs2);
        break;
    }
    case 4:
    {
        printf("xor %u,%u,%u\n", rds, rs1, rs2);
        break;
    }
    case 5:
    {
        if(func7 == 0x20)
            printf("sra %u,%u,%u\n", rds, rs1, rs2);
        else
            printf("srl %u,%u,%u\n", rds, rs1, rs2);
        break;
    }
    case 6:
    {
        printf("or %u,%u,%u\n", rds, rs1, rs2);
        break;
    }
    case 7:
    {
        printf("and %u,%u,%u\n", rds, rs1, rs2);
        break;
    }
    }
}

void riscv32i_emulate_0x0D(risc32_vm_state_t *vm, uint32_t instruction)
{
    uint32_t rds = ((instruction >> 7) & 0x1f);
    uint32_t imm = (instruction >> 12);
    printf("lui %u, %u\n", rds, imm);
}

void riscv32i_emulate_0x18(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("riscv32i 0x18 opcode\n");
}

void riscv32i_emulate_0x19(risc32_vm_state_t *vm, uint32_t instruction)
{
    uint32_t imm = 0;
}

void riscv32i_emulate_0x1B(risc32_vm_state_t *vm, uint32_t instruction)
{
    printf("riscv32i 0x1b opcode\n");
}

// We already check instruction for correct code
void risv32i_emulate(risc32_vm_state_t *vm, uint32_t instruction)
{
    uint32_t opcode = ((instruction >> 2) & 0x1f);

    switch (opcode) {
    case 0x00:
    {
        riscv32i_emulate_0x00(vm, instruction);
        break;
    }
    case 0x03:
    {
        riscv32i_emulate_0x03(vm, instruction);
        break;
    }
    case 0x04:
    {
        riscv32i_emulate_0x04(vm, instruction);
        break;
    }
    case 0x05:
    {
        riscv32i_emulate_0x05(vm, instruction);
        break;
    }
    case 0x08:
    {
        riscv32i_emulate_0x08(vm, instruction);
        break;
    }
    case 0x0C:
    {
        riscv32i_emulate_0x0C(vm, instruction);
        break;
    }
    case 0x0D:
    {
        riscv32i_emulate_0x0D(vm, instruction);
        break;
    }
    case 0x18:
    {
        riscv32i_emulate_0x18(vm, instruction);
        break;
    }
    case 0x19:
    {
        riscv32i_emulate_0x19(vm, instruction);
        break;
    }
    case 0x1B:
    {
        riscv32i_emulate_0x1B(vm, instruction);
        break;
    }
    }

    vm->registers[REGISTER_PC]++;
}
