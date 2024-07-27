/*
gpio-sifive.c - SiFive GPIO Controller
Copyright (C) 2024  LekKit <github.com/LekKit>

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

#include "gpio-sifive.h"
#include "plic.h"
#include "mem_ops.h"
#include "utils.h"
#include "fdtlib.h"

// See https://static.dev.sifive.com/FU540-C000-v1.0.pdf

#define GPIO_SIFIVE_REG_INPUT     0x00 // Pin input value
#define GPIO_SIFIVE_REG_INPUT_EN  0x04 // Pin input enable
#define GPIO_SIFIVE_REG_OUTPUT_EN 0x08 // Pin output enable
#define GPIO_SIFIVE_REG_OUTPUT    0x0C // Pin output value
#define GPIO_SIFIVE_REG_PUE       0x10 // Pull-up enable
#define GPIO_SIFIVE_REG_DS        0x14 // Drive strength
#define GPIO_SIFIVE_REG_RISE_IE   0x18 // Rise interrupt enable
#define GPIO_SIFIVE_REG_RISE_IP   0x1C // Rise interrupt pending
#define GPIO_SIFIVE_REG_FALL_IE   0x20 // Rise interrupt enable
#define GPIO_SIFIVE_REG_FALL_IP   0x24 // Fall interrupt pending
#define GPIO_SIFIVE_REG_HIGH_IE   0x28 // High interrupt enable
#define GPIO_SIFIVE_REG_HIGH_IP   0x2C // High interrupt pending
#define GPIO_SIFIVE_REG_LOW_IE    0x30 // Low interrupt enable
#define GPIO_SIFIVE_REG_LOW_IP    0x34 // Low interrupt pending
#define GPIO_SIFIVE_REG_OUT_XOR   0x40 // Output XOR (Invert)

#define GPIO_SIFIVE_MMIO_SIZE 0x44

typedef struct {
    rvvm_gpio_dev_t* gpio;
    plic_ctx_t* plic;
    uint32_t plic_irqs[GPIO_SIFIVE_PINS];

    // Cache IRQ lines state
    uint32_t irqs;

    // Input pins
    uint32_t pins;

    // Controller registers
    uint32_t input_en;
    uint32_t output_en;
    uint32_t output;
    uint32_t pue;
    uint32_t ds;
    uint32_t rise_ie;
    uint32_t rise_ip;
    uint32_t fall_ie;
    uint32_t fall_ip;
    uint32_t high_ie;
    uint32_t high_ip;
    uint32_t low_ie;
    uint32_t low_ip;
    uint32_t out_xor;
} gpio_sifive_dev_t;

static void gpio_sifive_update_irqs(gpio_sifive_dev_t* bus)
{
    // Combine pin IRQs
    uint32_t ip = (atomic_load_uint32(&bus->rise_ip) & atomic_load_uint32(&bus->rise_ie))
                | (atomic_load_uint32(&bus->fall_ip) & atomic_load_uint32(&bus->fall_ie))
                | (atomic_load_uint32(&bus->high_ip) & atomic_load_uint32(&bus->high_ie))
                | (atomic_load_uint32(&bus->low_ip)  & atomic_load_uint32(&bus->low_ie));

    // Update PLIC IRQs
    if (atomic_swap_uint32(&bus->irqs, ip) != ip) {
        for (size_t i=0; i<GPIO_SIFIVE_PINS; ++i) {
            if (ip & (1U << i)) {
                plic_raise_irq(bus->plic, bus->plic_irqs[i]);
            } else {
                plic_lower_irq(bus->plic, bus->plic_irqs[i]);
            }
        }
    }
}

static void gpio_sifive_update_pins(gpio_sifive_dev_t* bus, uint32_t pins)
{
    uint32_t old_pins = atomic_swap_uint32(&bus->pins, pins);
    uint32_t enable = atomic_load_uint32(&bus->input_en);
    uint32_t pins_rise = (pins & ~old_pins);
    uint32_t pins_fall = (~pins & old_pins);
    atomic_or_uint32(&bus->rise_ip, pins_rise & enable);
    atomic_or_uint32(&bus->fall_ip, pins_fall & enable);
    atomic_or_uint32(&bus->high_ip, pins & enable);
    atomic_or_uint32(&bus->low_ip, ~pins & enable);
    gpio_sifive_update_irqs(bus);
}

static void gpio_sifive_update_out(gpio_sifive_dev_t* bus)
{
    uint32_t out = atomic_load_uint32(&bus->output);
    out &= atomic_load_uint32(&bus->output_en);
    out ^= atomic_load_uint32(&bus->out_xor);
    gpio_pins_out(bus->gpio, 0, out);
}

static bool gpio_sifive_pins_in(rvvm_gpio_dev_t* gpio, size_t off, uint32_t pins)
{
    if (off == 0) {
        gpio_sifive_dev_t* bus = gpio->io_dev;
        gpio_sifive_update_pins(bus, pins);
        return true;
    }
    return false;
}

static uint32_t gpio_sifive_pins_read(rvvm_gpio_dev_t* gpio, size_t off)
{
    if (off == 0) {
        gpio_sifive_dev_t* bus = gpio->io_dev;
        uint32_t out = atomic_load_uint32(&bus->output);
        out &= atomic_load_uint32(&bus->output_en);
        out ^= atomic_load_uint32(&bus->out_xor);
        return out;
    }
    return 0;
}

static bool gpio_sifive_mmio_read(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    gpio_sifive_dev_t* bus = dev->data;
    memset(data, 0, size);
    switch (offset) {
        case GPIO_SIFIVE_REG_INPUT:
            write_uint32_le_m(data, atomic_load_uint32(&bus->pins) & atomic_load_uint32(&bus->input_en));
            break;
        case GPIO_SIFIVE_REG_INPUT_EN:
            write_uint32_le_m(data, atomic_load_uint32(&bus->input_en));
            break;
        case GPIO_SIFIVE_REG_OUTPUT_EN:
            write_uint32_le_m(data, atomic_load_uint32(&bus->output_en));
            break;
        case GPIO_SIFIVE_REG_OUTPUT:
            write_uint32_le_m(data, atomic_load_uint32(&bus->output));
            break;
        case GPIO_SIFIVE_REG_PUE:
            write_uint32_le_m(data, atomic_load_uint32(&bus->pue));
            break;
        case GPIO_SIFIVE_REG_DS:
            write_uint32_le_m(data, atomic_load_uint32(&bus->ds));
            break;
        case GPIO_SIFIVE_REG_RISE_IE:
            write_uint32_le_m(data, atomic_load_uint32(&bus->rise_ie));
            break;
        case GPIO_SIFIVE_REG_RISE_IP:
            write_uint32_le_m(data, atomic_load_uint32(&bus->rise_ip));
            break;
        case GPIO_SIFIVE_REG_FALL_IE:
            write_uint32_le_m(data, atomic_load_uint32(&bus->fall_ie));
            break;
        case GPIO_SIFIVE_REG_FALL_IP:
            write_uint32_le_m(data, atomic_load_uint32(&bus->fall_ip));
            break;
        case GPIO_SIFIVE_REG_HIGH_IE:
            write_uint32_le_m(data, atomic_load_uint32(&bus->high_ie));
            break;
        case GPIO_SIFIVE_REG_HIGH_IP:
            write_uint32_le_m(data, atomic_load_uint32(&bus->high_ip));
            break;
        case GPIO_SIFIVE_REG_LOW_IE:
            write_uint32_le_m(data, atomic_load_uint32(&bus->low_ie));
            break;
        case GPIO_SIFIVE_REG_LOW_IP:
            write_uint32_le_m(data, atomic_load_uint32(&bus->low_ip));
            break;
        case GPIO_SIFIVE_REG_OUT_XOR:
            write_uint32_le_m(data, atomic_load_uint32(&bus->out_xor));
            break;
    }
    return true;
}

static bool gpio_sifive_mmio_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    gpio_sifive_dev_t* bus = dev->data;
    UNUSED(size);
    switch (offset) {
        case GPIO_SIFIVE_REG_INPUT_EN:
            atomic_store_uint32(&bus->input_en, read_uint32_le_m(data));
            gpio_sifive_update_pins(bus, atomic_load_uint32(&bus->pins));
            break;
        case GPIO_SIFIVE_REG_OUTPUT_EN:
            atomic_store_uint32(&bus->output_en, read_uint32_le_m(data));
            gpio_sifive_update_out(bus);
            break;
        case GPIO_SIFIVE_REG_OUTPUT:
            atomic_store_uint32(&bus->output, read_uint32_le_m(data));
            gpio_sifive_update_out(bus);
            break;
        case GPIO_SIFIVE_REG_PUE:
            atomic_store_uint32(&bus->pue, read_uint32_le_m(data));
            break;
        case GPIO_SIFIVE_REG_DS:
            atomic_store_uint32(&bus->ds, read_uint32_le_m(data));
            break;
        case GPIO_SIFIVE_REG_RISE_IE:
            atomic_store_uint32(&bus->rise_ie, read_uint32_le_m(data));
            gpio_sifive_update_irqs(bus);
            break;
        case GPIO_SIFIVE_REG_RISE_IP:
            atomic_and_uint32(&bus->rise_ip, ~read_uint32_le_m(data));
            gpio_sifive_update_irqs(bus);
            break;
        case GPIO_SIFIVE_REG_FALL_IE:
            atomic_store_uint32(&bus->fall_ie, read_uint32_le_m(data));
            gpio_sifive_update_irqs(bus);
            break;
        case GPIO_SIFIVE_REG_FALL_IP:
            atomic_and_uint32(&bus->fall_ip, ~read_uint32_le_m(data));
            gpio_sifive_update_irqs(bus);
            break;
        case GPIO_SIFIVE_REG_HIGH_IE:
            atomic_store_uint32(&bus->high_ie, read_uint32_le_m(data));
            gpio_sifive_update_irqs(bus);
            break;
        case GPIO_SIFIVE_REG_HIGH_IP:
            atomic_and_uint32(&bus->high_ip, ~read_uint32_le_m(data));
            gpio_sifive_update_irqs(bus);
            break;
        case GPIO_SIFIVE_REG_LOW_IE:
            atomic_store_uint32(&bus->low_ie, read_uint32_le_m(data));
            gpio_sifive_update_irqs(bus);
            break;
        case GPIO_SIFIVE_REG_LOW_IP:
            atomic_and_uint32(&bus->low_ip, ~read_uint32_le_m(data));
            gpio_sifive_update_irqs(bus);
            break;
        case GPIO_SIFIVE_REG_OUT_XOR:
            atomic_store_uint32(&bus->out_xor, read_uint32_le_m(data));
            gpio_sifive_update_out(bus);
            break;
    }
    return true;
}

static void gpio_sifive_remove(rvvm_mmio_dev_t* dev)
{
    gpio_sifive_dev_t* bus = dev->data;
    gpio_free(bus->gpio);
    free(bus);
}

static void gpio_sifive_update(rvvm_mmio_dev_t* dev)
{
    gpio_sifive_dev_t* bus = dev->data;
    gpio_update(bus->gpio);
}

static rvvm_mmio_type_t gpio_sifive_dev_type = {
    .name = "gpio_sifive",
    .remove = gpio_sifive_remove,
    .update = gpio_sifive_update,
};

PUBLIC rvvm_mmio_handle_t gpio_sifive_init(rvvm_machine_t* machine, rvvm_gpio_dev_t* gpio,
                                           rvvm_addr_t base_addr, plic_ctx_t* plic, uint32_t* irqs)
{
    gpio_sifive_dev_t* bus = safe_new_obj(gpio_sifive_dev_t);
    bus->gpio = gpio;
    bus->plic = plic;

    // Amount of IRQs controlls amount of GPIO pins
    // Each GPIO pin should have a unique IRQ!
    for (size_t i=0; i<GPIO_SIFIVE_PINS; ++i) {
        bus->plic_irqs[i] = irqs[i];
    }

    if (gpio) {
        gpio->io_dev = bus;
        gpio->pins_in = gpio_sifive_pins_in;
        gpio->pins_read = gpio_sifive_pins_read;
    }

    rvvm_mmio_dev_t gpio_sifive = {
        .addr = base_addr,
        .size = GPIO_SIFIVE_MMIO_SIZE,
        .data = bus,
        .read = gpio_sifive_mmio_read,
        .write = gpio_sifive_mmio_write,
        .type = &gpio_sifive_dev_type,
        .min_op_size = 4,
        .max_op_size = 4,
    };

    rvvm_mmio_handle_t handle = rvvm_attach_mmio(machine, &gpio_sifive);
    if (handle == RVVM_INVALID_MMIO) return handle;

#ifdef USE_FDT
    struct fdt_node* gpio_fdt = fdt_node_create_reg("gpio", base_addr);
    fdt_node_add_prop_reg(gpio_fdt, "reg", base_addr, GPIO_SIFIVE_MMIO_SIZE);
    fdt_node_add_prop_str(gpio_fdt, "compatible", "sifive,gpio0");
    fdt_node_add_prop_u32(gpio_fdt, "interrupt-parent", plic_get_phandle(plic));
    fdt_node_add_prop_cells(gpio_fdt, "interrupts", bus->plic_irqs, GPIO_SIFIVE_PINS);
    fdt_node_add_prop(gpio_fdt, "gpio-controller", NULL, 0);
    fdt_node_add_prop_u32(gpio_fdt, "#gpio-cells", 2);
    fdt_node_add_prop(gpio_fdt, "interrupt-controller", NULL, 0);
    fdt_node_add_prop_u32(gpio_fdt, "#interrupt-cells", 2);
    fdt_node_add_prop_u32(gpio_fdt, "ngpios", 32);
    fdt_node_add_prop_str(gpio_fdt, "status", "okay");
    fdt_node_add_child(rvvm_get_fdt_soc(machine), gpio_fdt);
#endif
    return handle;
}

PUBLIC rvvm_mmio_handle_t gpio_sifive_init_auto(rvvm_machine_t* machine, rvvm_gpio_dev_t* gpio)
{
    plic_ctx_t* plic = rvvm_get_plic(machine);
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, GPIO_SIFIVE_DEFAULT_MMIO, GPIO_SIFIVE_MMIO_SIZE);
    uint32_t irqs[GPIO_SIFIVE_PINS] = {0};
    for (size_t i=0; i<GPIO_SIFIVE_PINS; ++i) {
        irqs[i] = plic_alloc_irq(plic);
    }
    return gpio_sifive_init(machine, gpio, addr, plic, irqs);
}
