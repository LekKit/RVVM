/*
riscv-aplic.c - RISC-V Advanced Platform-Level Interrupt Controller
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

/*
 * TODO: Replace <cpu/riscv_hart.h> with <rvvm/rvvm_cpu.h>
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
 * APLIC Registers
 */
#define APLIC_REG_DOMAINCFG       0x0000 // Domain configuration
#define APLIC_REG_SOURCECFG       0x0004 // Source configurations (1 - 1023)
#define APLIC_REG_SOURCECFG_SIZE  0x0FFC // Source configurations size
#define APLIC_REG_MMSIADDRCFG     0x1BC0 // Machine MSI address configuration
#define APLIC_REG_MMSIADDRCFGH    0x1BC4 // Machine MSI address configuration (high)
#define APLIC_REG_SMSIADDRCFG     0x1BC8 // Supervisor MSI address configuration
#define APLIC_REG_SMSIADDRCFGH    0x1BCC // Supervisor MSI address configuration (high)
#define APLIC_REG_SETIP           0x1C00 // Interrupt-pending bits (0 - 31)
#define APLIC_REG_SETIP_SIZE      0x0020 // Interrupt-pending bits size
#define APLIC_REG_SETIPNUM        0x1CDC // Set interrupt-pending bit by number
#define APLIC_REG_IN_CLRIP        0x1D00 // Rectified inputs, clear interrupt-pending bits (0 - 31)
#define APLIC_REG_IN_CLRIP_SIZE   0x0020 // Rectified inputs, clear interrupt-pending bits size
#define APLIC_REG_CLRIPNUM        0x1DDC // Clear interrupt-pending bit by number
#define APLIC_REG_SETIE           0x1E00 // Interrupt-enabled bits (0 - 31)
#define APLIC_REG_SETIE_SIZE      0x0020 // Interrupt-enabled bits size
#define APLIC_REG_SETIENUM        0x1EDC // Set interrupt-enabled bit by number
#define APLIC_REG_CLRIE           0x1F00 // Clear interrupt-enabled bits (0 - 31)
#define APLIC_REG_CLRIE_SIZE      0x0020 // Clear interrupt-enabled bits size
#define APLIC_REG_CLRIENUM        0x1FDC // Clear interrupt-enabled bit by number
#define APLIC_REG_SETIPNUM_LE     0x2000 // Set interrupt-pending bit by number (Little-endian)
#define APLIC_REG_SETIPNUM_BE     0x2004 // Set interrupt-pending bit by number (Big-endian)
#define APLIC_REG_GENMSI          0x3000 // Generate MSI
#define APLIC_REG_TARGET          0x3004 // Interrupt targets (1 - 1023)
#define APLIC_REG_TARGET_SIZE     0x0FFC // Interrupt targets size

/*
 * APLIC Register constants
 */
#define APLIC_DOMAINCFG_BE        0x00000001UL // Big-endian mode
#define APLIC_DOMAINCFG_DM        0x00000004UL // MSI delivery mode
#define APLIC_DOMAINCFG_IE        0x00000100UL // Interrupts enabled
#define APLIC_DOMAINCFG_MASK      0x00000101UL // Valid domain config mask
#define APLIC_DOMAINCFG           0x80000004UL // Hardwired domain config

#define APLIC_SOURCECFG_DELEGATE  0x0400 // Delegate to child domain
#define APLIC_SOURCECFG_INACTIVE  0x0000 // Inactive source
#define APLIC_SOURCECFG_DETACHED  0x0001 // Detached source
#define APLIC_SOURCECFG_EDGE_RISE 0x0004 // Active, edge-triggered on rise
#define APLIC_SOURCECFG_EDGE_FALL 0x0005 // Active, edge-triggered on fall
#define APLIC_SOURCECFG_LVL_HIGH  0x0006 // Active, level-triggered when high
#define APLIC_SOURCECFG_LVL_LOW   0x0007 // Active, level-triggered when low
#define APLIC_SOURCECFG_MASK      0x0007 // Valid source config mask

#define APLIC_MSIADDRCFGH_L       0x80000000UL // Locked

/*
 * APLIC Implementation constants
 */
#define APLIC_SRC_REGS            2  // Size of bitset arrays
#define APLIC_SRC_IDTS            64 // Size of identity arrays
#define APLIC_SRC_COUNT           63 // Number of interrupt lines

#define APLIC_MDOMAIN             0
#define APLIC_SDOMAIN             1

typedef struct aplic_dev aplic_dev_t;

typedef struct {
    // APLIC controller context
    aplic_dev_t* aplic;

    // Delegation invert
    uint32_t invert;

    // Domain configuration
    uint32_t config;
} aplic_domain_t;

struct aplic_dev {
    // Machine & IRQ controller handles
    rvvm_machine_t* machine;
    rvvm_irq_dev_t* irq_dev;

    // APLIC domains
    aplic_domain_t domain[2];

    // Bitset of interrupts delegated to S-mode
    uint32_t deleg[APLIC_SRC_REGS];

    // Bitset of raised external interrupts
    uint32_t raised[APLIC_SRC_REGS];

    // Bitset of inverted external interrupts
    uint32_t invert[APLIC_SRC_REGS];

    // Bitset of pending interrupts
    uint32_t pending[APLIC_SRC_REGS];

    // Bitset of enabled interrupts
    uint32_t enabled[APLIC_SRC_REGS];

    // Interrupt source configuration
    uint32_t source[APLIC_SRC_IDTS];

    // Interrupt target configuration
    uint32_t target[APLIC_SRC_IDTS];
};

/*
 * APLIC Core helpers
 */

static void aplic_gen_msi(aplic_dev_t* aplic, bool smode, uint32_t target)
{
    size_t hartid = target >> 18;
    if (likely(hartid < vector_size(aplic->machine->harts))) {
        rvvm_hart_t* hart = vector_at(aplic->machine->harts, hartid);
        riscv_send_aia_irq(hart, smode, target & 0x3FF);
    }
}

static uint32_t aplic_rectified_bits(aplic_dev_t* aplic, size_t reg)
{
    return atomic_load_uint32_relax(&aplic->raised[reg]) //
         ^ atomic_load_uint32_relax(&aplic->invert[reg]);
}

static bool aplic_rectified_src(aplic_dev_t* aplic, size_t irq)
{
    return !!(aplic_rectified_bits(aplic, irq >> 5) & bit_set32(irq));
}

static bool aplic_detached_src(aplic_dev_t* aplic, size_t irq)
{
    return atomic_load_uint32_relax(&aplic->source[irq]) <= APLIC_SOURCECFG_DETACHED;
}

static void aplic_pending_interrupt(aplic_dev_t* aplic, size_t irq)
{
    if (likely(irq - 1 < APLIC_SRC_COUNT)) {
        size_t   reg  = irq >> 5;
        uint32_t mask = bit_set32(irq);
        if (likely(atomic_load_uint32_relax(&aplic->enabled[reg]) & mask)) {
            bool            smode  = !!(atomic_load_uint32_relax(&aplic->deleg[reg]) & mask);
            aplic_domain_t* domain = &aplic->domain[smode];
            if (likely(atomic_load_uint32_relax(&domain->config) & APLIC_DOMAINCFG_IE)) {
                uint32_t target = atomic_load_uint32_relax(&aplic->target[irq]);
                aplic_gen_msi(aplic, smode, target);
                return;
            }
        }
        atomic_or_uint32(&aplic->pending[reg], mask);
    }
}

static void aplic_update_interrupt(aplic_dev_t* aplic, size_t irq)
{
    if (aplic_rectified_src(aplic, irq) && !aplic_detached_src(aplic, irq)) {
        aplic_pending_interrupt(aplic, irq);
    }
}

/*
 * APLIC Domain helpers
 */

static uint32_t aplic_valid_bits(aplic_domain_t* domain, size_t reg)
{
    aplic_dev_t* aplic = domain->aplic;
    return atomic_load_uint32_relax(&aplic->deleg[reg]) ^ domain->invert;
}

static bool aplic_valid_src(aplic_domain_t* domain, size_t irq)
{
    return !!(aplic_valid_bits(domain, irq >> 5) & bit_set32(irq));
}

static bool aplic_ungated_src(aplic_domain_t* domain, size_t irq)
{
    if (likely(aplic_valid_src(domain, irq))) {
        return aplic_rectified_src(domain->aplic, irq) || aplic_detached_src(domain->aplic, irq);
    }
    return false;
}

/*
 * APLIC Unsafe input handling (registers r/w)
 */

static inline uint32_t aplic_read_ip(aplic_domain_t* domain, size_t reg)
{
    if (likely(reg < APLIC_SRC_REGS)) {
        aplic_dev_t* aplic = domain->aplic;
        return atomic_load_uint32_relax(&aplic->pending[reg]) & aplic_valid_bits(domain, reg);
    }
    return 0;
}

static inline uint32_t aplic_read_in(aplic_domain_t* domain, size_t reg)
{
    if (likely(reg < APLIC_SRC_REGS)) {
        return aplic_rectified_bits(domain->aplic, reg) & aplic_valid_bits(domain, reg);
    }
    return 0;
}

static inline uint32_t aplic_read_ie(aplic_domain_t* domain, size_t reg)
{
    if (likely(reg < APLIC_SRC_REGS)) {
        aplic_dev_t* aplic = domain->aplic;
        return atomic_load_uint32_relax(&aplic->enabled[reg]) & aplic_valid_bits(domain, reg);
    }
    return 0;
}

static inline uint32_t aplic_read_sourcecfg(aplic_domain_t* domain, size_t irq)
{
    if (likely(irq - 1 < APLIC_SRC_COUNT)) {
        aplic_dev_t* aplic = domain->aplic;
        if (aplic_valid_src(domain, irq)) {
            // Source configuration from our domain
            return atomic_load_uint32_relax(&aplic->source[irq]);
        } else if (domain->invert) {
            // Source configuration delegated and it's an M-mode domain
            return APLIC_SOURCECFG_DELEGATE;
        }
    }
    return 0;
}

static inline uint32_t aplic_read_targetcfg(aplic_domain_t* domain, size_t irq)
{
    if (likely(irq - 1 < APLIC_SRC_COUNT && aplic_valid_src(domain, irq))) {
        return atomic_load_uint32_relax(&domain->aplic->target[irq]);
    }
    return 0;
}

static inline void aplic_write_setipnum(aplic_domain_t* domain, size_t irq)
{
    if (likely(irq - 1 < APLIC_SRC_COUNT && aplic_ungated_src(domain, irq))) {
        aplic_pending_interrupt(domain->aplic, irq);
    }
}

static void aplic_write_setip(aplic_domain_t* domain, size_t reg, uint32_t bits)
{
    if (likely(reg < APLIC_SRC_REGS)) {
        uint32_t irqs = bits & aplic_valid_bits(domain, reg);
        // Re-notify rectified interrupts
        while (irqs) {
            uint32_t bit = bit_ctz32(irqs);
            aplic_write_setipnum(domain, (reg << 5) + bit);
            irqs &= ~bit_set32(bit);
        }
    }
}

static void aplic_write_clrip(aplic_domain_t* domain, size_t reg, uint32_t bits)
{
    if (likely(reg < APLIC_SRC_REGS)) {
        uint32_t clr = bits & aplic_valid_bits(domain, reg);
        if (clr) {
            aplic_dev_t* aplic = domain->aplic;
            atomic_and_uint32(&aplic->pending[reg], ~clr);
        }
    }
}

static inline void aplic_write_clripnum(aplic_domain_t* domain, size_t irq)
{
    aplic_write_clrip(domain, irq >> 5, bit_set32(irq));
}

static void aplic_write_setie(aplic_domain_t* domain, size_t reg, uint32_t bits)
{
    if (likely(reg < APLIC_SRC_REGS)) {
        aplic_dev_t* aplic = domain->aplic;
        uint32_t     set   = bits & aplic_valid_bits(domain, reg);
        if (likely(set)) {
            set &= ~atomic_or_uint32(&aplic->enabled[reg], set);
        }
        if (set) {
            uint32_t irqs = atomic_and_uint32(&aplic->pending[reg], ~set);
            // Notify about re-enabled pending interrupts
            while (irqs) {
                uint32_t bit = bit_ctz32(irqs);
                aplic_pending_interrupt(aplic, (reg << 5) + bit);
                irqs &= ~bit_set32(bit);
            }
        }
    }
}

static inline void aplic_write_setienum(aplic_domain_t* domain, size_t irq)
{
    aplic_write_setie(domain, irq >> 5, bit_set32(irq));
}

static void aplic_write_clrie(aplic_domain_t* domain, size_t reg, uint32_t val)
{
    if (likely(reg < APLIC_SRC_REGS)) {
        uint32_t clr = val & aplic_valid_bits(domain, reg);
        if (clr) {
            aplic_dev_t* aplic = domain->aplic;
            atomic_and_uint32(&aplic->enabled[reg], ~clr);
        }
    }
}

static inline void aplic_write_clrienum(aplic_domain_t* domain, size_t irq)
{
    aplic_write_clrie(domain, irq >> 5, bit_set32(irq));
}

static inline void aplic_write_sourcecfg(aplic_domain_t* domain, size_t irq, uint32_t val)
{
    if (likely(irq - 1 < APLIC_SRC_COUNT)) {
        aplic_dev_t* aplic = domain->aplic;
        size_t       reg   = irq >> 5;
        uint32_t     mask  = bit_set32(irq);
        if (domain->invert) {
            // M-mode source configuration write
            if (val & APLIC_SOURCECFG_DELEGATE) {
                // Enable delegation
                atomic_or_uint32(&aplic->deleg[reg], mask);
            } else {
                // Disable delegation
                atomic_and_uint32(&aplic->deleg[reg], ~mask);
            }
        }
        if (aplic_valid_src(domain, irq)) {
            // Source configuration for our domain
            uint32_t cfg = val & APLIC_SOURCECFG_MASK;
            atomic_store_uint32_relax(&aplic->source[irq], cfg);
            if (cfg == APLIC_SOURCECFG_LVL_LOW || cfg == APLIC_SOURCECFG_EDGE_FALL) {
                // Enable input inversion
                atomic_or_uint32(&aplic->invert[reg], mask);
            } else {
                // Disable input inversion
                atomic_and_uint32(&aplic->invert[reg], ~mask);
            }
        }
    }
}

static void aplic_mmio_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    aplic_domain_t* domain = rvvm_region_data(dev);
    uint32_t        config = atomic_load_uint32_relax(&domain->config);
    uint32_t        val    = 0;
    UNUSED(size);

    if (off == APLIC_REG_DOMAINCFG) {
        // Hardwired MSI delivery mode
        val = config | APLIC_DOMAINCFG;
    } else if (off - APLIC_REG_SETIP < APLIC_REG_SETIP_SIZE) {
        val = aplic_read_ip(domain, (off - APLIC_REG_SETIP) >> 2);
    } else if (off - APLIC_REG_IN_CLRIP < APLIC_REG_IN_CLRIP_SIZE) {
        val = aplic_read_in(domain, (off - APLIC_REG_IN_CLRIP) >> 2);
    } else if (off - APLIC_REG_SETIE < APLIC_REG_SETIE_SIZE) {
        val = aplic_read_ie(domain, (off - APLIC_REG_SETIE) >> 2);
    } else if (off - APLIC_REG_SOURCECFG < APLIC_REG_SOURCECFG_SIZE) {
        val = aplic_read_sourcecfg(domain, ((off - APLIC_REG_SOURCECFG) >> 2) + 1);
    } else if (off - APLIC_REG_TARGET < APLIC_REG_TARGET_SIZE) {
        val = aplic_read_targetcfg(domain, ((off - APLIC_REG_TARGET) >> 2) + 1);
    } else if (off == APLIC_REG_MMSIADDRCFGH || off == APLIC_REG_SMSIADDRCFGH) {
        // Hardwired MSI addresses
        val = APLIC_MSIADDRCFGH_L;
    }

    if (likely(!(config & APLIC_DOMAINCFG_BE))) {
        write_uint32_le(data, val);
    } else {
        write_uint32_be_m(data, val);
    }
}

static void aplic_mmio_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    aplic_domain_t* domain = rvvm_region_data(dev);
    uint32_t        config = atomic_load_uint32_relax(&domain->config);
    uint32_t        val    = 0;
    UNUSED(size);

    if (likely(!(config & APLIC_DOMAINCFG_BE))) {
        val = read_uint32_le(data);
    } else {
        val = read_uint32_be_m(data);
    }

    switch (off) {
        case APLIC_REG_DOMAINCFG:
            atomic_store_uint32_relax(&domain->config, val & APLIC_DOMAINCFG_MASK);
            return;
        case APLIC_REG_SETIPNUM:
            aplic_write_setipnum(domain, val);
            return;
        case APLIC_REG_CLRIPNUM:
            aplic_write_clripnum(domain, val);
            return;
        case APLIC_REG_SETIENUM:
            aplic_write_setienum(domain, val);
            return;
        case APLIC_REG_CLRIENUM:
            aplic_write_clrienum(domain, val);
            return;
        case APLIC_REG_SETIPNUM_LE:
            aplic_write_setipnum(domain, read_uint32_le(data));
            return;
        case APLIC_REG_SETIPNUM_BE:
            aplic_write_setipnum(domain, read_uint32_be_m(data));
            return;
        case APLIC_REG_GENMSI:
            aplic_gen_msi(domain->aplic, !domain->invert, val);
            return;
    }

    if (off - APLIC_REG_SETIP < APLIC_REG_SETIP_SIZE) {
        aplic_write_setip(domain, (off - APLIC_REG_SETIP) >> 2, val);
    } else if (off - APLIC_REG_IN_CLRIP < APLIC_REG_IN_CLRIP_SIZE) {
        aplic_write_clrip(domain, (off - APLIC_REG_IN_CLRIP) >> 2, val);
    } else if (off - APLIC_REG_SETIE < APLIC_REG_SETIE_SIZE) {
        aplic_write_setie(domain, (off - APLIC_REG_SETIE) >> 2, val);
    } else if (off - APLIC_REG_CLRIE < APLIC_REG_CLRIE_SIZE) {
        aplic_write_clrie(domain, (off - APLIC_REG_CLRIE) >> 2, val);
    } else if (off - APLIC_REG_SOURCECFG < APLIC_REG_SOURCECFG_SIZE) {
        aplic_write_sourcecfg(domain, ((off - APLIC_REG_SOURCECFG) >> 2) + 1, val);
    } else if (off - APLIC_REG_TARGET < APLIC_REG_TARGET_SIZE) {
        size_t reg = ((off - APLIC_REG_TARGET) >> 2) + 1;
        if (aplic_valid_src(domain, reg)) {
            // Target configuration for our domain
            atomic_store_uint32_relax(&domain->aplic->target[reg], val);
        }
    }
}

static void aplic_suspend(rvvm_reg_dev_t* dev, rvvm_snapshot_t* snap, bool resume)
{
    aplic_domain_t* domain = rvvm_region_data(dev);
    if (snap && domain->invert) {
        aplic_dev_t* aplic = domain->aplic;
        rvvm_snapshot_section(snap, "riscv-aplic");
        rvvm_snapshot_field(snap, aplic->domain[APLIC_MDOMAIN].config);
        rvvm_snapshot_field(snap, aplic->domain[APLIC_SDOMAIN].config);
        for (size_t i = 0; i < APLIC_SRC_REGS; ++i) {
            rvvm_snapshot_field(snap, aplic->deleg[i]);
            rvvm_snapshot_field(snap, aplic->raised[i]);
            rvvm_snapshot_field(snap, aplic->invert[i]);
            rvvm_snapshot_field(snap, aplic->pending[i]);
            rvvm_snapshot_field(snap, aplic->enabled[i]);
        }
        for (size_t i = 0; i < APLIC_SRC_IDTS; ++i) {
            rvvm_snapshot_field(snap, aplic->source[i]);
            rvvm_snapshot_field(snap, aplic->target[i]);
        }
    }
    UNUSED(resume);
}

static void aplic_cleanup(rvvm_reg_dev_t* dev)
{
    aplic_domain_t* domain = rvvm_region_data(dev);
    if (domain->aplic->machine) {
        domain->aplic->machine = NULL;
    } else {
        rvvm_irq_dev_free(domain->aplic->irq_dev);
        free(domain->aplic);
    }
}

static void aplic_set_irq(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq, bool lvl)
{
    aplic_dev_t* aplic = rvvm_irq_dev_data(irq_dev);
    if (irq - 1 < APLIC_SRC_COUNT) {
        uint32_t mask = bit_set32(irq);
        if (lvl) {
            if ((~atomic_or_uint32(&aplic->raised[irq >> 5], mask)) & mask) {
                aplic_update_interrupt(aplic, irq);
            }
        } else {
            if (atomic_and_uint32(&aplic->raised[irq >> 5], ~mask) & mask) {
                aplic_update_interrupt(aplic, irq);
            }
        }
    }
}

static size_t aplic_fdt_irq_cells(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq, uint32_t* cells, size_t size)
{
    if (irq_dev && cells && size >= 2) {
        cells[0] = irq;
        cells[1] = APLIC_SOURCECFG_EDGE_RISE;
        return 2;
    }
    return 0;
}

static const rvvm_reg_type_t aplic_type = {
    .name     = "riscv-aplic",
    .read     = aplic_mmio_read,
    .write    = aplic_mmio_write,
    .suspend  = aplic_suspend,
    .cleanup  = aplic_cleanup,
    .min_size = 4,
    .max_size = 4,
};

static const rvvm_irq_dev_cb_t aplic_cb = {
    .set_irq       = aplic_set_irq,
    .fdt_irq_cells = aplic_fdt_irq_cells,
};

static rvvm_reg_dev_t* aplic_attach_domain(rvvm_machine_t* machine, rvvm_addr_t addr, aplic_domain_t* domain)
{
    rvvm_reg_desc_t desc = {
        .addr = addr,
        .size = 0x4000,
        .data = domain,
        .type = &aplic_type,
    };

    rvvm_reg_dev_t*  dev = rvvm_region_init_auto(machine, &desc);
    rvvm_fdt_node_t* soc = rvvm_get_fdt_soc(machine);

    if (dev && soc) {
        rvvm_fdt_node_t* imsic_fdt = rvvm_fdt_find_reg_any(soc, domain->invert ? "imsics_m" : "imsics_s");
        rvvm_fdt_node_t* aplic_fdt = fdt_node_create_reg(domain->invert ? "aplic_m" : "aplic_s", desc.addr);
        rvvm_fdt_prop_set_reg(aplic_fdt, "reg", desc.addr, desc.size);
        rvvm_fdt_prop_set_str(aplic_fdt, "compatible", "riscv,aplic");
        rvvm_fdt_prop_set_u32(aplic_fdt, "msi-parent", rvvm_fdt_phandle(imsic_fdt));
        rvvm_fdt_prop_set_flag(aplic_fdt, "interrupt-controller");
        rvvm_fdt_prop_set_u32(aplic_fdt, "#interrupt-cells", 2);
        rvvm_fdt_prop_set_u32(aplic_fdt, "#address-cells", 0);
        rvvm_fdt_prop_set_u32(aplic_fdt, "riscv,num-sources", APLIC_SRC_COUNT);
        if (domain->invert) {
            rvvm_fdt_node_t* aplic_s    = rvvm_fdt_find_reg_any(soc, "aplic_s");
            uint32_t         children   = rvvm_fdt_phandle(aplic_s);
            uint32_t         delegate[] = {children, 1, APLIC_SRC_COUNT};
            rvvm_fdt_prop_set_u32(aplic_fdt, "riscv,children", children);
            rvvm_fdt_prop_set_cells(aplic_fdt, "riscv,delegate", delegate, STATIC_ARRAY_SIZE(delegate));
            rvvm_fdt_prop_set_cells(aplic_fdt, "riscv,delegation", delegate, STATIC_ARRAY_SIZE(delegate));
        }
        rvvm_fdt_attach(soc, aplic_fdt);
        if (!domain->invert) {
            rvvm_irq_dev_set_phandle(domain->aplic->irq_dev, rvvm_fdt_phandle(aplic_fdt));
        }
    }
    return dev;
}

RVVM_PUBLIC rvvm_irq_dev_t* rvvm_riscv_aplic_init(rvvm_machine_t* machine, rvvm_addr_t maddr, rvvm_addr_t saddr)
{
    aplic_dev_t* aplic = safe_new_obj(aplic_dev_t);

    aplic->domain[APLIC_MDOMAIN].invert = -1;
    aplic->domain[APLIC_MDOMAIN].aplic  = aplic;
    aplic->domain[APLIC_SDOMAIN].aplic  = aplic;

    aplic->machine = machine;
    aplic->irq_dev = rvvm_irq_dev_init(&aplic_cb, aplic);
    rvvm_irq_dev_set_base(aplic->irq_dev, 1);

    for (size_t i = 0; i < APLIC_SRC_REGS; ++i) {
        aplic->deleg[i] = -1;
    }

    if (!aplic_attach_domain(machine, saddr, &aplic->domain[APLIC_SDOMAIN]) || //
        !aplic_attach_domain(machine, maddr, &aplic->domain[APLIC_MDOMAIN])) {
        return NULL;
    }

    rvvm_set_intc(machine, aplic->irq_dev);
    return aplic->irq_dev;
}

POP_OPTIMIZATION_SIZE
