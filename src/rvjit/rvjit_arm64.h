/*
rvjit_arm64.h - RVJIT ARM64 Backend
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

#include "rvjit.h"
#include "mem_ops.h"

#ifndef RVJIT_ARM64_H
#define RVJIT_ARM64_H

#define ARM64_REG_X0 0x0

#ifdef RVJIT_ABI_SYSV
#define VM_PTR_REG ARM64_REG_X0
#endif

static inline size_t rvjit_native_default_hregmask()
{
    // X0 - X15 registers are caller-saved
    // X0 is preserver as vmptr
    // X16-X18 are possibly usable, needs more research
    return 0xFFFE;
}

static inline size_t rvjit_native_abireclaim_hregmask()
{
    // We have enough caller-saved registers, no need for push/pop as well
    return 0;
}

// TODO: needs to be actually implemented :O
#error No support for ARM64 yet!

#endif
