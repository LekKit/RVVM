/*
pci-bus.h - Peripheral Component Interconnect Bus
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

#ifndef PCI_BUS_H
#define PCI_BUS_H

#include "rvvm.h"
#include "spinlock.h"

struct pci_bar_desc {
    rvvm_mmio_handler_t read;
    rvvm_mmio_handler_t write;
    paddr_t len;
    uint8_t min_op_size;
    uint8_t max_op_size;
};

struct pci_func_desc {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t class_code;
    uint8_t  prog_if;
    uint8_t  irq_pin;
    struct pci_bar_desc bar[6];
};

struct pci_device_desc {
    struct pci_func_desc func[8];
};

struct pci_func {
    struct pci_func_desc *desc;
    struct pci_device *dev;
    rvvm_mmio_handle_t bar_mapping[6];
    spinlock_t irq_lock;
    uint16_t command;
    uint16_t status;
    uint8_t irq_line;
    void *data;
};

struct pci_bus;
struct pci_device {
    struct pci_device_desc *desc;
    struct pci_bus *bus;
    struct pci_func func[8];
};

struct pci_bus {
    vector_t(struct pci_device) devices;
    struct rvvm_machine_t *machine;
    void *intc_data;
    uint32_t irq[4];
    paddr_t io_addr;
    paddr_t io_len;
    paddr_t mem_addr;
    paddr_t mem_len;
};

struct pci_bus_list {
    struct pci_bus *buses;
    size_t count;
    uint8_t bus_shift; /* 20 for ECAM, 16 for regular CAM */
};

struct pci_device* pci_bus_add_device(rvvm_machine_t *machine, struct pci_bus *bus, struct pci_device_desc *desc, void *data);

struct pci_bus_list* pci_bus_init(rvvm_machine_t *machine,
        size_t bus_count,
        bool is_ecam,
        paddr_t base_addr,
        paddr_t io_addr,
        paddr_t io_len,
        paddr_t mem_addr,
        paddr_t mem_len,
        void *intc_data,
        uint32_t irq);

#ifdef USE_FDT
/* Only one bus is supported atm, however, bus_count can be any */
struct pci_bus_list* pci_bus_init_dt(rvvm_machine_t *machine,
        size_t bus_count,
        bool is_ecam,
        paddr_t base_addr,
        paddr_t io_addr,
        paddr_t io_len,
        paddr_t mem_addr,
        paddr_t mem_len,
        void *intc_data,
        uint32_t irq);
#endif

void pci_send_irq(struct pci_func *func);
void pci_clear_irq(struct pci_func *func);

#endif
