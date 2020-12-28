#pragma once

#include "riscv32.h"

inline uint32_t riscv32i_read_register_u(risc32_vm_state_t *vm, uint32_t reg)
{
    assert(reg < REGISTERS_MAX);

    // always return 0 for x0
    if(reg == REGISTER_X0)
        return 0;

    return vm->registers[reg];
}

inline void riscv32i_write_register_u(risc32_vm_state_t *vm, uint32_t reg, uint32_t data)
{
    assert(reg < REGISTERS_MAX);

    // always ignore for x0
    if(reg == REGISTER_X0)
        return;

    vm->registers[reg] = data;
}

inline int32_t riscv32i_read_register_s(risc32_vm_state_t *vm, uint32_t reg)
{
    assert(reg < REGISTERS_MAX);

    // always return 0 for x0
    if(reg == REGISTER_X0)
        return 0;

    return vm->registers[reg];
}

inline void riscv32i_write_register_s(risc32_vm_state_t *vm, uint32_t reg, int32_t data)
{
    assert(reg < REGISTERS_MAX);

    // always ignore for x0
    if(reg == REGISTER_X0)
        return;

    vm->registers[reg] = data;
}


