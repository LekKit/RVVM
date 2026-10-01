/*
rvvm_pci.c - RVVM PCI Core
Copyright (C) 2020-2026 LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

/*
 * TODO: Capability region handling
 * TODO: Proper MMIO->IO window instead of offseting
 * TODO: Legacy PCI access via IO ports
 * TODO: MSI/MSI-X vector count scaling
 * TODO: Improve PCIe port / switch handling
 * TODO: Lock-free rvvm_pci_func_from_addr()
 * TODO: DMA core
 */

#include <core/rvvm.h>

#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_fdt.h>
#include <rvvm/rvvm_irq.h>
#include <rvvm/rvvm_pci.h>
#include <rvvm/rvvm_region.h>
#include <rvvm/rvvm_snapshot.h>

#include <util/bit_ops.h>
#include <util/locking.h>
#include <util/mem_ops.h>
#include <util/utils.h>
#include <util/vector.h>

PUSH_OPTIMIZATION_SIZE

/*
 * PCI Configuration Space Registers
 *
 * All registers are treated as 32-bit wide
 */
#define PCI_REG_DEVICE          0x00 // Vendor ID [0:15], Device ID [16:31]
#define PCI_REG_CMD             0x04 // Command [0:15], Status [16:31]
#define PCI_REG_CLASS           0x08 // Revision [0:7], Interface [8:15], Subclass [16:23], Class [24:31]
#define PCI_REG_INFO            0x0C // Cacheline [0:7], Latency [8:15], Header type [16:23], BIST [24:31]
#define PCI_REG_BAR0            0x10 // BAR0 Base Address
#define PCI_REG_BAR1            0x14 // BAR1 Base Address / Upper 32 bits of BAR0
#define PCI_REG_BAR2            0x18 // BAR2 Base Address / Upper 32 bits of BAR1 / Bus behind bridge
#define PCI_REG_BAR3            0x1C // BAR3 Base Address / Upper 32 bits of BAR2 / IO window behind bridge
#define PCI_REG_BAR4            0x20 // BAR4 Base Address / Upper 32 bits of BAR3 / MMIO window behind bridge
#define PCI_REG_BAR5            0x24 // BAR5 Base Address / Upper 32 bits of BAR4 / Prefetch window behind bridge
#define PCI_REG_SUBSYSTEM       0x2C // Subsystem Vendor ID [0:15], Subsystem ID [16:31]
#define PCI_REG_ROM             0x30 // Expansion ROM Base Address
#define PCI_REG_CAPABILITIES    0x34 // Capabilities List Pointer
#define PCI_REG_INTERRUPT       0x3C // Interrupt Line [0:7], Interrupt Pin [8:15]

/*
 * PCI Configuration Space Capability Registers
 *
 * Capability list starts at 0x40
 */
#define PCI_REG_ECAP            0x40 // Endpoint capability
#define PCI_REG_PMCSR           0x84 // Power Management Control/Status
#define PCI_REG_MSI             0x90 // MSI capability
#define PCI_REG_MSI_AL          0x94 // MSI address low
#define PCI_REG_MSI_AH          0x98 // MSI address high
#define PCI_REG_MSI_DATA        0x9C // MSI data
#define PCI_REG_MSI_MASK        0xA0 // MSI mask vector
#define PCI_REG_MSI_PEND        0xA4 // MSI pending vector
#define PCI_REG_MSIX            0xB0 // MSI-X capability
#define PCI_REG_MSIX_TBL        0xB4 // MSI-X table offset/BAR
#define PCI_REG_MSIX_PBO        0xB8 // MSI-X pending bits offset/BAR

/*
 * Command register bits
 */
#define PCI_CMD_IO              0x00000001UL // Accessible through IO ports
#define PCI_CMD_MEM             0x00000002UL // Accessible through MMIO
#define PCI_CMD_BUS_MASTER      0x00000004UL // May use DMA
#define PCI_CMD_INTX_OFF        0x00000400UL // INTx Interrupt Disabled

#define PCI_CMD_INTX            0x00080000UL // INTx Interrupt Raised
#define PCI_CMD_CAP             0x00100000UL // Capabilities List Present

#define PCI_CMD_VALID           (PCI_CMD_IO | PCI_CMD_MEM | PCI_CMD_BUS_MASTER | PCI_CMD_INTX_OFF)

/*
 * Info register bits
 */
#define PCI_INFO_PCI_PCI        0x00010000UL // PCI-PCI Bridge
#define PCI_INFO_MULTIFUNC      0x00800000UL // Multi-function device

/*
 * BAR register bits
 */
#define PCI_BAR_IO              0x00000001UL // IO BAR
#define PCI_BAR_64BIT           0x00000004UL // 64-bit MMIO BAR
#define PCI_BAR_PREFETCH        0x00000008UL // Prefetchable BAR

/*
 * Expansion ROM register bits
 */
#define PCI_ROM_ENABLED         0x00000001UL // Expansion ROM enabled

/*
 * PCI capabilities offset
 */
#define PCI_CAP_OFFSET          0x00000040UL // Capabilities List Offset

/*
 * PCI Express capability port types
 */
#define PCI_ECAP_ENDPOINT       0x00000000UL // PCI Express Endpoint
#define PCI_ECAP_LEGACY         0x00100000UL // Legacy PCI Express Endpoint
#define PCI_ECAP_ROOT_PORT      0x00400000UL // Root Port of PCI Express Root Complex
#define PCI_ECAP_UP_PORT        0x00500000UL // Upstream Port of PCI Express Switch
#define PCI_ECAP_DN_PORT        0x00600000UL // Downstream Port of PCI Express Switch
#define PCI_ECAP_INTEGRATED     0x00900000UL // Root Complex Integrated Endpoint

/*
 * Power management register bits
 */
#define PCI_PMCSR_STATE         0x00000003UL // Power State (D0 - D3)

/*
 * MSI-X interrupts
 */
#define PCI_MSIX_MAX_IRQS       32
#define PCI_MSIX_TBL_SIZE       (((PCI_MSIX_MAX_IRQS + 1) >> 1) << 3)
#define PCI_MSIX_PBA_SIZE       ((PCI_MSIX_MAX_IRQS + 0x1F) >> 5)
#define PCI_MSIX_BAR_SIZE       (PCI_MSIX_TBL_SIZE + PCI_MSIX_PBA_SIZE)
#define PCI_MSIX_ENABLED        0x80000000
#define PCI_MSIX_MASKED         0x40000000
#define PCI_MSIX_VALID          0xC0000000

/*
 * MSI interrupts
 */
#define PCI_MSI_ENABLED         0x00010000 // MSI Enabled
#define PCI_MSI_DATA            0x0000FFFF // MSI Data (16-bit)
#define PCI_MSI_BIT             0x00000001 // MSI Pending/Masked bit

/*
 * Implementation constants
 */
#define PCI_BUS_IRQS            0x04
#define PCI_BUS_DEVS            0x20
#define PCI_DEV_FUNCS           0x08
#define PCI_FUNC_BARS           0x06
#define PCI_MAX_NONPREFETCH_BAR 0x08000000U

/*
 * PCI Capabilities read-only template
 */
static const uint32_t pci_caps_base[] = {
    0x01028010, // [40] PCI Express (v2) Endpoint, IntMsgNum 0
    0x00008002, // DevCap: MaxPayload 512 bytes, RBE+
    0x00002050, // DevCtl: RlxdOrd+
    0x01800D02, // LnkCap: Speed 5GT/s, Width x16
    0x01020003, // LnkSta: Speed 5GT/s, Width x16
    0x00020060,
    0x00200028,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000002,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,

    0x00039001, // [80] Power Management version 3
    0x00000008, // NoSoftRst+
    0x00000000,
    0x00000000,

    0x0180B005, // [90] MSI: Enable- Count=1/1 Maskable+ 64bit+
    0x00000000, // Message Address Low
    0x00000000, // Message Address High
    0x00000000, // Message Data
    0x00000000, // Mask
    0x00000000, // Pending
    0x00000000,
    0x00000000,

    // [B0] MSI-X: Enable- Count=X Masked-
    0x00000011 | ((PCI_MSIX_MAX_IRQS - 1) << 16),
};

/*
 * Data structures
 */

struct rvvm_pci_function {
    // Owning bus
    rvvm_pci_bus_t* bus;

    // Regions
    rvvm_reg_dev_t* bar[PCI_FUNC_BARS];
    rvvm_reg_dev_t* rom;

    // Bus address
    rvvm_pci_addr_t addr;

    // Device information
    uint32_t device_id;
    uint32_t class_code;
    uint32_t subsystem;
    uint32_t irq_pin;
    uint32_t info;

    // PCI registers
    uint32_t command;
    uint32_t status;
    uint32_t irq_line;

    // Bridge assignment
    uint32_t bridge_io;
    uint32_t bridge_mem;

    // Power management
    uint32_t pm_csr;

    // MSI
    uint32_t msi_ctl;
    uint32_t msi_addr_low;
    uint32_t msi_addr_high;
    uint32_t msi_data;
    uint32_t msi_mask;
    uint32_t msi_pending;

    // MSI-X (Sits in a dedicated BAR)
    uint32_t msix_ctl;
    uint32_t msix_bar;
    uint32_t msix[PCI_MSIX_BAR_SIZE];
};

typedef struct {
    rvvm_pci_func_t* func[PCI_DEV_FUNCS];
} rvvm_pci_dev_t;

struct rvvm_pci_bus {
    // Owning machine
    rvvm_machine_t* machine;

    // Wired interrupt controller
    rvvm_irq_dev_t* irq_dev;

    // Wired interrupts
    rvvm_irq_t irqs[PCI_BUS_IRQS];

    // IO/MMIO window
    rvvm_addr_t io_base;
    rvvm_addr_t mem_base;
    rvvm_addr_t mem_size;

    // Device vector
    vector_t(rvvm_pci_dev_t*) dev;

    rvvm_lock_t lock;
};

/*
 * Interrupt handling
 */

// Get INTx IRQ pin routing id for a device
static inline rvvm_irq_t pci_bus_intx_irq(rvvm_pci_bus_t* bus, uint32_t dev_id, uint32_t irq_pin)
{
    return bus->irqs[(dev_id + irq_pin + 3) & 3];
}

// Get INTx IRQ for a function
static inline rvvm_irq_t pci_func_intx_irq(rvvm_pci_func_t* func)
{
    return pci_bus_intx_irq(func->bus, func->addr >> 3, func->irq_pin);
}

// Set INTx IRQ level
static void pci_func_set_intx(rvvm_pci_func_t* func, bool lvl)
{
    if (func->irq_pin && lvl != !!atomic_load_uint32_relax(&func->status)) {
        atomic_store_uint32_relax(&func->status, lvl ? PCI_CMD_INTX : 0);
        if (likely(!(atomic_load_uint32_relax(&func->command) & PCI_CMD_INTX_OFF))) {
            // INTx enabled
            rvvm_irq_set(func->bus->irq_dev, pci_func_intx_irq(func), lvl);
        }
    }
}

// Set MSI IRQ level, must be called with Bus mastering
static bool pci_func_set_msi(rvvm_pci_func_t* func, bool lvl)
{
    if (likely(atomic_load_uint32_relax(&func->msi_ctl) & PCI_MSI_ENABLED)) {
        // MSI enabled
        if (likely(lvl && !(atomic_load_uint32_relax(&func->msi_mask)))) {
            // MSI not masked, perform an MSI write
            uint32_t    data = atomic_load_uint32_relax(&func->msi_data);
            rvvm_addr_t addr = atomic_load_uint32_relax(&func->msi_addr_low)
                             | ((uint64_t)atomic_load_uint32_relax(&func->msi_addr_high) << 32);
            rvvm_send_msi_irq(func->bus->machine, addr, data);
            return true;
        }
        // Vector is masked or lowering IRQ, set pending bit
        atomic_store_uint32_relax(&func->msi_pending, lvl ? PCI_MSI_BIT : 0);
        return true;
    }
    return false;
}

// Set MSI-X IRQ level, must be called with Bus mastering
static bool pci_func_set_msix(rvvm_pci_func_t* func, uint32_t vec, bool lvl)
{
    uint32_t msix_ctl = atomic_load_uint32_relax(&func->msix_ctl);
    if (likely((msix_ctl & PCI_MSIX_ENABLED) && vec < PCI_MSIX_MAX_IRQS)) {
        // MSI-X enabled, valid vector index
        uint32_t mask_bit = bit_set32(vec);
        uint32_t mask_off = PCI_MSIX_TBL_SIZE + (vec >> 5);
        if (lvl) {
            uint32_t vec_off = vec << 2;
            uint32_t vec_ctl = atomic_load_uint32_relax(&func->msix[vec_off + 3]);
            if (likely(!(msix_ctl & PCI_MSIX_MASKED) && !(vec_ctl & 1))) {
                // MSI-X vector not masked, perform an MSI write
                uint32_t    data = atomic_load_uint32_relax(&func->msix[vec_off + 2]);
                rvvm_addr_t addr = atomic_load_uint32_relax(&func->msix[vec_off])
                                 | ((uint64_t)atomic_load_uint32_relax(&func->msix[vec_off + 1]) << 32);
                rvvm_send_msi_irq(func->bus->machine, addr, data);
            } else {
                // Vector is masked, set pending bit
                atomic_or_uint32(&func->msix[mask_off], mask_bit);
            }
        } else if (unlikely(atomic_load_uint32_relax(&func->msix[mask_off]) & mask_bit)) {
            // Unset pending bit
            atomic_and_uint32(&func->msix[mask_off], ~mask_bit);
        }
        return true;
    }
    return false;
}

// Re-send pending MSI-X IRQs on MSI-X vector mask or function mask update
static void pci_func_update_msix(rvvm_pci_func_t* func)
{
    if (atomic_load_uint32_relax(&func->msix_ctl) == PCI_MSIX_ENABLED) {
        for (size_t reg = 0; reg < PCI_MSIX_PBA_SIZE; ++reg) {
            uint32_t irqs = atomic_load_uint32_relax(&func->msix[PCI_MSIX_TBL_SIZE + reg]);
            if (irqs) {
                irqs = atomic_swap_uint32(&func->msix[PCI_MSIX_TBL_SIZE + reg], 0);
            }
            while (irqs) {
                uint32_t bit = bit_ctz32(irqs);
                pci_func_set_msix(func, (reg << 5) | bit, true);
                irqs &= ~bit_set32(bit);
            }
        }
    }
}

// Read MSI-X table / pending bits
static void pci_msix_bar_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    rvvm_pci_func_t* func = rvvm_region_data(dev);
    UNUSED(size);
    write_uint32_le(data, atomic_load_uint32_relax(&func->msix[off >> 2]));
}

// Write MSI-X table / pending bits
static void pci_msix_bar_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    rvvm_pci_func_t* func = rvvm_region_data(dev);
    size_t           reg  = off >> 2;
    uint32_t         val  = read_uint32_le(data);
    UNUSED(size);
    if (reg < PCI_MSIX_TBL_SIZE && (reg & 3) == 3) {
        // MSI-X vector mask write
        uint32_t prev = atomic_load_uint32_relax(&func->msix[reg]);
        atomic_store_uint32_relax(&func->msix[reg], val & 1);
        if ((prev & ~val) & 1) {
            // MSI-X vector unmasked
            uint32_t vec      = reg >> 2;
            uint32_t mask_bit = bit_set32(vec);
            uint32_t mask_off = PCI_MSIX_TBL_SIZE + (vec >> 5);
            if ((atomic_load_uint32_relax(&func->msix[mask_off]) & mask_bit) && //
                (atomic_and_uint32(&func->msix[mask_off], ~mask_bit) & mask_bit)) {
                // Re-send previously pending MSI-X IRQ
                pci_func_set_msix(func, vec, true);
            }
        }
    } else {
        atomic_store_uint32_relax(&func->msix[reg], val);
    }
}

// MSI-X BAR region type
static const rvvm_reg_type_t pci_msix_bar_type = {
    .name     = "pci-msix",
    .read     = pci_msix_bar_read,
    .write    = pci_msix_bar_write,
    .min_size = 4,
    .max_size = 4,
};

/*
 * Configiration space handling
 */

// Read base PCI capabilities
static uint32_t pci_func_caps_base(rvvm_pci_func_t* func, size_t reg)
{
    if (reg >= PCI_CAP_OFFSET && func->addr) {
        size_t cap = (reg - PCI_CAP_OFFSET) >> 2;
        if (cap < STATIC_ARRAY_SIZE(pci_caps_base)) {
            return pci_caps_base[cap];
        }
    }
    return 0;
}

// Read PCI function BAR register, no bar_id checking
static uint32_t pci_func_bar_read(rvvm_pci_func_t* func, size_t bar_id)
{
    rvvm_reg_desc_t desc;
    if (rvvm_region_get_desc(func->bar[bar_id], &desc)) {
        if (desc.attr & RVVM_REG_ATTR_PIO) {
            // IO port BAR
            // TODO: Better IO window handling
            return (desc.addr - func->bus->io_base) | PCI_BAR_IO;
        } else {
            // MMIO BAR
            uint32_t ret = desc.addr;
            if ((desc.attr & RVVM_REG_ATTR_BAR64) && //
                (bar_id + 1) < PCI_FUNC_BARS && !func->bar[bar_id + 1]) {
                // This is a 64-bit BAR
                ret |= PCI_BAR_64BIT;
                if (desc.size >= PCI_MAX_NONPREFETCH_BAR) {
                    // This is a prefetchable BAR (GPU VRAM, etc)
                    ret |= PCI_BAR_PREFETCH;
                }
            }
            return ret;
        }
    } else if (bar_id && rvvm_region_get_desc(func->bar[bar_id - 1], &desc)) {
        if (desc.attr & RVVM_REG_ATTR_BAR64) {
            // This is an upper half of a 64-bit BAR
            return desc.addr >> 32;
        }
    }
    return 0;
}

// Write PCI function BAR register, no bar_id checking
static void pci_func_bar_write(rvvm_pci_func_t* func, size_t bar_id, uint32_t val)
{
    rvvm_reg_desc_t desc;
    if (rvvm_region_get_desc(func->bar[bar_id], &desc)) {
        // Replace lower 32 bits
        desc.addr = bit_replace64(desc.addr, 0, 32, val & ~0x0FUL);
    } else if (bar_id && rvvm_region_get_desc(func->bar[bar_id - 1], &desc) //
               && (desc.attr & RVVM_REG_ATTR_BAR64)) {
        // This is an upper half of a 64-bit BAR, replace upper 32 bits
        desc.addr = bit_replace64(desc.addr, 32, 32, val);
        bar_id    = bar_id - 1;
    } else {
        return;
    }
    if (desc.attr & RVVM_REG_ATTR_PIO) {
        // TODO: Better IO window handling
        desc.addr = (desc.addr & ~0x03U) + func->bus->io_base;
    } else {
        // Align MMIO BAR to page size
        desc.addr = desc.addr & ~0x0FFFULL;
    }
    // Align to BAR size (Must be power of 2) for proper BAR size probing
    desc.addr = desc.addr & ~(bit_next_pow2(desc.size) - 1);
    // Update BAR address
    rvvm_region_set_desc(func->bar[bar_id], &desc);
}

// Read PCI function configuration space
static uint32_t pci_func_cfg_read(rvvm_pci_func_t* func, size_t reg)
{
    uint32_t val = 0;
    if (!func) {
        // Non-existent devices have their config space filled with 0xFF
        return 0xFFFFFFFFUL;
    }
    val = pci_func_caps_base(func, reg);
    switch (reg) {
        case PCI_REG_DEVICE:
            return func->device_id;
        case PCI_REG_CMD: {
            uint32_t ret = atomic_load_uint32_relax(&func->command);
            if (!(ret & PCI_CMD_INTX_OFF)) {
                ret |= atomic_load_uint32_relax(&func->status);
            }
            if (func->addr) {
                // Skip capability list on the host bridge
                ret |= PCI_CMD_CAP;
            }
            return ret;
        }
        case PCI_REG_CLASS:
            return func->class_code;
        case PCI_REG_INFO:
            return func->info;
        case PCI_REG_INTERRUPT:
            return atomic_load_uint32_relax(&func->irq_line) | ((uint32_t)func->irq_pin) << 8;
        case PCI_REG_BAR0:
        case PCI_REG_BAR1:
            return pci_func_bar_read(func, (reg - PCI_REG_BAR0) >> 2);
        case PCI_REG_BAR2:
        case PCI_REG_BAR3:
        case PCI_REG_BAR4:
        case PCI_REG_BAR5:
            // Only BAR0 & BAR1 exist for a PCI-PCI bridge
            if (func->info & PCI_INFO_PCI_PCI) {
                // PCIe Root Ports within 00:XX.x cover every other bus in the system
                uint8_t secondary_bus = (func->addr >> 3) + (func->addr & 0x7);
                switch (reg) {
                    case PCI_REG_BAR2:
                        return (secondary_bus << 16) | (secondary_bus << 8);
                    case PCI_REG_BAR3:
                        return atomic_load_uint32_relax(&func->bridge_io);
                    case PCI_REG_BAR4:
                        return atomic_load_uint32_relax(&func->bridge_mem);
                }
            } else {
                return pci_func_bar_read(func, (reg - PCI_REG_BAR0) >> 2);
            }
            return 0;
        case PCI_REG_SUBSYSTEM:
            return func->subsystem;
        case PCI_REG_ROM: {
            rvvm_reg_desc_t desc;
            if (rvvm_region_get_desc(func->rom, &desc)) {
                return desc.addr | PCI_ROM_ENABLED;
            }
            return 0;
        }
        case PCI_REG_CAPABILITIES:
            if (func->addr) {
                return PCI_CAP_OFFSET;
            }
            return 0;

        // PCI Express Capability
        case PCI_REG_ECAP:
            if (func->info & PCI_INFO_PCI_PCI) {
                // This is a PCI-PCI bridge (PCI Express Root Port)
                val |= PCI_ECAP_ROOT_PORT;
            } else if (!(func->addr >> 8)) {
                // This is an integrated endpoint on bus 00
                val |= PCI_ECAP_INTEGRATED;
            }
            return val;

        // Power Management Capability
        case PCI_REG_PMCSR:
            return val | atomic_load_uint32_relax(&func->pm_csr);

        case PCI_REG_MSI:
            val |= atomic_load_uint32_relax(&func->msi_ctl);
            if (!func->msix_bar) {
                // Hide MSI-X capability if MSI-X BAR is missing
                val &= ~(0xFF00U);
            }
            return val;

        // MSI Capability
        case PCI_REG_MSI_AL:
            return atomic_load_uint32_relax(&func->msi_addr_low);
        case PCI_REG_MSI_AH:
            return atomic_load_uint32_relax(&func->msi_addr_high);
        case PCI_REG_MSI_DATA:
            return atomic_load_uint32_relax(&func->msi_data);
        case PCI_REG_MSI_MASK:
            return atomic_load_uint32_relax(&func->msi_mask);
        case PCI_REG_MSI_PEND:
            return atomic_load_uint32_relax(&func->msi_pending);

        // MSI-X Capability
        case PCI_REG_MSIX:
            return val | atomic_load_uint32_relax(&func->msix_ctl);
        case PCI_REG_MSIX_TBL:
            return func->msix_bar;
        case PCI_REG_MSIX_PBO:
            return func->msix_bar | PCI_MSIX_TBL_SIZE;
    }
    return val;
}

// Write PCI function configuration space
static void pci_func_cfg_write(rvvm_pci_func_t* func, size_t reg, uint32_t val)
{
    if (!func) {
        return;
    }
    switch (reg) {
        case PCI_REG_CMD: {
            uint32_t prev = atomic_load_uint32_relax(&func->command);
            atomic_store_uint32_relax(&func->command, val & PCI_CMD_VALID);
            if ((val & ~prev) & PCI_CMD_INTX_OFF) {
                // Disabled INTx interrupt
                rvvm_irq_lower(func->bus->irq_dev, pci_func_intx_irq(func));
            } else if ((prev & ~val) & PCI_CMD_INTX_OFF) {
                // Enabled INTx interrupt
                if (atomic_load_uint32_relax(&func->status) & PCI_CMD_INTX) {
                    // Re-deliver INTx interrupt
                    rvvm_irq_raise(func->bus->irq_dev, pci_func_intx_irq(func));
                }
            }
            return;
        }
        case PCI_REG_BAR0:
        case PCI_REG_BAR1:
            pci_func_bar_write(func, (reg - PCI_REG_BAR0) >> 2, val);
            return;
        case PCI_REG_BAR2:
        case PCI_REG_BAR3:
        case PCI_REG_BAR4:
        case PCI_REG_BAR5:
            // Only BAR0 & BAR1 exist for a PCI-PCI bridge
            if (func->info & PCI_INFO_PCI_PCI) {
                switch (reg) {
                    case PCI_REG_BAR3:
                        atomic_store_uint32_relax(&func->bridge_io, val);
                        return;
                    case PCI_REG_BAR4:
                        atomic_store_uint32_relax(&func->bridge_mem, val);
                        return;
                }
            } else {
                pci_func_bar_write(func, (reg - PCI_REG_BAR0) >> 2, val);
            }
            return;
        case PCI_REG_ROM: {
            rvvm_reg_desc_t desc;
            if (rvvm_region_get_desc(func->rom, &desc)) {
                desc.addr = val & ~(bit_next_pow2(EVAL_MAX(desc.size, 0x1000)) - 1);
                rvvm_region_set_desc(func->rom, &desc);
            }
            return;
        }
        case PCI_REG_INTERRUPT:
            atomic_store_uint32_relax(&func->irq_line, (uint8_t)val);
            return;

        // Power Management Capability
        case PCI_REG_PMCSR:
            atomic_store_uint32_relax(&func->pm_csr, val & PCI_PMCSR_STATE);
            return;

        // MSI Capability
        case PCI_REG_MSI:
            atomic_store_uint32_relax(&func->msi_ctl, val & PCI_MSI_ENABLED);
            return;
        case PCI_REG_MSI_AL:
            atomic_store_uint32_relax(&func->msi_addr_low, val);
            return;
        case PCI_REG_MSI_AH:
            atomic_store_uint32_relax(&func->msi_addr_high, val);
            return;
        case PCI_REG_MSI_DATA:
            atomic_store_uint32_relax(&func->msi_data, val & PCI_MSI_DATA);
            return;
        case PCI_REG_MSI_MASK:
            atomic_store_uint32_relax(&func->msi_mask, val & PCI_MSI_BIT);
            if (atomic_load_uint32_relax(&func->msi_pending) && atomic_and_uint32(&func->msi_pending, val)) {
                // Re-deliver unmasked MSI IRQ
                pci_func_set_msi(func, true);
            }
            return;

        // MSI-X Capability
        case PCI_REG_MSIX: {
            uint32_t prev = atomic_load_uint32_relax(&func->msix_ctl);
            atomic_store_uint32_relax(&func->msix_ctl, val & PCI_MSIX_VALID);
            if ((prev & ~val) & PCI_MSIX_MASKED) {
                // Globally unmasked MSI-X
                pci_func_update_msix(func);
            }
            return;
        }
    }
}

/*
 * PCI Function enumeration and initialization
 */

// Convert PCI address to internal device vector index
static inline size_t pci_bus_addr_to_idx(rvvm_pci_addr_t addr)
{
    addr >>= 3;
    return (addr < PCI_BUS_DEVS) ? addr : ((addr >> 5) + PCI_BUS_DEVS);
}

// Convert PCI device address to internal device vector index
static inline rvvm_pci_addr_t pci_bus_idx_to_addr(size_t idx)
{
    return (idx < PCI_BUS_DEVS) ? (idx << 3) : ((idx - PCI_BUS_DEVS) << 8);
}

// Check PCI address validity
static inline bool pci_bus_addr_valid(rvvm_pci_addr_t addr)
{
    addr >>= 3;
    return (addr < PCI_BUS_DEVS) || !(addr & (PCI_BUS_DEVS - 1));
}

// Free a PCI function
static void pci_func_free(rvvm_pci_func_t* func)
{
    if (func) {
        for (size_t bar_id = 0; bar_id < PCI_FUNC_BARS; ++bar_id) {
            rvvm_region_remove(func->bar[bar_id]);
        }
        rvvm_region_remove(func->rom);
        free(func);
    }
}

// Assign PCI function to bus address
static bool pci_func_assign(rvvm_pci_func_t* func, rvvm_pci_addr_t addr)
{
    if (func->bus) {
        rvvm_pci_bus_t* bus = func->bus;
        rvvm_pci_dev_t* dev = NULL;

        // Lock and pause the vCPUs
        rvvm_lock(&bus->lock);
        bool start = rvvm_pause_machine(bus->machine);
        if (!pci_bus_addr_valid(addr)) {
            // Pick lowest usable bus address
            for (size_t i = 0; i < 0x100; ++i) {
                if (i >= vector_size(bus->dev) || !vector_at(bus->dev, i)) {
                    addr = pci_bus_idx_to_addr(i);
                    break;
                }
            }
        }
        if (pci_bus_addr_valid(addr)) {
            // Assign function
            size_t idx   = pci_bus_addr_to_idx(addr);
            size_t fn_id = addr & 0x07;
            func->addr   = addr;
            if (idx < vector_size(bus->dev)) {
                // Device already exists
                dev = vector_at(bus->dev, idx);
            }
            if (!dev) {
                // Allocate new device
                dev = safe_new_obj(rvvm_pci_dev_t);
                vector_put(bus->dev, idx, dev);
            }
            if (!dev->func[fn_id]) {
                // Function address available
                dev->func[fn_id] = func;
                if (fn_id) {
                    // Mark a multi-function device
                    for (size_t i = 0; i < PCI_DEV_FUNCS; ++i) {
                        if (dev->func[i]) {
                            dev->func[i]->info |= PCI_INFO_MULTIFUNC;
                        }
                    }
                }
            } else {
                // Mark failure
                dev = NULL;
            }
        }
        if (start) {
            rvvm_start_machine(bus->machine);
        }
        rvvm_unlock(&bus->lock);
        return !!dev;
    }
    return false;
}

/*
 * Public interfaces
 */

RVVM_PUBLIC rvvm_pci_func_t* rvvm_pci_func_init(rvvm_machine_t*             machine, /**/
                                                const rvvm_pci_func_desc_t* desc,    /**/
                                                rvvm_pci_addr_t             addr)
{
    rvvm_pci_func_t* func = safe_new_obj(rvvm_pci_func_t);

    // If func->bus == NULL, the attach will fail and invoke cleanup
    func->bus = rvvm_get_pci_bus(machine);

    // Fill function description
    func->device_id  = desc->vendor_id //
                     | (((uint32_t)desc->device_id) << 16);
    func->subsystem  = desc->subsys_ven //
                     | (((uint32_t)desc->subsys_dev) << 16);
    func->class_code = desc->revision                      //
                     | (((uint32_t)desc->prog_iface) << 8) //
                     | (((uint32_t)desc->class_code) << 16);
    // Advertise 64-byte cacheline
    func->info = 16;
    if ((func->class_code >> 16) == 0x0604) {
        // This is a PCI-PCI bridge
        func->info |= PCI_INFO_PCI_PCI;
    }
    if (!func->subsystem) {
        // Easter egg
        func->subsystem = 0x510010DCUL;
    }

    // Fill registers
    func->command = PCI_CMD_IO | PCI_CMD_MEM | PCI_CMD_BUS_MASTER;
    func->irq_pin = desc->irq_pin;
    if (func->bus) {
        func->irq_line = pci_func_intx_irq(func);
    }

    // Attach function BAR regions
    for (size_t bar_id = 0; bar_id < PCI_FUNC_BARS; ++bar_id) {
        if (desc->bar[bar_id]) {
            rvvm_reg_desc_t bar = *desc->bar[bar_id];
            if (!(bar.attr & RVVM_REG_ATTR_PIO) && func->bus) {
                bar.addr = func->bus->mem_base;
            }
            func->bar[bar_id] = rvvm_region_init(machine, &bar);
            if (func->bar[bar_id] == NULL) {
                // Failed to attach function BAR, mark failure
                func->bus = NULL;
            }
        } else if (desc->irq_vecs > 1 && bar_id >= 4 && !func->msix_bar) {
            // Try to attach MSI-X BAR (Unless it's a host bridge)
            rvvm_reg_desc_t msix_bar = {
                .size = sizeof(func->msix),
                .data = func,
                .type = &pci_msix_bar_type,
            };
            func->bar[bar_id] = rvvm_region_init(machine, &msix_bar);
            if (func->bar[bar_id]) {
                // Successfully attached MSI-X BAR
                func->msix_bar = bar_id;
            }
        }
    }

    // Attach function expansion ROM region
    if (desc->rom) {
        func->rom = rvvm_region_init(machine, desc->rom);
        if (func->rom == NULL) {
            // Failed to attach function ROM, mark failure
            func->bus = NULL;
        }
    }

    if (!pci_func_assign(func, addr)) {
        // Failed to assign function on the bus, mark failure
        func->bus = NULL;
    }
    if (!func->bus) {
        // Failed to attach function, clean up
        pci_func_free(func);
        return NULL;
    }

    return func;
}

RVVM_PUBLIC void rvvm_pci_func_remove(rvvm_pci_func_t* func)
{
    if (func && func->bus) {
        rvvm_pci_bus_t* bus = func->bus;
        size_t          idx = pci_bus_addr_to_idx(func->addr);
        // Lock and pause the vCPUs
        rvvm_scoped_lock (&bus->lock) {
            bool start = rvvm_pause_machine(bus->machine);
            // Unmap function
            vector_at(bus->dev, idx)->func[func->addr & 0x07] = NULL;
            if (start) {
                rvvm_start_machine(bus->machine);
            }
        }
        pci_func_free(func);
    }
}

RVVM_PUBLIC rvvm_pci_func_t* rvvm_pci_func_from_addr(rvvm_machine_t* machine, rvvm_pci_addr_t addr)
{
    rvvm_pci_func_t* func = NULL;
    if (machine && machine->pci_bus && pci_bus_addr_valid(addr)) {
        rvvm_pci_bus_t* bus = machine->pci_bus;
        size_t          idx = pci_bus_addr_to_idx(addr);
        rvvm_scoped_lock (&bus->lock) {
            if (idx < vector_size(bus->dev)) {
                func = vector_at(bus->dev, idx)->func[addr & 0x07];
            }
        }
    }
    return func;
}

RVVM_PUBLIC rvvm_pci_addr_t rvvm_pci_addr_from_func(rvvm_pci_func_t* func)
{
    return func ? func->addr : 0;
}

RVVM_PUBLIC void rvvm_pci_set_irq(rvvm_pci_func_t* func, uint32_t vec, bool lvl)
{
    if (likely(func && (atomic_load_uint32_relax(&func->command) & PCI_CMD_BUS_MASTER))) {
        // Try to set MSI/MSI-X IRQ level
        if (!pci_func_set_msix(func, vec, lvl) && !pci_func_set_msi(func, lvl)) {
            // Set INTx IRQ level
            pci_func_set_intx(func, lvl);
        }
    }
}

RVVM_PUBLIC void* rvvm_pci_get_dma_ex(rvvm_pci_func_t* func, rvvm_addr_t addr, size_t* size, uint32_t attr)
{
    // TODO: DMA refcounting and attributes
    UNUSED(attr);
    if (likely(func && size && (atomic_load_uint32_relax(&func->command) & PCI_CMD_BUS_MASTER))) {
        return rvvm_get_dma_ptr(func->bus->machine, addr, *size);
    }
    return NULL;
}

RVVM_PUBLIC void rvvm_pci_end_dma(rvvm_pci_func_t* func, void* ptr)
{
    // TODO: DMA refcounting and attributes
    UNUSED(func && ptr);
}

/*
 * PCIe ECAM
 */

static rvvm_pci_func_t* pci_ecam_get_func(rvvm_reg_dev_t* ecam, size_t off)
{
    rvvm_pci_bus_t*  bus  = rvvm_region_data(ecam);
    rvvm_pci_addr_t  addr = off >> 12;
    size_t           idx  = pci_bus_addr_to_idx(addr);
    rvvm_pci_func_t* func = NULL;
    if (idx < vector_size(bus->dev)) {
        rvvm_pci_dev_t* dev = vector_at(bus->dev, idx);
        if (dev) {
            func = dev->func[addr & 0x07];
        }
    }
    return func;
}

static void pci_ecam_read(rvvm_reg_dev_t* ecam, void* data, size_t size, size_t off)
{
    UNUSED(size);
    write_uint32_le(data, pci_func_cfg_read(pci_ecam_get_func(ecam, off), off & 0xFFC));
}

static void pci_ecam_write(rvvm_reg_dev_t* ecam, const void* data, size_t size, size_t off)
{
    UNUSED(size);
    pci_func_cfg_write(pci_ecam_get_func(ecam, off), off & 0xFFC, read_uint32_le(data));
}

static void pci_ecam_cleanup(rvvm_reg_dev_t* ecam)
{
    rvvm_pci_bus_t* bus = rvvm_region_data(ecam);
    vector_foreach (bus->dev, i) {
        rvvm_pci_dev_t* dev = vector_at(bus->dev, i);
        for (size_t fn_id = 0; fn_id < PCI_DEV_FUNCS; ++fn_id) {
            // Must not call rvvm_pci_func_remove(), regions are
            // already removed by RVVM core and this would double-free
            free(dev->func[fn_id]);
        }
        free(dev);
    }
    vector_free(bus->dev);
    free(bus);
}

static void pci_func_suspend(rvvm_snapshot_t* snap, rvvm_pci_func_t* func)
{
    rvvm_snapshot_field(snap, func->command);
    rvvm_snapshot_field(snap, func->status);
    rvvm_snapshot_field(snap, func->irq_line);
    rvvm_snapshot_field(snap, func->bridge_io);
    rvvm_snapshot_field(snap, func->bridge_mem);
    rvvm_snapshot_field(snap, func->pm_csr);

    rvvm_snapshot_field(snap, func->msi_ctl);
    rvvm_snapshot_field(snap, func->msi_addr_low);
    rvvm_snapshot_field(snap, func->msi_addr_high);
    rvvm_snapshot_field(snap, func->msi_data);
    rvvm_snapshot_field(snap, func->msi_mask);
    rvvm_snapshot_field(snap, func->msi_pending);

    rvvm_snapshot_field(snap, func->msix_ctl);
    rvvm_snapshot_field(snap, func->msix_bar);
    for (size_t i = 0; i < PCI_MSIX_BAR_SIZE; ++i) {
        rvvm_snapshot_field(snap, func->msix[i]);
    }

    // Where the guest mapped each BAR, which the regions are placed at on resume
    for (size_t bar_id = 0; bar_id < PCI_FUNC_BARS; ++bar_id) {
        rvvm_reg_desc_t desc = ZERO_INIT;
        uint64_t        addr = 0;
        bool            bar  = rvvm_region_get_desc(func->bar[bar_id], &desc);
        if (bar) {
            addr = desc.addr;
        }
        rvvm_snapshot_field(snap, addr);
        if (bar && !rvvm_snapshot_writing(snap) && desc.addr != addr) {
            desc.addr = addr;
            rvvm_region_set_desc(func->bar[bar_id], &desc);
        }
    }
}

static void pci_ecam_suspend(rvvm_reg_dev_t* ecam, rvvm_snapshot_t* snap, bool resume)
{
    if (snap) {
        rvvm_pci_bus_t* bus = rvvm_region_data(ecam);
        rvvm_snapshot_section(snap, "pci-ecam");
        vector_foreach (bus->dev, i) {
            rvvm_pci_dev_t* dev = vector_at(bus->dev, i);
            for (size_t fn_id = 0; fn_id < PCI_DEV_FUNCS; ++fn_id) {
                if (dev->func[fn_id]) {
                    pci_func_suspend(snap, dev->func[fn_id]);
                }
            }
        }
    }
    UNUSED(resume);
}

static const rvvm_reg_type_t pci_ecam_type = {
    .name     = "pci-ecam",
    .read     = pci_ecam_read,
    .write    = pci_ecam_write,
    .suspend  = pci_ecam_suspend,
    .cleanup  = pci_ecam_cleanup,
    .min_size = 4,
    .max_size = 4,
};

RVVM_PUBLIC bool rvvm_pci_ecam_init(rvvm_machine_t*   machine,  /**/
                                    uint32_t          domain,   /**/
                                    rvvm_addr_t       addr,     /**/
                                    rvvm_irq_dev_t*   irq_dev,  /**/
                                    const rvvm_irq_t* irqs,     /**/
                                    rvvm_addr_t       io_base,  /**/
                                    rvvm_addr_t       mem_base, /**/
                                    rvvm_addr_t       mem_size)
{
    rvvm_pci_bus_t* bus = safe_new_obj(rvvm_pci_bus_t);
    UNUSED(domain);

    // Assign wired interrupts
    if (!irq_dev) {
        irq_dev = rvvm_get_intc(machine);
    }
    for (size_t i = 0; i < PCI_BUS_IRQS; ++i) {
        bus->irqs[i] = rvvm_irq_alloc(irq_dev, irqs ? irqs[i] : 0);
    }

    bus->machine  = machine;
    bus->irq_dev  = irq_dev;
    bus->io_base  = io_base;
    bus->mem_base = mem_base;
    bus->mem_size = mem_size;

    rvvm_reg_desc_t pci_ecam_desc = {
        .addr = addr,
        .size = 0x10000000,
        .data = bus,
        .type = &pci_ecam_type,
    };

    if (!rvvm_region_init(machine, &pci_ecam_desc)) {
        // Failed to attach PCI ECAM
        return NULL;
    }

    // Register PCI bus
    rvvm_set_pci_bus(machine, bus);

    // Host Bridge: SiFive, Inc. FU740-C000 RISC-V SoC PCI Express x8
    rvvm_pci_func_desc_t bridge_desc = {
        .vendor_id  = 0xF15E,
        .class_code = 0x0600,
    };
    rvvm_pci_func_init(machine, &bridge_desc, 0);

    if (rvvm_has_arg("pcie_ports")) {
        // Root Ports
        rvvm_pci_func_desc_t root_port_desc = {
            .vendor_id  = 0x1556,
            .device_id  = 0xBE00,
            .class_code = 0x0604,
            .irq_pin    = RVVM_PCI_PIN_INTA,
        };
        for (rvvm_pci_addr_t pci_addr = 1; pci_addr < 4; ++pci_addr) {
            rvvm_pci_func_init(machine, &root_port_desc, pci_addr);
        }
    }

    rvvm_fdt_node_t* soc = rvvm_get_fdt_soc(machine);
    if (soc) {
        rvvm_fdt_node_t* imsic_fdt = rvvm_fdt_find_reg_any(soc, "imsics_s");
        rvvm_fdt_node_t* pci_fdt   = rvvm_fdt_init_reg("pci", pci_ecam_desc.addr);
        rvvm_fdt_prop_set_reg(pci_fdt, "reg", pci_ecam_desc.addr, pci_ecam_desc.size);
        rvvm_fdt_prop_set_str(pci_fdt, "compatible", "pci-host-ecam-generic");
        rvvm_fdt_prop_set_str(pci_fdt, "device_type", "pci");
        rvvm_fdt_prop_set_flag(pci_fdt, "dma-coherent");

        if (imsic_fdt) {
            rvvm_fdt_prop_set_u32(pci_fdt, "msi-parent", rvvm_fdt_phandle(imsic_fdt));
        }

        uint32_t bus_range[] = {0x00, 0xFF};
        rvvm_fdt_prop_set_cells(pci_fdt, "bus-range", bus_range, STATIC_ARRAY_SIZE(bus_range));

        rvvm_fdt_prop_set_u32(pci_fdt, "#address-cells", 3);
        rvvm_fdt_prop_set_u32(pci_fdt, "#size-cells", 2);
        rvvm_fdt_prop_set_u32(pci_fdt, "#interrupt-cells", 1);

#define FDT_ADDR(addr) (((uint64_t)(addr)) >> 32), ((uint32_t)(addr))

        // clang-format off
        uint32_t ranges[] = {
            // IO Window
            0x01000000UL, FDT_ADDR(0x00000000UL),    FDT_ADDR(io_base),         FDT_ADDR(0x10000UL),
            // Non-prefetchable MMIO32 Window
            0x02000000UL, FDT_ADDR(mem_base),        FDT_ADDR(mem_base),        FDT_ADDR(mem_size),
            // Prefetchable MMIO64 Window (0x4000000000...0x7FFFFFFFFF -> 0x4000000000)
            0x43000000UL, FDT_ADDR(0x4000000000ULL), FDT_ADDR(0x4000000000ULL), FDT_ADDR(0x4000000000ULL),
        };
        // clang-format on

        rvvm_fdt_prop_set_cells(pci_fdt, "ranges", ranges, STATIC_ARRAY_SIZE(ranges));

        // Crossing-style IRQ routing for IRQ balancing
        // INTA of dev 2 routes the same way as INTB of dev 1, etc
        uint32_t           intc_handle = rvvm_irq_fdt_phandle(irq_dev);
        vector_t(uint32_t) irq_map     = ZERO_INIT;

        for (uint32_t dev_id = 0; dev_id < PCI_BUS_IRQS; ++dev_id) {
            for (uint32_t irq_pin = 1; irq_pin <= PCI_BUS_IRQS; ++irq_pin) {
                rvvm_irq_t irq      = pci_bus_intx_irq(bus, dev_id, irq_pin);
                uint32_t   cells[8] = ZERO_INIT;
                size_t     count    = rvvm_irq_fdt_cells(irq_dev, irq, cells, STATIC_ARRAY_SIZE(cells));

                // PCI address
                vector_push_back(irq_map, dev_id << 11);
                vector_push_back(irq_map, 0);
                vector_push_back(irq_map, 0);

                // PCI irq pin
                vector_push_back(irq_map, irq_pin);

                // Interrupt controller handle
                vector_push_back(irq_map, intc_handle);

                // Interrupt cells
                for (size_t cell = 0; cell < count; ++cell) {
                    vector_push_back(irq_map, cells[cell]);
                }
            }
        }

        rvvm_fdt_prop_set_cells(pci_fdt, "interrupt-map", vector_buffer(irq_map), vector_size(irq_map));
        vector_free(irq_map);

        uint32_t irq_mask[] = {0x1800, 0x0000, 0x0000, 0x0007};
        rvvm_fdt_prop_set_cells(pci_fdt, "interrupt-map-mask", irq_mask, STATIC_ARRAY_SIZE(irq_mask));

        rvvm_fdt_attach(soc, pci_fdt);
    }
    return true;
}

POP_OPTIMIZATION_SIZE
