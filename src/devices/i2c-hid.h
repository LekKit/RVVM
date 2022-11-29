/*
i2c-hid.h - I2C HID Host Controller Interface
Copyright (C) 2022  X512 <github.com/X547>

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

#ifndef _I2C_HID_H_
#define _I2C_HID_H_

#include "hid_dev.h"
#include "rvvmlib.h"

PUBLIC void i2c_hid_init_auto(rvvm_machine_t* machine, hid_dev_t* hid_dev);

#endif  // _I2C_HID_H_
