/*
i2c-hid.c - i2c HID driver
Copyright (C) 2022  X512 <github.com/X547>

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

#include "i2c-hid.h"
#include "hid_api.h"
#include "i2c-oc.h"
#include "plic.h"
#include "spinlock.h"
#include "utils.h"
#include "bit_ops.h"

#include <stdio.h>

#ifdef USE_FDT
#include "fdtlib.h"
#endif

#define I2C_HID_DESC_REG    1
#define I2C_HID_REPORT_REG  2
#define I2C_HID_INPUT_REG   3
#define I2C_HID_OUTPUT_REG  4
#define I2C_HID_COMMAND_REG 5
#define I2C_HID_DATA_REG    6

#define I2C_HID_COMMAND_RESET        1
#define I2C_HID_COMMAND_GET_REPORT   2
#define I2C_HID_COMMAND_SET_REPORT   3
#define I2C_HID_COMMAND_GET_IDLE     4
#define I2C_HID_COMMAND_SET_IDLE     5
#define I2C_HID_COMMAND_GET_PROTOCOL 6
#define I2C_HID_COMMAND_SET_PROTOCOL 7
#define I2C_HID_COMMAND_SET_POWER    8

enum {
    wHIDDescLength,
    bcdVersion,
    wReportDescLength,
    wReportDescRegister,
    wInputRegister,
    wMaxInputLength,
    wOutputRegister,
    wMaxOutputLength,
    wCommandRegister,
    wDataRegister,
    wVendorID,
    wProductID,
    wVersionID,
};


struct report_id_queue {
    int16_t first;
    int16_t last;
    int16_t list[256];
};

typedef struct {
    hid_dev_t* hid_dev;

    spinlock_t  lock;

    // IRQ data
    plic_ctx_t* plic;
    uint32_t irq;
    bool int_pending;

    struct report_id_queue report_id_queue;

    // i2c IO state
    bool is_write;
    int32_t io_offset;
    uint16_t reg;
    uint8_t command;
    uint8_t report_type;
    uint8_t report_id;
    uint16_t data_size;
    uint16_t data_val;
    bool is_reset;
} i2c_hid_t;


static void report_id_queue_init(struct report_id_queue* queue)
{
    queue->first = -1;
    queue->last = -1;
    for (int16_t i = 0; i < 256; i++)
        queue->list[i] = -1;
}

static void report_id_queue_insert(struct report_id_queue* queue, uint8_t report_id)
{
    if (report_id == queue->last || queue->list[report_id] >= 0)
        return;
    if (queue->first < 0)
        queue->first = report_id;
    else
        queue->list[queue->last] = report_id;
    queue->last = report_id;
}

static int16_t report_id_queue_get(struct report_id_queue* queue)
{
    return queue->first;
}

static void report_id_queue_remove_at(struct report_id_queue* queue, uint8_t report_id)
{
    if (queue->first < 0) return;
    if (report_id == queue->first) {
        queue->first = queue->list[report_id];
        if (queue->first < 0) queue->last = -1;
    } else {
        int16_t prev = queue->first;
        while (prev >= 0 && queue->list[prev] != report_id) prev = queue->list[prev];
        if (prev < 0) return;
        queue->list[prev] = queue->list[report_id];
    }
    queue->list[report_id] = -1;
}


static void i2c_hid_reset(i2c_hid_t* i2c_hid, bool is_init)
{
    report_id_queue_init(&i2c_hid->report_id_queue);
    i2c_hid->reg = I2C_HID_INPUT_REG;
    i2c_hid->command = 0;
    i2c_hid->report_type = 0;
    i2c_hid->report_id = 0;
    i2c_hid->is_reset = !is_init;

    if (i2c_hid->hid_dev->reset) i2c_hid->hid_dev->reset(i2c_hid->hid_dev->dev);

    i2c_hid->int_pending = !is_init;
    if (!is_init)
        plic_send_irq(i2c_hid->plic, i2c_hid->irq);
}

static void i2c_hid_input_available(void* host, uint8_t report_id)
{
    i2c_hid_t* i2c_hid = (i2c_hid_t*)host;
    spin_lock(&i2c_hid->lock);
    if (!i2c_hid->is_reset) {
        report_id_queue_insert(&i2c_hid->report_id_queue, report_id);
        if (!i2c_hid->int_pending) {
            i2c_hid->int_pending = true;
            plic_send_irq(i2c_hid->plic, i2c_hid->irq);
        }
    }
    spin_unlock(&i2c_hid->lock);
}

static bool i2c_hid_read_data_size(i2c_hid_t* i2c_hid, uint32_t offset, uint8_t val)
{
    if (offset < 2)
        i2c_hid->data_size = bit_replace(i2c_hid->data_size, offset*8, 8, val);
    if (offset >= 1 && offset >= i2c_hid->data_size)
        return false;
    return true;
}

static void i2c_hid_read_report(i2c_hid_t* i2c_hid, uint8_t report_type, uint8_t report_id, uint32_t offset, uint8_t* val)
{
    i2c_hid->hid_dev->read_report(i2c_hid->hid_dev->dev, report_type, report_id, offset, val);
    if (offset < 2)
        i2c_hid->data_size = bit_replace(i2c_hid->data_size, offset*8, 8, *val);
    if (report_type == REPORT_TYPE_INPUT && offset >= 1 && offset == (uint32_t)(i2c_hid->data_size > 2 ? i2c_hid->data_size - 1 : 1)) {
        report_id_queue_remove_at(&i2c_hid->report_id_queue, report_id);
        if (report_id_queue_get(&i2c_hid->report_id_queue) >= 0)
            plic_send_irq(i2c_hid->plic, i2c_hid->irq);
        else
            i2c_hid->int_pending = false;
    }
}

static bool i2c_hid_write_report(i2c_hid_t* i2c_hid, uint8_t report_type, uint8_t report_id, uint32_t offset, uint8_t val)
{
    if (!i2c_hid_read_data_size(i2c_hid, offset, val))
        return false;
    i2c_hid->hid_dev->write_report(i2c_hid->hid_dev->dev, report_type, report_id, offset, val);
    return true;
}

static uint8_t i2c_hid_read_reg(i2c_hid_t* i2c_hid, uint16_t reg, uint32_t offset)
{
    switch (reg) {
    case I2C_HID_DESC_REG: {
        uint16_t field_val;
        switch (offset/2) {
        case wHIDDescLength:      field_val = 0x1e; break;
        case bcdVersion:          field_val = 0x0100; break;
        case wReportDescLength:   field_val = i2c_hid->hid_dev->report_desc_size; break;
        case wReportDescRegister: field_val = I2C_HID_REPORT_REG; break;
        case wInputRegister:      field_val = I2C_HID_INPUT_REG; break;
        case wMaxInputLength:     field_val = i2c_hid->hid_dev->max_input_size; break;
        case wOutputRegister:     field_val = I2C_HID_OUTPUT_REG; break;
        case wMaxOutputLength:    field_val = i2c_hid->hid_dev->max_output_size; break;
        case wCommandRegister:    field_val = I2C_HID_COMMAND_REG; break;
        case wDataRegister:       field_val = I2C_HID_DATA_REG; break;
        case wVendorID:           field_val = i2c_hid->hid_dev->vendor_id; break;
        case wProductID:          field_val = i2c_hid->hid_dev->product_id; break;
        case wVersionID:          field_val = i2c_hid->hid_dev->version_id; break;
        default:                  field_val = 0; break;
        }
        return bit_cut(field_val, 8*(offset%2), 8);
    }
    case I2C_HID_REPORT_REG:
        if (offset < i2c_hid->hid_dev->report_desc_size)
            return i2c_hid->hid_dev->report_desc[offset];
        break;
    case I2C_HID_INPUT_REG: {
        int16_t report_id = report_id_queue_get(&i2c_hid->report_id_queue);
        if (report_id < 0) {
            i2c_hid->int_pending = false;
            return 0;
        }
        uint8_t val = 0;
        i2c_hid_read_report(i2c_hid, REPORT_TYPE_INPUT, report_id, offset, &val);
        return val;
    }
    case I2C_HID_DATA_REG:
        switch (i2c_hid->command) {
        case I2C_HID_COMMAND_GET_REPORT: {
            uint8_t val = 0;
            i2c_hid_read_report(i2c_hid, i2c_hid->report_type, i2c_hid->report_id, offset, &val);
            return val;
        }
        case I2C_HID_COMMAND_GET_IDLE: {
            uint16_t field_val = 0;
            switch (offset/2) {
            case 0: field_val = 4; break;
            case 1:
                if (i2c_hid->hid_dev->get_idle)
                    i2c_hid->hid_dev->get_idle(i2c_hid->hid_dev->dev, i2c_hid->report_id, &field_val);
                break;
            }
            return bit_cut(field_val, 8*(offset%2), 8);
        }
        case I2C_HID_COMMAND_GET_PROTOCOL: {
            uint16_t field_val = 0;
            switch (offset/2) {
            case 0: field_val = 4; break;
            case 1:
                if (i2c_hid->hid_dev->get_protocol)
                    i2c_hid->hid_dev->get_protocol(i2c_hid->hid_dev->dev, &field_val);
                break;
            }
            return bit_cut(field_val, 8*(offset%2), 8);
        }
        }
        break;
    }
    return 0;
}

static bool i2c_hid_write_reg(i2c_hid_t* i2c_hid, uint16_t reg, uint32_t offset, uint8_t val)
{
    switch (reg) {
    case I2C_HID_OUTPUT_REG:
        return i2c_hid_write_report(i2c_hid, REPORT_TYPE_OUTPUT, 0, offset, val);
    case I2C_HID_COMMAND_REG:
        switch (offset) {
        case 0:
            i2c_hid->report_id   = bit_cut(val, 0, 4);
            i2c_hid->report_type = bit_cut(val, 4, 2);
            return true;
        case 1:
            i2c_hid->command = bit_cut(val, 0, 4);
            //fprintf(stderr, "  command: %u\n", i2c_hid->command);
            if (i2c_hid->report_id == 0b1111)
                return true;
            break;
        case 2:
            i2c_hid->report_id = val;
            break;
        }
        switch (i2c_hid->command) {
        case I2C_HID_COMMAND_SET_IDLE:
            if (i2c_hid->data_size == 4 && i2c_hid->hid_dev->set_idle)
                i2c_hid->hid_dev->set_idle(i2c_hid->hid_dev->dev, i2c_hid->report_id, i2c_hid->data_val);
            break;
        case I2C_HID_COMMAND_SET_PROTOCOL:
            if (i2c_hid->data_size == 4 && i2c_hid->hid_dev->set_protocol)
                i2c_hid->hid_dev->set_protocol(i2c_hid->hid_dev->dev, i2c_hid->data_val);
            break;
        case I2C_HID_COMMAND_SET_POWER:
            if (i2c_hid->hid_dev->set_power)
                i2c_hid->hid_dev->set_power(i2c_hid->hid_dev->dev, i2c_hid->report_id%4);
            break;
        }
        break;
    case I2C_HID_DATA_REG:
        switch (i2c_hid->command) {
        case I2C_HID_COMMAND_SET_REPORT:
            return i2c_hid_write_report(i2c_hid, i2c_hid->report_type, i2c_hid->report_id, offset, val);
        default:
            if (!i2c_hid_read_data_size(i2c_hid, offset, val))
                return false;
            if (offset/2 == 1)
                i2c_hid->data_val = bit_replace(i2c_hid->data_val, offset*8, 8, val);           
            return true;
        }
        break;
    }
    return false;
}

static bool i2c_hid_start(void* dev, bool is_write)
{
    i2c_hid_t* i2c_hid = (i2c_hid_t*)dev;
    spin_lock(&i2c_hid->lock);
    //fprintf(stderr, "i2c_hid_start(is_write: %d)\n", is_write);
    i2c_hid->is_write = is_write;
    i2c_hid->io_offset = 0;
    spin_unlock(&i2c_hid->lock);
    return true;
}

static bool i2c_hid_write(void* dev, uint8_t byte)
{
    i2c_hid_t* i2c_hid = (i2c_hid_t*)dev;
    spin_lock(&i2c_hid->lock);
    //fprintf(stderr, "i2c_hid_write, io_offset: %u, val: %#02x\n", i2c_hid->io_offset, byte);
    switch (i2c_hid->io_offset) {
    case 0:
    case 1:
        i2c_hid->reg = bit_replace(i2c_hid->reg, i2c_hid->io_offset*8, 8, byte);
        i2c_hid->io_offset++;
        //if (i2c_hid->io_offset == 2) {
        //  fprintf(stderr, "  reg: %u\n", i2c_hid->reg);
        //}
        break;
    default:
        if (i2c_hid_write_reg(i2c_hid, i2c_hid->reg, i2c_hid->io_offset - 2, byte))
            i2c_hid->io_offset++;
        else
            i2c_hid->io_offset = 0;
        break;
    }
    spin_unlock(&i2c_hid->lock);
    return true;
}

static bool i2c_hid_read(void* dev, uint8_t* byte)
{
    i2c_hid_t* i2c_hid = (i2c_hid_t*)dev;
    spin_lock(&i2c_hid->lock);
    //fprintf(stderr, "i2c_hid_read, io_offset: %u\n", i2c_hid->io_offset);
    *byte = i2c_hid_read_reg(i2c_hid, i2c_hid->reg, i2c_hid->io_offset++);
    //fprintf(stderr, "  val: %#02x\n", *byte);
    spin_unlock(&i2c_hid->lock);
    return true;
}

static void i2c_hid_stop(void* dev)
{
    i2c_hid_t* i2c_hid = (i2c_hid_t*)dev;
    spin_lock(&i2c_hid->lock);
    //fprintf(stderr, "i2c_hid_stop\n");
    i2c_hid->is_reset = false;
    switch (i2c_hid->command) {
    case I2C_HID_COMMAND_RESET:
        i2c_hid_reset(i2c_hid, false);
        break;
    }
    i2c_hid->reg = I2C_HID_INPUT_REG;
    i2c_hid->command = 0;
    i2c_hid->data_size = 0;
    spin_unlock(&i2c_hid->lock);
}

static void i2c_hid_remove(void* dev)
{
    i2c_hid_t* i2c_hid = (i2c_hid_t*)dev;
    i2c_hid->hid_dev->remove(i2c_hid->hid_dev->dev);
    free(i2c_hid);
}

static void i2c_hid_init(rvvm_machine_t* machine, i2c_bus_t* bus, uint16_t addr, plic_ctx_t* plic, uint32_t irq, hid_dev_t* hid_dev)
{
    UNUSED(machine);
    i2c_hid_t* i2c_hid = safe_new_obj(i2c_hid_t);

    spin_init(&i2c_hid->lock);

    i2c_dev_t i2c_dev = {
        .addr = addr,
        .data = i2c_hid,
        .start = i2c_hid_start,
        .write = i2c_hid_write,
        .read = i2c_hid_read,
        .stop = i2c_hid_stop,
        .remove = i2c_hid_remove
    };
    addr = i2c_attach_dev(bus, &i2c_dev);

    i2c_hid->plic = plic;
    i2c_hid->irq = irq;

    i2c_hid->hid_dev = hid_dev;
    hid_dev->host = i2c_hid;
    hid_dev->input_available = i2c_hid_input_available;

    i2c_hid_reset(i2c_hid, true);

#ifdef USE_FDT
    struct fdt_node* i2c_fdt = fdt_node_create_reg("i2c", addr);
    fdt_node_add_prop_str(i2c_fdt, "compatible", "hid-over-i2c");
    fdt_node_add_prop_u32(i2c_fdt, "reg", addr);
    fdt_node_add_prop_u32(i2c_fdt, "hid-descr-addr", I2C_HID_DESC_REG);
    fdt_node_add_prop_u32(i2c_fdt, "interrupt-parent", plic_get_phandle(plic));
    fdt_node_add_prop_u32(i2c_fdt, "interrupts", irq);
    fdt_node_add_child(i2c_bus_fdt_node(bus), i2c_fdt);
#endif
}

PUBLIC void i2c_hid_init_auto(rvvm_machine_t* machine, hid_dev_t* hid_dev)
{
    i2c_bus_t* bus = rvvm_get_i2c_bus(machine);
    plic_ctx_t* plic = rvvm_get_plic(machine);
    i2c_hid_init(machine, bus, I2C_AUTO_ADDR, plic, plic_alloc_irq(plic), hid_dev);
}
