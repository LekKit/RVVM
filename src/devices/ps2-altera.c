/*
ps2-altera.c - Altera PS2 Controller
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

#include "riscv32.h"
#include "riscv32_mmu.h"
#include "bit_ops.h"

#include "plic.h"
#include "ps2-altera.h"
#include "spinlock.h"

#define ALTERA_DATA 0
#define ALTERA_CONTROL 4
#define ALTERA_REG_SIZE (2 * 4)

#define ALTERA_RE 0
#define ALTERA_RI 8
#define ALTERA_CE 10

struct altps2
{
	struct ps2_device *child; // device bound to this IRQ port
	spinlock_t lock;

	// IRQ data
	rvvm_hart_t *hart; // hart to send IRQ to
	void *intc_data; // private interrupt controller data
	uint32_t irq;

	bool irq_enabled;
	bool irq_pending;
	bool error;

};

static bool altps2_mmio_handler_impl(rvvm_hart_t* vm, struct altps2* ps2port, uint32_t offset, uint32_t* data, uint8_t access)
{
	UNUSED(vm);
	if (access == MMU_READ) {
		switch (offset)
		{
			case ALTERA_DATA:
				{
					uint8_t val;
					uint16_t avail = ps2port->child->ps2_op(ps2port->child, &val, false);
					*data = (val &= 0xff);
					*data |= (avail != 0) << 15; // RVALID bit
					*data |= avail << 16; // RAVAIL
					break;
				}
			case ALTERA_CONTROL:
				*data |= ps2port->irq_enabled << ALTERA_RE;
				*data |= ps2port->irq_pending << ALTERA_RI;
				ps2port->irq_pending = false; // not sure when this is cleared
				*data |= ps2port->error << ALTERA_CE;
				break;
			default:
				return false;
		}

		return true;
	} else if (access == MMU_WRITE) {
		switch (offset)
		{
			case ALTERA_DATA:
				{
					uint8_t val = (*data & 0xff);
					uint16_t err = ps2port->child->ps2_op(ps2port->child, &val, true);
					if (!err)
					{
						ps2port->error = true;
					}
					break;
				}
				break;
			case ALTERA_CONTROL:
				ps2port->irq_enabled = bit_check(*data, ALTERA_RE);
				if (!bit_check(*data, ALTERA_CE))
				{
					ps2port->error = 0;
				}
				break;
			default:
				return false;
		}

		return true;
	}

	return false;
}

static bool altps2_mmio_handler(rvvm_hart_t* vm, riscv32_mmio_device_t* device, uint32_t offset, void* memory_data, uint32_t size, uint8_t access)
{
	struct altps2 *ps2port = device->data;
	spin_lock(&ps2port->lock);
	bool ret = false;

	if ((size % 4 != 0) || (offset % 4 != 0))
	{
		// TODO: misalign
		goto out;
	}

	for (size_t i = 0; i < size; i += 4) {
		if (!altps2_mmio_handler_impl(vm, ps2port, offset + i, (uint32_t*)((char*)memory_data + i), access)) {
			ps2port->error = 1;
			goto out;
		}
	}

	ret = true;
out:
	spin_unlock(&ps2port->lock);
	return ret;
}

void altps2_init(rvvm_hart_t *vm, uint32_t base_addr, void *intc_data, uint32_t irq, struct ps2_device *child)
{
	struct altps2 *ptr = safe_calloc(1, sizeof (struct altps2));

	ptr->child = child;
	ptr->hart = vm;
	ptr->intc_data = intc_data;
	ptr->irq = irq;

	child->port_data = ptr;

	spin_init(&ptr->lock);

	riscv32_mmio_add_device(vm, base_addr, base_addr + ALTERA_REG_SIZE, altps2_mmio_handler, ptr);
}

// Send interrupt via PS/2 controller.
// Unlocked version - call from MMIO handler (aka ps2_op)
void altps2_interrupt_unlocked(struct ps2_device *dev)
{
	struct altps2 *ptr = (struct altps2 *)dev->port_data;
	if (!ptr->irq_enabled)
	{
		return;
	}

	ptr->irq_pending = true;
	plic_send_irq(ptr->hart, ptr->intc_data, ptr->irq);
}

// Send interrupt via PS/2 controller.
// Locked version - call from other threads
void altps2_interrupt(struct ps2_device *dev)
{
	struct altps2 *ptr = (struct altps2 *)dev->port_data;
	spin_lock(&ptr->lock);
	altps2_interrupt_unlocked(dev);
	spin_unlock(&ptr->lock);
}
