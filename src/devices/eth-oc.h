/*
eth-oc.h - OpenCores Ethernet MAC controller
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#ifndef ETH_OC_H
#define ETH_OC_H

#include "riscv32.h"
#include "rvvm_types.h"

#ifdef USE_NET
void ethoc_init(rvvm_hart_t *vm, const char *tap_name, paddr_t regs_base_addr, void *intc_data, uint32_t irq);
#else
static inline void ethoc_init(rvvm_hart_t *vm, const char *tap_name, paddr_t regs_base_addr, void *intc_data, uint32_t irq)
{
    UNUSED(vm);
    UNUSED(tap_name);
    UNUSED(regs_base_addr);
    UNUSED(intc_data);
    UNUSED(irq);
}
#endif

#endif
