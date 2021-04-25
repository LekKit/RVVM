/*
ps2-altera.h - Altera PS2 Controller
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#ifndef PS2_ALTERA_H
#define PS2_ALTERA_H
#include "riscv32.h"

struct ps2_device
{
	// PS/2 device R/W operation
	// ps2dev - this struct
	// val - actual byte to read/write from/to device
	// is_write - true when writing to device, false otherwise
	// on read - returns bytes available, on write - returns 0 on error
	uint16_t (*ps2_op)(struct ps2_device *ps2dev, uint8_t *val, bool is_write);

	void *data; // private device data
	void *port_data; // private PS/2 port data - used to send IRQ
};

void altps2_init(riscv32_vm_state_t *vm, uint32_t base_addr, void *intc_data, uint32_t irq, struct ps2_device *child);
void altps2_interrupt(struct ps2_device *dev);
#endif
