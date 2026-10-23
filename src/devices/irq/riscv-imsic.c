/*
riscv-imsic.c - RISC-V Incoming Message-Signaled Interrupt Controller
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

/*
 * TODO: Use <rvvm/rvvm_region.h>
 * TODO: Replace <cpu/riscv_hart.h> with <rvvm/rvvm_cpu.h>
 */

#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_fdt.h>

#include <rvvm/rvvm.h>

#include <util/bit_ops.h>
#include <util/mem_ops.h>

#include <cpu/riscv_hart.h>

PUSH_OPTIMIZATION_SIZE

#define IMSIC_REG_SETEIPNUM_LE 0x00
#define IMSIC_REG_SETEIPNUM_BE 0x04

typedef struct {
    bool smode;
} imsic_ctx_t;

static bool imsic_mmio_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    imsic_ctx_t* imsic  = dev->data;
    size_t       hartid = offset >> 12;
    UNUSED(size);

    if (hartid < vector_size(dev->machine->harts)) {
        rvvm_hart_t* hart = vector_at(dev->machine->harts, hartid);
        switch (offset & 0xFFC) {
            case IMSIC_REG_SETEIPNUM_LE: {
                riscv_send_aia_irq(hart, imsic->smode, read_uint32_le(data));
                break;
            }
            case IMSIC_REG_SETEIPNUM_BE: {
                riscv_send_aia_irq(hart, imsic->smode, read_uint32_be_m(data));
                break;
            }
        }
    }

    return true;
}

static rvvm_mmio_type_t imsic_dev_type = {
    .name = "riscv_imsic",
};

static bool riscv_imsic_init(rvvm_machine_t* machine, rvvm_addr_t addr, bool smode)
{
    if (rvvm_machine_running(machine)) {
        rvvm_error("Can't enable AIA on already running machine!");
        return false;
    }

    vector_foreach (machine->harts, i) {
        riscv_hart_aia_init(vector_at(machine->harts, i));
    }

    rvvm_append_isa_string(machine, "_smaia_ssaia");

    imsic_ctx_t* imsic = safe_new_obj(imsic_ctx_t);

    rvvm_mmio_dev_t imsic_mmio = {
        .addr        = addr,
        .size        = vector_size(machine->harts) << 12,
        .data        = imsic,
        .min_op_size = 4,
        .max_op_size = 4,
        .read        = rvvm_mmio_none,
        .write       = imsic_mmio_write,
        .type        = &imsic_dev_type,
    };

    imsic->smode = smode;

    if (!rvvm_attach_msi_target(machine, &imsic_mmio)) {
        rvvm_error("Failed to attach RISC-V IMSIC!");
        return false;
    }

    rvvm_fdt_node_t* soc = rvvm_get_fdt_soc(machine);
    if (soc) {
        rvvm_fdt_node_t*   fdt     = rvvm_fdt_init_reg(smode ? "imsics_s" : "imsics_m", imsic_mmio.addr);
        rvvm_fdt_node_t*   cpus    = rvvm_fdt_find(rvvm_get_fdt_root(machine), "cpus");
        vector_t(uint32_t) irq_ext = {0};

        rvvm_fdt_prop_set_reg(fdt, "reg", imsic_mmio.addr, imsic_mmio.size);
        rvvm_fdt_prop_set_str(fdt, "compatible", "riscv,imsics");
        rvvm_fdt_prop_set_flag(fdt, "interrupt-controller");
        rvvm_fdt_prop_set_u32(fdt, "#interrupt-cells", 0);
        rvvm_fdt_prop_set_flag(fdt, "msi-controller");
        rvvm_fdt_prop_set_u32(fdt, "#msi-cells", 0);
        rvvm_fdt_prop_set_u32(fdt, "riscv,num-ids", RVVM_AIA_IRQ_LIMIT - 1);

        vector_foreach (machine->harts, i) {
            rvvm_fdt_node_t* cpu     = rvvm_fdt_find_reg(cpus, "cpu", i);
            rvvm_fdt_node_t* cpu_irq = rvvm_fdt_find(cpu, "interrupt-controller");

            if (cpu_irq) {
                vector_push_back(irq_ext, rvvm_fdt_phandle(cpu_irq));
                vector_push_back(irq_ext, smode ? RISCV_INTERRUPT_SEXTERNAL : RISCV_INTERRUPT_MEXTERNAL);
            } else {
                rvvm_warn("Missing CPU IRQ nodes in FDT!");
            }
        }

        rvvm_fdt_prop_set_cells(fdt, "interrupts-extended", vector_buffer(irq_ext), vector_size(irq_ext));
        rvvm_fdt_attach(soc, fdt);
        vector_free(irq_ext);
    }
    return true;
}

RVVM_PUBLIC bool rvvm_riscv_imsic_init(rvvm_machine_t* machine, /**/
                                       rvvm_addr_t     maddr,   /**/
                                       rvvm_addr_t     saddr)
{
    size_t      imsic_size = vector_size(machine->harts) << 12;
    rvvm_addr_t m_addr     = rvvm_mmio_zone_auto(machine, maddr, imsic_size);
    rvvm_addr_t s_addr     = rvvm_mmio_zone_auto(machine, saddr, imsic_size);
    return riscv_imsic_init(machine, m_addr, false) && riscv_imsic_init(machine, s_addr, true);
}

POP_OPTIMIZATION_SIZE
