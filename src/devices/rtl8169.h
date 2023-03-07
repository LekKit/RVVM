/*
rtl8169.h - Realtek RTL8169 NIC
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

#ifndef RVVM_RTL8169_H
#define RVVM_RTL8169_H

#include "rvvmlib.h"
#include "pci-bus.h"

PUBLIC pci_dev_t* rtl8169_init(pci_bus_t* pci_bus);
PUBLIC pci_dev_t* rtl8169_init_auto(rvvm_machine_t* machine);

#endif
