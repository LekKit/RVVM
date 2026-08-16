/*
uart16550_core.h - Reusable 16550-family UART register core
Copyright (C) 2021  LekKit <github.com/LekKit>
Copyright (C) 2026  Sol Astrius <sol@astrius.ink>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef RVVM_UART16550_CORE_H
#define RVVM_UART16550_CORE_H

#include <rvvm/rvvm_snapshot.h>

#include "chardev.h"

/*
 * Backend-agnostic 16550A register core. The bus wrapper (rvvm_reg_dev_t
 * MMIO/PIO for ns16550a, BAR-windowed multi-port for an Exar PCIe combo
 * card, etc.) embeds a `uart16550_core_t`, drives byte-wide register
 * access via `uart16550_core_read/write`, and supplies an IRQ callback
 * so the core stays unaware of whether the line is wired INTx, PCI INTx,
 * or MSI.
 */

// Register offsets within an 8-byte 16550 window.
//
// DLAB (LCR bit 7) banks RBR/THR/IER under the divisor latch low/high
// at offsets 0/1; FCR is write-only at 2 with IIR read-only there.
#define UART16550_REG_RBR_DLL 0x0
#define UART16550_REG_THR_DLL 0x0
#define UART16550_REG_IER_DLM 0x1
#define UART16550_REG_IIR     0x2
#define UART16550_REG_FCR     0x2
#define UART16550_REG_LCR     0x3
#define UART16550_REG_MCR     0x4
#define UART16550_REG_LSR     0x5
#define UART16550_REG_MSR     0x6
#define UART16550_REG_SCR     0x7

// IRQ callback. `level=true` asserts the line, `false` deasserts.
// The core may call this from any thread that pokes registers or
// receives a chardev notify; wrappers must be reentrant-safe.
typedef void (*uart16550_irq_fn)(void* ctx, bool level);

typedef struct {
    chardev_t*       chardev;
    uart16550_irq_fn irq_fn;
    void*            irq_ctx;

    uint32_t flags;
    uint32_t ier;
    uint32_t lcr;
    uint32_t mcr;
    uint32_t scr;
    uint32_t dll;
    uint32_t dlm;

    // Sticky "kernel has enabled RX" gate. Real silicon's RBR is empty
    // after reset; pre-init RBR reads return floating bus values which
    // the kernel discards as junk. Our chardev backend never resets, so
    // bytes that arrived before the guest finishes startup would get
    // eaten by `serial8250_do_startup`'s two junk-drain RBR reads. We
    // suppress chardev draining until the guest writes IER with the
    // RECV bit (kernel signalling "I'm ready to receive"); flips true
    // once and stays true until the core is destroyed.
    uint32_t rx_armed;
} uart16550_core_t;

// Bind a chardev to a core in caller-owned storage. Hooks the chardev's
// notify path back to this core, so flag changes raise/lower the IRQ
// line via `irq_fn`. `chardev` and `irq_fn` may be NULL.
void uart16550_core_init(uart16550_core_t* core, chardev_t* chardev,
                         uart16550_irq_fn irq_fn, void* irq_ctx);

// Free the chardev owned by this core (if any). The core storage
// itself is owned by the caller.
void uart16550_core_cleanup(uart16550_core_t* core);

// Single-byte register read/write. `reg` is a UART16550_REG_* offset.
// Out-of-range reads return 0; out-of-range writes are dropped.
uint8_t uart16550_core_read(uart16550_core_t* core, uint32_t reg);
void    uart16550_core_write(uart16550_core_t* core, uint32_t reg, uint8_t val);

// Force a re-poll of the chardev backend; useful for periodic update
// hooks where the bus wrapper wants to push pending TX or pull RX.
void uart16550_core_update(uart16550_core_t* core);

// Serialize/deserialize core register state into the current snapshot
// section (caller opens the section so multi-instance wrappers can
// distinguish their ports). Direction is implicit in `snap`'s mode.
void uart16550_core_suspend(uart16550_core_t* core, rvvm_snapshot_t* snap);

#endif
