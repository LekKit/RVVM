/*
usb-xhci.h - USB Extensible Host Controller Interface
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

/*
 * WIP
 */

#ifdef USE_XHCI

#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_pci.h>
#include <rvvm/rvvm_region.h>
#include <rvvm/rvvm_snapshot.h>

#include <util/atomics.h>
#include <util/bit_ops.h>
#include <util/mem_ops.h>
#include <util/utils.h>

#include "usb-xhci.h"
#include "compiler.h"

PUSH_OPTIMIZATION_SIZE

/*
 * Register regions
 */

#define XHCI_CAPABILITY_BASE  0x0    // Capability registers base offset
#define XHCI_OPERATIONAL_BASE 0x80   // Operational registers base offset
#define XHCI_PORT_REGS_BASE   0x400  // Port registers base offset
#define XHCI_RUNTIME_BASE     0x2000 // Runtime registers base offset
#define XHCI_DOORBELL_BASE    0x3000 // Doorbell registers base offset
#define XHCI_EXT_CAPS_BASE    0x8000 // Extended capabilities base offset

#define XHCI_PORT_REGS_SIZE 0x1000  // Port registers region size
#define XHCI_RUNTIME_SIZE   0x1000  // Runtime registers region size
#define XHCI_DOORBELL_SIZE  0x1000  // Doorbell registers region size
#define XHCI_BAR_SIZE       0x10000 // Size of XHCI MMIO BAR

/*
 * Capability registers
 */

#define XHCI_REG_CAPLENGTH_HCIVERSION 0x00 // Capability Register Length (byte 0), Interface Version Number (byte 2)
#define XHCI_REG_HCSPARAMS1           0x04 // Structural Parameters 1
#define XHCI_REG_HCSPARAMS2           0x08 // Structural Parameters 2
#define XHCI_REG_HCSPARAMS3           0x0C // Structural Parameters 3
#define XHCI_REG_HCCPARAMS1           0x10 // Capability Parameters
#define XHCI_REG_DBOFF                0x14 // Doorbell Offset
#define XHCI_REG_RTSOFF               0x18 // Runtime Registers Space Offset
#define XHCI_REG_HCCPARMS2            0x1C // Capability Parameters 2

// Capability constants
#define XHCI_VERSION 0x0100 // XHCI v1.0.0

#define XHCI_MAX_SLOTS 0x20 // 32 slots max
#define XHCI_MAX_PORTS 0x20 // 32 ports max
#define XHCI_MAX_INTRS 0x01 // 1 interrupter

#define XHCI_CAPLEN_HCIVERSION (XHCI_OPERATIONAL_BASE | (XHCI_VERSION << 16))
#define XHCI_HCSPARAMS1        (XHCI_MAX_SLOTS | (XHCI_MAX_INTRS << 8) | (XHCI_MAX_PORTS << 24))

#define XHCI_HCCPARAMS1 ((XHCI_EXT_CAPS_BASE << 14) | 0x1) // 64-bit addressing

/*
 * Operational registers
 */

#define XHCI_REG_USBCMD   0x80 // USB Command
#define XHCI_REG_USBSTS   0x84 // USB Status
#define XHCI_REG_PAGESIZE 0x88 // Page Size
#define XHCI_REG_DNCTRL   0x94 // Device Notification Control
#define XHCI_REG_CRCR     0x98 // Command Ring Control
#define XHCI_REG_CRCR_H   0x9C
#define XHCI_REG_DCBAAP   0xB0 // Device Context Base Address Array Pointer
#define XHCI_REG_DCBAAP_H 0xB4 // Upper half of DCBAAP
#define XHCI_REG_CONFIG   0xB8 // Configure

// USB Command values
#define XHCI_USBCMD_RS    0x1 // Run/Stop
#define XHCI_USBCMD_HCRST 0x2 // Host Controller Reset
#define XHCI_USBCMD_INTE  0x4 // Interrupter Enable
#define XHCI_USBCMD_HSEE  0x8 // Host System Error Enable
#define XHCI_USBCMD_MASK (XHCI_USBCMD_RS | XHCI_USBCMD_HCRST | XHCI_USBCMD_INTE | XHCI_USBCMD_HSEE)

// USB Status values
#define XHCI_USBSTS_HCH  0x1   // HCHalted
#define XHCI_USBSTS_HSE  0x4   // Host System Error
#define XHCI_USBSTS_EINT 0x8   // Event interrupt
#define XHCI_USBSTS_PCD  0x10  // Port Change Detected
#define XHCI_USBSTS_MASK (XHCI_USBSTS_HSE | XHCI_USBSTS_EINT | XHCI_USBSTS_PCD)

// Device Notification Control values
#define XHCI_DNCTRL_MASK 0xFFFF

#define XHCI_ALIGN_64BYTE (~0x3FULL) // 64 byte aligned

// Command Ring Control values
#define XHCI_CRCR_RCS 0x1 // Ring Cycle State
#define XHCI_CRCR_CS  0x2 // Command Stop
#define XHCI_CRCR_CA  0x4 // Command Abort
#define XHCI_CRCR_CRR 0x8 // Command Ring Running

#define XHCI_CRCR_RMASK (XHCI_ALIGN_64BYTE | XHCI_CRCR_CRR)
#define XHCI_CRCR_WMASK (XHCI_ALIGN_64BYTE | XHCI_CRCR_RCS | XHCI_CRCR_CS | XHCI_CRCR_CA)

/*
 * Port registers
 */

// Each port occupies 0x10 bytes at XHCI_PORT_REGS_BASE
#define XHCI_REG_PORTSC    0x0 // Port Status and Control
#define XHCI_REG_PORTPMSC  0x4 // Port Power Management Status and Control
#define XHCI_REG_PORTLI    0x8 // Port Link Info
#define XHCI_REG_PORTHLPMC 0xC // Port Hardware LPM Control

#define XHCI_PORTSC_CCS 0x1      // Current Connect Status
#define XHCI_PORTSC_PED 0x2      // Port Enabled/Disabled
#define XHCI_PORTSC_PR  0x10     // Port Reset
#define XHCI_PORTSC_PP  0x200    // Port Power
#define XHCI_PORTSC_CSC 0x2000   // Connect Status Change
#define XHCI_PORTSC_PEC 0x4000   // Port Enabled/Disabled Change
#define XHCI_PORTSC_WRC 0x8000   // Warm Port Reset Change
#define XHCI_PORTSC_PRC 0x20000  // Port Reset Change
#define XHCI_PORTSC_PLC 0x40000  // Port Link State Change
#define XHCI_PORTSC_CEC 0x80000  // Port Config Error Change
#define XHCI_PORTSC_CAS 0x100000 // Cold Attach Status

/*
 * Runtime registers
 */

// Each interrupter occupies 0x20 bytes at XHCI_RUNTIME_BASE + 0x20
#define XHCI_REG_IMAN     0x0  // Interrupter Management
#define XHCI_REG_IMOD     0x4  // Interrupter Moderation
#define XHCI_REG_ERSTSZ   0x8  // Event Ring Segment Table Size
#define XHCI_REG_ERSTBA   0x10 // Event Ring Segment Table Base Address
#define XHCI_REG_ERSTBA_H 0x14 // Upper half of ERSTBA
#define XHCI_REG_ERDP     0x18 // Event Ring Dequeue Pointer
#define XHCI_REG_ERDP_H   0x1C // Upper half of ERDP

#define XHCI_IMAN_IP 0x1 // Interrupt Pending
#define XHCI_IMAN_IE 0x2 // Interrupt Enable

#define XHCI_ERDP_EHB 0x8 // Event Handler Busy

/*
 * Transfer Request Block
 */

#define XHCI_TRB_SIZE 0x10 // Size of a single TRB structure

// TRB structure fields
#define XHCI_TRB_PTR 0x0 // Data Buffer Pointer (64-bit)
#define XHCI_TRB_STS 0x8 // Status (32-bit)
#define XHCI_TRB_CTR 0xC // Control (32-bit)

// Get TRB transfer length from status word
#define XHCI_TRB_LENGTH(status) ((status) & 0x1FFFF)

// TRB Status word command completion codes
#define XHCI_TRB_COMP_SUCCESS     0x1  // Success
#define XHCI_TRB_COMP_DB_ERR      0x2  // Data Buffer Error
#define XHCI_TRB_COMP_BABBLE      0x3  // Babble Detected Error
#define XHCI_TRB_COMP_TX_ERR      0x4  // USB Transaction Error
#define XHCI_TRB_COMP_TRB_ERR     0x5  // TRB Error
#define XHCI_TRB_COMP_STALL       0x6  // Stall Error
#define XHCI_TRB_COMP_RESOURCE    0x7  // Resource Error
#define XHCI_TRB_COMP_NA_SLOTS    0x8  // No Slots Available Error
#define XHCI_TRB_COMP_STREAM_ERR  0xA  // Stream Error
#define XHCI_TRB_COMP_SLOT_NE     0xB  // Slot Not Enabled
#define XHCI_TRB_COMP_ENDPOINT_NE 0xC  // Endpoint Not Enabled
#define XHCI_TRB_COMP_SHORT_PKT   0xD  // Short Packet
#define XHCI_TRB_COMP_UNDERRUN    0xE  // Ring Underrun
#define XHCI_TRB_COMP_OVERRUN     0xF  // Ring Overrun
#define XHCI_TRB_COMP_EINVAL      0x11 // Parameter Error
#define XHCI_TRB_COMP_BW_OVER     0x12 // Bandwidth Overrun Error
#define XHCI_TRB_COMP_CTX_STATE   0x13 // Context State Error
#define XHCI_TRB_COMP_PING_ERR    0x14 // No Ping Response Error
#define XHCI_TRB_COMP_ER_FULL     0x15 // Event Ring is full
#define XHCI_TRB_COMP_DEV_ERR     0x16 // Incompatible Device Error
#define XHCI_TRB_COMP_MISSED_INT  0x17 // Missed Service Error
#define XHCI_TRB_COMP_CMD_STOP    0x18 // Stopped command ring
#define XHCI_TRB_COMP_CMD_ABORT   0x19 // Aborted command and stopped command ring
#define XHCI_TRB_COMP_STOP        0x1A // Transfer stopped by a stop endpoint command
#define XHCI_TRB_COMP_STOP_INVAL  0x1B // Same as STOP, but transfer length is invalid
#define XHCI_TRB_COMP_MEL_ERR     0x1D // Max Exit Latency Too Large Error
#define XHCI_TRB_COMP_BUFF_OVER   0x1F // Isoch Buffer Overrun
#define XHCI_TRB_COMP_ISSUES      0x20 // Event Lost Error
#define XHCI_TRB_COMP_UNKNOWN     0x21 // Undefined Error
#define XHCI_TRB_COMP_STRID_ERR   0x22 // Invalid Stream ID Error
#define XHCI_TRB_COMP_2ND_BW_ERR  0x23 // Secondary Bandwidth Error
#define XHCI_TRB_COMP_SPLIT_ERR   0x24 // Split Transaction Error

// Get TRB interrupter target to send events to
#define XHCI_TRB_INTERRUPTER(status) (((status) >> 22) & 0x3FF)

// TRB Control word bits
#define XHCI_TRB_CTR_C   0x1   // Cycle bit
#define XHCI_TRB_CTR_ENT 0x2   // Evaluate Next TRB
#define XHCI_TRB_CTR_ISP 0x4   // Interrupt on Short Packet
#define XHCI_TRB_CTR_NS  0x8   // No Snoop (Cache optimization hint, ignore)
#define XHCI_TRB_CTR_CH  0x10  // Chain bit
#define XHCI_TRB_CTR_IOC 0x20  // Interrupt On Completion
#define XHCI_TRB_CTR_IDT 0x40  // Immediate Data
#define XHCI_TRB_CTR_BEI 0x200 // Block Event Interrupt

// Get TRB type from a TRB control word
#define XHCI_TRB_TYPE(ctrl) (((ctrl) >> 10) & 0x3F)

// TRB types
#define XHCI_TRB_TYPE_NORMAL          0x1  // Normal
#define XHCI_TRB_TYPE_SETUP           0x2  // Setup Stage
#define XHCI_TRB_TYPE_DATA            0x3  // Data Stage
#define XHCI_TRB_TYPE_STATUS          0x4  // Status Stage
#define XHCI_TRB_TYPE_ISOC            0x5  // Isoch
#define XHCI_TRB_TYPE_LINK            0x6  // Link
#define XHCI_TRB_TYPE_EVENT_DATA      0x7  // Event Data
#define XHCI_TRB_TYPE_NOOP            0x8  // No-Op
#define XHCI_TRB_TYPE_ENABLE_SLOT     0x9  // Enable Slot Command
#define XHCI_TRB_TYPE_DISABLE_SLOT    0xA  // Disable Slot Command
#define XHCI_TRB_TYPE_ADDR_DEV        0xB  // Address Device Command
#define XHCI_TRB_TYPE_CONFIG_EP       0xC  // Configure Endpoint Command
#define XHCI_TRB_TYPE_EVAL_CONTEXT    0xD  // Evaluate Context Command
#define XHCI_TRB_TYPE_RESET_EP        0xE  // Reset Endpoint Command
#define XHCI_TRB_TYPE_STOP_RING       0xF  // Stop Endpoint Command
#define XHCI_TRB_TYPE_SET_DEQ         0x10 // Set TR Dequeue Pointer Command
#define XHCI_TRB_TYPE_RESET_DEV       0x11 // Reset Device Command
#define XHCI_TRB_TYPE_FORCE_EVENT     0x12 // Force Event Command (Optional, for virtualization only)
#define XHCI_TRB_TYPE_NEG_BANDWIDTH   0x13 // Negotiate Bandwidth Command (Optional)
#define XHCI_TRB_TYPE_SET_LT          0x14 // Set Latency Tolerance Value Command (Optional)
#define XHCI_TRB_TYPE_GET_BW          0x15 // Get Port Bandwidth Command (Optional)
#define XHCI_TRB_TYPE_FORCE_HEADER    0x16 // Force Header Command
#define XHCI_TRB_TYPE_CMD_NOOP        0x17 // No Op Command
#define XHCI_TRB_TYPE_TRANSFER        0x20 // Transfer Event
#define XHCI_TRB_TYPE_COMPLETION      0x21 // Command Completion Event
#define XHCI_TRB_TYPE_PORT_STATUS     0x22 // Port Status Change Event
#define XHCI_TRB_TYPE_BANDWIDTH_EVENT 0x23 // Bandwidth Request Event (Optional)
#define XHCI_TRB_TYPE_DOORBELL        0x24 // Doorbell Event (Optional, for virtualization only)
#define XHCI_TRB_TYPE_HC_EVENT        0x25 // Host Controller Event
#define XHCI_TRB_TYPE_DEV_NOTE        0x26 // Device Notification Event
#define XHCI_TRB_TYPE_MFINDEX_WRAP    0x27 // MFINDEX Wrap Event

static const uint32_t xhci_ext_caps[] = {
    0x02000802, // Supported Protocol Capability: USB 2.0
    0x20425355, // "USB "
    0x30001001, // Compatible ports 0x01 - 0x10, 3 protocol speeds
    0x00000000,
    0x000C0021, // Protocol speed 1: 12  Mbps
    0x05DC0012, // Protocol speed 2: 1.5 Mbps
    0x01E00023, // Protocol speed 3: 480 Mbps
    0x00000000,

    0x03000002, // Supported Protocol Capability: USB 3.0
    0x20425355, // "USB "
    0x10001011, // Compatible ports 0x11 - 0x20, 1 speed mode
    0x00000000,
    0x00050134, // Protocol speed 4: 5 Gbps
    0x00000000,
};

typedef struct {
    uint64_t ptr;
    uint32_t sts;
    uint32_t ctr;
    uint32_t interrupter;
} xhci_event_trb_t;

typedef struct {
    // Registers
    uint32_t iman;     // Interrupt Management
    uint32_t erstsz;   // Event Ring Segment Table Size
    uint32_t erstba;   // Event Ring Segment Base Address
    uint32_t erstba_h; // Event Ring Segment Base Address (High 32 bits)
    uint32_t erdp;     // Event Ring Dequeue Pointer
    uint32_t erdp_h;   // Event Ring Dequeue Pointer (High 32 bits)

    // Internal
    uint32_t crste;   // Current Ring Segment Table Entry
    uint32_t crsba;   // Current Ring Segment Base Address
    uint32_t crsba_h; // Current Ring Segment Base Address (High 32 bits)
    uint32_t crssz;   // Current Ring Segment Size
    uint32_t crse;    // Current Ring Segment Entry
} xhci_interrupter_t;

typedef struct {
    rvvm_pci_func_t* func;

    // Operational registers
    uint32_t usbcmd;
    uint32_t usbsts;
    uint32_t dnctrl;
    uint32_t crcr;
    uint32_t crcr_h;
    uint32_t dcbaap;
    uint32_t dcbaap_h;
    uint32_t config;

    xhci_interrupter_t ints[XHCI_MAX_INTRS];
} xhci_bus_t;

static inline void xhci_interrupt(xhci_bus_t* xhci, uint32_t irq_vec)
{
    atomic_or_uint32(&xhci->usbsts, XHCI_USBSTS_EINT);
    if (likely(atomic_load_uint32_relax(&xhci->usbcmd) & XHCI_USBCMD_INTE)) {
        // TODO: What interrupt vector should be used here?
        rvvm_pci_send_irq(xhci->func, irq_vec);
    }
}

static inline rvvm_addr_t xhci_load_addr(const uint32_t* low, const uint32_t* high)
{
    return atomic_load_uint32_relax(low) | (((uint64_t)atomic_load_uint32_relax(high)) << 32);
}

static inline rvvm_addr_t xhci_cmd_ring_addr(const xhci_bus_t* xhci)
{
    // Command ring must be 64-byte aligned
    return xhci_load_addr(&xhci->crcr, &xhci->crcr_h) & XHCI_ALIGN_64BYTE;
}

static void xhci_report_event(xhci_bus_t* xhci, const xhci_event_trb_t* event)
{
    if (event->interrupter < XHCI_MAX_INTRS) {
        xhci_interrupter_t* intr = &xhci->ints[event->interrupter];

        // Obtain TRB entry
        rvvm_addr_t crsba = 0;
        uint32_t crse = 0;
        uint32_t crse_next = 0;
        do {
            uint32_t crssz = atomic_load_uint32_relax(&intr->crssz);
            crsba = xhci_load_addr(&intr->crsba, &intr->crsba_h);
            crse = atomic_load_uint32_relax(&intr->crse);
            if (crse >= crssz) {
                // Reached end of ring segment
                rvvm_addr_t erstba = xhci_load_addr(&intr->erstba, &intr->erstba_h);
                uint32_t erstsz = atomic_load_uint32_relax(&intr->erstsz);
                uint32_t crste = atomic_load_uint32_relax(&intr->crste);
                uint32_t crste_next = 0;
                if (crste >= erstsz) {
                    // Reached end of ring segment table
                    crste_next = 0;
                } else {
                    crste_next = crste + 1;
                }

                uint8_t* crs_ptr = rvvm_pci_get_dma(xhci->func, erstba + (crste << 4), 0x10);
                if (likely(crs_ptr)) {
                    crsba = read_uint64_le(crs_ptr) & (~0x3FULL);
                    crssz = read_uint16_le(crs_ptr + 0x8);
                    rvvm_pci_end_dma(xhci->func, crs_ptr);
                    atomic_store_uint32_relax(&intr->crsba, crsba);
                    atomic_store_uint32_relax(&intr->crsba_h, crsba >> 32);
                    atomic_store_uint32_relax(&intr->crssz, crssz);
                } else {
                    rvvm_warn("XHCI DMA error for event ring segment table");
                    return;
                }

                if (!atomic_cas_uint32(&intr->crste, crste, crste_next)) {
                    // CAS failed - retry everything
                    continue;
                }

                crse_next = 0;
            } else {
                crse_next = crse + 1;
            }
        } while (!atomic_cas_uint32(&intr->crse, crse, crse_next));

        // Submit TRB
        uint8_t* dma = rvvm_pci_get_dma(xhci->func, crsba + (crse << 4), XHCI_TRB_SIZE);
        if (likely(dma)) {
            rvvm_warn("xhci event submitted at %x", (uint32_t)(crsba + (crse << 4)));
            write_uint64_le(dma + XHCI_TRB_PTR, event->ptr);
            write_uint32_le(dma + XHCI_TRB_STS, event->sts);
            atomic_store_uint32_le(dma + XHCI_TRB_CTR, event->ctr);
            rvvm_pci_end_dma(xhci->func, dma);

            atomic_or_uint32(&intr->erdp, XHCI_ERDP_EHB);
            if (atomic_or_uint32(&intr->iman, XHCI_IMAN_IP) & XHCI_IMAN_IE) {
                xhci_interrupt(xhci, 0);
            }
        } else {
            rvvm_warn("XHCI DMA error for event ring TRB");
        }
    }
}

static uint32_t xhci_port_reg_read(xhci_bus_t* xhci, size_t id, size_t offset)
{
    UNUSED(xhci);
    UNUSED(id);
    if (offset == XHCI_REG_PORTSC) {
        // NOTE: WIP testing stuff
        if (id == 0x10) {
            // NOTE: This causes a guest driver hang for now due to unimplemented cmd queue!
            return XHCI_PORTSC_CCS | XHCI_PORTSC_PED | XHCI_PORTSC_PP;
        }
        return XHCI_PORTSC_PED | XHCI_PORTSC_PP;
    }
    // Power management registers do not really matter
    return 0;
}

static void xhci_port_reg_write(xhci_bus_t* xhci, size_t id, size_t offset, uint32_t val)
{
    UNUSED(xhci);
    UNUSED(id);
    UNUSED(offset);
    UNUSED(val);
}

static void xhci_doorbell_write(xhci_bus_t* xhci, size_t id, uint32_t val)
{
    UNUSED(xhci);
    UNUSED(id);
    UNUSED(val);
    if (id == 0) {
        // Command Ring doorbell
        rvvm_warn("xhci command doorbell rang");
        uint32_t crcr = atomic_load_uint32_relax(&xhci->crcr);
        rvvm_addr_t cr_addr = xhci_cmd_ring_addr(xhci);
        rvvm_addr_t start = cr_addr;
        do {
            uint8_t* dma = rvvm_pci_get_dma(xhci->func, cr_addr, XHCI_TRB_SIZE);
            if (dma) {
                uint64_t ptr = read_uint64_le(dma);
                uint32_t sts = read_uint32_le(dma + 0x8);
                uint32_t ctr = read_uint32_le(dma + 0xC);
                rvvm_pci_end_dma(xhci->func, dma);

                rvvm_warn("crcr: %x, ctr: %x", crcr, ctr);
                if (!!(crcr & XHCI_CRCR_RCS) != !!(ctr & XHCI_TRB_CTR_C)) {
                    // Cycle bit mismatch, stop the ring
                    rvvm_warn("xhci command ring stopped");
                    atomic_store_uint32_relax(&xhci->crcr, (crcr & XHCI_CRCR_RCS) | (cr_addr & XHCI_ALIGN_64BYTE));
                    atomic_store_uint32_relax(&xhci->crcr_h, cr_addr >> 32);
                    break;
                }

                rvvm_warn("xhci command TRB: ptr %x, status %x, control %x", (uint32_t)ptr, sts, ctr);
                rvvm_warn("xhci command TRB Type: %x", XHCI_TRB_TYPE(ctr));

                xhci_event_trb_t event = {
                    .ptr = cr_addr,
                    .ctr = (XHCI_TRB_TYPE_COMPLETION << 10) | (crcr & XHCI_CRCR_RCS),
                    .sts = (XHCI_TRB_COMP_SUCCESS << 24),
                    .interrupter = XHCI_TRB_INTERRUPTER(ctr),
                };
                xhci_report_event(xhci, &event);

                if (XHCI_TRB_TYPE(ctr) == XHCI_TRB_TYPE_LINK) {
                    // Jump to link TRB pointer
                    cr_addr = ptr & XHCI_ALIGN_64BYTE;
                } else {
                    // Advance the ring
                    cr_addr += XHCI_TRB_SIZE;
                }
            } else {
                // DMA error
                rvvm_warn("xhci command DMA failed!");
                break;
            }
        } while (cr_addr != start);
    } else {
        rvvm_warn("xhci doorbell %08x to %02x", val, (uint32_t)id);
    }
}

static uint32_t xhci_interrupter_read(xhci_bus_t* xhci, size_t id, size_t offset)
{
    if (id < XHCI_MAX_INTRS) {
        xhci_interrupter_t* interrupter = &xhci->ints[id];
        switch (offset) {
            case XHCI_REG_IMAN:
                return atomic_load_uint32_relax(&interrupter->iman);
            case XHCI_REG_ERSTSZ:
                return atomic_load_uint32_relax(&interrupter->erstsz);
            case XHCI_REG_ERSTBA:
                return atomic_load_uint32_relax(&interrupter->erstba);
            case XHCI_REG_ERSTBA_H:
                return atomic_load_uint32_relax(&interrupter->erstba_h);
            case XHCI_REG_ERDP:
                return atomic_load_uint32_relax(&interrupter->erdp);
            case XHCI_REG_ERDP_H:
                return atomic_load_uint32_relax(&interrupter->erdp_h);
        }
    }
    return 0;
}

static void xhci_interrupter_write(xhci_bus_t* xhci, size_t id, size_t offset, uint32_t val)
{
    if (id < XHCI_MAX_INTRS) {
        xhci_interrupter_t* interrupter = &xhci->ints[id];
        switch (offset) {
            case XHCI_REG_IMAN:
                val = (val ^ XHCI_IMAN_IP);
                atomic_and_uint32(&interrupter->iman, val & (XHCI_IMAN_IP | XHCI_IMAN_IE));
                atomic_or_uint32(&interrupter->iman, val & XHCI_IMAN_IE);
                return;
            case XHCI_REG_ERSTSZ:
                atomic_store_uint32_relax(&interrupter->erstsz, val);
                return;
            case XHCI_REG_ERSTBA:
                atomic_store_uint32_relax(&interrupter->erstba, val & XHCI_ALIGN_64BYTE);
                return;
            case XHCI_REG_ERSTBA_H:
                atomic_store_uint32_relax(&interrupter->erstba_h, val);
                return;
            case XHCI_REG_ERDP:
                atomic_store_uint32_relax(&interrupter->erdp, val & XHCI_ALIGN_64BYTE);
                return;
            case XHCI_REG_ERDP_H:
                atomic_store_uint32_relax(&interrupter->erdp_h, val);
                return;
        }
    }
}

static void xhci_pci_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    xhci_bus_t* xhci = rvvm_region_data(dev);
    uint32_t val = 0;
    UNUSED(size);


    if ((off - XHCI_RUNTIME_BASE) < XHCI_RUNTIME_SIZE) {
        // Runtime registers
        size_t runtime_off = (off - XHCI_RUNTIME_BASE);
        if (runtime_off < 0x20) {
            // TODO: Microframe index at XHCI_RUNTIME_BASE
            val = 0;
        } else {
            // Interrupter read
            val = xhci_interrupter_read(xhci, (runtime_off - 0x20) >> 5, runtime_off & 0x1C);
        }

    } else if ((off - XHCI_PORT_REGS_BASE) < XHCI_PORT_REGS_SIZE) {
        // Port registers
        size_t port_id = (off - XHCI_PORT_REGS_BASE) >> 4;
        size_t port_off = (off & 0xC);
        val = xhci_port_reg_read(xhci, port_id, port_off);

    } else if (off >= XHCI_EXT_CAPS_BASE) {
        // Extended capabilities
        size_t entry = (off - XHCI_EXT_CAPS_BASE) >> 2;
        if (entry < STATIC_ARRAY_SIZE(xhci_ext_caps)) {
            val = xhci_ext_caps[entry];
        }

    } else switch (off) {
        // Capability registers
        case XHCI_REG_CAPLENGTH_HCIVERSION:
            val = XHCI_CAPLEN_HCIVERSION;
            break;
        case XHCI_REG_HCSPARAMS1:
            val = XHCI_HCSPARAMS1;
            break;
        case XHCI_REG_HCSPARAMS2:
            val = 0;
            break;
        case XHCI_REG_HCSPARAMS3:
            val = 0;
            break;
        case XHCI_REG_HCCPARAMS1:
            val = XHCI_HCCPARAMS1;
            break;
        case XHCI_REG_DBOFF:
            val = XHCI_DOORBELL_BASE;
            break;
        case XHCI_REG_RTSOFF:
            val = XHCI_RUNTIME_BASE;
            break;

        // Operational registers
        case XHCI_REG_USBCMD:
            val = atomic_load_uint32_relax(&xhci->usbcmd);
            break;
        case XHCI_REG_USBSTS:
            val = atomic_load_uint32_relax(&xhci->usbsts);
            if (!(atomic_load_uint32_relax(&xhci->usbcmd) & XHCI_USBCMD_RS)) {
                // Controller is halted
                val |= XHCI_USBSTS_HCH;
            }
            break;
        case XHCI_REG_PAGESIZE:
            val = 0x1; // 4k pages
            break;
        case XHCI_REG_DNCTRL:
            val = atomic_load_uint32_relax(&xhci->dnctrl);
            break;
        case XHCI_REG_CRCR:
            val = atomic_load_uint32_relax(&xhci->crcr) & XHCI_CRCR_RMASK;
            break;
        case XHCI_REG_CRCR_H:
            val = atomic_load_uint32_relax(&xhci->crcr_h);
            break;
        case XHCI_REG_DCBAAP:
            val = atomic_load_uint32_relax(&xhci->dcbaap);
            break;
        case XHCI_REG_DCBAAP_H:
            val = atomic_load_uint32_relax(&xhci->dcbaap_h);
            break;
        case XHCI_REG_CONFIG:
            val = atomic_load_uint32_relax(&xhci->config);
            break;

        default:
            rvvm_warn("xhci read  %08x from %04x", val, (uint32_t)off);
            break;
    }

    write_uint32_le(data, val);
}

static void xhci_pci_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    xhci_bus_t* xhci = rvvm_region_data(dev);
    uint32_t val = read_uint32_le(data);
    UNUSED(size);

    if ((off - XHCI_DOORBELL_BASE) < XHCI_DOORBELL_SIZE) {
        // Doorbell registers
        xhci_doorbell_write(xhci, (off - XHCI_DOORBELL_BASE) >> 2, val);

    } else if ((off - XHCI_RUNTIME_BASE) < XHCI_RUNTIME_SIZE) {
        // Runtime registers
        size_t runtime_off = (off - XHCI_RUNTIME_BASE);
        if (runtime_off >= 0x20) {
            // Interrupter write
            xhci_interrupter_write(xhci, (runtime_off - 0x20) >> 5, runtime_off & 0x1C, val);
        }

    } else if ((off - XHCI_PORT_REGS_BASE) < XHCI_PORT_REGS_SIZE) {
        // Port registers
        size_t port_id = (off - XHCI_PORT_REGS_BASE) >> 4;
        size_t port_off = (off & 0xC);
        xhci_port_reg_write(xhci, port_id, port_off, val);

    } else switch (off) {
        // Operational registers
        case XHCI_REG_USBCMD:
            if (val & XHCI_USBCMD_HCRST) {
                // Perform controller reset
                atomic_store_uint32_relax(&xhci->usbcmd, 0);
                atomic_store_uint32_relax(&xhci->usbsts, 0);
                atomic_store_uint32_relax(&xhci->dnctrl, 0);
                atomic_store_uint32_relax(&xhci->crcr,   0);
                atomic_store_uint32_relax(&xhci->crcr_h, 0);
                atomic_store_uint32_relax(&xhci->dcbaap, 0);
                atomic_store_uint32_relax(&xhci->dcbaap_h, 0);
                atomic_store_uint32_relax(&xhci->config, 0);
                // TODO: Atomicity
                memset(xhci->ints, 0, sizeof(xhci->ints));
            } else {
                atomic_store_uint32_relax(&xhci->usbcmd, val & XHCI_USBCMD_MASK);
            }
            break;
        case XHCI_REG_USBSTS:
            atomic_and_uint32(&xhci->usbsts, val);
            break;
        case XHCI_REG_DNCTRL:
            atomic_store_uint32_relax(&xhci->dnctrl, val & XHCI_DNCTRL_MASK);
            break;
        case XHCI_REG_CRCR:
            atomic_store_uint32_relax(&xhci->crcr, val & XHCI_CRCR_WMASK);
            break;
        case XHCI_REG_CRCR_H:
            atomic_store_uint32_relax(&xhci->crcr_h, val);
            break;
        case XHCI_REG_DCBAAP:
            atomic_store_uint32_relax(&xhci->dcbaap, val & XHCI_ALIGN_64BYTE);
            break;
        case XHCI_REG_DCBAAP_H:
            atomic_store_uint32_relax(&xhci->dcbaap_h, val);
            break;
        case XHCI_REG_CONFIG:
            atomic_store_uint32_relax(&xhci->config, EVAL_MAX(val, XHCI_MAX_SLOTS));
            break;

        default:
            rvvm_warn("xhci write %08x to   %04x", val, (uint32_t)off);
            break;
    }
}

static const rvvm_reg_type_t xhci_type = {
    .name     = "xhci",
    .read     = xhci_pci_read,
    .write    = xhci_pci_write,
    .min_size = 4,
    .max_size = 4,
};

rvvm_pci_func_t* rvvm_usb_xhci_init(rvvm_machine_t* machine, rvvm_pci_addr_t addr)
{
    xhci_bus_t* xhci = safe_new_obj(xhci_bus_t);

    rvvm_reg_desc_t xhci_bar = {
        .size = XHCI_BAR_SIZE,
        .data = xhci,
        .type = &xhci_type,
        .attr = RVVM_REG_ATTR_BAR64,
    };
    rvvm_pci_func_desc_t xhci_desc = {
        .vendor_id  = 0x100b, // National Semiconductor Corporation
        .device_id  = 0x0012, // USB Controller
        .class_code = 0x0C03, // Serial bus controller, USB controller
        .prog_iface = 0x30,   // XHCI
        .irq_pin = RVVM_PCI_PIN_INTA,
        .bar[0] = &xhci_bar,
    };

    rvvm_pci_func_t* func = rvvm_pci_func_init(machine, &xhci_desc, addr);
    if (func) {
        // Successfully plugged in
        xhci->func = func;
    }
    return func;
}

POP_OPTIMIZATION_SIZE

#endif
