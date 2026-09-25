/*
ns16550a.c - NS16550A UART
Copyright (C) 2021  LekKit <github.com/LekKit>
Copyright (C) 2026  Sol Astrius <sol@astrius.ink>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_fdt.h>
#include <rvvm/rvvm_irq.h>
#include <rvvm/rvvm_region.h>
#include <rvvm/rvvm_snapshot.h>

#include "chardev.h"
#include "mem_ops.h"
#include "ns16550a.h"
#include "uart16550_core.h"
#include "utils.h"

PUSH_OPTIMIZATION_SIZE

typedef struct {
    uart16550_core_t core;
    rvvm_irq_dev_t*  irq_dev;
    rvvm_irq_t       irq;
} ns16550a_dev_t;

static void ns16550a_irq_fn(void* ctx, bool level)
{
    ns16550a_dev_t* uart = ctx;
    rvvm_irq_set(uart->irq_dev, uart->irq, level);
}

static void ns16550a_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    ns16550a_dev_t* uart = rvvm_region_data(dev);
    UNUSED(size);
    write_uint8(data, uart16550_core_read(&uart->core, off));
}

static void ns16550a_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    ns16550a_dev_t* uart = rvvm_region_data(dev);
    UNUSED(size);
    uart16550_core_write(&uart->core, off, read_uint8(data));
}

static void ns16550a_poll(rvvm_reg_dev_t* dev)
{
    ns16550a_dev_t* uart = rvvm_region_data(dev);
    uart16550_core_update(&uart->core);
}

static void ns16550a_suspend(rvvm_reg_dev_t* dev, rvvm_snapshot_t* snap, bool resume)
{
    if (snap) {
        ns16550a_dev_t* uart = rvvm_region_data(dev);
        rvvm_snapshot_section(snap, "serial-ns16550a");
        uart16550_core_suspend(&uart->core, snap);
    }
    UNUSED(resume);
}

static void ns16550a_cleanup(rvvm_reg_dev_t* dev)
{
    ns16550a_dev_t* uart = rvvm_region_data(dev);
    rvvm_irq_dealloc(uart->irq_dev, uart->irq);
    uart16550_core_cleanup(&uart->core);
    free(uart);
}

static const rvvm_reg_type_t ns16550a_type = {
    .name     = "serial-ns16550a",
    .read     = ns16550a_read,
    .write    = ns16550a_write,
    .poll     = ns16550a_poll,
    .suspend  = ns16550a_suspend,
    .cleanup  = ns16550a_cleanup,
    .min_size = 1,
    .max_size = 1,
};

RVVM_PUBLIC rvvm_reg_dev_t* rvvm_ns16550a_init(rvvm_machine_t* machine, //
                                               chardev_t*      chardev, //
                                               rvvm_addr_t     addr,    //
                                               uint32_t        attr,    //
                                               rvvm_irq_dev_t* irq_dev, //
                                               rvvm_irq_t      irq)
{
    ns16550a_dev_t* uart = safe_new_obj(ns16550a_dev_t);
    rvvm_reg_desc_t desc = {
        .addr = addr,
        .size = (attr & RVVM_REG_ATTR_PIO) ? 0x08 : 0x1000,
        .data = uart,
        .type = &ns16550a_type,
        .attr = attr,
    };

    if (!irq_dev) {
        irq_dev = rvvm_get_intc(machine);
    }

    uart->irq_dev = irq_dev;
    uart->irq     = rvvm_irq_alloc(irq_dev, irq);

    uart16550_core_init(&uart->core, chardev, ns16550a_irq_fn, uart);

    rvvm_reg_dev_t*  dev = rvvm_region_init_auto(machine, &desc);
    rvvm_fdt_node_t* soc = rvvm_get_fdt_soc(machine);

    if (dev && soc) {
        rvvm_fdt_node_t* fdt = rvvm_fdt_init_reg("uart", desc.addr);
        rvvm_fdt_prop_set_reg(fdt, "reg", desc.addr, desc.size);
        rvvm_fdt_prop_set_str(fdt, "compatible", "ns16550a");
        rvvm_fdt_prop_set_u32(fdt, "clock-frequency", 20000000);
        rvvm_fdt_prop_set_u32(fdt, "fifo-size", 16);
        rvvm_fdt_prop_set_str(fdt, "status", "okay");
        rvvm_irq_fdt_describe(fdt, uart->irq_dev, uart->irq);
        rvvm_fdt_prop_set_flag(fdt, "wakeup-source");
        rvvm_fdt_attach(soc, fdt);

        if (desc.addr == 0x10000000UL) {
            rvvm_fdt_node_t* chosen = rvvm_fdt_find(rvvm_get_fdt_root(machine), "chosen");
            if (!rvvm_fdt_prop_get(chosen, "stdout-path", NULL)) {
                rvvm_fdt_prop_set_str(chosen, "stdout-path", "/soc/uart@10000000");
                rvvm_append_cmdline(machine, "console=ttyS");
            }
        }
    }

    return dev;
}

POP_OPTIMIZATION_SIZE
