/*
riscv32i_registers.h - register functions definitions for riscv32i
Copyright (C) 2021  LekKit <github.com/LekKit>
                    Mr0maks <mr.maks0443@gmail.com>

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

#pragma once

#include "bit_ops.h"
#include "riscv32.h"

static inline reg_t riscv32i_read_register_u(const riscv32_vm_state_t *vm, uint32_t reg)
{
    assert(reg < REGISTERS_MAX);
    return vm->registers[reg] & gen_mask(XLEN(vm));
}

static inline void riscv32i_write_register_u(riscv32_vm_state_t *vm, uint32_t reg, reg_t data)
{
    assert(reg < REGISTERS_MAX);
    vm->registers[reg] = sign_extend(data, XLEN(vm));
}

static inline sreg_t riscv32i_read_register_s(const riscv32_vm_state_t *vm, uint32_t reg)
{
    assert(reg < REGISTERS_MAX);
    return sign_extend(vm->registers[reg] & gen_mask(XLEN(vm)), XLEN(vm));
}

static inline void riscv32i_write_register_s(riscv32_vm_state_t *vm, uint32_t reg, sreg_t data)
{
    assert(reg < REGISTERS_MAX);
    vm->registers[reg] = sign_extend(data, XLEN(vm));
}

const char *riscv32i_translate_register(uint32_t reg);
