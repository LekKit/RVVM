/*
clint.c - RISC-V Advanced Core Local Interruptor
Copyright (C) 2021  LekKit <github.com/LekKit>

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

#include "clint.h"
#include "riscv_hart.h"
#include "mem_ops.h"
#include "bit_ops.h"

#define CLINT_MMIO_SIZE    0x10000
#define ACLINT_MSWI_SIZE   0x4000
#define ACLINT_MTIMER_SIZE 0x8000

static rvvm_mmio_type_t aclint_mswi_dev_type = {
    .name = "aclint_mswi",
};

static rvvm_mmio_type_t aclint_mtimer_dev_type = {
    .name = "aclint_mtimer",
};

static bool aclint_mswi_read(rvvm_mmio_dev_t* device, void* data, size_t offset, uint8_t size)
{
    size_t hartid = offset >> 2;
    UNUSED(size);

    if (hartid < vector_size(device->machine->harts)) {
        rvvm_hart_t* vm = vector_at(device->machine->harts, hartid);
        write_uint32_le_m(data, bit_cut(vm->csr.ip, 3, 1));
        return true;
    }

    return false;
}

static bool aclint_mswi_write(rvvm_mmio_dev_t* device, void* data, size_t offset, uint8_t size)
{
    size_t hartid = offset >> 2;
    UNUSED(size);

    if (hartid < vector_size(device->machine->harts)) {
        rvvm_hart_t* vm = vector_at(device->machine->harts, hartid);
        if (read_uint32_le_m(data)) {
            riscv_interrupt(vm, INTERRUPT_MSOFTWARE);
        } else {
            riscv_interrupt_clear(vm, INTERRUPT_MSOFTWARE);
        }
        return true;
    }

    return false;
}

static bool aclint_mtimer_read(rvvm_mmio_dev_t* device, void* data, size_t offset, uint8_t size)
{
    size_t hartid = offset >> 3;
    UNUSED(size);

    if (offset == 0x7FF8) {
        write_uint64_le_m(data, rvtimer_get(&device->machine->timer));
        return true;
    }

    if (hartid < vector_size(device->machine->harts)) {
        rvvm_hart_t* vm = vector_at(device->machine->harts, hartid);
        write_uint64_le_m(data, vm->timer.timecmp);
        return true;
    }

    return false;
}

static bool aclint_mtimer_write(rvvm_mmio_dev_t* device, void* data, size_t offset, uint8_t size)
{
    size_t hartid = offset >> 3;
    UNUSED(size);

    if (offset == 0x7FF8) {
        rvtimer_rebase(&device->machine->timer, read_uint64_le_m(data));
        vector_foreach(device->machine->harts, i) {
            vector_at(device->machine->harts, i)->timer = device->machine->timer;
        }
        return true;
    }

    if (hartid < vector_size(device->machine->harts)) {
        rvvm_hart_t* vm = vector_at(device->machine->harts, hartid);
        vm->timer.timecmp = read_uint64_le_m(data);
        return true;
    }

    return false;
}

PUBLIC void clint_init(rvvm_machine_t* machine, rvvm_addr_t addr)
{
    rvvm_mmio_dev_t aclint_mswi = {
        .addr = addr,
        .size = ACLINT_MSWI_SIZE,
        .min_op_size = 4,
        .max_op_size = 4,
        .read = aclint_mswi_read,
        .write = aclint_mswi_write,
        .type = &aclint_mswi_dev_type,
    };

    rvvm_mmio_dev_t aclint_mtimer = {
        .addr = addr + ACLINT_MSWI_SIZE,
        .size = ACLINT_MTIMER_SIZE,
        .min_op_size = 8,
        .max_op_size = 8,
        .read = aclint_mtimer_read,
        .write = aclint_mtimer_write,
        .type = &aclint_mtimer_dev_type,
    };

    rvvm_attach_mmio(machine, &aclint_mswi);
    rvvm_attach_mmio(machine, &aclint_mtimer);

#ifdef USE_FDT
    struct fdt_node* clint = fdt_node_create_reg("clint", addr);
    struct fdt_node* cpus = fdt_node_find(rvvm_get_fdt_root(machine), "cpus");
    size_t irq_ext_cells = vector_size(machine->harts) << 2;
    uint32_t* irq_ext = safe_calloc(irq_ext_cells, sizeof(uint32_t));

    fdt_node_add_prop_reg(clint, "reg", addr, CLINT_MMIO_SIZE);
    fdt_node_add_prop(clint, "compatible", "sifive,clint0\0riscv,clint0", 27);

    vector_foreach(machine->harts, i) {
        struct fdt_node* cpu = fdt_node_find_reg(cpus, "cpu", i);
        struct fdt_node* cpu_irq = fdt_node_find(cpu, "interrupt-controller");
        if (cpu_irq) {
            uint32_t irq_phandle = fdt_node_get_phandle(cpu_irq);
            irq_ext[(i << 2)] = irq_ext[(i << 2) + 2] = irq_phandle;
            irq_ext[(i << 2) + 1] = INTERRUPT_MSOFTWARE;
            irq_ext[(i << 2) + 3] = INTERRUPT_MTIMER;
        } else {
            rvvm_warn("Missing nodes in FDT!");
        }
    }

    fdt_node_add_prop_cells(clint, "interrupts-extended", irq_ext, irq_ext_cells);
    fdt_node_add_child(rvvm_get_fdt_soc(machine), clint);
    free(irq_ext);
#endif
}

PUBLIC void clint_init_auto(rvvm_machine_t* machine)
{
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, CLINT_DEFAULT_MMIO, CLINT_MMIO_SIZE);
    clint_init(machine, addr);
}
