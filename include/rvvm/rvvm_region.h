/*
<rvvm/rvvm_region.h> - RVVM Region-based devices API
Copyright (C) 2020-2026 LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _RVVM_REGION_API_H
#define _RVVM_REGION_API_H

#include <rvvm/rvvm_base.h>

RVVM_EXTERN_C_BEGIN

/**
 * @defgroup rvvm_reg_dev Region-based devices API
 * @addtogroup rvvm_reg_dev
 * @{
 */

/**
 * @name Region attributes
 * Passed to `rvvm_reg_desc_t::attr`
 * @{
 */
#define RVVM_REG_ATTR_RSV   0x0001 /**< Region is reserved and ignored    */
#define RVVM_REG_ATTR_ROM   0x0002 /**< Region ignores writes             */
#define RVVM_REG_ATTR_FIX   0x0004 /**< Region address is fixed           */
#define RVVM_REG_ATTR_PIO   0x0008 /**< Region accessed via Port IO (x86) */
#define RVVM_REG_ATTR_BAR64 0x0100 /**< Region is a 64-bit BAR (PCI)      */

/** @} */

/**
 * Region-based device callbacks
 *
 * Must remain valid during device lifetime.
 *
 * Callbacks are optional (Nullable)
 */
typedef struct {
    /**
     * Device identifier string
     *
     * Used in logger, snapshot and registry.
     */
    const char* name;

    /**
     * Region read callback
     *
     * \param dev  Region device handle
     * \param data Pointer to data to be read
     * \param size Access size
     * \param off  Offset within region (Always aligned to size)
     *
     * This function may be called concurrently on same region
     */
    void (*read)(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off);

    /**
     * Region write callback
     *
     * \param dev  Region device handle
     * \param data Pointer to written data
     * \param size Access size
     * \param off  Offset within region (Always aligned to size)
     *
     * This function may be called concurrently on same region
     */
    void (*write)(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off);

    /**
     * Periodic poll callback
     *
     * Ticks at host refresh rate, suitable for input polling / display refresh.
     *
     * \param dev Region device handle
     *
     * This function is exclusively called from a single thread
     */
    void (*poll)(rvvm_reg_dev_t* dev);

    /**
     * Reset callback
     *
     * \param dev  Region device handle
     *
     * This function is exclusively called from a single thread
     */
    void (*reset)(rvvm_reg_dev_t* dev);

    /**
     * Suspend/resume, serialize snapshot
     *
     * \param dev    Region device handle
     * \param snap   Snapshot state if non-null
     * \param resume Deserialize, resume device processing
     *
     * This function is exclusively called from a single thread
     */
    void (*suspend)(rvvm_reg_dev_t* dev, rvvm_snapshot_t* snap, bool resume);

    /**
     * Cleanup callback, should drop device private data / mappings
     *
     * Called on machine cleanup, failure to attach device, or explicit
     * `rvvm_region_remove()` / `rvvm_region_free_desc()` calls.
     *
     * \param dev Region device handle
     *
     * This function is exclusively called from a single thread
     */
    void (*cleanup)(rvvm_reg_dev_t* dev);

    /**
     * Minimum operation size and alignment allowed
     *
     * This transparently normalizes partial/multiple register accesses.
     *
     * Must be power of two, or zero for no limit.
     * Should specify minimum device register size.
     */
    uint32_t min_size;

    /**
     * Maximum operation size allowed
     *
     * This transparently normalizes partial/multiple register accesses.
     *
     * Must be power of two, or zero for no limit.
     * Should specify maximum device register size.
     */
    uint32_t max_size;

} rvvm_reg_type_t;

/**
 * Region device instance description
 *
 * Copied by value by `rvvm_region_init()`, `rvvm_region_set_desc()`.
 */
typedef struct {
    /**
     * Region address in memory / port space
     */
    rvvm_addr_t addr;

    /**
     * Region size
     */
    size_t size;

    /**
     * Private device data, opaque
     *
     * Lifetime and ownership are managed by the device implementation.
     */
    void* data;

    /**
     * Directly mapped memory region for RAM-like devices
     *
     * If this is non-null, memory accesses are served directly,
     * instead of invoking read/write callbacks.
     *
     * Must remain valid until cleanup or until replaced using `rvvm_region_set_desc()`.
     * Should be page-aligned for best performance.
     */
    void* mmap;

    /**
     * Region type and callbacks
     */
    const rvvm_reg_type_t* type;

    /**
     * Region attributes
     *
     * Defaults to read-write memory with address allocation.
     */
    uint32_t attr;

} rvvm_reg_desc_t;

/**
 * Attach region device to machine
 *
 * Region device unconditionally transfers ownership.
 * In case of a failed attach, the device description is cleaned up.
 *
 * If the requested address is busy and `RVVM_REG_ATTR_FIX` is not set,
 * nearest usable address is automatically picked for the region.
 *
 * \param machine Machine handle (Nullable, guarantees description cleanup)
 * \param desc    Region description
 * \return        Region device handle (`NULL` on failure)
 *
 * This function is thread-safe
 */
RVVM_PUBLIC rvvm_reg_dev_t* rvvm_region_init(rvvm_machine_t* machine, const rvvm_reg_desc_t* desc);

/**
 * Remove region device from machine
 *
 * This should only be used for device hot-removal.
 * Cleanup on machine destruction is automatic.
 *
 * \param dev Region device handle (Nullable)
 * \note      Must not be called after `rvvm_machine_free()` on owning machine,
 *            nor from device's own callbacks or internal threads
 *
 * This function is thread-safe
 */
RVVM_PUBLIC void rvvm_region_remove(rvvm_reg_dev_t* dev);

/**
 * Invoke region device cleanup via description
 *
 * Can be used to clean up device descriptions without attaching to a machine.
 *
 * \param desc Region description
 */
static inline void rvvm_region_free_desc(const rvvm_reg_desc_t* desc)
{
    rvvm_region_init(NULL, desc);
}

/**
 * Get region device private data
 *
 * \param dev Region device handle (Nullable)
 * \return    Region device private data
 *
 * This function is thread-safe
 */
RVVM_PUBLIC void* rvvm_region_data(rvvm_reg_dev_t* dev);

/**
 * Get region device owning machine
 *
 * \param dev Region device handle (Nullable)
 * \return    Machine handle (`NULL` on failure)
 *
 * This function is thread-safe
 */
RVVM_PUBLIC rvvm_machine_t* rvvm_region_machine(rvvm_reg_dev_t* dev);

/**
 * Get region device description
 *
 * \param dev  Region device handle (Nullable)
 * \param desc Region description (Filled by pointer)
 * \return     Success
 *
 * This function is thread-safe
 */
RVVM_PUBLIC bool rvvm_region_get_desc(rvvm_reg_dev_t* dev, rvvm_reg_desc_t* desc);

/**
 * Update region device description
 *
 * Can be used to relocate, resize, update region private data or mapping.
 *
 * Region updates are atomic in respect to running vCPUs.
 *
 * \param dev  Region device handle (Nullable)
 * \param desc Region description
 * \return     Success
 *
 * This function is thread-safe
 */
RVVM_PUBLIC bool rvvm_region_set_desc(rvvm_reg_dev_t* dev, const rvvm_reg_desc_t* desc);

/**
 * Attach region device to machine, obtain resulting description back
 *
 * Region device unconditionally transfers ownership.
 * In case of a failed attach, the device description is cleaned up.
 *
 * If the requested address is busy and `RVVM_REG_ATTR_FIX` is not set,
 * nearest usable address is automatically picked for the region.
 *
 * \param machine Machine handle (Nullable, guarantees description cleanup)
 * \param desc    Region description
 * \return        Region device handle (`NULL` on failure)
 *
 * This function is thread-safe
 */
static inline rvvm_reg_dev_t* rvvm_region_init_auto(rvvm_machine_t* machine, rvvm_reg_desc_t* desc)
{
    rvvm_reg_dev_t* dev = rvvm_region_init(machine, desc);
    if (dev) {
        rvvm_region_get_desc(dev, desc);
    }
    return dev;
}

/** @}*/

RVVM_EXTERN_C_END

#endif
