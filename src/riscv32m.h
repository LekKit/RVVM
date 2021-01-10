/*
riscv32m.h - RISC-V M instructions extension emulator definitions
Copyright (C) 2021  Mr0maks <mr.maks0443@gmail.com>

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

#include "riscv.h"
#include "riscv32.h"

#define RISCV32C_VERSION 20 // 2.0

#define RV32M_MUL          0x10C
#define RV32M_MULH         0x12C
#define RV32M_MULHSU       0x14C
#define RV32M_MULHU        0x16C
#define RV32M_DIV          0x18C
#define RV32M_DIVU         0x1AC
#define RV32M_REM          0x1CC
#define RV32M_REMU         0x1EC
