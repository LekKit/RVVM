/*
plic.h - Platform-Level Interrupt Controller
Copyright (C) 2023  LekKit <github.com/LekKit>
              2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#ifndef RVVM_PLIC_H
#define RVVM_PLIC_H

#include "rvvmlib.h"

#define PLIC_DEFAULT_MMIO 0xC000000

PUBLIC plic_ctx_t* plic_init(rvvm_machine_t* machine, rvvm_addr_t base_addr);
PUBLIC plic_ctx_t* plic_init_auto(rvvm_machine_t* machine);

// Allocate new IRQ
PUBLIC uint32_t plic_alloc_irq(plic_ctx_t* plic);

// Get FDT phandle of the PLIC
PUBLIC uint32_t plic_get_phandle(plic_ctx_t* plic);

// Send IRQ through PLIC
PUBLIC bool plic_send_irq(plic_ctx_t* plic, uint32_t irq);

// Assert IRQ line level
PUBLIC bool plic_raise_irq(plic_ctx_t* plic, uint32_t irq);
PUBLIC bool plic_lower_irq(plic_ctx_t* plic, uint32_t irq);

#endif
