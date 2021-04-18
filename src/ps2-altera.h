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
