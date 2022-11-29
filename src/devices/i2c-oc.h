/*
i2c-oc.h - OpenCores I2C Controller
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

#ifndef RVVM_I2C_OC_H
#define RVVM_I2C_OC_H

#include "rvvmlib.h"

#define I2C_OC_DEFAULT_MMIO 0x10030000

typedef struct i2c_bus i2c_bus_t;

typedef struct {
    uint16_t addr;
    void*    data;
    // Return false on NACK or no data to read
    bool (*write)(void* dev, uint8_t byte);
    bool (*read)(void* dev, uint8_t* byte);
    //void (*start)(); // Not needed since r/w implicitly starts a transaction?
    void (*stop)();    // Stop the current transaction
    void (*remove)();  // Device cleanup
} i2c_dev_t;

PUBLIC i2c_bus_t* i2c_oc_init(rvvm_machine_t* machine, rvvm_addr_t base_addr, plic_ctx_t* plic, uint32_t irq);
PUBLIC i2c_bus_t* i2c_oc_init_auto(rvvm_machine_t* machine);

PUBLIC bool       i2c_attach_dev(i2c_bus_t* bus, const i2c_dev_t* dev_desc);

#endif
