/*
rtc-ds7142.h - Dallas DS1742 Real-time Clock
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

#ifndef RVVM_RTC_DS1742_H
#define RVVM_RTC_DS1742_H

#include "rvvmlib.h"

#define RTC_DS1742_DEFAULT_MMIO 0x101000

PUBLIC rvvm_mmio_handle_t rtc_ds1742_init(rvvm_machine_t* machine, rvvm_addr_t base_addr);
PUBLIC rvvm_mmio_handle_t rtc_ds1742_init_auto(rvvm_machine_t* machine);

#endif
 
