/*
pci-bus.c - Peripheral Component Interconnect Bus
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

#include "pci-bus.h"
#include "bit_ops.h"
#include "plic.h"
#include <stdio.h>

static void pci_bus_remove(rvvm_mmio_dev_t *dev)
{
    struct pci_bus_list *list = (struct pci_bus_list *) dev->data;
    for (size_t i = 0; i < list->count; ++i) {
        vector_free(list->buses[i].devices);
    }
}

rvvm_mmio_type_t pci_bus_type = {
    .name = "pci_cam",
    .remove = pci_bus_remove,
};

#define PCI_REG_DEV_VEN_ID 0x0
#define PCI_REG_STATUS_CMD 0x4
#define PCI_REG_CLASS_REV  0x8
#define PCI_REG_BIST_HDR_LATENCY_CACHE 0xC

#define PCI_REG_BAR0       0x10
#define PCI_REG_BAR1       0x14
#define PCI_REG_BAR2       0x18
#define PCI_REG_BAR3       0x1C
#define PCI_REG_BAR4       0x20
#define PCI_REG_BAR5       0x24

#define PCI_REG_SSID_SVID  0x2C

#define PCI_REG_EXPANSION_ROM 0x30
#define PCI_REG_CAP_PTR    0x34
#define PCI_REG_IRQ_PIN_LINE 0x3c

static inline bool pci_bus_read_invalid(uint8_t reg, void *dest, uint8_t size)
{
    switch (reg) {
        case PCI_REG_DEV_VEN_ID:
            /* nonexistent devices have vendor ID of 0xFFFF */
            memset(dest, 0xFF, size);
            return true;
        default:
            /* ignore others */
            memset(dest, 0x0, size);
            return true;
    }
}

static inline bool pci_bus_write_invalid(uint8_t reg, void *dest, uint8_t size)
{
    UNUSED(reg);
    UNUSED(dest);
    UNUSED(size);
    return true;
}

static bool pci_bar_is_io(const struct pci_bar_desc *desc) {
    // return desc->read || desc->write;
    UNUSED(desc);
    return false;
}

void pci_send_irq(struct pci_func *func) {
    /* no interrupt specified */
    if (func->desc->irq_pin == 0) {
        return;
    }

    /* check interrupt disable bit */
    if (bit_check(func->command, 10)) {
        return;
    }

    /* set interrupt status bit */
    func->status |= (1 << 3);
    struct pci_device *dev = func->dev;
    struct pci_bus *bus = dev->bus;
    plic_send_irq(bus->machine, bus->intc_data, bus->irq[func->desc->irq_pin - 1]);
}

void pci_clear_irq(struct pci_func *func) {
    /* clear interrupt status bit */
    func->status &= ~(1 << 3);
}

static bool pci_bus_read(rvvm_mmio_dev_t *mmio_dev, void *dest, paddr_t offset, uint8_t size)
{
    struct pci_bus_list *list = (struct pci_bus_list *) mmio_dev->data;
    uint8_t reg = bit_cut(offset, 0, list->bus_shift - 8);
    uint8_t fun = bit_cut(offset, list->bus_shift - 8, 3);
    uint8_t dev = bit_cut(offset, list->bus_shift - 5, 5);
    uint8_t bus = offset >> list->bus_shift;

    rvvm_info("PCI read %x:%x.%x reg 0x%x size %d", bus, dev, fun, reg, size);

    if (bus >= list->count) {
        return pci_bus_read_invalid(reg, dest, size);
    }
    struct pci_bus *pci_bus = &list->buses[bus];

    if (dev >= vector_size(pci_bus->devices)) {
        return pci_bus_read_invalid(reg, dest, size);
    }
    struct pci_device *device = &vector_at(pci_bus->devices, dev);

    if (fun >= 8) {
        return pci_bus_read_invalid(reg, dest, size);
    }
    struct pci_func *func = &device->func[fun];

    switch (reg) {
        case PCI_REG_DEV_VEN_ID:
            {
                uint32_t reg = func->desc->vendor_id | (uint32_t)func->desc->device_id << 16;
                memcpy(dest, &reg, size);
                return true;
            }
        case PCI_REG_STATUS_CMD:
            {
                /* idk why '| 3' is needed, kernel should set these bits... */
                uint32_t reg = func->status << 16 | func->command | 3;
                memcpy(dest, &reg, size);
                return true;
            }
        case PCI_REG_CLASS_REV:
            {
                uint32_t reg = func->desc->class_code << 16
                   | (uint32_t)func->desc->prog_if << 8;
                memcpy(dest, &reg, size);
                return true;
            }
        case PCI_REG_BIST_HDR_LATENCY_CACHE:
            {
                bool mf = false;
                for (size_t i = 1; i < 8; ++i) {
                    for (size_t j = 0; j < 6; ++j) {
                        if (device->func[i].desc->bar[j].len != 0) {
                            mf = true;
                            break;
                        }
                    }
                }

                uint32_t reg = (uint32_t)mf << 23;
                memcpy(dest, &reg, size);
                return true;
            }
        case PCI_REG_CAP_PTR: /* currently no capabilities supported */
        case PCI_REG_SSID_SVID: /* not necessary */
        case PCI_REG_EXPANSION_ROM: /* not needed for now */
        case 0xf8: /* needed for Intel ATA probe */
        case 0x40: /* needed for Intel ATA probe */
            memset(dest, 0x0, size);
            return true;
        case PCI_REG_IRQ_PIN_LINE:
            {
                uint32_t reg = func->irq_line | (uint32_t)func->desc->irq_pin << 8;
                memcpy(dest, &reg, size);
                return true;
            }
        case PCI_REG_BAR0:
        case PCI_REG_BAR1:
        case PCI_REG_BAR2:
        case PCI_REG_BAR3:
        case PCI_REG_BAR4:
        case PCI_REG_BAR5:
            {
                uint8_t bar_num = (reg - 0x10) >> 2;
                if (func->desc->bar[bar_num].len == 0) {
                    memset(dest, 0x0, size);
                    return true;
                }

                rvvm_mmio_dev_t *bar_dev = rvvm_get_mmio(mmio_dev->machine,
                        func->bar_mapping[bar_num]);
                if (bar_dev == NULL) {
                    memset(dest, 0x0, size);
                    return true;
                }

                uint32_t reg = (uint32_t)bar_dev->begin | pci_bar_is_io(&func->desc->bar[bar_num]);
                memcpy(dest, &reg, size);
                return true;
            }
    }

    return pci_bus_read_invalid(reg, dest, size);
}

static bool pci_bus_write(rvvm_mmio_dev_t *mmio_dev, void *dest, paddr_t offset, uint8_t size)
{
    struct pci_bus_list *list = (struct pci_bus_list *) mmio_dev->data;
    uint8_t reg = bit_cut(offset, 0, list->bus_shift - 8);
    uint8_t fun = bit_cut(offset, list->bus_shift - 8, 3);
    uint8_t dev = bit_cut(offset, list->bus_shift - 5, 5);
    uint8_t bus = offset >> list->bus_shift;

    rvvm_info("PCI write %x:%x.%x reg 0x%x size %d", bus, dev, fun, reg, size);

    if (bus >= list->count) {
        return pci_bus_write_invalid(reg, dest, size);
    }
    struct pci_bus *pci_bus = &list->buses[bus];

    if (dev >= vector_size(pci_bus->devices)) {
        return pci_bus_write_invalid(reg, dest, size);
    }
    struct pci_device *device = &vector_at(pci_bus->devices, dev);

    if (fun >= 8) {
        return pci_bus_write_invalid(reg, dest, size);
    }
    struct pci_func *func = &device->func[fun];

    switch (reg) {
        case PCI_REG_EXPANSION_ROM: /* not needed for now, works as BAR */
        case PCI_REG_BIST_HDR_LATENCY_CACHE:
        case 0x40: /* needed for Intel ATA probe */
            return true;
        case PCI_REG_STATUS_CMD:
            {
                uint32_t val;
                memcpy(&val, dest, size);
                func->command = val & 0xff;
                return true;
            }
        case PCI_REG_BAR0:
        case PCI_REG_BAR1:
        case PCI_REG_BAR2:
        case PCI_REG_BAR3:
        case PCI_REG_BAR4:
        case PCI_REG_BAR5:
            {
                uint8_t bar_num = (reg - 0x10) >> 2;
                uint32_t len = (uint32_t)func->desc->bar[bar_num].len;
                if (len == 0) {
                    return true;
                }

                uint32_t addr;
                memcpy(&addr, dest, size);
                uint32_t mask = (addr & 1) ? ~(uint32_t)3 : ~(uint32_t)15;
                addr &= mask;
                if (~(uint32_t)0 - addr < len) {
                    addr = -len;
                }

                rvvm_mmio_dev_t *bar_dev = rvvm_get_mmio(mmio_dev->machine,
                        func->bar_mapping[bar_num]);
                if (bar_dev == NULL) {
                    return true;
                }

                /* must be atomic... */
                paddr_t mmio_len = bar_dev->end - bar_dev->begin;
                bar_dev->begin = (paddr_t)addr;
                bar_dev->end = (paddr_t)addr + mmio_len;
                /* ...up to this point. But no way to make it... */
                return true;
            }
        case PCI_REG_IRQ_PIN_LINE:
            {
                uint32_t val;
                memcpy(&val, dest, size);
                func->irq_line = val & 0xff;
                return true;
            }
    }

    return pci_bus_write_invalid(reg, dest, size);
}

rvvm_mmio_type_t bar_type = {
    .name = "pci_bar_map",
};

struct pci_device* pci_bus_add_device(rvvm_machine_t *machine,
                struct pci_bus *bus,
                struct pci_device_desc *desc,
                void *data)
{
    vector_emplace_back(bus->devices);
    struct pci_device *dev = &vector_at(bus->devices, vector_size(bus->devices) - 1);
    dev->bus = bus;
    dev->desc = desc;

    for (size_t fun = 0; fun < 8; ++fun) {
        struct pci_func *func = &dev->func[fun];
        struct pci_func_desc *func_desc = &dev->desc->func[fun];
        func->dev = dev;
        func->desc = func_desc;
        func->command = 0x7; /* io+mem access and bus mastering */
        func->data = data;

        for (size_t i = 0; i < 6; ++i) {
            struct pci_bar_desc *bar = &dev->desc->func[fun].bar[i];
            if (bar->len == 0) {
                func->bar_mapping[i] = RVVM_INVALID_MMIO;
                continue;
            }

            paddr_t addr;
            if (pci_bar_is_io(bar)) {
                addr = bus->io_addr;
                bus->io_addr += bar->len;
                bus->io_len -= bar->len;
            } else {
                addr = bus->mem_addr;
                bus->mem_addr += bar->len;
                bus->mem_len -= bar->len;
            }

            rvvm_mmio_dev_t bar_map = {
                .machine = machine,
                .begin = addr,
                .end = addr + bar->len,
                .min_op_size = bar->min_op_size,
                .max_op_size = bar->max_op_size,
                .read = bar->read,
                .write = bar->write,
                .type = &bar_type,
                .data = (void*) func,
            };

            func->bar_mapping[i] = rvvm_attach_mmio(machine, &bar_map);
        }
    }

    return dev;
}

struct pci_bus_list* pci_bus_init(rvvm_machine_t *machine,
        size_t bus_count,
        bool is_ecam,
        paddr_t base_addr,
        paddr_t io_addr,
        paddr_t io_len,
        paddr_t mem_addr,
        paddr_t mem_len,
        void *intc_data,
        uint32_t irq)
{
    size_t shift = is_ecam ? 20 : 16;

    rvvm_mmio_dev_t cam_dev = {
        .machine = machine,
        .begin = base_addr,
        .end = base_addr + (bus_count << shift),
        .min_op_size = 4,
        .max_op_size = 4,
        .read = pci_bus_read,
        .write = pci_bus_write,
        .type = &pci_bus_type,
    };

    struct pci_bus_list *list = safe_malloc(sizeof(struct pci_bus_list));
    list->count = bus_count;
    list->buses = (struct pci_bus *) safe_calloc(sizeof(struct pci_bus), bus_count);
    list->bus_shift = shift;
    for (size_t i = 0; i < bus_count; ++i) {
        vector_init(list->buses[i].devices);

        list->buses[i].machine = machine;
        list->buses[i].intc_data = intc_data;
        for (size_t j = 0; j < 4; ++j) {
            list->buses[i].irq[j] = irq;
        }

        list->buses[i].io_addr = io_addr;
        list->buses[i].io_len = io_len;
        list->buses[i].mem_addr = mem_addr;
        list->buses[i].mem_len = mem_len;
    }
    cam_dev.data = list;

    rvvm_attach_mmio(machine, &cam_dev);

    return list;
}

#ifdef USE_FDT
struct pci_bus_list* pci_bus_init_dt(rvvm_machine_t *machine,
        size_t bus_count,
        bool is_ecam,
        paddr_t base_addr,
        paddr_t io_addr,
        paddr_t io_len,
        paddr_t mem_addr,
        paddr_t mem_len,
        void *intc_data,
        uint32_t irq)
{
    struct pci_bus_list *list = pci_bus_init(machine,
            bus_count,
            is_ecam,
            base_addr,
            io_addr,
            io_len,
            mem_addr,
            mem_len,
            intc_data,
            irq);

    struct fdt_node* soc = fdt_node_find(machine->fdt, "soc");
    struct fdt_node* plic = soc ? fdt_node_find_reg_any(soc, "plic") : NULL;
    if (plic == NULL) {
        rvvm_warn("Missing nodes in FDT!");
        return list;
    }

    struct fdt_node* pci_node = fdt_node_create_reg("pci", base_addr);
    const char *compatible = is_ecam ? "pci-host-ecam-generic" : "pci-host-cam-generic";
    fdt_node_add_prop_str(pci_node, "compatible", compatible);
    fdt_node_add_prop_str(pci_node, "device_type", "pci");
    fdt_node_add_prop_u32(pci_node, "#address-cells", 3);
    fdt_node_add_prop_u32(pci_node, "#size-cells", 2);

#ifdef USE_RV64
#define HIADDR(addr) ((addr) >> 32)
#define LOADDR(addr) ((addr) & ~(uint32_t)0)
#define ADDR(addr) HIADDR(addr), LOADDR(addr)
#else
#define HIADDR(addr) 0
#define LOADDR(addr) ((addr) & ~(uint32_t)0)
#define ADDR(addr) HIADDR(addr), LOADDR(addr)
#endif

    size_t shift = is_ecam ? 20 : 16;
    paddr_t len = bus_count << shift;
    uint32_t reg[4] = {
        ADDR(base_addr), ADDR(len)
    };
    fdt_node_add_prop_cells(pci_node, "reg", reg, 4);
    uint32_t bus_range[2] = {
        0, bus_count - 1,
    };
    fdt_node_add_prop_cells(pci_node, "bus-range", bus_range, 2);

#define CFG_HI(cacheable, space, bus, dev, fun, reg) \
    ((cacheable) << 30 | (space) << 24 | (bus) << 16 | (dev) << 11 | (fun) << 8 | (reg))

    uint32_t ranges[7 * 2] = {
        CFG_HI(0, 1, 0, 0, 0, 0),
        ADDR(io_addr),
        ADDR(io_addr),
        ADDR(io_len),

        CFG_HI(0, 2, 0, 0, 0, 0),
        ADDR(mem_addr),
        ADDR(mem_addr),
        ADDR(mem_len),
    };
    fdt_node_add_prop_cells(pci_node, "ranges", ranges, 7 * 2);

    fdt_node_add_prop_u32(pci_node, "#interrupt-cells", 1);
    uint32_t interrupt_map[6] = {
        CFG_HI(0, 0, 0, 0, 0, 0), 0, 0, 1, fdt_node_get_phandle(plic), irq,
    };
    fdt_node_add_prop_cells(pci_node, "interrupt-map", interrupt_map, 6);
    uint32_t interrupt_mask[4] = {
        CFG_HI(0, 0, 0, 0, 0, 0), 0, 0, 7,
    };
    fdt_node_add_prop_cells(pci_node, "interrupt-map-mask", interrupt_mask, 4);
    fdt_node_add_child(soc, pci_node);

    return list;
}
#endif
