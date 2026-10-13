/*
uart16550_core.c - Reusable 16550-family UART register core
Copyright (C) 2021  LekKit <github.com/LekKit>
Copyright (C) 2026  Sol Astrius <sol@astrius.ink>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "uart16550_core.h"

#include "atomics.h"
#include "compiler.h"
#include "utils.h"

#include <string.h>

PUSH_OPTIMIZATION_SIZE

// IER bits — interrupt enable mask. THR (TX-empty) and RECV (RX-ready)
// are the only two we wire up; LSR/MSR change interrupts are listed
// for register-level fidelity but never asserted.
#define UART16550_IER_RECV 0x1
#define UART16550_IER_THR  0x2
#define UART16550_IER_LSR  0x4
#define UART16550_IER_MSR  0x8

// IIR identification codes. FIFO bits are advertised so guests probing
// for a 16550A vs an older 16450 see "FIFOs enabled" — actual buffering
// happens in the chardev backend, not in the core.
#define UART16550_IIR_FIFO 0xC0
#define UART16550_IIR_NONE 0x1
#define UART16550_IIR_THR  0x2
#define UART16550_IIR_RECV 0x4

#define UART16550_LSR_RECV 0x1
#define UART16550_LSR_THR  0x60

#define UART16550_LCR_DLAB 0x80

static void uart16550_set_irq(uart16550_core_t* core, bool level)
{
    if (core->irq_fn) {
        core->irq_fn(core->irq_ctx, level);
    }
}

// Mask of chardev flags the guest is currently allowed to observe.
// RX is gated on `rx_armed` (kernel has enabled IER.RECV at least once);
// TX always passes through — the guest can transmit before it's ready
// to receive, and our backend never back-pressures so there's no risk.
static uint32_t uart16550_visible_flags(uart16550_core_t* core)
{
    uint32_t flags = chardev_poll(core->chardev);
    if (!atomic_load_uint32_relax(&core->rx_armed)) {
        flags &= ~CHARDEV_RX;
    }
    return flags;
}

// Recompute IRQ assertion from current flags ∧ IER. Called whenever
// either side changes: backend notify (flag delta) or guest IER write.
static void uart16550_update_irq(uart16550_core_t* core)
{
    uint32_t flags = atomic_load_uint32_relax(&core->flags);
    uint32_t ier   = atomic_load_uint32_relax(&core->ier);
    bool     level = ((flags & CHARDEV_RX) && (ier & UART16550_IER_RECV))
                  || ((flags & CHARDEV_TX) && (ier & UART16550_IER_THR));
    uart16550_set_irq(core, level);
}

// Chardev → core notify hook. Stored flags are updated atomically; if
// they actually changed, re-evaluate the IRQ. RX is masked off until
// `rx_armed` flips so pre-init chardev traffic doesn't raise spurious
// IRQs the kernel would then drain via junk-RBR-reads.
static void uart16550_notify(void* io_dev, uint32_t flags)
{
    uart16550_core_t* core = io_dev;
    if (!atomic_load_uint32_relax(&core->rx_armed)) {
        flags &= ~CHARDEV_RX;
    }
    if (atomic_swap_uint32(&core->flags, flags) != flags) {
        uart16550_update_irq(core);
    }
}

// Re-poll the chardev for current readiness and route through notify
// if anything changed. Used after RBR drains and THR pushes, where
// the backend won't have raised an edge on its own.
static void uart16550_poll_chardev(uart16550_core_t* core)
{
    uint32_t flags = uart16550_visible_flags(core);
    if (flags != atomic_load_uint32_relax(&core->flags)) {
        uart16550_notify(core, flags);
    }
}

void uart16550_core_init(uart16550_core_t* core, chardev_t* chardev,
                         uart16550_irq_fn irq_fn, void* irq_ctx)
{
    memset(core, 0, sizeof(*core));
    core->chardev = chardev;
    core->irq_fn  = irq_fn;
    core->irq_ctx = irq_ctx;

    if (chardev) {
        chardev->io_dev = core;
        chardev->notify = uart16550_notify;
    }
}

void uart16550_core_cleanup(uart16550_core_t* core)
{
    chardev_free(core->chardev);
    core->chardev = NULL;
}

void uart16550_core_update(uart16550_core_t* core)
{
    chardev_update(core->chardev);
}

uint8_t uart16550_core_read(uart16550_core_t* core, uint32_t reg)
{
    switch (reg) {
        case UART16550_REG_RBR_DLL:
            if (atomic_load_uint32_relax(&core->lcr) & UART16550_LCR_DLAB) {
                return atomic_load_uint32_relax(&core->dll);
            } else if (uart16550_visible_flags(core) & CHARDEV_RX) {
                uint8_t byte = 0;
                chardev_read(core->chardev, &byte, 1);
                uart16550_poll_chardev(core);
                return byte;
            }
            return 0;
        case UART16550_REG_IER_DLM:
            if (atomic_load_uint32_relax(&core->lcr) & UART16550_LCR_DLAB) {
                return atomic_load_uint32_relax(&core->dlm);
            }
            return atomic_load_uint32_relax(&core->ier);
        case UART16550_REG_IIR: {
            uint32_t flags = uart16550_visible_flags(core);
            uint32_t ier   = atomic_load_uint32_relax(&core->ier);
            if ((flags & CHARDEV_RX) && (ier & UART16550_IER_RECV)) {
                return UART16550_IIR_RECV | UART16550_IIR_FIFO;
            } else if ((flags & CHARDEV_TX) && (ier & UART16550_IER_THR)) {
                return UART16550_IIR_THR | UART16550_IIR_FIFO;
            }
            return UART16550_IIR_NONE | UART16550_IIR_FIFO;
        }
        case UART16550_REG_LCR:
            return atomic_load_uint32_relax(&core->lcr);
        case UART16550_REG_MCR:
            return atomic_load_uint32_relax(&core->mcr);
        case UART16550_REG_LSR: {
            uint32_t flags = uart16550_visible_flags(core);
            return ((flags & CHARDEV_RX) ? UART16550_LSR_RECV : 0)
                 | ((flags & CHARDEV_TX) ? UART16550_LSR_THR  : 0);
        }
        case UART16550_REG_MSR:
            // CTS+DSR+DCD asserted; matches the original ns16550a value
            // so guests probing modem status see a permanently-connected
            // line rather than a hung modem.
            return 0xB0;
        case UART16550_REG_SCR:
            return atomic_load_uint32_relax(&core->scr);
        default:
            return 0;
    }
}

void uart16550_core_write(uart16550_core_t* core, uint32_t reg, uint8_t val)
{
    switch (reg) {
        case UART16550_REG_THR_DLL:
            if (atomic_load_uint32_relax(&core->lcr) & UART16550_LCR_DLAB) {
                atomic_store_uint32_relax(&core->dll, val);
            } else {
                chardev_write(core->chardev, &val, 1);
                uart16550_poll_chardev(core);
            }
            break;
        case UART16550_REG_IER_DLM:
            if (atomic_load_uint32_relax(&core->lcr) & UART16550_LCR_DLAB) {
                atomic_store_uint32_relax(&core->dlm, val);
            } else {
                atomic_store_uint32_relax(&core->ier, val);
                // Arm the RX gate the moment the kernel signals it
                // wants to receive. Sticky — flow control later
                // toggling RECV off doesn't silently re-enable junk
                // draining at the next port reopen.
                if (val & UART16550_IER_RECV) {
                    atomic_store_uint32_relax(&core->rx_armed, 1);
                    // Backfill flags from chardev. By now RX may have
                    // been pending for a while with no IRQ raised; sync
                    // through notify so the fresh-armed line raises if
                    // data is already waiting.
                    uart16550_poll_chardev(core);
                }
                uart16550_update_irq(core);
            }
            break;
        case UART16550_REG_LCR:
            atomic_store_uint32_relax(&core->lcr, val);
            break;
        case UART16550_REG_MCR:
            atomic_store_uint32_relax(&core->mcr, val);
            break;
        case UART16550_REG_SCR:
            atomic_store_uint32_relax(&core->scr, val);
            break;
        default:
            break;
    }
}

void uart16550_core_suspend(uart16550_core_t* core, rvvm_snapshot_t* snap)
{
    rvvm_snapshot_field(snap, core->flags);
    rvvm_snapshot_field(snap, core->ier);
    rvvm_snapshot_field(snap, core->lcr);
    rvvm_snapshot_field(snap, core->mcr);
    rvvm_snapshot_field(snap, core->scr);
    rvvm_snapshot_field(snap, core->dll);
    rvvm_snapshot_field(snap, core->dlm);
    rvvm_snapshot_field(snap, core->rx_armed);
}

POP_OPTIMIZATION_SIZE
