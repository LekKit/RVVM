#include "riscv32.h"
#include "riscv32_mmu.h"
#include "bit_ops.h"

#include "plic.h"
#include "ps2-altera.h"

#define ALTERA_DATA 0
#define ALTERA_CONTROL 4
#define ALTERA_REG_SIZE (2 * 4)

#define ALTERA_RE 0
#define ALTERA_RI 8
#define ALTERA_CE 10

struct altps2
{
	struct ps2_device *child; // device bound to this IRQ port

	// IRQ data
	riscv32_vm_state_t *hart; // hart to send IRQ to
	void *intc_data; // private interrupt controller data
	uint32_t irq;

	bool irq_enabled;
	bool irq_pending;
	bool error;

};

static bool altps2_mmio_handler_impl(riscv32_vm_state_t* vm, struct altps2* ps2port, uint32_t offset, uint32_t* data, uint8_t access)
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

static bool altps2_mmio_handler(riscv32_vm_state_t* vm, riscv32_mmio_device_t* device, uint32_t offset, void* memory_data, uint32_t size, uint8_t access)
{
	struct altps2 *ps2port = device->data;
	if ((size % 4 != 0) || (offset % 4 != 0))
	{
		// TODO: misalign
		return false;
	}

	for (size_t i = 0; i < size; i += 4) {
		if (!altps2_mmio_handler_impl(vm, ps2port, offset + i, (uint32_t*)memory_data, access)) {
			ps2port->error = 1;
			return false;
		}
	}

	return true;
}

void altps2_init(riscv32_vm_state_t *vm, uint32_t base_addr, void *intc_data, uint32_t irq, struct ps2_device *child)
{
	struct altps2 *ptr = calloc(1, sizeof (struct altps2));

	ptr->child = child;
	ptr->hart = vm;
	ptr->intc_data = intc_data;
	ptr->irq = irq;

	child->port_data = ptr;

	riscv32_mmio_add_device(vm, base_addr, base_addr + ALTERA_REG_SIZE, altps2_mmio_handler, ptr);
}

void altps2_interrupt(struct ps2_device *dev)
{
	struct altps2 *ptr = (struct altps2 *)dev->port_data;
	if (!ptr->irq_enabled)
	{
		return;
	}

	ptr->irq_pending = true;
	plic_send_irq(ptr->hart, ptr->intc_data, ptr->irq);
}
