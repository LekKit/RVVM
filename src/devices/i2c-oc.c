/*
i2c-oc.c - OpenCores I2C Controller
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

#include "i2c-oc.h"
#include "plic.h"
#include "spinlock.h"
#include "vector.h"
#include "mem_ops.h"
#include "utils.h"

#ifdef USE_FDT
#include "fdtlib.h"
#endif

struct i2c_bus {
    vector_t(i2c_dev_t) devices;
    plic_ctx_t* plic;
    struct fdt_node* fdt_node;
    uint32_t irq;
    spinlock_t lock;
    uint16_t sel_addr;
    uint16_t clock;
    uint8_t  control;
    uint8_t  status;
    uint8_t  tx_byte;
    uint8_t  rx_byte;
};

#define I2C_OC_REG_SIZE 0x14

// OpenCores I2C registers
#define I2C_OC_CLKLO    0x00 // Clock prescale low byte
#define I2C_OC_CLKHI    0x04 // Clock prescale high byte
#define I2C_OC_CTR      0x08 // Control register
#define I2C_OC_TXRXR    0x0C // Transmit & Receive register (W/R)
#define I2C_OC_CRSR     0x10 // Command & Status Register (W/R)

// Register values
#define I2C_OC_CTR_MASK 0xC0 // Mask of legal bits
#define I2C_OC_CTR_EN   0x80 // Core enable bit
#define I2C_OC_CTR_IEN  0x40 // Interrupt enable bit

#define I2C_OC_CR_STA   0x80 // Generate (repeated) start condition
#define I2C_OC_CR_STO   0x40 // Generate stop condition
#define I2C_OC_CR_RD    0x20 // Read from slave
#define I2C_OC_CR_WR    0x10 // Write to slave
#define I2C_OC_CR_ACK   0x08 // Send ACK (0) or NACK (1) to master
#define I2C_OC_CR_IACK  0x01 // Interrupt acknowledge, clear a pending IRQ

#define I2C_OC_SR_ACK   0x80 // Received ACK from slave (0), NACK is 1
#define I2C_OC_SR_BSY   0x40 // I2C bus busy
#define I2C_OC_SR_AL    0x20 // Arbitration lost
#define I2C_OC_SR_TIP   0x02 // Transfer in progress
#define I2C_OC_SR_IF    0x01 // Interrupt flag

static void i2c_oc_interrupt(i2c_bus_t* bus)
{
    bus->status |= I2C_OC_SR_IF;
    if (bus->control & I2C_OC_CTR_IEN) plic_send_irq(bus->plic, bus->irq);
}

static i2c_dev_t* i2c_oc_get_dev(i2c_bus_t* bus, uint16_t addr)
{
    vector_foreach(bus->devices, i) {
        i2c_dev_t* i2c_dev = &vector_at(bus->devices, i);
        if (i2c_dev->addr == addr) return i2c_dev;
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
            uint8_t cmd = read_uint8(data);
            bus->status |= I2C_OC_SR_ACK;
            if (cmd & I2C_OC_CR_IACK) {
                // Clear a pending interrupt
                bus->status &= ~I2C_OC_SR_IF;
            }
            if (cmd & I2C_OC_CR_STA) {
                // Start the transaction
                bus->sel_addr = 0xFFFF;
                bus->status |= I2C_OC_SR_BSY;
            }
            if ((cmd & I2C_OC_CR_WR)) {
                if (bus->sel_addr == 0xFFFF) {
                    // Get device address, signal start of transaction
                    bus->sel_addr = bus->tx_byte >> 1;
                    i2c_dev_t* i2c_dev = i2c_oc_get_dev(bus, bus->sel_addr);
                    bool is_write = !(bus->tx_byte & 1);
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
                if (i2c_dev && i2c_dev->stop) i2c_dev->stop(i2c_dev->data);
                bus->sel_addr = 0xFFFF;
                bus->status &= ~I2C_OC_SR_BSY;
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
    vector_foreach(bus->devices, i) {
        i2c_dev_t* i2c_dev = &vector_at(bus->devices, i);
        if (i2c_dev->remove) i2c_dev->remove(i2c_dev->data);
    }
    vector_free(bus->devices);
    free(bus);
}

static rvvm_mmio_type_t i2c_oc_dev_type = {
    .name = "i2c_opencores",
    .remove = i2c_oc_remove,
};

PUBLIC i2c_bus_t* i2c_oc_init(rvvm_machine_t* machine, rvvm_addr_t base_addr, plic_ctx_t* plic, uint32_t irq)
{
    i2c_bus_t* bus = safe_new_obj(i2c_bus_t);
    bus->plic = plic;
    bus->irq = irq;

    rvvm_mmio_dev_t i2c_oc = {
        .addr = base_addr,
        .size = I2C_OC_REG_SIZE,
        .data = bus,
        .read = i2c_oc_mmio_read,
        .write = i2c_oc_mmio_write,
        .type = &i2c_oc_dev_type,
        .min_op_size = 1,
        .max_op_size = 4,
    };
    if (rvvm_attach_mmio(machine, &i2c_oc) == RVVM_INVALID_MMIO) return NULL;
#ifdef USE_FDT
    struct fdt_node* i2c_clock = fdt_node_create_reg("i2c_osc", base_addr);
    fdt_node_add_prop_str(i2c_clock, "compatible", "fixed-clock");
    fdt_node_add_prop_u32(i2c_clock, "#clock-cells", 0);
    fdt_node_add_prop_u32(i2c_clock, "clock-frequency", 32768);
    fdt_node_add_prop_str(i2c_clock, "clock-output-names", "clk");
    fdt_node_add_child(rvvm_get_fdt_soc(machine), i2c_clock);

    struct fdt_node* i2c_fdt = fdt_node_create_reg("i2c", base_addr);
    fdt_node_add_prop_reg(i2c_fdt, "reg", base_addr, I2C_OC_REG_SIZE);
    fdt_node_add_prop_str(i2c_fdt, "compatible", "opencores,i2c-ocores");
    fdt_node_add_prop_u32(i2c_fdt, "interrupt-parent", plic_get_phandle(plic));
    fdt_node_add_prop_u32(i2c_fdt, "interrupts", irq);
    fdt_node_add_prop_u32(i2c_fdt, "clocks", fdt_node_get_phandle(i2c_clock));
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
    plic_ctx_t* plic = rvvm_get_plic(machine);
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, I2C_OC_DEFAULT_MMIO, I2C_OC_REG_SIZE);
    return i2c_oc_init(machine, addr, plic, plic_alloc_irq(plic));
}

PUBLIC uint16_t i2c_attach_dev(i2c_bus_t* bus, const i2c_dev_t* dev_desc)
{
    if (bus == NULL) return 0;
    i2c_dev_t tmp = *dev_desc;
    if (dev_desc->addr == I2C_AUTO_ADDR) tmp.addr = 0x8;
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
    if (bus == NULL) return NULL;
    return bus->fdt_node;
}
