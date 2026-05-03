/*
parport-pci.c - PCI Parallel Port (NetMos/MosChip MCS9900)
Copyright (C) 2026  Sol Astrius <sol@astrius.ink>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "parport-pci.h"
#include "compiler.h"
#include "mem_ops.h"
#include "spinlock.h"
#include "utils.h"

PUSH_OPTIMIZATION_SIZE

// NetMos / MosChip MCS9900 PCI parallel port (single SPP port +
// IEEE 1284 nibble-mode reverse channel).
//
// PCI identity (ASIX MCS9900 datasheet, Rev 2.00, §6.2 "PCIe
// Configuration Space", the 2S+1P parallel function): vendor 0x9710,
// device 0x9900, class 0x0701 (Communication / Parallel), subsystem
// 0xA000:0x2000. BAR 0 holds the SPP register window. We expose a single
// SPP port (prog_if 0x00); the silicon reports prog_if 0x03 (IEEE 1284
// controller), but SPP is sufficient for a host driver to bind and use
// the nibble-mode reverse channel below.
//
// Register map (8 bytes at BAR 0), the standard PC parallel-port SPP
// layout (IEEE 1284-1994 §7.3, Compatibility Mode). Byte-addressable.
//   +0x00 Data    (RW)  D0..D7, drives pins 2-9 of the DB-25 connector
//                       In nibble-reverse mode reads return last write
//                       (loopback latch); peripheral data flows via
//                       Status[3,4,5,7] per IEEE 1284 §6.3 (Nibble Mode).
//   +0x01 Status  (RO)  bit 7 nBusy   (1 = idle / data bit 3 inverted)
//                       bit 6 nAck    (1 = idle, 0 = data acknowledged)
//                       bit 5 PaperOut(active high — also "PError" in 1284)
//                       bit 4 Select  (1 = peripheral online, also xflag)
//                       bit 3 nError  (1 = no error / end-of-data marker
//                                      between bytes in nibble mode)
//                       bit 2 IRQ     (set on nAck rising edge while IRQ
//                                      enabled; read-clear)
//                       bits 1:0 reserved
//   +0x02 Control (RW)  SPP control register, in the host driver's logical
//                       view (the chip-to-wire inversion on bits 0/1/3 is
//                       invisible to software). So:
//                       bit 0 STROBE     (1 = strobe asserted, wire low)
//                       bit 1 AUTOFD     (1 = autofd asserted, "HostBusy"
//                                         in IEEE 1284 negotiation)
//                       bit 2 INIT       (1 = peripheral not in reset)
//                       bit 3 SELIN      (1 = peripheral selected for
//                                         compat printing; 0 during 1284
//                                         "active" — host deasserts to
//                                         enter negotiation)
//                       bit 4 IRQ enable
//                       bit 5 BIDIR (data direction; 1 = input)
//                       bits 7:6 unused
//   +0x03..0x07         EPP/ECP registers; SPP-only emulation returns 0.
//
// Forward path (PHASE_FWD_IDLE):
//   Standard Centronics (IEEE 1284 §7.3). Guest writes data to +0x00,
//   then writes Control with nStrobe asserted (chip bit 0 = 0) and back.
//   We trigger the backend on the chip bit 0 rising edge (deassert),
//   which is the wire-falling-edge that latches the byte at the
//   peripheral.
//
// Reverse path (negotiate to nibble mode, then read; IEEE 1284 §7.4
// negotiation, §6.3 nibble mode):
//   The host writes the extensibility request 0x00 to Data (nibble mode,
//   event 0), then drives Control through the negotiation events. We
//   respond on each control transition by computing a new Status value
//   and (if Control bit 4 IRQ-enable is set) firing an INTx edge on nAck
//   rising. After negotiation the port is in PHASE_REV_IDLE and the host
//   pulses nAutoFd to clock each nibble out via Status bits 3/4/5/7
//   (IEEE 1284 Figures 7-8).
//
// IRQ delivery uses pci_send_irq (edge), since real parport interrupts
// are edge-triggered on nAck (IEEE 1284 §7.3) and the host dismisses by
// reading Status, not via any explicit register write.

#define PARPORT_PCI_VENDOR_ID     0x9710  // NetMos / MosChip
#define PARPORT_PCI_DEVICE_ID     0x9900
#define PARPORT_PCI_CLASS         0x0701  // Communication / Parallel
#define PARPORT_PCI_PROG_IF       0x00    // SPP

// Status idle byte: nBusy=1, nAck=1, PaperOut=0, Select=1, nError=1.
// Returned in PHASE_FWD_IDLE — peripheral always reports idle so lp's
// status polling never sees a busy/error condition.
#define PARPORT_STATUS_IDLE       0xD8u

// Control reset value 0x0C is the SPP selected/idle state: bit 2 INIT=1
// (peripheral active / not in reset), bit 3 SELIN=1 (peripheral selected
// for compat printing — wire low, active-low signal asserted). Strobe and
// AutoFd cleared (wires high, idle).
#define PARPORT_CONTROL_RESET     0x0Cu

// Control register bits (SPP logical view).
#define CTL_STROBE                (1u << 0)  // 1 = strobe asserted
#define CTL_AUTOFD                (1u << 1)  // 1 = autofd asserted (HostBusy)
#define CTL_INIT                  (1u << 2)
#define CTL_SELIN                 (1u << 3)  // 1 = compat select; 0 = 1284 active
#define CTL_IRQ_EN                (1u << 4)
#define CTL_BIDIR                 (1u << 5)

// Status register bits.
#define ST_ERROR                  (1u << 3)  // nError; 1 = no error / no data
#define ST_SELECT                 (1u << 4)
#define ST_PAPEROUT               (1u << 5)
#define ST_IRQ                    (1u << 2)  // latched IRQ flag, read-clear
#define ST_ACK                    (1u << 6)  // 1 = idle
#define ST_BUSY                   (1u << 7)  // 1 = idle (active low)

// IEEE 1284 phase. Event numbers below are IEEE 1284-1994's own
// (§7.4 negotiation, Figures 7-8; §6.3 nibble mode).
typedef enum {
    PHASE_FWD_IDLE = 0,    // Compat mode; Centronics writes work
    PHASE_NEGOT_REPLY,     // Host issued event 1; we drove event 2 reply
    PHASE_REV_IDLE,        // Negotiated to nibble; awaiting event 7
    PHASE_REV_LO_DAV,      // First nibble on Status, nAck=0 (event 9)
    PHASE_REV_LO_DONE,     // First nibble done, nAck=1 (event 11)
    PHASE_REV_HI_DAV,      // Second nibble on Status, nAck=0
    PHASE_REV_HI_DONE,     // Second nibble done, nAck=1 (transient)
} parport_phase_t;

#define PARPORT_RING_SIZE 256u
#define PARPORT_RING_MASK (PARPORT_RING_SIZE - 1u)

typedef struct {
    pci_func_t* pci_func;
    spinlock_t  lock;

    uint8_t     data;
    uint8_t     control;

    // Reverse-channel state (1284 negotiation + nibble delivery)
    parport_phase_t phase;
    uint8_t     cur_byte;       // Byte being split into nibbles
    bool        cur_byte_valid;
    bool        irq_pending;    // ST_IRQ latch (cleared on Status read)

    // Input ring (host → guest). Single producer / single consumer; the
    // producer is whatever thread calls parport_pci_inject_byte (see
    // -parport_in path in main.c), the consumer is the MMIO write
    // handler. Lock-protected — the lock is also held during status
    // reads so we don't need separate atomics.
    uint8_t     ring[PARPORT_RING_SIZE];
    uint16_t    ring_head;      // producer writes here
    uint16_t    ring_tail;      // consumer reads here

    // Backend
    parport_pci_write_fn write_fn;
    void*                user_data;
} parport_pci_dev_t;

static bool ring_empty(const parport_pci_dev_t* pp)
{
    return pp->ring_head == pp->ring_tail;
}

static bool ring_full(const parport_pci_dev_t* pp)
{
    return (uint16_t)(pp->ring_head - pp->ring_tail) >= PARPORT_RING_SIZE;
}

static bool ring_pop(parport_pci_dev_t* pp, uint8_t* out)
{
    if (ring_empty(pp)) return false;
    *out = pp->ring[pp->ring_tail & PARPORT_RING_MASK];
    pp->ring_tail++;
    return true;
}

// Encode a 4-bit nibble into Status[3,4,5,7] per IEEE 1284 §6.3 nibble
// mode, where the four reverse data bits travel on the status lines:
// data bit 0 → nFault (ST_ERROR), bit 1 → Select (ST_SELECT), bit 2 →
// PError (ST_PAPEROUT), bit 3 → Busy. Busy is active-low (nBusy), so a
// set data bit 3 clears ST_BUSY.
static uint8_t encode_nibble_status_bits(uint8_t nibble)
{
    uint8_t s = 0;
    if (nibble & 0x1) s |= ST_ERROR;
    if (nibble & 0x2) s |= ST_SELECT;
    if (nibble & 0x4) s |= ST_PAPEROUT;
    if (!(nibble & 0x8)) s |= ST_BUSY;  // bit 3 inverted: 1 = clear nBusy
    return s;
}

// Compute the Status register value from current device state.
// Pure function of (phase, cur_byte, ring state, irq_pending).
static uint8_t compute_status(const parport_pci_dev_t* pp)
{
    uint8_t s;
    switch (pp->phase) {
        case PHASE_FWD_IDLE:
            // Steady-state idle. nFault=1 (no error), Select=1 (online),
            // PaperOut=0 (paper present), nAck=1, nBusy=1 (idle).
            s = ST_ERROR | ST_SELECT | ST_BUSY | ST_ACK;
            break;

        case PHASE_NEGOT_REPLY:
            // Event 2 reply. nFault=1, Select=1, PaperOut=1 (PError
            // asserted), nAck=0 (asserted — peripheral is responding),
            // nBusy=1.
            s = ST_ERROR | ST_SELECT | ST_PAPEROUT | ST_BUSY;
            break;

        case PHASE_REV_IDLE: {
            // Between bytes. Set nError based on whether we have more
            // data: the host checks the nFault/Error status bit at the
            // start of each byte and treats set = end-of-data. So:
            //   ring empty AND no cached byte → ST_ERROR set (end)
            //   data available                 → ST_ERROR clear
            // nAck=1 (idle), Select=1 (mode supported, xflag accept).
            bool has_data = !ring_empty(pp) || pp->cur_byte_valid;
            s = ST_SELECT | ST_BUSY | ST_ACK;
            if (!has_data) s |= ST_ERROR;
            break;
        }

        case PHASE_REV_LO_DAV:
            // First nibble on data lines, nAck=0 (event 9).
            s = encode_nibble_status_bits(pp->cur_byte & 0xF);
            break;

        case PHASE_REV_LO_DONE:
            // First nibble still latched, nAck=1 (event 11).
            s = encode_nibble_status_bits(pp->cur_byte & 0xF) | ST_ACK;
            break;

        case PHASE_REV_HI_DAV:
            s = encode_nibble_status_bits((pp->cur_byte >> 4) & 0xF);
            break;

        case PHASE_REV_HI_DONE:
            s = encode_nibble_status_bits((pp->cur_byte >> 4) & 0xF) | ST_ACK;
            break;

        default:
            s = PARPORT_STATUS_IDLE;
            break;
    }

    if (pp->irq_pending) s |= ST_IRQ;
    return s;
}

// Apply state-machine transitions on Control writes. Returns true if an
// IRQ should be raised after dropping the device lock (caller observes
// pp->irq_pending and pp->control to gate). Also returns the byte to
// send forward (via *fwd_byte) when the Centronics strobe trips.
static bool handle_control_transition(parport_pci_dev_t* pp,
                                      uint8_t prev, uint8_t next,
                                      bool* fwd_strobe, uint8_t* fwd_byte)
{
    *fwd_strobe = false;
    bool ack_was_high = !!(compute_status(pp) & ST_ACK);

    // PHASE_FWD_IDLE: detect Centronics strobe (CTL_STROBE 0→1) AND
    // 1284 negotiation request (event 1: AUTOFD asserts, SELIN
    // deasserts — the host drives these together).
    if (pp->phase == PHASE_FWD_IDLE) {
        if (!(prev & CTL_STROBE) && (next & CTL_STROBE)) {
            *fwd_strobe = true;
            *fwd_byte = pp->data;
        }
        bool autofd_assert  = !(prev & CTL_AUTOFD) && (next & CTL_AUTOFD);
        bool selin_deassert = (prev & CTL_SELIN) && !(next & CTL_SELIN);
        if (autofd_assert && selin_deassert) {
            // Mode byte already in pp->data. Accept nibble (0x00); for
            // anything else we stay in FWD_IDLE and the host sees no
            // event-2 reply (status never matches), times out, and
            // negotiation fails (not 1284 compliant). The host sends the
            // extensibility request 0x00 for nibble mode (IEEE 1284
            // §7.4, event 0).
            if (pp->data == 0x00) {
                pp->phase = PHASE_NEGOT_REPLY;
            }
        }
    } else {
        // In any non-FWD phase, the host re-asserting SELIN (chip 0→1)
        // is termination (IEEE 1284 §7.4, Figure 7; events 22-29).
        // Drop back to forward idle; the host's follow-up events 24/27/29
        // just poll nAck, which our FWD_IDLE status drives high.
        if (!(prev & CTL_SELIN) && (next & CTL_SELIN)) {
            pp->phase = PHASE_FWD_IDLE;
            pp->cur_byte_valid = false;
            return false;
        }
    }

    // AUTOFD edges drive the phase machine in 1284 modes. "Assert" =
    // chip bit 0→1 (HostBusy on wire); "release" = chip bit 1→0.
    bool autofd_assert  = !(prev & CTL_AUTOFD) && (next & CTL_AUTOFD);
    bool autofd_release = (prev & CTL_AUTOFD) && !(next & CTL_AUTOFD);

    switch (pp->phase) {
        case PHASE_NEGOT_REPLY:
            // Event 4 second half: AUTOFD released. Move to REV_IDLE
            // and drive nAck high (event 6). The intermediate STROBE
            // pulse (events 3-4 first half) is harmless — we just don't
            // act on it during negotiation.
            if (autofd_release) {
                pp->phase = PHASE_REV_IDLE;
            }
            break;

        case PHASE_REV_IDLE:
            // Event 7 of first nibble: load next byte and present.
            if (autofd_assert) {
                if (!pp->cur_byte_valid) {
                    if (!ring_pop(pp, &pp->cur_byte)) {
                        // No data — leave phase unchanged. The host will
                        // see nibble bits stuck and time out at event 9.
                        // Normally the host checks ST_ERROR in REV_IDLE
                        // first and bails before reaching here.
                        break;
                    }
                    pp->cur_byte_valid = true;
                }
                pp->phase = PHASE_REV_LO_DAV;
            }
            break;

        case PHASE_REV_LO_DAV:
            if (autofd_release) pp->phase = PHASE_REV_LO_DONE;
            break;

        case PHASE_REV_LO_DONE:
            if (autofd_assert) pp->phase = PHASE_REV_HI_DAV;
            break;

        case PHASE_REV_HI_DAV:
            if (autofd_release) {
                // Byte fully transferred. Consume it and return to idle;
                // ring state determines whether nFault signals more data
                // or end-of-data on the next status read.
                pp->cur_byte_valid = false;
                pp->phase = PHASE_REV_IDLE;
            }
            break;

        case PHASE_REV_HI_DONE:
        case PHASE_FWD_IDLE:
        default:
            break;
    }

    // Detect nAck rising edge across the transition for IRQ delivery.
    // We could just snapshot the status before/after; the cheaper test
    // is "was low before, is high now" and gate on Control IRQ enable.
    bool ack_now_high = !!(compute_status(pp) & ST_ACK);
    if (!ack_was_high && ack_now_high && (next & CTL_IRQ_EN)) {
        pp->irq_pending = true;
        return true;
    }
    return false;
}

// pci_dev_t doesn't expose the BAR private-data pointer back to the
// caller, so we keep a small registration map (pci_dev_t* → device
// state) and look up at inject time. The map is sized for the handful
// of parports a single machine could plausibly have.
#define PARPORT_MAX_INSTANCES 4
static parport_pci_dev_t* g_parport_instances[PARPORT_MAX_INSTANCES];
static pci_dev_t*         g_parport_pci_dev[PARPORT_MAX_INSTANCES];
static spinlock_t         g_parport_instances_lock;

static void parport_pci_remove(rvvm_mmio_dev_t* dev)
{
    parport_pci_dev_t* pp = dev->data;
    // Drop the registration so a later inject — or a reused pci_dev_t
    // pointer at the same address — can't resolve to this freed instance.
    spin_lock(&g_parport_instances_lock);
    for (size_t i = 0; i < PARPORT_MAX_INSTANCES; i++) {
        if (g_parport_instances[i] == pp) {
            g_parport_instances[i] = NULL;
            g_parport_pci_dev[i]   = NULL;
            break;
        }
    }
    spin_unlock(&g_parport_instances_lock);
    free(pp);
}

static rvvm_mmio_type_t parport_pci_type = {
    .name   = "netmos_9900_parport",
    .remove = parport_pci_remove,
};

static bool parport_pci_mmio_read(rvvm_mmio_dev_t* dev, void* data, size_t off, uint8_t size)
{
    UNUSED(size);
    parport_pci_dev_t* pp = dev->data;
    spin_lock(&pp->lock);
    uint8_t val = 0;
    bool ok = true;
    switch (off) {
        case 0x00:
            // Data register read. In nibble-mode reverse the actual
            // peripheral byte is delivered via Status bits, not Data;
            // returning the last write is the standard SPP loopback
            // behavior the host expects.
            val = pp->data;
            break;
        case 0x01:
            val = compute_status(pp);
            // ST_IRQ is read-clear (cleared when Status is read).
            pp->irq_pending = false;
            break;
        case 0x02:
            val = pp->control;
            break;
        case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
            val = 0;
            break;
        default:
            ok = false;
            break;
    }
    spin_unlock(&pp->lock);
    if (ok) write_uint8(data, val);
    return ok;
}

static bool parport_pci_mmio_write(rvvm_mmio_dev_t* dev, void* data, size_t off, uint8_t size)
{
    UNUSED(size);
    parport_pci_dev_t* pp = dev->data;
    uint8_t byte = read_uint8(data);

    parport_pci_write_fn fn  = NULL;
    void*                ud  = NULL;
    uint8_t              out = 0;
    bool                 fwd = false;
    bool                 raise_irq = false;

    spin_lock(&pp->lock);
    bool ok = true;
    switch (off) {
        case 0x00:
            pp->data = byte;
            break;
        case 0x02: {
            uint8_t prev = pp->control;
            pp->control = byte;
            raise_irq = handle_control_transition(pp, prev, byte, &fwd, &out);
            if (fwd && pp->phase == PHASE_FWD_IDLE) {
                fn = pp->write_fn;
                ud = pp->user_data;
            } else {
                fwd = false;
            }
            break;
        }
        case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
            break;
        default:
            ok = false;
            break;
    }
    pci_func_t* pci_func = pp->pci_func;
    spin_unlock(&pp->lock);

    if (ok && fwd && fn) {
        fn(ud, out);
    }
    if (raise_irq && pci_func) {
        pci_send_irq(pci_func, 0);
    }
    return ok;
}

static void register_instance(pci_dev_t* dev, parport_pci_dev_t* pp)
{
    spin_lock(&g_parport_instances_lock);
    for (size_t i = 0; i < PARPORT_MAX_INSTANCES; i++) {
        if (g_parport_instances[i] == NULL) {
            g_parport_instances[i] = pp;
            g_parport_pci_dev[i]   = dev;
            break;
        }
    }
    spin_unlock(&g_parport_instances_lock);
}

static parport_pci_dev_t* lookup_instance(pci_dev_t* dev)
{
    parport_pci_dev_t* found = NULL;
    spin_lock(&g_parport_instances_lock);
    for (size_t i = 0; i < PARPORT_MAX_INSTANCES; i++) {
        if (g_parport_pci_dev[i] == dev) {
            found = g_parport_instances[i];
            break;
        }
    }
    spin_unlock(&g_parport_instances_lock);
    return found;
}

PUBLIC bool parport_pci_inject_byte(pci_dev_t* dev, uint8_t byte)
{
    parport_pci_dev_t* pp = lookup_instance(dev);
    if (!pp) return false;

    bool ok = false;
    bool wake_irq = false;
    spin_lock(&pp->lock);
    if (!ring_full(pp)) {
        pp->ring[pp->ring_head & PARPORT_RING_MASK] = byte;
        pp->ring_head++;
        ok = true;
        // If the guest is already in PHASE_REV_IDLE waiting for data,
        // an IRQ would be nice — but parport_wait_peripheral here is
        // polling for a *Status* change (nFault going low), not an
        // edge-triggered ACK event. The host re-reads Status on the
        // next poll cycle and sees ST_ERROR clear, so no IRQ needed
        // for correctness. We could fire one to shorten the 10ms slow
        // poll latency, but only when IRQ enable is set and the host
        // has a pending wait — neither of which we can detect from
        // here. Skip it.
        UNUSED(wake_irq);
    }
    spin_unlock(&pp->lock);
    return ok;
}

PUBLIC pci_dev_t* parport_pci_init(pci_bus_t* pci_bus,
                                   parport_pci_write_fn write_fn,
                                   void* user_data)
{
    parport_pci_dev_t* pp = safe_new_obj(parport_pci_dev_t);
    pp->control   = PARPORT_CONTROL_RESET;
    pp->phase     = PHASE_FWD_IDLE;
    pp->write_fn  = write_fn;
    pp->user_data = user_data;

    pci_func_desc_t desc = {
        .vendor_id        = PARPORT_PCI_VENDOR_ID,
        .device_id        = PARPORT_PCI_DEVICE_ID,
        .class_code       = PARPORT_PCI_CLASS,
        .prog_if          = PARPORT_PCI_PROG_IF,
        // Subsystem Vendor/Device ID the MCS9900 2S+1P parallel function
        // reports (datasheet §6.2; PCI Local Bus Spec 3.0 §6.2.4). Host
        // drivers that strict-match on subsystem ID need these to bind.
        .subsys_vendor_id = 0xA000,
        .subsys_device_id = 0x2000,
        // BAR 0 is an I/O-port BAR (PCI Local Bus Spec 3.0 §6.2.5.1). The
        // guest reaches it via its PCI resource + inb/outb, which on
        // RISC-V routes through the PCI bridge's I/O range in the DT.
        .bar_io_mask      = 0x01,
        .irq_pin          = PCI_IRQ_PIN_INTA,
        .bar[0] = {
            .size        = 8,
            .min_op_size = 1,
            .max_op_size = 4,
            .read        = parport_pci_mmio_read,
            .write       = parport_pci_mmio_write,
            .data        = pp,
            .type        = &parport_pci_type,
        },
    };

    pci_dev_t* dev = pci_attach_func(pci_bus, &desc);
    if (dev) {
        pp->pci_func = pci_get_device_func(dev, 0);
        register_instance(dev, pp);
    }
    return dev;
}

PUBLIC pci_dev_t* parport_pci_init_auto(rvvm_machine_t* machine,
                                        parport_pci_write_fn write_fn,
                                        void* user_data)
{
    return parport_pci_init(rvvm_get_pci_bus(machine), write_fn, user_data);
}

POP_OPTIMIZATION_SIZE
