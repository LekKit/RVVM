/*
riscv-plic.c - RISC-V Platform-Level Interrupt Controller
Copyright (C) 2023  LekKit <github.com/LekKit>
              2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

/*
 * TODO: Replace <cpu/riscv_hart.h> with <rvvm/rvvm_cpu.h>
 * TODO: Handle machine = NULL and CPU hotplug!
 */

#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_fdt.h>
#include <rvvm/rvvm_irq.h>
#include <rvvm/rvvm_region.h>
#include <rvvm/rvvm_snapshot.h>

#include <util/atomics.h>
#include <util/bit_ops.h>
#include <util/mem_ops.h>

#include <cpu/riscv_hart.h>

PUSH_OPTIMIZATION_SIZE

/*
 * PLIC Implementation constants
 */
#define PLIC_SRC_LIMIT 64                           // Max interrupt identities
#define PLIC_SRC_REGS  ((PLIC_SRC_LIMIT + 31) >> 5) // Number of bitset regs

typedef struct {
    rvvm_machine_t* machine;
    rvvm_irq_dev_t* irq_dev;

    uint32_t   prio[PLIC_SRC_LIMIT];
    uint32_t   pending[PLIC_SRC_REGS];
    uint32_t   raised[PLIC_SRC_REGS];
    uint32_t** enable;    // [CTX][SRC_REG]
    uint32_t*  threshold; // [CTX]
} plic_dev_t;

static inline uint32_t plic_ctx_prio(uint32_t ctx)
{
    // In QEMU, those are reversed for whatever reason, but on most actual
    // boards it's done this way. Should we ever do 1:1 QEMU compat, swap those...
    return (ctx & 1) ? RISCV_INTERRUPT_SEXTERNAL : RISCV_INTERRUPT_MEXTERNAL;
}

static inline uint32_t plic_ctx_hartid(uint32_t ctx)
{
    return ctx >> 1;
}

static inline uint32_t plic_ctx_count(plic_dev_t* plic)
{
    return vector_size(plic->machine->harts) << 1;
}

// Check if the IRQ is pending
static inline bool plic_irq_pending(plic_dev_t* plic, uint32_t irq)
{
    return bit_check32(atomic_load_uint32_relax(&plic->pending[irq >> 5]), irq);
}

// Check if the IRQ is enabled for specific CTX
static inline bool plic_irq_enabled(plic_dev_t* plic, uint32_t ctx, uint32_t irq)
{
    return bit_check32(atomic_load_uint32_relax(&plic->enable[ctx][irq >> 5]), irq);
}

// Notify specific CTX about inbound IRQ
static bool plic_notify_ctx_irq(plic_dev_t* plic, uint32_t ctx, uint32_t irq)
{
    // Can we deliver this IRQ to this CTX?
    if (!plic_irq_enabled(plic, ctx, irq)) {
        return false;
    }

    if (atomic_load_uint32_relax(&plic->prio[irq]) <= //
        atomic_load_uint32_relax(&plic->threshold[ctx])) {
        // This IRQ priority isn't high enough
        return false;
    }

    riscv_interrupt(vector_at(plic->machine->harts, plic_ctx_hartid(ctx)), plic_ctx_prio(ctx));
    return true;
}

// Notify any hart responsible for this IRQ
static void plic_notify_irq(plic_dev_t* plic, uint32_t irq)
{
    for (size_t ctx = 0; ctx < plic_ctx_count(plic); ++ctx) {
        if (plic_notify_ctx_irq(plic, ctx, irq)) {
            return;
        }
    }
}

// Update on changes to IRQ enable register of CTX
static void plic_update_ctx_irq_reg(plic_dev_t* plic, uint32_t ctx, uint32_t reg)
{
    uint32_t irqs = atomic_load_uint32_relax(&plic->pending[reg]) //
                  & atomic_load_uint32(&plic->enable[ctx][reg]);
    while (irqs) {
        uint32_t bit = bit_ctz32(irqs);
        plic_notify_irq(plic, (reg << 5) | bit);
        irqs &= ~bit_set32(bit);
    }
}

// Update a CTX (Also used for IRQ claim process)
static uint32_t plic_update_ctx(plic_dev_t* plic, uint32_t ctx, bool claim)
{
    uint32_t threshold = atomic_load_uint32_relax(&plic->threshold[ctx]);
    uint32_t signals = 0, max_irq = 0, max_prio = 0;

    riscv_interrupt_clear(vector_at(plic->machine->harts, plic_ctx_hartid(ctx)), plic_ctx_prio(ctx));

    for (size_t reg = 0; reg < PLIC_SRC_REGS; ++reg) {
        uint32_t irqs = atomic_load_uint32_relax(&plic->pending[reg]) //
                      & atomic_load_uint32_relax(&plic->enable[ctx][reg]);
        while (irqs) {
            uint32_t bit  = bit_ctz32(irqs);
            uint32_t irq  = (reg << 5) | bit;
            uint32_t prio = atomic_load_uint32_relax(&plic->prio[irq]);
            if (prio > threshold) {
                // Count IRQs above CTX threshold
                signals++;
            }
            if (prio > max_prio) {
                // Determine highest priority IRQ
                max_prio = prio;
                max_irq  = irq;
            }
            irqs &= ~bit_set32(bit);
        }
    }

    if (claim && max_prio > threshold) {
        // Don't count the to-be-claimed IRQ as notifying
        signals--;
    }

    if (signals) {
        riscv_interrupt(vector_at(plic->machine->harts, plic_ctx_hartid(ctx)), plic_ctx_prio(ctx));
    }

    return max_irq;
}

/*
 * Update PLIC state entirely
 * Use after any operation that potentially causes an IRQ cease to signal
 *
 * Efforts are made so that this function is called in very rare cases,
 * and usually it is replaced by a partial update for performance
 */
static void plic_full_update(plic_dev_t* plic)
{
    for (size_t ctx = 0; ctx < plic_ctx_count(plic); ++ctx) {
        plic_update_ctx(plic, ctx, false);
    }
}

/*
 * PLIC Unsafe input handling
 */

static void plic_set_irq_prio(plic_dev_t* plic, uint32_t irq, uint32_t prio)
{
    if (likely(irq && irq < PLIC_SRC_LIMIT)) {
        uint32_t old_prio = atomic_swap_uint32(&plic->prio[irq], prio);
        if (prio < old_prio) {
            if (plic_irq_pending(plic, irq)) {
                // Pending IRQ priority was lowered - do a full PLIC state update
                plic_full_update(plic);
            }
        } else if (prio > old_prio && plic_irq_pending(plic, irq)) {
            // IRQ priority was raised and it now signals
            plic_notify_irq(plic, irq);
        }
    }
}

static void plic_set_enable_bits(plic_dev_t* plic, uint32_t ctx, uint32_t reg, uint32_t enable)
{
    if (likely(ctx < plic_ctx_count(plic) && reg < PLIC_SRC_REGS)) {
        uint32_t old_enable    = atomic_swap_uint32(&plic->enable[ctx][reg], enable);
        uint32_t irqs_disabled = old_enable & ~enable;
        if (irqs_disabled) {
            if (irqs_disabled & atomic_load_uint32(&plic->pending[reg])) {
                // Some pending IRQs were disabled - do a full PLIC state update
                plic_full_update(plic);
            }
        } else if (enable & ~old_enable) {
            // Some IRQs were enabled - do a partial check
            plic_update_ctx_irq_reg(plic, ctx, reg);
        }
    }
}

static void plic_set_ctx_threshold(plic_dev_t* plic, uint32_t ctx, uint32_t threshold)
{
    if (likely(ctx < plic_ctx_count(plic))) {
        uint32_t old_threshold = atomic_swap_uint32(&plic->threshold[ctx], threshold);
        if (old_threshold != threshold) {
            // CTX threshold changed - do a CTX update
            plic_update_ctx(plic, ctx, false);
        }
    }
}

static uint32_t plic_claim_irq(plic_dev_t* plic, uint32_t ctx)
{
    uint32_t irq  = 0;
    uint32_t mask = 0;
    if (likely(ctx < plic_ctx_count(plic))) {
        // Loop until either there's no IRQ to claim, or we successfully claim one
        do {
            irq  = plic_update_ctx(plic, ctx, true);
            mask = bit_set32(irq);
        } while (irq && !(atomic_and_uint32(&plic->pending[irq >> 5], ~mask) & mask));
    }
    return irq;
}

static void plic_complete_irq(plic_dev_t* plic, uint32_t ctx, uint32_t irq)
{
    if (likely(ctx < plic_ctx_count(plic) && irq < PLIC_SRC_LIMIT)) {
        uint32_t raised = atomic_load_uint32_relax(&plic->raised[irq >> 5]) & bit_set32(irq);
        if (raised) {
            // Rearm raised interrupt as pending after completion
            atomic_or_uint32(&plic->pending[irq >> 5], raised);
            plic_notify_ctx_irq(plic, ctx, irq);
        }
    }
}

static void plic_mmio_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    plic_dev_t* plic = rvvm_region_data(dev);
    uint32_t    val  = 0;
    UNUSED(size);

    if (likely(off >= 0x200000UL)) {
        // Context flags
        uint32_t ctx = (off - 0x200000UL) >> 12;
        switch ((off - 0x200000UL) & 0xFFF) {
            case 0x00: // Threshold
                if (ctx < plic_ctx_count(plic)) {
                    val = atomic_load_uint32_relax(&plic->threshold[ctx]);
                }
                break;
            case 0x04: // Claim
                val = plic_claim_irq(plic, ctx);
                break;
        }
    } else if (off >= 0x2000) {
        // Enable bits
        uint32_t reg = ((off - 0x2000) >> 2) & 0x1F;
        uint32_t ctx = (off - 0x2000) >> 7;
        if (reg < PLIC_SRC_REGS && ctx < plic_ctx_count(plic)) {
            val = atomic_load_uint32_relax(&plic->enable[ctx][reg]);
        }
    } else if (off >= 0x1000) {
        // Pending bits
        uint32_t reg = (off - 0x1000) >> 2;
        if (reg < PLIC_SRC_REGS) {
            val = atomic_load_uint32_relax(&plic->pending[reg]);
        }
    } else {
        // Interrupt priority
        uint32_t irq = off >> 2;
        if (irq && irq < PLIC_SRC_LIMIT) {
            val = atomic_load_uint32_relax(&plic->prio[irq]);
        }
    }

    write_uint32_le(data, val);
}

static void plic_mmio_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    plic_dev_t* plic = rvvm_region_data(dev);
    uint32_t    val  = read_uint32_le(data);
    UNUSED(size);

    if (likely(off >= 0x200000UL)) {
        // Context flags
        uint32_t ctx = (off - 0x200000UL) >> 12;
        switch ((off - 0x200000UL) & 0xFFF) {
            case 0x00: // Threshold
                plic_set_ctx_threshold(plic, ctx, val);
                break;
            case 0x04: // Complete
                plic_complete_irq(plic, ctx, val);
                break;
        }
    } else if (off >= 0x2000) {
        // Enable bits
        uint32_t reg = ((off - 0x2000) >> 2) & 0x1F;
        uint32_t ctx = (off - 0x2000) >> 7;
        plic_set_enable_bits(plic, ctx, reg, read_uint32_le(data));
    } else {
        // Interrupt priority
        plic_set_irq_prio(plic, off >> 2, read_uint32_le(data));
    }
}

static void plic_suspend(rvvm_reg_dev_t* dev, rvvm_snapshot_t* snap, bool resume)
{
    if (snap) {
        plic_dev_t* plic = rvvm_region_data(dev);
        rvvm_snapshot_section(snap, "riscv-plic");
        for (size_t i = 0; i < PLIC_SRC_LIMIT; ++i) {
            rvvm_snapshot_field(snap, plic->prio[i]);
        }
        for (size_t i = 0; i < PLIC_SRC_REGS; ++i) {
            rvvm_snapshot_field(snap, plic->pending[i]);
            rvvm_snapshot_field(snap, plic->raised[i]);
        }
        for (size_t i = 0; i < plic_ctx_count(plic); ++i) {
            rvvm_snapshot_field(snap, plic->threshold[i]);
            for (size_t j = 0; j < PLIC_SRC_REGS; ++j) {
                rvvm_snapshot_field(snap, plic->enable[i][j]);
            }
        }
    }
    UNUSED(resume);
}

static void plic_cleanup(rvvm_reg_dev_t* dev)
{
    plic_dev_t* plic = rvvm_region_data(dev);
    rvvm_irq_dev_free(plic->irq_dev);
    for (size_t ctx = 0; ctx < plic_ctx_count(plic); ++ctx) {
        free(plic->enable[ctx]);
    }
    free(plic->enable);
    free(plic->threshold);
    free(plic);
}

static void plic_set_irq(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq, bool lvl)
{
    plic_dev_t* plic = rvvm_irq_dev_data(irq_dev);
    if (irq > 0 && irq < PLIC_SRC_LIMIT) {
        uint32_t mask = bit_set32(irq);
        if (lvl) {
            if (!(atomic_or_uint32(&plic->raised[irq >> 5], mask) & mask) && //
                !(atomic_or_uint32(&plic->pending[irq >> 5], mask) & mask)) {
                plic_notify_irq(plic, irq);
            }
        } else {
            atomic_and_uint32(&plic->raised[irq >> 5], ~mask);
        }
    }
}

static const rvvm_reg_type_t plic_type = {
    .name     = "riscv-plic",
    .read     = plic_mmio_read,
    .write    = plic_mmio_write,
    .suspend  = plic_suspend,
    .cleanup  = plic_cleanup,
    .min_size = 4,
    .max_size = 4,
};

static const rvvm_irq_dev_cb_t plic_cb = {
    .set_irq = plic_set_irq,
};

RVVM_PUBLIC rvvm_intc_t* rvvm_riscv_plic_init(rvvm_machine_t* machine, rvvm_addr_t addr)
{
    plic_dev_t*     plic = safe_new_obj(plic_dev_t);
    rvvm_reg_desc_t desc = {
        .addr = addr,
        .size = 0x4000000UL,
        .data = plic,
        .type = &plic_type,
    };

    plic->machine = machine;
    plic->irq_dev = rvvm_irq_dev_init(&plic_cb, plic);
    rvvm_irq_dev_set_base(plic->irq_dev, 1);

    plic->enable    = safe_new_arr(uint32_t*, plic_ctx_count(plic));
    plic->threshold = safe_new_arr(uint32_t, plic_ctx_count(plic));
    for (size_t ctx = 0; ctx < plic_ctx_count(plic); ++ctx) {
        plic->enable[ctx] = safe_new_arr(uint32_t, PLIC_SRC_REGS);
    }

    rvvm_reg_dev_t*  dev = rvvm_region_init_auto(machine, &desc);
    rvvm_fdt_node_t* soc = rvvm_get_fdt_soc(machine);

    if (dev && soc) {
        rvvm_fdt_node_t*   plic_fdt = fdt_node_create_reg("plic", desc.addr);
        rvvm_fdt_node_t*   cpus     = rvvm_fdt_find(rvvm_get_fdt_root(machine), "cpus");
        vector_t(uint32_t) irq_ext  = {0};

        vector_foreach (machine->harts, i) {
            rvvm_fdt_node_t* cpu     = rvvm_fdt_find_reg(cpus, "cpu", i);
            rvvm_fdt_node_t* cpu_irq = rvvm_fdt_find(cpu, "interrupt-controller");
            uint32_t         phandle = rvvm_fdt_phandle(cpu_irq);
            if (phandle) {
                vector_push_back(irq_ext, phandle);
                vector_push_back(irq_ext, plic_ctx_prio(0));
                vector_push_back(irq_ext, phandle);
                vector_push_back(irq_ext, plic_ctx_prio(1));
            }
        }

        rvvm_fdt_prop_set_reg(plic_fdt, "reg", desc.addr, desc.size);
        rvvm_fdt_prop_set_str(plic_fdt, "compatible", "sifive,plic-1.0.0");
        rvvm_fdt_prop_set_flag(plic_fdt, "interrupt-controller");
        rvvm_fdt_prop_set_cells(plic_fdt, "interrupts-extended", vector_buffer(irq_ext), vector_size(irq_ext));
        rvvm_fdt_prop_set_u32(plic_fdt, "#interrupt-cells", 1);
        rvvm_fdt_prop_set_u32(plic_fdt, "#address-cells", 0);
        rvvm_fdt_prop_set_u32(plic_fdt, "riscv,ndev", PLIC_SRC_LIMIT - 1);

        vector_free(irq_ext);

        rvvm_fdt_attach(soc, plic_fdt);
        rvvm_irq_dev_set_phandle(plic->irq_dev, rvvm_fdt_phandle(plic_fdt));
    }
    if (dev) {
        rvvm_set_intc(machine, plic->irq_dev);
        return plic->irq_dev;
    }
    return NULL;
}

POP_OPTIMIZATION_SIZE
