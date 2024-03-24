/*
riscv_priv.h - RISC-V Privileged
Copyright (C) 2021  LekKit <github.com/LekKit>

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

#ifndef RISCV_PRIV_H
#define RISCV_PRIV_H

#include "rvvm.h"

NOINLINE void riscv_emulate_opc_system(rvvm_hart_t* vm, const uint32_t insn);
NOINLINE void riscv_emulate_opc_misc_mem(rvvm_hart_t* vm, const uint32_t insn);

#endif
