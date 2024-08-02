/*
mtd-physmap.h - Memory Technology Device Mapping
Copyright (C) 2023  LekKit <github.com/LekKit>

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

#ifndef RVVM_MTD_PHYSMAP_H
#define RVVM_MTD_PHYSMAP_H

#include "rvvmlib.h"

/*
 * The main purpose of this device is to allow guests to flash
 * different firmware into the board memory chip
 */

#define MTD_PHYSMAP_DEFAULT_MMIO 0x04000000

PUBLIC rvvm_mmio_dev_t* mtd_physmap_init_blk(rvvm_machine_t* machine, rvvm_addr_t addr, void* blk_dev);
PUBLIC rvvm_mmio_dev_t* mtd_physmap_init(rvvm_machine_t* machine, rvvm_addr_t addr, const char* image_path, bool rw);
PUBLIC rvvm_mmio_dev_t* mtd_physmap_init_auto(rvvm_machine_t* machine, const char* image_path, bool rw);

#endif

