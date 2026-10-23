/*
riscv-aclint.c - RISC-V Advanced Core Local Interruptor
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

/*
 * TODO: Replace <cpu/riscv_hart.h> with <rvvm/rvvm_cpu.h>
 */

#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_fdt.h>
#include <rvvm/rvvm_region.h>

#include <util/mem_ops.h>

#include <cpu/riscv_hart.h>

PUSH_OPTIMIZATION_SIZE

static void aclint_mswi_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    rvvm_machine_t* machine = rvvm_region_machine(dev);
    size_t          hartid  = off >> 2;
    UNUSED(size);

    if (hartid < vector_size(machine->harts)) {
        rvvm_hart_t* vm = vector_at(machine->harts, hartid);
        write_uint32_le(data, (riscv_interrupts_raised(vm) >> RISCV_INTERRUPT_MSOFTWARE) & 1);
    }
}

static void aclint_mswi_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    rvvm_machine_t* machine = rvvm_region_machine(dev);
    size_t          hartid  = off >> 2;
    UNUSED(size);

    if (hartid < vector_size(machine->harts)) {
        rvvm_hart_t* vm = vector_at(machine->harts, hartid);
        if (read_uint32_le(data) & 1) {
            riscv_interrupt(vm, RISCV_INTERRUPT_MSOFTWARE);
        } else {
            riscv_interrupt_clear(vm, RISCV_INTERRUPT_MSOFTWARE);
        }
    }
}

static void aclint_mtimer_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    rvvm_machine_t* machine = rvvm_region_machine(dev);
    size_t          hartid  = off >> 3;
    UNUSED(size);

    if (hartid < vector_size(machine->harts)) {
        rvvm_hart_t* vm = vector_at(machine->harts, hartid);
        write_uint64_le(data, rvtimecmp_get(&vm->mtimecmp));
    } else if (off == 0x7FF8) {
        write_uint64_le(data, rvtimer_get(&machine->timer));
    }
}

static void aclint_mtimer_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    rvvm_machine_t* machine = rvvm_region_machine(dev);
    size_t          hartid  = off >> 3;
    UNUSED(size);

    if (hartid < vector_size(machine->harts)) {
        rvvm_hart_t* vm = vector_at(machine->harts, hartid);
        rvtimecmp_set(&vm->mtimecmp, read_uint64_le(data));
        if (rvtimecmp_pending(&vm->mtimecmp)) {
            riscv_interrupt(vm, RISCV_INTERRUPT_MTIMER);
        } else {
            riscv_interrupt_clear(vm, RISCV_INTERRUPT_MTIMER);
        }
    } else if (off == 0x7FF8) {
        rvtimer_rebase(&machine->timer, read_uint64_le(data));
    }
}

static const rvvm_reg_type_t mswi_type = {
    .name     = "riscv-aclint-mswi",
    .read     = aclint_mswi_read,
    .write    = aclint_mswi_write,
    .min_size = 4,
    .max_size = 4,
};

static const rvvm_reg_type_t mtimer_type = {
    .name     = "riscv-aclint-mtimer",
    .read     = aclint_mtimer_read,
    .write    = aclint_mtimer_write,
    .min_size = 8,
    .max_size = 8,
};

RVVM_PUBLIC bool rvvm_riscv_clint_init(rvvm_machine_t* machine, rvvm_addr_t addr)
{
    rvvm_reg_desc_t mswi_desc = {
        .addr = addr,
        .size = 0x4000,
        .type = &mswi_type,
        .attr = RVVM_REG_ATTR_FIX,
    };

    rvvm_reg_desc_t mtimer_desc = {
        .addr = addr + 0x4000,
        .size = 0x8000,
        .type = &mtimer_type,
        .attr = RVVM_REG_ATTR_FIX,
    };

    if (!rvvm_region_init(machine, &mswi_desc) || !rvvm_region_init(machine, &mtimer_desc)) {
        return false;
    }

    rvvm_fdt_node_t* soc = rvvm_get_fdt_soc(machine);
    if (soc) {
        rvvm_fdt_node_t*   cpus    = rvvm_fdt_find(rvvm_get_fdt_root(machine), "cpus");
        vector_t(uint32_t) irq_ext = {0};

        vector_foreach (machine->harts, i) {
            struct fdt_node* cpu         = rvvm_fdt_find_reg(cpus, "cpu", i);
            struct fdt_node* cpu_irq     = rvvm_fdt_find(cpu, "interrupt-controller");
            uint32_t         irq_phandle = rvvm_fdt_phandle(cpu_irq);

            if (irq_phandle) {
                vector_push_back(irq_ext, irq_phandle);
                vector_push_back(irq_ext, RISCV_INTERRUPT_MSOFTWARE);
                vector_push_back(irq_ext, irq_phandle);
                vector_push_back(irq_ext, RISCV_INTERRUPT_MTIMER);
            } else {
                rvvm_debug("Missing /cpus/cpu/interrupt-controller node in FDT");
            }
        }

        rvvm_fdt_node_t* fdt = rvvm_fdt_init_reg("clint", addr);
        rvvm_fdt_prop_set_reg(fdt, "reg", addr, 0x10000);
        rvvm_fdt_prop_set_str_list(fdt, "compatible", "sifive,clint0\0riscv,clint0");
        rvvm_fdt_prop_set_cells(fdt, "interrupts-extended", vector_buffer(irq_ext), vector_size(irq_ext));
        rvvm_fdt_attach(soc, fdt);
        vector_free(irq_ext);
    }

    return true;
}

POP_OPTIMIZATION_SIZE
