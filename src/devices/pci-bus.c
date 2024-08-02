/*
pci-bus.c - Peripheral Component Interconnect Bus
Copyright (C) 2021  LekKit <github.com/LekKit>
                    cerg2010cerg2010 <github.com/cerg2010cerg2010>

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
#include "fdtlib.h"
#include "utils.h"
#include "atomics.h"
#include "bit_ops.h"
#include "mem_ops.h"

#define PCI_REG_DEV_VEN_ID    0x0
#define PCI_REG_STATUS_CMD    0x4
#define PCI_REG_CLASS_REV     0x8
#define PCI_REG_BIST_HDR_LATENCY_CACHE 0xC

#define PCI_REG_BAR0          0x10
#define PCI_REG_BAR1          0x14
#define PCI_REG_BAR2          0x18
#define PCI_REG_BAR3          0x1C
#define PCI_REG_BAR4          0x20
#define PCI_REG_BAR5          0x24

#define PCI_REG_SSID_SVID     0x2C

#define PCI_REG_EXPANSION_ROM 0x30
#define PCI_REG_CAP_PTR       0x34
#define PCI_REG_IRQ_PIN_LINE  0x3c

#define PCI_CMD_IO_SPACE      0x1  // Accessible through IO ports
#define PCI_CMD_MEM_SPACE     0x2  // Accessible through MMIO
#define PCI_CMD_BUS_MASTER    0x4  // May use DMA
#define PCI_CMD_MWI_ENABLE    0x10 // May use Memory Write and Invalidate
#define PCI_CMD_DEFAULT       0x17
#define PCI_CMD_IRQ_DISABLE   0x400

#define PCI_STATUS_IRQ        0x8

typedef struct {
    pci_dev_t* dev;
    rvvm_mmio_dev_t* bar[PCI_FUNC_BARS];

    // Atomic variables
    uint32_t status;
    uint32_t command;
    uint32_t irq_line;

    // RO attributes
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t class_code;
    uint8_t prog_if;
    uint8_t rev;
    uint8_t irq_pin;
} pci_func_t;

struct pci_device {
    pci_bus_t* bus;
    pci_func_t* func[PCI_DEV_FUNCS];
    uint8_t dev_id;
};

struct pci_bus {
    rvvm_machine_t* machine;
    plic_ctx_t* plic;
    uint32_t irq[PCI_BUS_IRQS];

    struct pci_device* dev[PCI_BUS_DEVS];

    rvvm_addr_t io_addr;
    size_t      io_len;
    rvvm_addr_t mem_addr;
    size_t      mem_len;

    uint8_t bus_shift; // 20 for ECAM (PCIe), 16 for regular CAM
    uint8_t bus_id;
};

static pci_func_t* pci_get_func(pci_bus_t* bus, uint8_t bus_id, uint8_t dev_id, uint8_t fun_id)
{
    pci_dev_t* dev = bus->dev[dev_id];
    if (bus_id != bus->bus_id || dev == NULL) {
        return NULL;
    }
    return dev->func[fun_id];
}

static bool pci_bus_read(rvvm_mmio_dev_t* mmio_dev, void* data, size_t offset, uint8_t size)
{
    pci_bus_t* bus = mmio_dev->data;
    uint8_t bus_id = offset >> bus->bus_shift;
    uint8_t dev_id = bit_cut(offset, bus->bus_shift - 5, 5);
    uint8_t fun_id = bit_cut(offset, bus->bus_shift - 8, 3);
    uint8_t reg = bit_cut(offset, 0, bus->bus_shift - 8);
    UNUSED(size);

    pci_func_t* func = pci_get_func(bus, bus_id, dev_id, fun_id);
    if (!func) {
        // Nonexistent devices have all 0xFFFF in their conf space
        write_uint32_le_m(data, 0xFFFFFFFF);
        return true;
    }

    write_uint32_le_m(data, 0);
    switch (reg) {
        case PCI_REG_DEV_VEN_ID:
            write_uint32_le_m(data, func->vendor_id | (uint32_t)func->device_id << 16);
            break;
        case PCI_REG_STATUS_CMD:
            write_uint32_le_m(data, atomic_load_uint32(&func->status) << 16 | atomic_load_uint32(&func->command));
            break;
        case PCI_REG_CLASS_REV:
            write_uint32_le_m(data, func->class_code << 16| (uint32_t)func->prog_if << 8 | func->rev);
            break;
        case PCI_REG_BIST_HDR_LATENCY_CACHE:
            // Set (1 << 16) for PCI-PCI bridges (func->class_code == 0x0604)
            write_uint32_le_m(data, 16);
            break;
        case PCI_REG_IRQ_PIN_LINE:
            write_uint32_le_m(data, atomic_load_uint32(&func->irq_line) | ((uint32_t)func->irq_pin) << 8);
            break;
        case PCI_REG_BAR0:
        case PCI_REG_BAR1:
        case PCI_REG_BAR2:
        case PCI_REG_BAR3:
        case PCI_REG_BAR4:
        case PCI_REG_BAR5: {
            uint8_t bar_num = (reg - PCI_REG_BAR0) >> 2;
            rvvm_mmio_dev_t* bar = func->bar[bar_num];
            if (bar) {
                write_uint32_le_m(data, bar->addr);
            }
            break;
        }
        case PCI_REG_SSID_SVID:
            write_uint32_le_m(data, 0xeba110dc);
            break;
        case PCI_REG_EXPANSION_ROM: // Not needed for now, works as BAR
        case PCI_REG_CAP_PTR:       // Currently no capabilities supported
            break;
    }

    return true;
}

static bool pci_bus_write(rvvm_mmio_dev_t* mmio_dev, void* data, size_t offset, uint8_t size)
{
    pci_bus_t* bus = (pci_bus_t*)mmio_dev->data;
    uint8_t bus_id = offset >> bus->bus_shift;
    uint8_t dev_id = bit_cut(offset, bus->bus_shift - 5, 5);
    uint8_t fun_id = bit_cut(offset, bus->bus_shift - 8, 3);
    uint8_t reg = bit_cut(offset, 0, bus->bus_shift - 8);
    UNUSED(size);

    pci_func_t* func = pci_get_func(bus, bus_id, dev_id, fun_id);
    if (!func) {
        // No such device
        return true;
    }

    switch (reg) {
        case PCI_REG_STATUS_CMD:
            atomic_store_uint32(&func->command, read_uint16_le_m(data));
            break;
        case PCI_REG_BAR0:
        case PCI_REG_BAR1:
        case PCI_REG_BAR2:
        case PCI_REG_BAR3:
        case PCI_REG_BAR4:
        case PCI_REG_BAR5: {
            uint8_t bar_num = (reg - PCI_REG_BAR0) >> 2;
            rvvm_mmio_dev_t* bar = func->bar[bar_num];
            if (bar) {
                uint32_t addr = read_uint32_le_m(data) & ~(uint32_t)15;
                if (~(uint32_t)0 - addr < bar->size) {
                    addr = -bar->size;
                }
                // Should be atomic
                bar->addr = addr;
                atomic_fence();
            }
            break;
        }
        case PCI_REG_IRQ_PIN_LINE:
            atomic_store_uint32(&func->irq_line, read_uint8(data));
            break;
        case PCI_REG_EXPANSION_ROM: // Not needed for now, works as BAR
            break;
    }

    return true;
}

static void pci_free_dev(pci_dev_t* dev, bool remove_bar)
{
    if (dev == NULL) return;
    for (size_t func_id = 0; func_id < PCI_DEV_FUNCS; ++func_id) {
        pci_func_t* func = dev->func[func_id];
        if (func && remove_bar) {
            for (size_t bar_id = 0; bar_id < PCI_FUNC_BARS; ++bar_id) {
                rvvm_remove_mmio(func->bar[bar_id]);
            }
        }
        free(func);
    }
    dev->bus->dev[dev->dev_id] = NULL;
    free(dev);
}

static void pci_bus_remove(rvvm_mmio_dev_t* mmio_dev)
{
    pci_bus_t* bus = mmio_dev->data;
    for (size_t dev_id = 0; dev_id < PCI_BUS_DEVS; ++dev_id) {
        pci_free_dev(bus->dev[dev_id], false);
    }
    free(bus);
}

static const rvvm_mmio_type_t pci_bus_type = {
    .name = "pci_bus",
    .remove = pci_bus_remove,
};

PUBLIC pci_bus_t* pci_bus_init(rvvm_machine_t* machine, plic_ctx_t* plic, uint32_t irq, bool ecam,
                               rvvm_addr_t base_addr, size_t bus_count,
                               rvvm_addr_t io_addr, size_t io_len,
                               rvvm_addr_t mem_addr, size_t mem_len)
{
    size_t bus_shift = ecam ? 20 : 16;
    pci_bus_t* bus = safe_new_obj(pci_bus_t);

    bus->machine = machine;
    bus->plic = plic;
    for (size_t j=0; j<PCI_BUS_IRQS; ++j) {
        // Allocate INTX IRQs
        bus->irq[j] = irq ? irq : plic_alloc_irq(plic);
    }
    bus->io_addr = io_addr;
    bus->io_len = io_len;
    bus->mem_addr = mem_addr;
    bus->mem_len = mem_len;
    bus->bus_id = 0;
    bus->bus_shift = bus_shift;

    rvvm_mmio_dev_t pci_bus_mmio = {
        .addr = base_addr,
        .size = (bus_count << bus_shift),
        .data = bus,
        .type = &pci_bus_type,
        .read = pci_bus_read,
        .write = pci_bus_write,
        .min_op_size = 4,
        .max_op_size = 4,
    };

    if (!rvvm_attach_mmio(machine, &pci_bus_mmio)) {
        // Failed to attach the PCI bus
        return NULL;
    }

    // Host Bridge: SiFive, Inc. FU740-C000 RISC-V SoC PCI Express x8
    pci_dev_desc_t bridge_desc = { .func[0] = { .vendor_id = 0xF15E, .class_code = 0x0600 } };
    pci_bus_add_device(bus, &bridge_desc);

    rvvm_set_pci_bus(machine, bus);

#ifdef USE_FDT
    struct fdt_node* pci_node = fdt_node_create_reg("pci", base_addr);
    fdt_node_add_prop_u32(pci_node, "#address-cells", 3);
    fdt_node_add_prop_u32(pci_node, "#size-cells", 2);
    fdt_node_add_prop_u32(pci_node, "#interrupt-cells", 1);
    fdt_node_add_prop_str(pci_node, "device_type", "pci");
    fdt_node_add_prop_reg(pci_node, "reg", base_addr, pci_bus_mmio.size);
    fdt_node_add_prop_str(pci_node, "compatible", ecam ? "pci-host-ecam-generic" : "pci-host-cam-generic");
    fdt_node_add_prop(pci_node, "dma-coherent", NULL, 0);

    #define FDT_ADDR(addr) (((uint64_t)(addr)) >> 32), ((addr) & ~(uint32_t)0)

    uint32_t bus_range[2] = { 0, bus_count - 1 };
    fdt_node_add_prop_cells(pci_node, "bus-range", bus_range, 2);

    // Range header: ((cacheable) << 30 | (space) << 24 | (bus) << 16 | (dev) << 11 | (fun) << 8 | (reg))
    uint32_t ranges[14] = {
        0x1000000, FDT_ADDR(0),        FDT_ADDR(io_addr),  FDT_ADDR(io_len),
        0x2000000, FDT_ADDR(mem_addr), FDT_ADDR(mem_addr), FDT_ADDR(mem_len),
    };
    fdt_node_add_prop_cells(pci_node, "ranges", ranges + (io_len ? 0 : 7), io_len ? 14 : 7);

    // Crossing-style IRQ routing for IRQ balancing
    // INTA of dev 2 routes the same way as INTB of dev 1, etc
    uint32_t plic_handle = plic_get_phandle(plic);
    uint32_t interrupt_map[96] = {
        0x0000, 0, 0, 1, plic_handle, bus->irq[0],
        0x0000, 0, 0, 2, plic_handle, bus->irq[1],
        0x0000, 0, 0, 3, plic_handle, bus->irq[2],
        0x0000, 0, 0, 4, plic_handle, bus->irq[3],
        0x0800, 0, 0, 1, plic_handle, bus->irq[1],
        0x0800, 0, 0, 2, plic_handle, bus->irq[2],
        0x0800, 0, 0, 3, plic_handle, bus->irq[3],
        0x0800, 0, 0, 4, plic_handle, bus->irq[0],
        0x1000, 0, 0, 1, plic_handle, bus->irq[2],
        0x1000, 0, 0, 2, plic_handle, bus->irq[3],
        0x1000, 0, 0, 3, plic_handle, bus->irq[0],
        0x1000, 0, 0, 4, plic_handle, bus->irq[1],
        0x1800, 0, 0, 1, plic_handle, bus->irq[3],
        0x1800, 0, 0, 2, plic_handle, bus->irq[0],
        0x1800, 0, 0, 3, plic_handle, bus->irq[2],
        0x1800, 0, 0, 4, plic_handle, bus->irq[1],
    };
    fdt_node_add_prop_cells(pci_node, "interrupt-map", interrupt_map, 96);

    uint32_t interrupt_mask[4] = { 0x1800, 0, 0, 7 };
    fdt_node_add_prop_cells(pci_node, "interrupt-map-mask", interrupt_mask, 4);

    fdt_node_add_child(rvvm_get_fdt_soc(machine), pci_node);
#endif
    return bus;
}

PUBLIC pci_bus_t* pci_bus_init_auto(rvvm_machine_t* machine)
{
    plic_ctx_t* plic = rvvm_get_plic(machine);
    bool ecam = true;
    size_t bus_count = 256; // TODO: Support more than 1 working bus
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, PCI_BASE_DEFAULT_MMIO, bus_count << (ecam ? 20 : 16));
    return pci_bus_init(machine, plic, 0, ecam, addr, bus_count,
                        PCI_IO_DEFAULT_ADDR, PCI_IO_DEFAULT_SIZE,
                        PCI_MEM_DEFAULT_MMIO, PCI_MEM_DEFAULT_SIZE);
}

static inline size_t pci_func_irq_pin_id(pci_func_t* func)
{
    return (func->dev->dev_id + func->irq_pin + 3) & 3;
}

static pci_func_t* pci_init_func(pci_dev_t* dev, const pci_func_desc_t* func_desc)
{
    pci_bus_t* bus = dev->bus;
    pci_func_t* func = safe_new_obj(pci_func_t);
    func->dev = dev;
    func->vendor_id  = func_desc->vendor_id;
    func->device_id  = func_desc->device_id;
    func->class_code = func_desc->class_code;
    func->prog_if    = func_desc->prog_if;
    func->rev        = func_desc->rev;
    func->irq_pin    = func_desc->irq_pin;
    func->command    = PCI_CMD_DEFAULT;
    if (func->irq_pin) {
        func->irq_line = bus->irq[pci_func_irq_pin_id(func)];
    }

    for (size_t bar_id = 0; bar_id < PCI_FUNC_BARS; ++bar_id) {
        rvvm_mmio_dev_t bar = func_desc->bar[bar_id];
        bar.size = align_size_up(bar.size, 16);
        if (bar.size) {
            // Align device BAR to it's size
            bar.addr = bus->mem_addr + ((bar.size - bus->mem_addr) % bar.size);
            bus->mem_len -= (bar.addr + bar.size) - bus->mem_addr;
            bus->mem_addr = bar.addr + bar.size;

            func->bar[bar_id] = rvvm_attach_mmio(bus->machine, &bar);
            if (func->bar[bar_id] == NULL) {
                // Failed to attach function BAR
                free(func);
                return NULL;
            }
        }
    }

    return func;
}

PUBLIC pci_dev_t* pci_bus_add_device(pci_bus_t* bus, const pci_dev_desc_t* desc)
{
    if (bus == NULL) return NULL;

    uint8_t dev_id = 0xFF;
    for (size_t i=0; i<PCI_BUS_DEVS; ++i) {
        if (bus->dev[i] == NULL) {
            dev_id = i;
            break;
        }
    }
    if (dev_id == 0xFF) {
        rvvm_warn("Too much devices on a single PCI bus");
        return NULL;
    }

    pci_dev_t* dev = safe_new_obj(pci_dev_t);
    dev->bus = bus;
    dev->dev_id = dev_id;

    for (size_t func_id = 0; func_id < PCI_DEV_FUNCS; ++func_id) {
        const pci_func_desc_t* func_desc = &desc->func[func_id];
        if (func_desc->vendor_id != 0 || func_desc->device_id != 0) {
            // Valid PCI function
            pci_func_t* func = pci_init_func(dev, func_desc);
            dev->func[func_id] = func;
            if (!func) {
                // Failed to attach function
                pci_free_dev(dev, true);
                return NULL;
            }
        }
    }

    // Pause the vCPUs and attach the device
    bool was_running = rvvm_pause_machine(bus->machine);
    bus->dev[dev->dev_id] = dev;
    if (was_running) rvvm_start_machine(bus->machine);

    return dev;
}

PUBLIC void pci_send_irq(pci_dev_t* dev, uint32_t func_id)
{
    if (dev == NULL || func_id >= PCI_DEV_FUNCS || dev->func[func_id] == NULL) {
        return;
    }
    pci_func_t* func = dev->func[func_id];
    pci_bus_t* bus = dev->bus;

    // Check IRQs enabled
    if (func->irq_pin && !(atomic_load_uint32(&func->command) & PCI_CMD_IRQ_DISABLE)) {
        // Set interrupt status bit
        atomic_or_uint32(&func->status, PCI_STATUS_IRQ);

        // Send IRQ
        uint32_t irq = bus->irq[pci_func_irq_pin_id(func)];
        plic_send_irq(bus->plic, irq);
    }
}

PUBLIC void pci_clear_irq(pci_dev_t* dev, uint32_t func_id)
{
    if (dev == NULL || func_id >= PCI_DEV_FUNCS || dev->func[func_id] == NULL) {
        return;
    }
    pci_func_t* func = dev->func[func_id];

    // Clear interrupt status bit
    atomic_and_uint32(&func->status, ~PCI_STATUS_IRQ);
}

PUBLIC void* pci_get_dma_ptr(pci_dev_t* dev, rvvm_addr_t addr, size_t size)
{
    if (dev == NULL) return NULL;
    return rvvm_get_dma_ptr(dev->bus->machine, addr, size);
}

PUBLIC void pci_remove_device(pci_dev_t* dev)
{
    if (dev == NULL) return;
    pci_bus_t* bus = dev->bus;

    // Pause the vCPUs and remove the device
    bool was_running = rvvm_pause_machine(bus->machine);
    pci_free_dev(dev, true);
    if (was_running) rvvm_start_machine(bus->machine);
}
