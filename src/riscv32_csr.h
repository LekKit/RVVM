/*
riscv32_csr.h - RISC-V Control and Status Register
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

#pragma once

#include "riscv32.h"

bool riscv32_csr_read(riscv32_vm_state_t *vm, uint32_t csr, uint32_t reg);
bool riscv32_csr_write(riscv32_vm_state_t *vm, uint32_t csr, uint32_t reg);
bool riscv32_csr_swap(riscv32_vm_state_t *vm, uint32_t csr, uint32_t rs, uint32_t rds);
void riscv32_csr_init(riscv32_vm_state_t *vm, const char *name, uint32_t csr, uint32_t (*callback_r)(riscv32_vm_state_t *vm, riscv32_csr_t *self), void (*callback_w)(riscv32_vm_state_t *vm, riscv32_csr_t *self, uint32_t value));
