/*
plic.c - Platform-level Interrupt Controller
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

#include "plic.h"
#include "riscv_hart.h"
#include "bit_ops.h"
#include "spinlock.h"
#include "vector.h"
#include <assert.h>

#define CTXFLAG_THRESHOLD 0
#define CTXFLAG_CLAIMCOMPLETE 1
#define CTXFLAG_MAX 16

/* adjustable limits */
#define SOURCE_MAX 32 /* max 1024 */
#define CTX_MAX 16 /* max 15672, must be multiple of 2 */

#define HARTID2CTX(hartid) ((hartid) << 1)
#define CTX2HARTID(ctx) ((ctx) >> 1)
#define CTX2PRIO(ctx) ((ctx) & 1 ? INTERRUPT_MEXTERNAL : INTERRUPT_SEXTERNAL)

struct plic {
    rvvm_machine_t* machine;
    spinlock_t lock;
    uint32_t alloc_irq;
    uint32_t phandle;
    uint32_t prio[SOURCE_MAX];
    uint32_t pending[(SOURCE_MAX+8+4-1)/8/4];
    uint32_t enable[(SOURCE_MAX+8+4-1)/8/4][CTX_MAX];
    uint32_t ctxflags[CTXFLAG_MAX][CTX_MAX];
    uint32_t busy[(CTX2HARTID(CTX_MAX)+8*4+1)/8/4];
};

static inline void set_hart_busy(struct plic *plic, uint32_t ctx, bool busy)
{
    if (busy)
        plic->busy[ctx / 32] |= (1 << (ctx & 31));
    else
        plic->busy[ctx / 32] &= ~(1 << (ctx & 31));
}

static inline bool is_hart_busy(struct plic *plic, uint32_t ctx)
{
    return bit_check(plic->busy[ctx / 32], ctx & 31);
}

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

static bool select_int(struct plic *dev, uint32_t ctx, uint32_t preferred_id)
{
    assert(ctx < CTX_MAX && preferred_id < SOURCE_MAX);
    if (!is_int_valid(dev, ctx, preferred_id))
    {
        /* cannot deliver this interrupt to this hart, go out */
        return false;
    }

    uint32_t cur_int = dev->ctxflags[CTXFLAG_CLAIMCOMPLETE][ctx];
    assert(cur_int < SOURCE_MAX);

    if (dev->prio[preferred_id] > dev->prio[cur_int])
    {
        /* new interrupt priority is higher than current, select it */
        dev->ctxflags[CTXFLAG_CLAIMCOMPLETE][ctx] = preferred_id;
        goto out;
    }

    if (preferred_id < cur_int)
    {
        /* new interrupt ID is less that current, select it */
        dev->ctxflags[CTXFLAG_CLAIMCOMPLETE][ctx] = preferred_id;
        goto out;
    }

out:
    return true;
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

static bool plic_prio_read_handler(struct plic *dev, uint32_t idx, uint32_t *data)
{
    if (idx >= SOURCE_MAX)
    {
        return true;
    }

    *data = dev->prio[idx];
    return true;
}

static bool plic_prio_write_handler(struct plic *dev, uint32_t idx, uint32_t *data)
{
    if (idx >= SOURCE_MAX)
    {
        return true;
    }

    if (idx == 0)
    {
        /* 0 is reserved, don't touch it */
        return true;
    }

    dev->prio[idx] = *data;
    return true;
}

static bool plic_pending_read_handler(struct plic *dev, uint32_t idx, uint32_t *data)
{
    if (idx >= SOURCE_MAX/8/4)
    {
        return true;
    }

    *data = dev->pending[idx];
    return true;
}

static bool plic_ie_read_handler(struct plic *dev, uint32_t offset, uint32_t *data)
{
    uint32_t idx = offset & 31;
    uint32_t ctx = offset / 32;

    if (idx >= SOURCE_MAX/8/4 || ctx >= CTX_MAX)
    {
        return true;
    }

    *data = dev->enable[idx][ctx];
    return true;
}

static bool plic_ie_write_handler(struct plic *dev, uint32_t offset, uint32_t *data)
{
    uint32_t idx = offset & 31;
    uint32_t ctx = offset / 32;

    if (idx >= SOURCE_MAX/8/4 || ctx >= CTX_MAX)
    {
        return true;
    }

    dev->enable[idx][ctx] = *data;
    return true;
}

static bool plic_ctxflag_read_handler(struct plic *dev, uint32_t offset, uint32_t *data)
{
    uint32_t idx = offset & 1023;
    uint32_t ctx = offset / 1024;

    if (idx >= CTXFLAG_MAX || ctx >= CTX_MAX)
    {
        /* others are reserved, ignore */
        return true;
    }

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
    return true;
}

static bool plic_ctxflag_write_handler(rvvm_machine_t *mach, struct plic *dev, uint32_t offset, uint32_t *data)
{
    uint32_t idx = offset & 1023;
    uint32_t ctx = offset / 1024;

    if (idx >= CTXFLAG_MAX || ctx >= CTX_MAX)
    {
        /* others are reserved, ignore */
        return true;
    }

    if (idx == CTXFLAG_CLAIMCOMPLETE)
    {
        /* interrupt completed signal */

        /* not sure if we need to check the value being written...
        * The spec says we need not to. So what is the point of writing
        * previously claimed interrupt ID here, since pending bit is cleared on claim? */

        select_int_from_pending(dev, ctx);

        //printf("clear plic int\n");

        /* choose the right hart and interrupt priority */
        size_t hartid = CTX2HARTID(ctx);
        uint8_t prio = CTX2PRIO(ctx);
        if (hartid >= vector_size(mach->harts))
        {
            /* bad context number passed */
            return true;
        }

        if (dev->ctxflags[CTXFLAG_CLAIMCOMPLETE][ctx] == 0)
        {
            /* if there's no interrupts waiting, clear the pending bit */
            set_hart_busy(dev, hartid, false);
            riscv_interrupt_clear(&vector_at(mach->harts, hartid), prio);
        }
        else
        {
            /* trigger CPU to execute our next interrupt */
            riscv_interrupt(&vector_at(mach->harts, hartid), prio);
        }
    }
    else
    {
        /* set threshold */
        dev->ctxflags[idx][ctx] = *data;
    }

    return true;
}

static bool plic_mmio_read_handler(rvvm_mmio_dev_t* device, void* memory_data, size_t offset, uint8_t size)
{
    struct plic* dev = (struct plic*)device->data;

    spin_lock(&dev->lock);
    bool ret = false;
    if (offset < 0x1000)
    {
        /* interrupt priority */
        offset /= 4;
        for (uint32_t i = 0; i < size / 4; ++i)
        {
            if (!plic_prio_read_handler(dev, offset + i, (uint32_t*)memory_data + i))
            {
                goto out;
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
            if (!plic_pending_read_handler(dev, offset + i, (uint32_t*)memory_data + i))
            {
                goto out;
            }
        }
    }
    else if (offset < 0x2000)
    {
        /* reserved, ignore */
        ret = true;
        goto out;
    }
    else if (offset < 0x1f2000)
    {
        /* enable bits */
        offset -= 0x2000;
        offset /= 4;
        for (uint32_t i = 0; i < size / 4; ++i)
        {
            if (!plic_ie_read_handler(dev, offset + i, (uint32_t*)memory_data + i))
            {
                goto out;
            }
        }
    }
    else if (offset < 0x200000)
    {
        /* reserved, ignore */
        ret = true;
        goto out;
    }
    else if (offset < 0x4000000)
    {
        /* context flags - threshold and claim/complete */
        offset -= 0x200000;
        offset /= 4;
        for (uint32_t i = 0; i < size / 4; ++i)
        {
            if (!plic_ctxflag_read_handler(dev, offset + i, (uint32_t*)memory_data + i))
            {
                goto out;
            }
        }
    }
    else
    {
        /* wtf is this? */
        goto out;
    }

    ret = true;
out:
    spin_unlock(&dev->lock);
    return ret;
}

static bool plic_mmio_write_handler(rvvm_mmio_dev_t* device, void* memory_data, size_t offset, uint8_t size)
{
    struct plic *dev = (struct plic*)device->data;

    spin_lock(&dev->lock);
    bool ret = false;
    if (offset < 0x1000)
    {
        /* interrupt priority */
        offset /= 4;
        for (uint32_t i = 0; i < size / 4; ++i)
        {
            if (!plic_prio_write_handler(dev, offset + i, (uint32_t*)memory_data + i))
            {
                goto out;
            }
        }
    }
    else if (offset < 0x1080)
    {
        /* R/O, do nothing. Pending bits are cleared by reading CLAIMCOMPLETE register */
        ret = true;
        goto out;
    }
    else if (offset < 0x2000)
    {
        /* reserved, ignore */
        ret = true;
        goto out;
    }
    else if (offset < 0x1f2000)
    {
        /* enable bits */
        offset -= 0x2000;
        offset /= 4;
        for (uint32_t i = 0; i < size / 4; ++i)
        {
            if (!plic_ie_write_handler(dev, offset + i, (uint32_t*)memory_data + i))
            {
                goto out;
            }
        }
    }
    else if (offset < 0x200000)
    {
        /* reserved, ignore */
        ret = true;
        goto out;
    }
    else if (offset < 0x4000000)
    {
        /* context flags - threshold and claim/complete */
        offset -= 0x200000;
        offset /= 4;
        for (uint32_t i = 0; i < size / 4; ++i)
        {
            if (!plic_ctxflag_write_handler(device->machine, dev, offset + i, (uint32_t*)memory_data + i))
            {
                goto out;
            }
        }
    }
    else
    {
        /* wtf is this? */
        goto out;
    }

    ret = true;
out:
    spin_unlock(&dev->lock);
    return ret;
}

static void plic_reset(rvvm_mmio_dev_t* device)
{
    struct plic* plic = (struct plic*)device->data;
    spin_lock(&plic->lock);
    memset(plic->prio, 0, sizeof(plic->prio));
    memset(plic->pending, 0, sizeof(plic->pending));
    memset(plic->enable, 0, sizeof(plic->enable));
    memset(plic->ctxflags, 0, sizeof(plic->ctxflags));
    memset(plic->busy, 0, sizeof(plic->busy));
    spin_unlock(&plic->lock);
}

static rvvm_mmio_type_t plic_dev_type = {
    .name = "plic",
    .reset = plic_reset,
};

// Create PLIC device
PUBLIC plic_ctx_t* plic_init(rvvm_machine_t* machine, rvvm_addr_t base_addr)
{
    struct plic* plic = safe_calloc(sizeof(struct plic), 1);
    plic->machine = machine;
    spin_init(&plic->lock);

    rvvm_mmio_dev_t plic_mmio;
    plic_mmio.min_op_size = 4;
    plic_mmio.max_op_size = 4;
    plic_mmio.read = plic_mmio_read_handler;
    plic_mmio.write = plic_mmio_write_handler;
    plic_mmio.type = &plic_dev_type;
    plic_mmio.addr = base_addr;
    plic_mmio.size = 0x4000000;
    plic_mmio.data = plic;
    rvvm_attach_mmio(machine, &plic_mmio);
#ifdef USE_FDT
    struct fdt_node* cpus = fdt_node_find(rvvm_get_fdt_root(machine), "cpus");
    if (cpus == NULL) {
        rvvm_warn("Missing /cpus node in FDT!");
        return plic;
    }

    uint32_t* irq_ext = safe_calloc(sizeof(uint32_t), vector_size(machine->harts) * 4);
    vector_foreach(machine->harts, i) {
        struct fdt_node* cpu = fdt_node_find_reg(cpus, "cpu", i);
        struct fdt_node* cpu_irq = fdt_node_find(cpu, "interrupt-controller");

        uint32_t irq_phandle = fdt_node_get_phandle(cpu_irq);
        irq_ext[(i * 4)] = irq_ext[(i * 4) + 2] = irq_phandle;
        irq_ext[(i * 4) + 1] = INTERRUPT_SEXTERNAL;
        irq_ext[(i * 4) + 3] = INTERRUPT_MEXTERNAL;
    }

    struct fdt_node* plic_node = fdt_node_create_reg("plic", base_addr);
    fdt_node_add_prop_u32(plic_node, "#interrupt-cells", 1);
    fdt_node_add_prop_reg(plic_node, "reg", base_addr, 0x4000000);
    fdt_node_add_prop_str(plic_node, "compatible", "sifive,plic-1.0.0");
    fdt_node_add_prop_u32(plic_node, "riscv,ndev", 32);
    fdt_node_add_prop(plic_node, "interrupt-controller", NULL, 0);
    fdt_node_add_prop_cells(plic_node, "interrupts-extended", irq_ext, vector_size(machine->harts) * 4);
    free(irq_ext);

    fdt_node_add_child(rvvm_get_fdt_soc(machine), plic_node);

    plic->phandle = fdt_node_get_phandle(plic_node);
#endif
    return plic;
}

PUBLIC plic_ctx_t* plic_init_auto(rvvm_machine_t* machine)
{
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, PLIC_DEFAULT_MMIO, 0x4000000);
    return plic_init(machine, addr);
}

// Allocate new IRQ
PUBLIC uint32_t plic_alloc_irq(plic_ctx_t* plic)
{
    if (plic == NULL) return 0;
    plic->alloc_irq++;
    return plic->alloc_irq;
}

// Get FDT phandle of the PLIC
PUBLIC uint32_t plic_get_phandle(plic_ctx_t* plic)
{
    if (plic == NULL) return 0;
    return plic->phandle;
}

// Send IRQ through PLIC
PUBLIC bool plic_send_irq(plic_ctx_t* plic, uint32_t irq)
{
    if (plic == NULL) return false;
    spin_lock(&plic->lock);

    /* mark the interrupt as pending */
    set_int_pending(plic, irq, 1);

    /* choose hart and interrupt priority to send IRQ to */
    vector_foreach(plic->machine->harts, i) {
        if (!is_hart_busy(plic, i)) {
            for (size_t ctx = HARTID2CTX(i); ctx < HARTID2CTX(i) + 2; ++ctx) {
                /* is_int_valid check is done in select_int */
                if (select_int(plic, ctx, irq)) {
                    /* found free hart, update the current selected interrupt ID */
                    set_hart_busy(plic, i, true);
                    riscv_interrupt(&vector_at(plic->machine->harts, i), CTX2PRIO(ctx));
                    break;
                }
            }
        }
    }
    /* do not forcefully pick any hart if there were
    * no free harts - interrupt will be selected automatically
    * when hart will handle it's interrupt (and notify us about
    * that by writing CLAIMCOMPLETE register) */
    spin_unlock(&plic->lock);
    return true;
}
