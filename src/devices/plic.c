#include "riscv32.h"
#include "riscv32_csr.h"
#include "riscv32_mmu.h"
#include "bit_ops.h"

#include "plic.h"

#define CTXFLAG_THRESHOLD 0
#define CTXFLAG_CLAIMCOMPLETE 1
#define CTXFLAG_MAX 2

/* adjustable limits */
#define SOURCE_MAX 32 /* max 1024 */
#define CTX_MAX 1 /* max 15672 */

struct plic
{
	uint32_t prio[SOURCE_MAX];
	uint32_t pending[SOURCE_MAX/8/4];
	uint32_t enable[SOURCE_MAX/8/4][CTX_MAX];
	uint32_t ctxflags[CTXFLAG_MAX][CTX_MAX];
};

static inline void set_int_pending(struct plic *plic, uint32_t x, bool val)
{
	if (val)
		plic->pending[x / 32] |= (1 << (x & 31));
	else
		plic->pending[x / 32] &= ~(1 << (x & 31));
}

static inline bool is_int_enabled(struct plic *plic, uint32_t ctx, uint32_t x)
{
	return bit_check(plic->enable[x / 32][ctx], x & 31);
}

static inline bool is_int_pending(struct plic *plic, uint32_t x)
{
	return bit_check(plic->pending[x / 32], x & 31);
}

static bool is_int_valid(struct plic *dev, uint32_t ctx, uint32_t id)
{
	assert(id < SOURCE_MAX);
	if (id == 0)
	{
		/* no interrupt 0 */
		return false;
	}

	/* is interrupt enabled for this hart? */
	if (!is_int_enabled(dev, ctx, id))
	{
		return false;
	}

	uint32_t intprio = dev->prio[id];

	/* is interrupt of this priority above threshold? */
	if (intprio <= dev->ctxflags[CTXFLAG_THRESHOLD][ctx])
	{
		return false;
	}

	/* yep, we can deliver this interrupt to this ctx */
	return true;
}

static void select_int(struct plic *dev, uint32_t ctx, uint32_t preferred_id)
{
	assert(ctx < CTX_MAX && preferred_id < SOURCE_MAX);
	if (!is_int_valid(dev, ctx, preferred_id))
	{
		/* cannot deliver this interrupt to this hart, go out */
		return;
	}

	uint32_t cur_int = dev->ctxflags[CTXFLAG_CLAIMCOMPLETE][ctx];
	assert(cur_int < SOURCE_MAX);

	if (dev->prio[preferred_id] > dev->prio[cur_int])
	{
		/* new interrupt priority is higher than current, select it */
		dev->ctxflags[CTXFLAG_CLAIMCOMPLETE][ctx] = preferred_id;
		return;
	}

	if (preferred_id < cur_int)
	{
		/* new interrupt ID is less that current, select it */
		dev->ctxflags[CTXFLAG_CLAIMCOMPLETE][ctx] = preferred_id;
		return;
	}
}

static void select_int_from_pending(struct plic *dev, uint32_t ctx)
{
	dev->ctxflags[CTXFLAG_CLAIMCOMPLETE][ctx] = 0;

	/* start from 1 as there's no zero interrupt */
	for (uint32_t i = 1; i < SOURCE_MAX; ++i)
	{
		if (is_int_pending(dev, i))
		{
			select_int(dev, ctx, i);
		}
	}
}

static bool plic_prio_handler(struct plic *dev, uint32_t idx, uint32_t *data, uint8_t access)
{
	if (idx >= SOURCE_MAX)
	{
		return true;
	}

	if (access == MMU_READ)
	{
		*data = dev->prio[idx];
	}
	else if (access == MMU_WRITE)
	{
		if (idx == 0)
		{
			/* 0 is reserved, don't touch it */
			return true;
		}
		dev->prio[idx] = *data;
	}

	return true;
}

static bool plic_pending_handler(struct plic *dev, uint32_t idx, uint32_t *data, uint8_t access)
{
	if (idx >= SOURCE_MAX/8/4)
	{
		return true;
	}

	if (access == MMU_READ)
	{
		*data = dev->pending[idx];
	}
	else if (access == MMU_WRITE)
	{
		/* R/O, do nothing. Pending bits are cleared by reading CLAIMCOMPLETE register */
	}

	return true;
}

static bool plic_ie_handler(struct plic *dev, uint32_t offset, uint32_t *data, uint8_t access)
{
	uint32_t idx = offset & 31;
	uint32_t ctx = offset / 32;

	if (idx >= SOURCE_MAX/8/4 || ctx >= CTX_MAX)
	{
		return true;
	}

	if (access == MMU_READ)
	{
		*data = dev->enable[idx][ctx];
	}
	else if (access == MMU_WRITE)
	{
		dev->enable[idx][ctx] = *data;
	}

	return true;
}

static bool plic_ctxflag_handler(riscv32_vm_state_t *vm, struct plic *dev, uint32_t offset, uint32_t *data, uint8_t access)
{
	uint32_t idx = offset & 1023;
	uint32_t ctx = offset / 1024;

	if (idx >= CTXFLAG_MAX || ctx >= CTX_MAX)
	{
		/* others are reserved, ignore */
		return true;
	}

	if (access == MMU_READ)
	{
		if (idx == CTXFLAG_CLAIMCOMPLETE)
		{
			/* interrupt claim */

			/* someone can change enable & priority/threshold values, so we need to recheck
			 * our previous decision. If we can't use chosen interrupt, then we need
			 * to get the new one. */
			if (!is_int_valid(dev, ctx, dev->ctxflags[idx][ctx]))
			{
				select_int_from_pending(dev, ctx);
			}

			/* interrupt is claimed by this hart, clear the pending bit */
			set_int_pending(dev, dev->ctxflags[idx][ctx], 0);
		}
		*data = dev->ctxflags[idx][ctx];
	}
	else if (access == MMU_WRITE)
	{
		if (idx == CTXFLAG_CLAIMCOMPLETE)
		{
			/* interrupt completed signal */

			/* not sure if we need to check the value being written... 
			 * The spec says we need not to. So what is the point of writing
			 * previously claimed interrupt ID here, since pending bit is cleared on claim? */
			select_int_from_pending(dev, ctx);

			//printf("clear plic int\n");
			if (dev->ctxflags[CTXFLAG_CLAIMCOMPLETE][ctx] == 0)
			{
				/* if there's no interrupts waiting, clear the pending bit */
				vm->csr.ip &= ~(1 << INTERRUPT_SEXTERNAL);
				vm->ev_int_mask &= ~(1 << INTERRUPT_SEXTERNAL);
			}
			else
			{
				/* trigger CPU to execute our next interrupt */
				vm->ev_int_mask |= (1 << INTERRUPT_SEXTERNAL);
				vm->ev_int = 1;
				vm->wait_event = 0;
			}
		}
		else
		{
			/* set threshold */
			dev->ctxflags[idx][ctx] = *data;
		}
	}

	return true;
}

static bool plic_mmio_handler(riscv32_vm_state_t* vm, riscv32_mmio_device_t* device, uint32_t offset, void* memory_data, uint32_t size, uint8_t access)
{
	struct plic *dev = (struct plic*)device->data;
	UNUSED(vm);

	if ((offset % 4) != 0 || (size % 4) != 0)
	{
		// XXX: all regs are 32-bit, handle misalign?
		return false;
	}

	if (offset < 0x1000)
	{
		/* interrupt priority */
		offset /= 4;
		for (uint32_t i = 0; i < size / 4; ++i)
		{
			if (!plic_prio_handler(dev, offset + i, (uint32_t*)memory_data + i, access))
			{
				return false;
			}
		}
	}
	else if (offset < 0x1080)
	{
		/* interrupt pending */
		offset -= 0x1000;
		offset /= 4;
		for (uint32_t i = 0; i < size / 4; ++i)
		{
			if (!plic_pending_handler(dev, offset + i, (uint32_t*)memory_data + i, access))
			{
				return false;
			}
		}
	}
	else if (offset < 0x2000)
	{
		/* reserved, ignore */
		return true;
	}
	else if (offset < 0x1f2000)
	{
		/* enable bits */
		offset -= 0x2000;
		offset /= 4;
		for (uint32_t i = 0; i < size / 4; ++i)
		{
			if (!plic_ie_handler(dev, offset + i, (uint32_t*)memory_data + i, access))
			{
				return false;
			}
		}
	}
	else if (offset < 0x200000)
	{
		/* reserved, ignore */
		return true;
	}
	else if (offset < 0x4000000)
	{
		/* context flags - threshold and claim/complete */
		offset -= 0x200000;
		offset /= 4;
		for (uint32_t i = 0; i < size / 4; ++i)
		{
			if (!plic_ctxflag_handler(vm, dev, offset + i, (uint32_t*)memory_data + i, access))
			{
				return false;
			}
		}
	}
	else
	{
		/* wtf is this? */
		return false;
	}

	return true;
}

void* plic_init(riscv32_vm_state_t *vm, uint32_t base_addr)
{
	struct plic *ptr = calloc(1, sizeof (struct plic));
	riscv32_mmio_add_device(vm, base_addr, base_addr + 0x4000000, plic_mmio_handler, ptr);
	return ptr;
}

// Send IRQ through PLIC to specific hart
// Parameters:
// vm - hart context
// data - PLIC private data from mmio_device_t->data
// id - irq number
bool plic_send_irq(riscv32_vm_state_t *vm, void *data, uint32_t id)
{
	struct plic *dev = (struct plic*)data;

	assert(id != 0);
	//printf("plic IRQ raise: %d\n", id);

	/* mark the interrupt as pending */
	set_int_pending(dev, id, 1);

	/* reading hart id is racy, don't do this for now */
#if 0
	/* read hart id */
	uint8_t prev_priv = vm->priv_mode;
	vm->priv_mode = PRIVILEGE_MACHINE;
	uint32_t hartid = 0;
	riscv32_csr_op(vm, 0x123, &hartid, CSR_SWAP);
	/* don't bother setting hartid back, it's R/O anyway... */
	vm->priv_mode = prev_priv;
#else
	uint32_t hartid = 0;
#endif

	/* update the current selected interrupt ID */
	select_int(dev, hartid, id);

	/* deliver the event to CPU. Use S-mode external interrupt as M-mode
	 * is useless - SBI just ignores it, but S-mode interrupts can be handled
	 * by the OS kernel. */
#if 0
	vm->ev_int_mask |= (1 << INTERRUPT_SEXTERNAL);
	vm->ev_int = true;
	vm->wait_event = 0;
#else
	riscv32_interrupt(vm, INTERRUPT_SEXTERNAL);
#endif
	return true;
}

