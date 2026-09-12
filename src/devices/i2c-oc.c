/*
i2c-oc.c - OpenCores I2C Controller
Copyright (C) 2022  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <rvvm/rvvm_snapshot.h>

#include "i2c-oc.h"
#include "fdtlib.h"
#include "mem_ops.h"
#include "spinlock.h"
#include "utils.h"
#include "vector.h"

PUSH_OPTIMIZATION_SIZE

struct rvvm_i2c_bus {
    vector_t(i2c_dev_t) devices;
    struct fdt_node*    fdt_node;
    rvvm_intc_t*        intc;
    rvvm_irq_t          irq;
    spinlock_t          lock;
    uint16_t            sel_addr;
    uint16_t            clock;
    uint8_t             control;
    uint8_t             status;
    uint8_t             tx_byte;
    uint8_t             rx_byte;
};

#define I2C_OC_MMIO_SIZE 0x1000

// OpenCores I2C registers
#define I2C_OC_CLKLO     0x00 // Clock prescale low byte
#define I2C_OC_CLKHI     0x04 // Clock prescale high byte
#define I2C_OC_CTR       0x08 // Control register
#define I2C_OC_TXRXR     0x0C // Transmit & Receive register (W/R)
#define I2C_OC_CRSR      0x10 // Command & Status Register (W/R)

// Register values
#define I2C_OC_CTR_MASK  0xC0 // Mask of legal bits
#define I2C_OC_CTR_EN    0x80 // Core enable bit
#define I2C_OC_CTR_IEN   0x40 // Interrupt enable bit

#define I2C_OC_CR_STA    0x80 // Generate (repeated) start condition
#define I2C_OC_CR_STO    0x40 // Generate stop condition
#define I2C_OC_CR_RD     0x20 // Read from slave
#define I2C_OC_CR_WR     0x10 // Write to slave
#define I2C_OC_CR_ACK    0x08 // Send ACK (0) or NACK (1) to master
#define I2C_OC_CR_IACK   0x01 // Interrupt acknowledge, clear a pending IRQ

#define I2C_OC_SR_ACK    0x80 // Received ACK from slave (0), NACK is 1
#define I2C_OC_SR_BSY    0x40 // I2C bus busy
#define I2C_OC_SR_AL     0x20 // Arbitration lost
#define I2C_OC_SR_TIP    0x02 // Transfer in progress
#define I2C_OC_SR_IF     0x01 // Interrupt flag

static void i2c_oc_interrupt(i2c_bus_t* bus)
{
    bus->status |= I2C_OC_SR_IF;
    if (bus->control & I2C_OC_CTR_IEN) {
        rvvm_send_irq(bus->intc, bus->irq);
    }
}

static i2c_dev_t* i2c_oc_get_dev(i2c_bus_t* bus, uint16_t addr)
{
    vector_foreach (bus->devices, i) {
        i2c_dev_t* i2c_dev = &vector_at(bus->devices, i);
        if (i2c_dev->addr == addr) {
            return i2c_dev;
        }
    }
    return NULL;
}

static bool i2c_oc_mmio_read(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    i2c_bus_t* bus = dev->data;
    memset(data, 0, size);
    spin_lock(&bus->lock);
    switch (offset) {
        case I2C_OC_CLKLO:
            write_uint8(data, bus->clock & 0xFF);
            break;
        case I2C_OC_CLKHI:
            write_uint8(data, bus->clock >> 8);
            break;
        case I2C_OC_CTR:
            write_uint8(data, bus->control);
            break;
        case I2C_OC_TXRXR:
            write_uint8(data, bus->rx_byte);
            break;
        case I2C_OC_CRSR:
            write_uint8(data, bus->status);
            break;
    }
    spin_unlock(&bus->lock);
    return true;
}

static bool i2c_oc_mmio_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    i2c_bus_t* bus = dev->data;
    UNUSED(size);
    spin_lock(&bus->lock);
    switch (offset) {
        case I2C_OC_CLKLO:
            bus->clock = (bus->clock & 0xFF00) | read_uint8(data);
            break;
        case I2C_OC_CLKHI:
            bus->clock = (bus->clock & 0xFF) | (read_uint8(data) << 8);
            break;
        case I2C_OC_CTR:
            bus->control = read_uint8(data) & I2C_OC_CTR_MASK;
            break;
        case I2C_OC_TXRXR:
            bus->tx_byte = read_uint8(data);
            break;
        case I2C_OC_CRSR: {
            uint8_t cmd  = read_uint8(data);
            bus->status |= I2C_OC_SR_ACK;
            if (cmd & I2C_OC_CR_IACK) {
                // Clear a pending interrupt
                bus->status &= ~I2C_OC_SR_IF;
            }
            if (cmd & I2C_OC_CR_STA) {
                // Start the transaction
                bus->sel_addr  = 0xFFFF;
                bus->status   |= I2C_OC_SR_BSY;
            }
            if ((cmd & I2C_OC_CR_WR)) {
                if (bus->sel_addr == 0xFFFF) {
                    // Get device address, signal start of transaction
                    bus->sel_addr       = bus->tx_byte >> 1;
                    i2c_dev_t* i2c_dev  = i2c_oc_get_dev(bus, bus->sel_addr);
                    bool       is_write = !(bus->tx_byte & 1);
                    if (i2c_dev && (!i2c_dev->start || i2c_dev->start(i2c_dev->data, is_write))) {
                        bus->status &= ~I2C_OC_SR_ACK;
                    }
                } else {
                    // Write byte
                    i2c_dev_t* i2c_dev = i2c_oc_get_dev(bus, bus->sel_addr);
                    if (i2c_dev && i2c_dev->write(i2c_dev->data, bus->tx_byte)) {
                        bus->status &= ~I2C_OC_SR_ACK;
                    }
                }
                i2c_oc_interrupt(bus);
            }
            if (cmd & I2C_OC_CR_RD) {
                // Read byte
                i2c_dev_t* i2c_dev = i2c_oc_get_dev(bus, bus->sel_addr);
                if (i2c_dev && i2c_dev->read(i2c_dev->data, &bus->rx_byte)) {
                    bus->status &= ~I2C_OC_SR_ACK;
                }
                i2c_oc_interrupt(bus);
            }
            if (cmd & I2C_OC_CR_STO) {
                // End of transaction
                i2c_dev_t* i2c_dev = i2c_oc_get_dev(bus, bus->sel_addr);
                if (i2c_dev && i2c_dev->stop) {
                    i2c_dev->stop(i2c_dev->data);
                }
                bus->sel_addr  = 0xFFFF;
                bus->status   &= ~I2C_OC_SR_BSY;
                i2c_oc_interrupt(bus);
            }
            break;
        }
    }
    spin_unlock(&bus->lock);
    return true;
}

static void i2c_oc_remove(rvvm_mmio_dev_t* dev)
{
    i2c_bus_t* bus = dev->data;
    vector_foreach (bus->devices, i) {
        i2c_dev_t* i2c_dev = &vector_at(bus->devices, i);
        if (i2c_dev->remove) {
            i2c_dev->remove(i2c_dev->data);
        }
    }
    vector_free(bus->devices);
    free(bus);
}

static void i2c_oc_suspend(rvvm_mmio_dev_t* dev, rvvm_snapshot_t* snap, bool resume)
{
    if (snap) {
        i2c_bus_t* bus = dev->data;
        rvvm_snapshot_section(snap, "i2c_opencores");
        rvvm_snapshot_field(snap, bus->sel_addr);
        rvvm_snapshot_field(snap, bus->clock);
        rvvm_snapshot_field(snap, bus->control);
        rvvm_snapshot_field(snap, bus->status);
        rvvm_snapshot_field(snap, bus->tx_byte);
        rvvm_snapshot_field(snap, bus->rx_byte);
    }
    UNUSED(resume);
}

static rvvm_mmio_type_t i2c_oc_dev_type = {
    .name    = "i2c_opencores",
    .remove  = i2c_oc_remove,
    .suspend = i2c_oc_suspend,
};

PUBLIC i2c_bus_t* i2c_oc_init(rvvm_machine_t* machine, rvvm_addr_t addr, rvvm_intc_t* intc, rvvm_irq_t irq)
{
    i2c_bus_t* bus = safe_new_obj(i2c_bus_t);
    bus->intc      = intc;
    bus->irq       = irq;

    rvvm_mmio_dev_t i2c_oc = {
        .addr        = addr,
        .size        = I2C_OC_MMIO_SIZE,
        .data        = bus,
        .read        = i2c_oc_mmio_read,
        .write       = i2c_oc_mmio_write,
        .type        = &i2c_oc_dev_type,
        .min_op_size = 1,
        .max_op_size = 4,
    };
    if (!rvvm_attach_mmio(machine, &i2c_oc)) {
        rvvm_error("Failed to attach OpenCores I2C controller!");
        return NULL;
    }

#ifdef USE_FDT
    struct fdt_node* i2c_osc = fdt_node_find(rvvm_get_fdt_root(machine), "i2c_osc");
    if (i2c_osc == NULL) {
        i2c_osc = fdt_node_create("i2c_osc");
        fdt_node_add_prop_str(i2c_osc, "compatible", "fixed-clock");
        fdt_node_add_prop_u32(i2c_osc, "#clock-cells", 0);
        fdt_node_add_prop_u32(i2c_osc, "clock-frequency", 20000000);
        fdt_node_add_prop_str(i2c_osc, "clock-output-names", "clk");
        fdt_node_add_child(rvvm_get_fdt_root(machine), i2c_osc);
    }

    struct fdt_node* i2c_fdt = fdt_node_create_reg("i2c", i2c_oc.addr);
    fdt_node_add_prop_reg(i2c_fdt, "reg", i2c_oc.addr, i2c_oc.size);
    fdt_node_add_prop_str(i2c_fdt, "compatible", "opencores,i2c-ocores");
    rvvm_fdt_describe_irq(i2c_fdt, intc, irq);
    fdt_node_add_prop_u32(i2c_fdt, "clocks", fdt_node_get_phandle(i2c_osc));
    fdt_node_add_prop_str(i2c_fdt, "clock-names", "clk");
    fdt_node_add_prop_u32(i2c_fdt, "reg-shift", 2);
    fdt_node_add_prop_u32(i2c_fdt, "reg-io-width", 1);
    fdt_node_add_prop_u32(i2c_fdt, "opencores,ip-clock-frequency", 20000000);
    fdt_node_add_prop_u32(i2c_fdt, "#address-cells", 1);
    fdt_node_add_prop_u32(i2c_fdt, "#size-cells", 0);
    fdt_node_add_prop_str(i2c_fdt, "status", "okay");
    fdt_node_add_child(rvvm_get_fdt_soc(machine), i2c_fdt);
    bus->fdt_node = i2c_fdt;
#endif
    rvvm_set_i2c_bus(machine, bus);
    return bus;
}

PUBLIC i2c_bus_t* i2c_oc_init_auto(rvvm_machine_t* machine)
{
    rvvm_intc_t* intc = rvvm_get_intc(machine);
    rvvm_addr_t  addr = rvvm_mmio_zone_auto(machine, I2C_OC_ADDR_DEFAULT, I2C_OC_MMIO_SIZE);
    return i2c_oc_init(machine, addr, intc, rvvm_alloc_irq(intc));
}

PUBLIC uint16_t i2c_attach_dev(i2c_bus_t* bus, const i2c_dev_t* dev_desc)
{
    if (bus == NULL) {
        return 0;
    }
    i2c_dev_t tmp = *dev_desc;
    if (dev_desc->addr == I2C_AUTO_ADDR) {
        tmp.addr = 0x8;
    }
    while (i2c_oc_get_dev(bus, tmp.addr)) {
        if (dev_desc->addr == I2C_AUTO_ADDR) {
            tmp.addr++;
        } else {
            rvvm_warn("Duplicate I2C device address on a single bus");
            return 0;
        }
    }
    vector_push_back(bus->devices, tmp);
    return tmp.addr;
}

PUBLIC struct fdt_node* i2c_bus_fdt_node(i2c_bus_t* bus)
{
    if (bus == NULL) {
        return NULL;
    }
    return bus->fdt_node;
}

POP_OPTIMIZATION_SIZE
