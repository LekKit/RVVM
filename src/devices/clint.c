/*
clint.h - Core-local Interrupt
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
#include "compiler.h"
#include "rvtimer.h"
#include "mem_ops.h"
#include "bit_ops.h"
#include "riscv_hart.h"

#define CLINT_MEM_SIZE 0x10000

static bool clint_mmio_read_handler(rvvm_mmio_dev_t* device, void* data, paddr_t offset, uint8_t size)
{
    rvvm_hart_t *vm = (rvvm_hart_t*) device->data;
    uint8_t tmp[8];

    // MSIP register, bit 0 drives MSIP interrupt bit of the hart
    if (offset == 0) {
        memset(data, 0, size);
        *(uint8_t*)data = bit_cut(vm->csr.ip, 3, 1);
        return true;
    }

    // MTIMECMP register, 64-bit compare register for timer interrupts
    if (offset >= 0x4000 && (offset + size) <= 0x4008) {
        write_uint64_le_m(tmp, vm->timer.timecmp);
        offset -= 0x4000;
        memcpy(data, tmp + offset, size);
        return true;
    }

    // MTIME register, 64-bit timer value
    if (offset >= 0xBFF8 && (offset + size) <= 0xC000) {
        write_uint64_le_m(tmp, rvtimer_get(&vm->timer));
        offset -= 0xBFF8;
        memcpy(data, tmp + offset, size);
        return true;
    }
    return false;
}

static bool clint_mmio_write_handler(rvvm_mmio_dev_t* device, void* data, paddr_t offset, uint8_t size)
{
    rvvm_hart_t *vm = (rvvm_hart_t*) device->data;
    uint8_t tmp[8];

    // MSIP register, bit 0 drives MSIP interrupt bit of the hart
    if (offset == 0) {
        uint8_t msip = ((*(uint8_t*)data) & 1);
        if (msip) {
            riscv_interrupt(vm, INTERRUPT_MSOFTWARE);
        } else {
            riscv_interrupt_clear(vm, INTERRUPT_MSOFTWARE);
        }
        return true;
    }

    // MTIMECMP register, 64-bit compare register for timer interrupts
    if (offset >= 0x4000 && (offset + size) <= 0x4008) {
        write_uint64_le_m(tmp, vm->timer.timecmp);
        offset -= 0x4000;
        memcpy(tmp + offset, data, size);
        vm->timer.timecmp = read_uint64_le_m(tmp);
        return true;
    }

    // MTIME register, 64-bit timer value
    if (offset >= 0xBFF8 && (offset + size) <= 0xC000) {
        write_uint64_le_m(tmp, rvtimer_get(&vm->machine->timer));
        offset -= 0xBFF8;
        memcpy(tmp + offset, data, size);
        rvtimer_rebase(&vm->machine->timer, read_uint64_le_m(tmp));
        vector_foreach(vm->machine->harts, i) {
            vector_at(vm->machine->harts, i).timer = vm->machine->timer;
        }
        return true;
    }

    return false;
}

static void clint_remove(rvvm_mmio_dev_t* device)
{
    UNUSED(device);
}

static rvvm_mmio_type_t clint_dev_type = {
    .name = "clint",
    .remove = clint_remove,
};

void clint_init(rvvm_machine_t* machine, paddr_t addr)
{
    rvvm_mmio_dev_t clint;
    clint.min_op_size = 1;
    clint.max_op_size = 8;
    clint.read = clint_mmio_read_handler;
    clint.write = clint_mmio_write_handler;
    clint.type = &clint_dev_type;

#ifdef USE_FDT
    struct fdt_node* soc = fdt_node_find(machine->fdt, "soc");
    struct fdt_node* cpus = fdt_node_find(machine->fdt, "cpus");
#endif

    vector_foreach(machine->harts, i) {
        clint.begin = addr;
        clint.end = addr + CLINT_MEM_SIZE;
        clint.data = &vector_at(machine->harts, i);
        rvvm_attach_mmio(machine, &clint);

#ifdef USE_FDT
        struct fdt_node* cpu = cpus ? fdt_node_find_reg(cpus, "cpu", i) : NULL;
        struct fdt_node* cpu_irq = cpu ? fdt_node_find(cpu, "interrupt-controller") : NULL;
        if (cpu_irq && soc) {
            uint32_t irq_phandle = fdt_node_get_phandle(cpu_irq);
            struct fdt_node* clint = fdt_node_create_reg("clint", addr);
            fdt_node_add_prop_reg(clint, "reg", addr, CLINT_MEM_SIZE);
            fdt_node_add_prop_str(clint, "compatible", "riscv,clint0");
            uint32_t irq_ext[4];
            irq_ext[0] = irq_ext[2] = irq_phandle;
            irq_ext[1] = INTERRUPT_MSOFTWARE;
            irq_ext[3] = INTERRUPT_MTIMER;
            fdt_node_add_prop_cells(clint, "interrupts-extended", irq_ext, 4);
            fdt_node_add_child(soc, clint);
        } else {
            rvvm_warn("Missing nodes in FDT!");
        }
#endif

        addr += CLINT_MEM_SIZE;
    }
}

void clint_init_auto(rvvm_machine_t* machine)
{
    clint_init(machine, CLINT_DEFAULT_MMIO);
}
