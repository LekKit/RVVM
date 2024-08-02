/*
syscon.h - Poweroff/reset syscon device
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

#ifndef RVVM_SYSCON_H
#define RVVM_SYSCON_H

#include "rvvmlib.h"

#define SYSCON_DEFAULT_MMIO 0x100000

PUBLIC rvvm_mmio_dev_t* syscon_init(rvvm_machine_t* machine, rvvm_addr_t base_addr);
PUBLIC rvvm_mmio_dev_t* syscon_init_auto(rvvm_machine_t* machine);

#endif
