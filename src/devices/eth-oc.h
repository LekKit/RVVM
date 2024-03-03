/*
eth-oc.h - OpenCores Ethernet MAC controller
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>
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

#ifndef ETH_OC_H
#define ETH_OC_H

#include "rvvmlib.h"
#include "plic.h"
#include "tap_api.h"

#define ETHOC_DEFAULT_MMIO 0x21000000

PUBLIC rvvm_mmio_handle_t ethoc_init(rvvm_machine_t* machine, tap_dev_t* tap,
                                     rvvm_addr_t base_addr, plic_ctx_t* plic, uint32_t irq);
PUBLIC rvvm_mmio_handle_t ethoc_init_auto(rvvm_machine_t* machine);

#endif
