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

// translate compressed register encoding into normal
static inline uint32_t riscv32c_translate_reg(uint32_t reg)
{
    assert(reg < 8);
    return REGISTER_X8 + reg;
}

static void riscv32c_illegal_insn(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: illegal instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_addi4spn(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: ADDI4SPN instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_addi_nop(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: ADDI_NOP instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_slli(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: SLLI instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_fld(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: FLD instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_jal(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: JAL instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_fldsp(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: FLDSP instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_lw(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: LW instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_li(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: LI instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_lwsp(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: LWSP instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_flw(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: FLW instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_addi16sp_lui(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: ADDI16SP_LUI instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_flwsp(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: FLWSP instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_alops1(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: ALOPS1 (glue for CA) instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_alops2(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: ALOPS2 (glue for CR) instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_fsd(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: FSD instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_j(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: J instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_fsdsp(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: FSDSP instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_sw(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: SW instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_beqz(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: BEQZ instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_swsp(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: SWSP instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_fsw(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: FSW instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_bnez(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: BNEZ instruction 0x%x in VM %p\n", instruction, vm);
}

static void riscv32c_fswsp(risc32_vm_state_t *vm, uint16_t instruction)
{
    printf("RVC: FSWSP instruction 0x%x in VM %p\n", instruction, vm);
}

static void (*opcodes[32])(risc32_vm_state_t *vm, uint16_t instruction);

void riscv32c_init()
{
    for (uint32_t i=0; i<32; ++i) opcodes[i] = riscv32c_illegal_insn;

    opcodes[RVC_ADDI4SPN] = riscv32c_addi4spn;
    opcodes[RVC_ADDI_NOP] = riscv32c_addi_nop;
    opcodes[RVC_SLLI] = riscv32c_slli;
    opcodes[RVC_FLD] = riscv32c_fld;
    opcodes[RVC_JAL] = riscv32c_jal;
    opcodes[RVC_FLDSP] = riscv32c_fldsp;
    opcodes[RVC_LW] = riscv32c_lw;
    opcodes[RVC_LI] = riscv32c_li;
    opcodes[RVC_LWSP] = riscv32c_lwsp;
    opcodes[RVC_FLW] = riscv32c_flw;
    opcodes[RVC_ADDI16SP_LUI] = riscv32c_addi16sp_lui;
    opcodes[RVC_FLWSP] = riscv32c_flwsp;

    // Those are tricky fuckers and need additional decoding
    // basically a glue instruction for CR and CA instructions
    opcodes[RVC_ALOPS1] = riscv32c_alops1;
    opcodes[RVC_ALOPS2] = riscv32c_alops2;

    opcodes[RVC_FSD] = riscv32c_fsd;
    opcodes[RVC_J] = riscv32c_j;
    opcodes[RVC_FSDSP] = riscv32c_fsdsp;
    opcodes[RVC_SW] = riscv32c_sw;
    opcodes[RVC_BEQZ] = riscv32c_beqz;
    opcodes[RVC_SWSP] = riscv32c_swsp;
    opcodes[RVC_FSW] = riscv32c_fsw;
    opcodes[RVC_BNEZ] = riscv32c_bnez;
    opcodes[RVC_FSWSP] = riscv32c_fswsp;
}

void riscv32c_emulate(risc32_vm_state_t *vm, uint16_t instruction)
{
    uint16_t funcid = RISCV32C_GET_FUNCID(instruction);
    opcodes[funcid](vm, instruction);
}
