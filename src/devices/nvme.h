/*
nvme.h - Non-Volatile Memory Express
Copyright (C) 2022  LekKit <github.com/LekKit>

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

#ifndef NVME_H
#define NVME_H

#include "rvvmlib.h"
#include "pci-bus.h"

PUBLIC pci_dev_t* nvme_init_blk(pci_bus_t* pci_bus, void* blk_dev);
PUBLIC pci_dev_t* nvme_init(pci_bus_t* pci_bus, const char* image_path, bool rw);

#endif
