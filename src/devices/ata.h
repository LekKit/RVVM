/*
ata.h - IDE/ATA disk controller
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

#ifndef ATA_H
#define ATA_H

#include "rvvmlib.h"
#include "pci-bus.h"
#include "blk_io.h"

#define ATA_DATA_DEFAULT_MMIO 0x40000000
#define ATA_CTL_DEFAULT_MMIO  0x40001000

PUBLIC void ata_init_pio(rvvm_machine_t* machine, rvvm_addr_t data_base_addr, rvvm_addr_t ctl_base_addr, blkdev_t* master, blkdev_t* slave);
PUBLIC void ata_init_pci(pci_bus_t* pci_bus, blkdev_t* master, blkdev_t* slave);

PUBLIC void ata_init_auto(rvvm_machine_t* machine, pci_bus_t* pci_bus, blkdev_t* image);

#endif
