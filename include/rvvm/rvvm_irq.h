/*
<rvvm/rvvm_irq.h> - RVVM Wired Interrupts API
Copyright (C) 2020-2026 LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _RVVM_IRQ_API_H
#define _RVVM_IRQ_API_H

#include <rvvm/rvvm_base.h>

RVVM_EXTERN_C_BEGIN

/**
 * @defgroup rvvm_irq_dev_api Wired interrupt contoller API
 * @addtogroup rvvm_irq_dev_api
 * @{
 */

/**
 * Interrupt controller callbacks
 *
 * Must be valid during controller lifetime, or static
 *
 * Callbacks are optional (Nullable)
 */
typedef struct {
    /**
     * Set interrupt line level on interrupt controller
     *
     * Callable from any thread, interrupt controller should use
     * locking or atomics internally to prevent race conditions
     *
     * Every interrupt line level transition must be observable as
     * an interrupt edge, even if the line level changes later
     *
     * Redundant interrupt line level updates (true->true, false->false)
     * may be ignored and treated as a no-op by interrupt controller
     *
     * All operations must establish a happens-before relationship
     */
    void (*set_irq)(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq, bool lvl);

    /**
     * Get interrupts-extended FDT cells for an IRQ
     *
     * If this is NULL, an interrupt number is encoded directly into FDT interrupt cell
     */
    size_t (*fdt_irq_cells)(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq, uint32_t* cells, size_t size);

} rvvm_irq_dev_cb_t;

/**
 * Create a new interrupt controller
 *
 * \param cb   Interrupt controller callbacks, referenced during lifetime
 * \param data Private interrupt controller data
 * \return     Interrupt controller handle
 */
RVVM_PUBLIC rvvm_irq_dev_t* rvvm_irq_dev_init(const rvvm_irq_dev_cb_t* cb, void* data);

/**
 * Free interrupt controller
 *
 * Should be called by interrupt controller implementation on device cleanup
 *
 * The interrupt controller must be cleaned last in device hierarchy,
 * so that the devices which were using its interrupt lines are already freed
 *
 * \param irq_dev Interrupt controller handle
 */
RVVM_PUBLIC void rvvm_irq_dev_free(rvvm_irq_dev_t* irq_dev);

/**
 * Set interrupt controller FDT phandle
 *
 * Should be called by FDT-based interrupt controller implementation
 *
 * \param irq_dev Interrupt controller handle
 * \param phandle FDT phandle
 */
RVVM_PUBLIC void rvvm_irq_dev_set_phandle(rvvm_irq_dev_t* irq_dev, uint32_t phandle);

/**
 * @}
 * @defgroup rvvm_irq_api Wired interrupts API
 * @addtogroup rvvm_irq_api
 * @{
 */

/**
 * Allocate interrupt line
 *
 * Should be called on device init to claim a dedicated interrupt line
 *
 * \param irq_dev Interrupt controller handle
 * \param irq     Suggest minimal IRQ line to allocate
 * \return        Allocated IRQ line
 *
 * This function always succeeds
 */
RVVM_PUBLIC rvvm_irq_t rvvm_irq_alloc(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq);

/**
 * Deallocate interrupt line
 *
 * Should be called when device no longer intends to use the interrupt line
 *
 * \param irq_dev Interrupt controller handle
 * \param irq     Allocated IRQ line
 */
RVVM_PUBLIC void rvvm_irq_dealloc(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq);

/**
 * Allocate specific interrupt line
 *
 * \param irq_dev Interrupt controller handle
 * \param irq     Interrupt line to allocate
 * \return        Success
 */
static inline bool rvvm_irq_alloc_exact(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq)
{
    rvvm_irq_t ret = rvvm_irq_alloc(irq_dev, irq);
    if (ret != irq) {
        rvvm_irq_dealloc(irq_dev, ret);
    }
    return ret == irq;
}

/**
 * Set interrupt line level
 *
 * Interrupt line state changes are observable in consistent order
 *
 * \param irq_dev Interrupt controller handle
 * \param irq     Interrupt line
 * \param lvl     Interrupt level
 */
RVVM_PUBLIC void rvvm_irq_set(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq, bool lvl);

/**
 * Raise interrupt line
 *
 * \param irq_dev Interrupt controller handle
 * \param irq     Interrupt line
 */
static inline void rvvm_irq_raise(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq)
{
    rvvm_irq_set(irq_dev, irq, true);
}

/**
 * Lower interrupt line
 *
 * \param irq_dev Interrupt controller handle
 * \param irq     Interrupt line
 */
static inline void rvvm_irq_lower(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq)
{
    rvvm_irq_set(irq_dev, irq, false);
}

/**
 * Send an edge-triggered interrupt
 *
 * \param irq_dev Interrupt controller handle
 * \param irq     Interrupt line
 */
static inline void rvvm_irq_send(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq)
{
    rvvm_irq_set(irq_dev, irq, true);
    rvvm_irq_set(irq_dev, irq, false);
}

/**
 * Add interrupt-parent & interrupts properties to FDT node
 *
 * \param node    FDT node to describe interrupt into
 * \param irq_dev Interrupt controller handle
 * \param irq     Interrupt line
 */
RVVM_PUBLIC void rvvm_irq_fdt_describe(rvvm_fdt_node_t* node, rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq);

/**
 * Get FDT phandle of an interrupt controller
 *
 * \param irq_dev Interrupt controller handle
 * \return        FDT phandle
 */
RVVM_PUBLIC uint32_t rvvm_irq_fdt_phandle(rvvm_irq_dev_t* irq_dev);

/**
 * Get interrupts-extended FDT cells for an IRQ
 *
 * \param irq_dev Interrupt controller handle
 * \param irq     Interrupt line
 * \param cells   FDT cells buffer
 * \param size    Buffer size (In cells)
 * \return        FDT cells count
 */
RVVM_PUBLIC size_t rvvm_irq_fdt_cells(rvvm_irq_dev_t* irq_dev, rvvm_irq_t irq, uint32_t* cells, size_t size);

/** @}*/

RVVM_EXTERN_C_END

#endif
