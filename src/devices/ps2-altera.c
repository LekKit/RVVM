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

#include "ps2-altera.h"
#include "mem_ops.h"
#include "atomics.h"
#include "utils.h"
#include "fdtlib.h"

#define ALTERA_REG_DATA 0x0
#define ALTERA_REG_CTRL 0x4

#define ALTERA_CTRL_RE 0x1   // IRQ Enabled
#define ALTERA_CTRL_RI 0x100 // IRQ Pending
#define ALTERA_CTRL_CE 0x400 // Controller Error

#define ALTERA_DATA_RVALID 0x8000

typedef struct {
    chardev_t* chardev;

    // IRQ data
    plic_ctx_t* plic;
    uint32_t irq;

    // Controller registers
    uint32_t ctrl;
} altps2_dev_t;

static void altps2_notify(void* io_dev, uint32_t flags)
{
    altps2_dev_t* ps2port = io_dev;
    if ((flags & CHARDEV_RX)
     && (atomic_or_uint32(&ps2port->ctrl, ALTERA_CTRL_RI) & ALTERA_CTRL_RE))  {
        plic_send_irq(ps2port->plic, ps2port->irq);
    }
}

static bool altps2_mmio_read(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    altps2_dev_t* ps2port = dev->data;
    memset(data, 0, size);
    switch (offset) {
        case ALTERA_REG_DATA: {
            uint8_t val = 0;
            uint32_t avail = chardev_read(ps2port->chardev, &val, 1);
            write_uint32_le(data, val | (avail ? ALTERA_DATA_RVALID : 0) | (avail << 16));
            break;
        }
        case ALTERA_REG_CTRL:
            write_uint32_le(data, atomic_load_uint32(&ps2port->ctrl));
            break;
    }
    return true;
}

static bool altps2_mmio_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    altps2_dev_t* ps2port = dev->data;
    uint32_t reg = read_uint32_le(data);
    UNUSED(size);
    switch (offset) {
        case ALTERA_REG_DATA: {
                uint8_t val = reg & 0xFF;
                if (!chardev_write(ps2port->chardev, &val, 1)) {
                    atomic_or_uint32(&ps2port->ctrl, ALTERA_CTRL_CE);
                }
            }
            break;
        case ALTERA_REG_CTRL:
            atomic_or_uint32(&ps2port->ctrl, reg & ALTERA_CTRL_RE);
            atomic_and_uint32(&ps2port->ctrl, ALTERA_CTRL_RI | (reg & (ALTERA_CTRL_RE | ALTERA_CTRL_CE)));
            break;
    }
    return true;
}

static void altps2_update(rvvm_mmio_dev_t* dev)
{
    altps2_dev_t* ps2port = dev->data;
    chardev_update(ps2port->chardev);
}

static void altps2_remove(rvvm_mmio_dev_t* dev)
{
    altps2_dev_t* ps2port = dev->data;
    chardev_free(ps2port->chardev);
    free(ps2port);
}

static rvvm_mmio_type_t altps2_dev_type = {
    .name = "altera_ps2",
    .update = altps2_update,
    .remove = altps2_remove,
};

void altps2_init(rvvm_machine_t* machine, rvvm_addr_t base_addr, plic_ctx_t* plic, uint32_t irq, chardev_t* chardev)
{
    altps2_dev_t* ps2port = safe_new_obj(altps2_dev_t);
    ps2port->chardev = chardev;
    ps2port->plic = plic;
    ps2port->irq = irq;

    if (chardev) {
        chardev->io_dev = ps2port;
        chardev->notify = altps2_notify;
    }

    rvvm_mmio_dev_t altps2_mmio = {
        .min_op_size = 4,
        .max_op_size = 4,
        .read = altps2_mmio_read,
        .write = altps2_mmio_write,
        .type = &altps2_dev_type,
        .addr = base_addr,
        .size = ALTPS2_MMIO_SIZE,
        .data = ps2port,
    };
    rvvm_attach_mmio(machine, &altps2_mmio);
#ifdef USE_FDT
    struct fdt_node* ps2 = fdt_node_create_reg("ps2", base_addr);
    fdt_node_add_prop_reg(ps2, "reg", base_addr, ALTPS2_MMIO_SIZE);
    fdt_node_add_prop_str(ps2, "compatible", "altr,ps2-1.0");
    fdt_node_add_prop_u32(ps2, "interrupt-parent", plic_get_phandle(plic));
    fdt_node_add_prop_u32(ps2, "interrupts", irq);
    fdt_node_add_child(rvvm_get_fdt_soc(machine), ps2);
#endif
}
