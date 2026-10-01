/*
rvvm_pci.h - RVVM PCI Bus API
Copyright (C) 2020-2026 LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _RVVM_PCI_API_H
#define _RVVM_PCI_API_H

#include <rvvm/rvvm_region.h>

RVVM_EXTERN_C_BEGIN

/**
 * @defgroup rvvm_pci_api PCI Bus API
 * @addtogroup rvvm_pci_api
 * @{
 */

/**
 * @name PCI Function INTx pins
 * Passed to `rvvm_pci_func_desc_t::irq_pin`
 * @{
 */
#define RVVM_PCI_PIN_NONE    0x00
#define RVVM_PCI_PIN_INTA    0x01
#define RVVM_PCI_PIN_INTB    0x02
#define RVVM_PCI_PIN_INTC    0x03
#define RVVM_PCI_PIN_INTD    0x04
/** @} */

/**
 * @name PCI Function attributes
 * Passed to `rvvm_pci_func_desc_t::attr`
 * @{
 */
#define RVVM_PCI_ATTR_LEGACY 0x0001 /**< Legacy PCI endpoint */
/** @} */

/**
 * @name PCI DMA attributes
 * Passed to `rvvm_pci_get_dma_ex()`
 * @{
 */
#define RVVM_PCI_DMA_RD      0x01 /**< Read via DMA mapping       */
#define RVVM_PCI_DMA_WR      0x02 /**< Write via DMA mapping      */
#define RVVM_PCI_DMA_RW      0x03 /**< Read/Write via DMA mapping */
#define RVVM_PCI_DMA_PART    0x04 /**< Allow partial DMA mapping  */
/** @} */

/**
 * @name PCI Bus addresses
 * Passed to `rvvm_pci_func_init()`
 * @{
 */
#define RVVM_PCI_ADDR_ANY    0xFFFFFFFFUL /**< Pick any bus address           */
#define RVVM_PCI_ADDR_HOT    0xFFFFFFFEUL /**< Pick hot-pluggable bus address */

/** @} */

/**
 * PCI function description
 *
 * Copied by value by `rvvm_pci_func_init()`
 */
typedef struct {
    /**
     * Vendor ID from PCI database
     */
    uint16_t vendor_id;

    /**
     * Device ID from PCI database
     */
    uint16_t device_id;

    /**
     * Subsystem vendor ID (May be zero)
     */
    uint16_t subsys_ven;

    /**
     * Subsystem device ID (May be zero)
     */
    uint16_t subsys_dev;

    /**
     * Class code, Subclass
     */
    uint16_t class_code;

    /**
     * Programming interface
     */
    uint8_t prog_iface;

    /**
     * Revision
     */
    uint8_t revision;

    /**
     * INTx interrupt pin (RVVM_PCI_PIN_*)
     */
    uint8_t irq_pin;

    /**
     * Number of interrupt vectors
     */
    uint8_t irq_vecs;

    /**
     * Attributes (RVVM_PCI_ATTR_*)
     */
    uint16_t attr;

    /**
     * BAR region descriptions (Nullable)
     */
    const rvvm_reg_desc_t* bar[6];

    /**
     * Expansion ROM region (Nullable)
     */
    const rvvm_reg_desc_t* rom;

    /**
     * Head of additional PCI capability list (Nullable)
     *
     * This list starts at 0xC0 in PCI configuration space
     */
    const rvvm_reg_desc_t* cap;

    /**
     * Head of additional PCIe extended capability list (Nullable)
     *
     * This list starts at 0x100 in PCI Express configuration space
     */
    const rvvm_reg_desc_t* ecap;

} rvvm_pci_func_desc_t;

/**
 * Attach PCI function to machine at specific bus address
 *
 * Function unconditionally transfers ownership.
 * In case of a failed attach, the function description is cleaned up.
 *
 * \param machine Machine handle (Nullable, guarantees description cleanup)
 * \param desc    PCI function description
 * \param addr    PCI bus address
 * \return        PCI function handle (`NULL` on failure)
 *
 * This function is thread-safe
 */
RVVM_PUBLIC rvvm_pci_func_t* rvvm_pci_func_init(rvvm_machine_t*             machine, /**/
                                                const rvvm_pci_func_desc_t* desc,    /**/
                                                rvvm_pci_addr_t             addr);

/**
 * Remove PCI function from machine
 *
 * This should only be used for device hot-removal.
 * Cleanup on machine destruction is automatic.
 *
 * \param func PCI function handle (Nullable)
 * \note       Must not be called after `rvvm_machine_free()` on owning machine,
 *             nor from device's own callbacks or internal threads
 *
 * This function is thread-safe
 */
RVVM_PUBLIC void rvvm_pci_func_remove(rvvm_pci_func_t* func);

/**
 * Get PCI function handle from bus address
 *
 * \param machine Machine handle (Nullable)
 * \param addr    PCI bus address
 * \return        PCI function handle (`NULL` on failure)
 *
 * This function is thread-safe
 */
RVVM_PUBLIC rvvm_pci_func_t* rvvm_pci_func_from_addr(rvvm_machine_t* machine, rvvm_pci_addr_t addr);

/**
 * Get bus address of a PCI function
 *
 * \param func PCI function handle (Nullable)
 * \return     PCI bus address (`RVVM_PCI_ADDR_ANY` on failure)
 *
 * This function is thread-safe
 */
RVVM_PUBLIC rvvm_pci_addr_t rvvm_pci_addr_from_func(rvvm_pci_func_t* func);

/**
 * Set interrupt level of a PCI function
 *
 * \param func PCI function handle (Nullable)
 * \param vec  Interrupt vector index
 * \param lvl  Interrupt line level
 *
 * The interrupt is delivered via INTx, MSI, or MSI-X according to
 * the current PCI configuration set for this function by the guest.
 *
 * This function is thread-safe
 */
RVVM_PUBLIC void rvvm_pci_set_irq(rvvm_pci_func_t* func, uint32_t vec, bool lvl);

/**
 * Raise interrupt vector of a PCI function
 *
 * \param func PCI function handle (Nullable)
 * \param vec  Interrupt vector index
 *
 * This function is thread-safe
 */
static inline void rvvm_pci_raise_irq(rvvm_pci_func_t* func, uint32_t vec)
{
    rvvm_pci_set_irq(func, vec, true);
}

/**
 * Lower interrupt vector of a PCI function
 *
 * \param func PCI function handle (Nullable)
 * \param vec  Interrupt vector index
 *
 * This function is thread-safe
 */
static inline void rvvm_pci_lower_irq(rvvm_pci_func_t* func, uint32_t vec)
{
    rvvm_pci_set_irq(func, vec, false);
}

/**
 * Send interrupt edge (pulse) from a PCI function
 *
 * \param func PCI function handle (Nullable)
 * \param vec  Interrupt vector index
 *
 * Should only be used for devices which cannot explicitly report
 * interrupt vector deassertion (for example VFIO devices).
 *
 * This function is thread-safe
 */
static inline void rvvm_pci_send_irq(rvvm_pci_func_t* func, uint32_t vec)
{
    rvvm_pci_set_irq(func, vec, true);
    rvvm_pci_set_irq(func, vec, false);
}

/**
 * Obtain a direct mapping into guest physical memory
 *
 * If `RVVM_PCI_DMA_PART` is set in `flags`, returned mapping may be smaller
 * than requested, which will be reflected in `*size`, otherwise this function
 * may fall back to IOMMU bounce buffer.
 *
 * The `RVVM_PCI_DMA_RD` / `RVVM_PCI_DMA_WR` specify synchronization semantics,
 * as well as optimize redundant IOMMU bounce buffer copies.
 *
 * For write-only mappings, the caller is expected to fully populate the mapping.
 *
 * The region backing the DMA mapping becomes locked against removal for the
 * mapping's lifetime.
 *
 * The returned mapping remains valid until `rvvm_pci_end_dma()` is called on it.
 *
 * \param func PCI function handle (Nullable)
 * \param addr Physical memory address
 * \param size Requested mapping size (Returns resulting mapping size)
 * \param attr DMA operation attributes (Read/Write/Partial)
 * \return     Pointer to DMA region (`NULL` on failure)
 * \note       DMA access must be ended via `rvvm_pci_end_dma()`
 *
 * This function is thread-safe
 */
RVVM_PUBLIC void* rvvm_pci_get_dma_ex(rvvm_pci_func_t* func, rvvm_addr_t addr, size_t* size, uint32_t attr);

/**
 * Obtain a direct RW mapping into guest physical memory of exact size
 *
 * The region backing the DMA mapping will become locked against removal,
 * the DMA core internally maintains reference counting on DMA mappings.
 *
 * The returned mapping remains valid until `rvvm_pci_end_dma()` is called on it.
 *
 * \param func PCI function handle (Nullable)
 * \param addr Physical memory address
 * \param size Requested mapping size
 * \return     Pointer to DMA region (`NULL` on failure)
 * \note       DMA access must be ended via `rvvm_pci_end_dma()`
 *
 * This function is thread-safe
 */
static inline void* rvvm_pci_get_dma(rvvm_pci_func_t* func, rvvm_addr_t addr, size_t size)
{
    return rvvm_pci_get_dma_ex(func, addr, &size, RVVM_PCI_DMA_RW);
}

/**
 * End direct memory access
 *
 * Must be called for every successful `rvvm_pci_get_dma()`/`rvvm_pci_get_dma_ex()`
 * after caller has finished using the obtained mapping.
 *
 * \param func PCI function handle (Nullable)
 * \param ptr  Pointer to DMA mapping (Nullable)
 *
 * This function is thread-safe
 */
RVVM_PUBLIC void rvvm_pci_end_dma(rvvm_pci_func_t* func, void* ptr);

/**
 * Copy data from guest physical memory
 *
 * This should be preferred over `rvvm_pci_get_dma()` + `memcpy()` + `rvvm_pci_end_dma()`
 * for small data chunks, or in cases where an extra copy via `memcpy()` can't be avoided.
 *
 * \param func PCI function handle (Nullable)
 * \param addr Physical memory address
 * \param data Buffer to copy into
 * \param size Buffer size
 * \return     Success
 *
 * This function is thread-safe
 */
RVVM_PUBLIC bool rvvm_pci_read_dma(rvvm_pci_func_t* func, rvvm_addr_t addr, void* data, size_t size);

/**
 * Copy data into guest physical memory
 *
 * This should be preferred over `rvvm_pci_get_dma()` + `memcpy()` + `rvvm_pci_end_dma()`
 * for small data chunks, or in cases where an extra copy via `memcpy()` can't be avoided.
 *
 * \param func PCI function handle (Nullable)
 * \param addr Physical memory address
 * \param data Buffer to copy from
 * \param size Buffer size
 * \return     Success
 *
 * This function is thread-safe
 */
RVVM_PUBLIC bool rvvm_pci_write_dma(rvvm_pci_func_t* func, rvvm_addr_t addr, const void* data, size_t size);

/** @}*/

RVVM_EXTERN_C_END

#endif
