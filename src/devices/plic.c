/*
plic.c - Platform-Level Interrupt Controller
Copyright (C) 2023  LekKit <github.com/LekKit>
              2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#include "plic.h"
#include "riscv_hart.h"
#include "bit_ops.h"
#include "mem_ops.h"
#include "atomics.h"

#define PLIC_CTXFLAG_THRESHOLD     0x0
#define PLIC_CTXFLAG_CLAIMCOMPLETE 0x1

#define PLIC_SOURCE_MAX 256 // Max 1024

#define PLIC_SRC_REG_COUNT ((PLIC_SOURCE_MAX + 0x1F) >> 5)

#define CTX_HARTID(ctx) ((ctx) >> 1)

// In QEMU, those are reversed for whatever reason, but on most actual
// boards it's done this way. Should we ever do 1:1 QEMU compat, swap those...
#define CTX_IRQ_PRIO(ctx) (((ctx) & 1) ? INTERRUPT_SEXTERNAL : INTERRUPT_MEXTERNAL)

struct plic {
    rvvm_machine_t* machine;
    uint32_t alloc_irq;
    uint32_t phandle;
    uint32_t prio[PLIC_SOURCE_MAX];
    uint32_t pending[PLIC_SRC_REG_COUNT];
    uint32_t raised[PLIC_SRC_REG_COUNT];
    uint32_t** enable;    // [CTX][SRC_REG]
    uint32_t*  threshold; // [CTX]
};

static inline uint32_t plic_ctx_count(plic_ctx_t* plic)
{
    return vector_size(plic->machine->harts) << 1;
}

// Check if the IRQ is enabled for specific CTX
static inline bool plic_irq_enabled(plic_ctx_t* plic, uint32_t ctx, uint32_t irq)
{
    return bit_check(atomic_load_uint32(&plic->enable[ctx][irq >> 5]), irq & 0x1F);
}

// Notify CTX about inbound IRQ
static bool plic_notify_irq(plic_ctx_t* plic, uint32_t ctx, uint32_t irq)
{
    // Can we deliver this IRQ to this CTX?
    if (!plic_irq_enabled(plic, ctx, irq)) return false;

    if (atomic_load_uint32(&plic->prio[irq]) <= atomic_load_uint32(&plic->threshold[ctx])) {
        // This IRQ priority isn't high enough
        return false;
    }

    riscv_interrupt(vector_at(plic->machine->harts, CTX_HARTID(ctx)), CTX_IRQ_PRIO(ctx));
    return true;
}

static uint32_t plic_claim_irq(plic_ctx_t* plic, uint32_t ctx)
{
    size_t nirqs = 0;
    uint32_t ret = 0;
    uint32_t threshold = atomic_load_uint32(&plic->threshold[ctx]);
    for (size_t i=0; i<PLIC_SRC_REG_COUNT; ++i) {
        uint32_t irqs = atomic_load_uint32(&plic->pending[i]);

        if (irqs) for (size_t j=0; j<32; ++j) {
            if (bit_check(irqs, j)) {
                uint32_t irq = (i << 5) | j;
                uint32_t prio = atomic_load_uint32(&plic->prio[irq]);
                if (plic_irq_enabled(plic, ctx, irq) && prio > threshold) {
                    threshold = prio;
                    ret = irq;
                    nirqs++;
                }
            }
        }
    }
    if (nirqs <= 1) {
        riscv_interrupt_clear(vector_at(plic->machine->harts, CTX_HARTID(ctx)), CTX_IRQ_PRIO(ctx));
    }
    uint32_t mask = 1 << (ret & 0x1F);
    if (!(atomic_and_uint32(&plic->pending[ret >> 5], ~mask) & mask)) {
        return 0;
    }
    return ret;
}

static void plic_scan_irqs(plic_ctx_t* plic, uint32_t ctx)
{
    for (size_t i=0; i<PLIC_SRC_REG_COUNT; ++i) {
        uint32_t raised = atomic_load_uint32(&plic->raised[i]);
        uint32_t irqs = atomic_load_uint32(&plic->pending[i]);
        // If we have completed but still asserted IRQs, fire them up again
        if (raised & ~irqs) {
            irqs |= raised;
            atomic_or_uint32(&plic->pending[i], raised);
        }

        if (irqs) for (size_t j=0; j<32; ++j) {
            if (bit_check(irqs, j)) {
                uint32_t irq = (i << 5) | j;
                plic_notify_irq(plic, ctx, irq);
            }
        }
    }
}

static bool plic_mmio_read(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    plic_ctx_t* plic = dev->data;
    memset(data, 0, size);

    if (offset < 0x1000) {
        // Interrupt priority
        uint32_t irq = offset >> 2;
        if (irq > 0 && irq < PLIC_SOURCE_MAX) {
            write_uint32_le(data, atomic_load_uint32(&plic->prio[irq]));
        }
    } else if (offset < 0x1080) {
        // Interrupt pending
        uint32_t reg = (offset - 0x1000) >> 2;
        if (reg < PLIC_SRC_REG_COUNT) {
            write_uint32_le(data, atomic_load_uint32(&plic->pending[reg]));
        }
    } else if (offset < 0x2000) {
        // Reserved, ignore
    } else if (offset < 0x1F2000) {
        // Enable bits
        uint32_t reg = ((offset - 0x2000) >> 2) & 0x1F;
        uint32_t ctx = (offset - 0x2000) >> 7;
        if (reg < PLIC_SRC_REG_COUNT && ctx < plic_ctx_count(plic)) {
            write_uint32_le(data, atomic_load_uint32(&plic->enable[ctx][reg]));
        }
    } else if (offset < 0x200000) {
        // Reserved, ignore
    } else if (offset < 0x4000000) {
        // Context flags - threshold and claim/complete
        uint32_t flag = ((offset - 0x200000) >> 2) & 0x3FF;
        uint32_t ctx = (offset - 0x200000) >> 12;
        if (ctx < plic_ctx_count(plic)) {
            if (flag == PLIC_CTXFLAG_CLAIMCOMPLETE) {
                write_uint32_le(data, plic_claim_irq(plic, ctx));
            } else if (flag == PLIC_CTXFLAG_THRESHOLD) {
                write_uint32_le(data, atomic_load_uint32(&plic->threshold[ctx]));
            }
        }
    }
    return true;
}

static bool plic_mmio_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    plic_ctx_t* plic = dev->data;
    UNUSED(size);

    if (offset < 0x1000) {
        // Interrupt priority
        uint32_t irq = offset >> 2;
        if (irq > 0 && irq < PLIC_SOURCE_MAX) {
            atomic_store_uint32(&plic->prio[irq], read_uint32_le(data));
        }
    } else if (offset < 0x1080) {
        // R/O, do nothing. Pending bits are cleared by reading CLAIMCOMPLETE register
    } else if (offset < 0x2000) {
        // Reserved, ignore
    } else if (offset < 0x1f2000) {
        // Enable bits
        uint32_t reg = ((offset - 0x2000) >> 2) & 0x1F;
        uint32_t ctx = (offset - 0x2000) >> 7;
        if (reg < PLIC_SRC_REG_COUNT && ctx < plic_ctx_count(plic)) {
            atomic_store_uint32(&plic->enable[ctx][reg], read_uint32_le(data));
        }
    } else if (offset < 0x200000) {
        // Reserved, ignore
    } else if (offset < 0x4000000) {
        // Context flags - threshold and claim/complete
        uint32_t flag = ((offset - 0x200000) >> 2) & 0x3FF;
        uint32_t ctx = (offset - 0x200000) >> 12;
        if (ctx < plic_ctx_count(plic)) {
            if (flag == PLIC_CTXFLAG_CLAIMCOMPLETE) {
                plic_scan_irqs(plic, ctx);
            } else if (flag == PLIC_CTXFLAG_THRESHOLD) {
                atomic_store_uint32(&plic->threshold[ctx], read_uint32_le(data));
            }
        }
    }

    return true;
}

static void plic_remove(rvvm_mmio_dev_t* dev)
{
    plic_ctx_t* plic = dev->data;

    for (size_t ctx=0; ctx<plic_ctx_count(plic); ++ctx){
        free(plic->enable[ctx]);
    }
    free(plic->enable);
    free(plic->threshold);
    free(plic);
}

static void plic_reset(rvvm_mmio_dev_t* dev)
{
    plic_ctx_t* plic = dev->data;

    for (size_t ctx=0; ctx<plic_ctx_count(plic); ++ctx){
        riscv_interrupt_clear(vector_at(plic->machine->harts, CTX_HARTID(ctx)), CTX_IRQ_PRIO(ctx));
        memset(plic->enable[ctx], 0, PLIC_SRC_REG_COUNT << 2);
    }
    memset(plic->prio, 0, sizeof(plic->prio));
    memset(plic->pending, 0, sizeof(plic->pending));
    memset(plic->raised, 0, sizeof(plic->raised));
    memset(plic->threshold, 0, plic_ctx_count(plic) << 2);
}

static rvvm_mmio_type_t plic_dev_type = {
    .name = "plic",
    .remove = plic_remove,
    .reset = plic_reset,
};

// Create PLIC device
PUBLIC plic_ctx_t* plic_init(rvvm_machine_t* machine, rvvm_addr_t base_addr)
{
    plic_ctx_t* plic = safe_new_obj(plic_ctx_t);
    plic->machine = machine;
    plic->enable = safe_calloc(sizeof(uint32_t*), plic_ctx_count(plic));
    for (size_t ctx=0; ctx<plic_ctx_count(plic); ++ctx){
        plic->enable[ctx] = safe_calloc(sizeof(uint32_t), PLIC_SRC_REG_COUNT);
    }
    plic->threshold = safe_calloc(sizeof(uint32_t), plic_ctx_count(plic));

    rvvm_mmio_dev_t plic_mmio = {
        .addr = base_addr,
        .size = 0x4000000,
        .min_op_size = 4,
        .max_op_size = 4,
        .read = plic_mmio_read,
        .write = plic_mmio_write,
        .data = plic,
        .type = &plic_dev_type,
    };
    rvvm_attach_mmio(machine, &plic_mmio);
#ifdef USE_FDT
    struct fdt_node* cpus = fdt_node_find(rvvm_get_fdt_root(machine), "cpus");
    if (cpus == NULL) {
        rvvm_warn("Missing /cpus node in FDT!");
        return plic;
    }

    uint32_t* irq_ext = safe_calloc(sizeof(uint32_t), vector_size(machine->harts) * 4);
    vector_foreach(machine->harts, i) {
        struct fdt_node* cpu = fdt_node_find_reg(cpus, "cpu", i);
        struct fdt_node* cpu_irq = fdt_node_find(cpu, "interrupt-controller");

        uint32_t irq_phandle = fdt_node_get_phandle(cpu_irq);
        irq_ext[(i * 4)] = irq_ext[(i * 4) + 2] = irq_phandle;
        irq_ext[(i * 4) + 1] = CTX_IRQ_PRIO(0);
        irq_ext[(i * 4) + 3] = CTX_IRQ_PRIO(1);
    }

    struct fdt_node* plic_node = fdt_node_create_reg("plic", base_addr);
    fdt_node_add_prop_u32(plic_node, "#interrupt-cells", 1);
    fdt_node_add_prop_reg(plic_node, "reg", base_addr, 0x4000000);
    fdt_node_add_prop_str(plic_node, "compatible", "sifive,plic-1.0.0");
    fdt_node_add_prop_u32(plic_node, "riscv,ndev", PLIC_SOURCE_MAX);
    fdt_node_add_prop(plic_node, "interrupt-controller", NULL, 0);
    fdt_node_add_prop_cells(plic_node, "interrupts-extended", irq_ext, vector_size(machine->harts) * 4);
    free(irq_ext);

    fdt_node_add_child(rvvm_get_fdt_soc(machine), plic_node);

    plic->phandle = fdt_node_get_phandle(plic_node);
#endif
    rvvm_set_plic(machine, plic);
    return plic;
}

PUBLIC plic_ctx_t* plic_init_auto(rvvm_machine_t* machine)
{
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, PLIC_DEFAULT_MMIO, 0x4000000);
    return plic_init(machine, addr);
}

// Allocate new IRQ
PUBLIC uint32_t plic_alloc_irq(plic_ctx_t* plic)
{
    if (plic == NULL) return 0;
    uint32_t irq = atomic_add_uint32(&plic->alloc_irq, 1);
    if (irq >= PLIC_SOURCE_MAX) {
        rvvm_warn("Ran out of PLIC interrupt IDs");
        irq = 0;
    }
    return irq;
}

// Get FDT phandle of the PLIC
PUBLIC uint32_t plic_get_phandle(plic_ctx_t* plic)
{
    if (plic == NULL) return 0;
    return plic->phandle;
}

// Send IRQ through PLIC
PUBLIC bool plic_send_irq(plic_ctx_t* plic, uint32_t irq)
{
    if (plic == NULL || irq == 0 || irq >= PLIC_SOURCE_MAX) {
        return false;
    }

    // Mark the IRQ pending
    atomic_or_uint32(&plic->pending[irq >> 5], 1 << (irq & 0x1F));

    // Assign & notify hart responsible for this IRQ
    for (size_t ctx=0; ctx<plic_ctx_count(plic); ++ctx) {
        if (plic_notify_irq(plic, ctx, irq)) break;
    }

    return true;
}

// Assert IRQ line level
PUBLIC bool plic_raise_irq(plic_ctx_t* plic, uint32_t irq)
{
    if (plic == NULL || irq == 0 || irq >= PLIC_SOURCE_MAX) {
        return false;
    }

    atomic_or_uint32(&plic->raised[irq >> 5], 1 << (irq & 0x1F));
    plic_send_irq(plic, irq);
    return true;
}

PUBLIC bool plic_lower_irq(plic_ctx_t* plic, uint32_t irq)
{
    if (plic == NULL || irq == 0 || irq >= PLIC_SOURCE_MAX) {
        return false;
    }
    atomic_and_uint32(&plic->raised[irq >> 5], ~(1 << (irq & 0x1F)));
    return true;
}
