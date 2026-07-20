/*
rtl8169.c - Realtek RTL8169 (8168B) NIC
Copyright (C) 2022  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

/*
 * TODO: Replace "tap_api.h" with <rvvm/rvvm_net.h>
 * TODO: Make network devices build with !USE_NET and use dummy backend
 */

#ifdef USE_NET

#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_pci.h>
#include <rvvm/rvvm_region.h>
#include <rvvm/rvvm_snapshot.h>

#include <util/mem_ops.h>
#include <util/utils.h>

#include "rtl8169.h"

/*
 * See https://people.freebsd.org/~wpaul/RealTek/RTL8111B_8168B_Registers_DataSheet_1.0.pdf
 */

/*
 * RTL8169 Registers
 */
#define RTL8169_REG_IDR0      0x00 // ID Register 0-3 (For MAC Address)
#define RTL8169_REG_IDR4      0x04 // ID Register 4-5
#define RTL8169_REG_MAR0      0x08 // Multicast Address Register 0-3
#define RTL8169_REG_MAR4      0x0C // Multicast Address Register 4-7
#define RTL8169_REG_DTCR1     0x10 // Dump Tally Counter Command Register (64-byte alignment)
#define RTL8169_REG_DTCR2     0x14
#define RTL8169_REG_TXDA1     0x20 // Transmit Descriptors Address (64-bit, 256-byte alignment)
#define RTL8169_REG_TXDA2     0x24
#define RTL8169_REG_TXHA1     0x28 // Transmit High Priority Descriptors Address (64-bit, 256-byte alignment)
#define RTL8169_REG_TXHA2     0x2C
#define RTL8169_REG_CR32      0x34 // Command Register (Aligned to 32-bit boundary for ease)
#define RTL8169_REG_CR        0x37 // Command Register (Actual offset in spec)
#define RTL8169_REG_TPOLL     0x38 // Transmit Priority Polling
#define RTL8169_REG_IMR       0x3C // Interrupt Mask
#define RTL8169_REG_ISR       0x3E // Interrupt Status
#define RTL8169_REG_TCR       0x40 // Transmit Configuration Register
#define RTL8169_REG_RCR       0x44 // Receive Configuration Register
#define RTL8169_REG_TCTR      0x48 // Timer Counter Register
#define RTL8169_REG_MPC       0x4C // Missed Packet Counter
#define RTL8169_REG_9346      0x50 // 93C46 Command Register, CFG 0-2
#define RTL8169_REG_CFG3      0x54 // Configuration Register 3-5
#define RTL8169_REG_TINT      0x58 // Timer Interrupt Register
#define RTL8169_REG_PHYAR     0x60 // PHY Access Register
#define RTL8169_REG_TBIR0     0x64 // TBI Control and Status Register
#define RTL8169_REG_TBANR     0x68 // TBI Auto-Negotiation Advertisement Register
#define RTL8169_REG_PHYS      0x6C // PHY Status Register
#define RTL8169_REG_ERIDR     0x70 // ERI GPHY Data Register, rtl8168b specific
#define RTL8169_REG_ERIAR     0x74 // ERI GPHY Access Register, rtl8168b specific
#define RTL8169_REG_EPHAR     0x80 // EPHY GPHY Data Register, rtl816cp specific
#define RTL8169_REG_OCPDR     0xB0 // OCP GPHY Data Register, rtl8168dp specific
#define RTL8169_REG_OCPAR     0xB4 // OCP GPHY Access Register, rtl8168dp specific
#define RTL8169_REG_RMS32     0xD8 // RX Packet Maximum Size (Aligned to 32-bit boundary for ease)
#define RTL8169_REG_RMS       0xDA // RX Packet Maximum Size (Actual offset in spec)
#define RTL8169_REG_CPCR      0xE0 // C+ Command Register
#define RTL8169_REG_RXDA1     0xE4 // Receive Descriptor Address (64-bit, 256-byte alignment)
#define RTL8169_REG_RXDA2     0xE8
#define RTL8169_REG_MTPS      0xEC // TX Packet Maximum Size

/*
 * Command Register bits
 */
#define RTL8169_CR_TE         0x04 // Transmitter Enable
#define RTL8169_CR_RE         0x08 // Receiver Enable
#define RTL8169_CR_RW         0x0C // R/W Register bits mask
#define RTL8169_CR_RST        0x10 // Reset

/*
 * Transmit Polling bits
 */
#define RTL8169_TPOLL_FSW     0x01 // Forced Software Interrupt
#define RTL8169_TPOLL_NPQ     0x40 // Normal Priority Queue Polling
#define RTL8169_TPOLL_HPQ     0x80 // High Priority Queue Polling

/*
 * Interrupt Status bits
 */
#define RTL8169_IRQ_ROK       0x00 // Receive OK
#define RTL8169_IRQ_RER       0x01 // Receiver Error
#define RTL8169_IRQ_TOK       0x02 // Transmit OK
#define RTL8169_IRQ_TER       0x03 // Transmitter Error
#define RTL8169_IRQ_RDU       0x04 // RX Descriptor Unavailable
#define RTL8169_IRQ_LCG       0x05 // Link Change
#define RTL8169_IRQ_FOVW      0x06 // RX FIFO Overflow (For RX ring overflow, use RDU)
#define RTL8169_IRQ_TDU       0x07 // TX Descriptor Unavailable
#define RTL8169_IRQ_SWI       0x10 // Software Interrupt

/*
 * Transmit Configuration bits
 */
#define RTL8169_TCR_IFG       0x03000000UL // 96ns for 1Gbit
#define RTL8169_TCR_NOCRC     0x00010000UL // No CRC applied for TX
#define RTL8169_TCR_MXDMA     0x00000700UL // Unlimited DMA burst
#define RTL8169_TCR_DEFAULT   (RTL8169_TCR_IFG | RTL8169_TCR_MXDMA)

/*
 * RTL8169 Family Models (XIDs)
 */
#define RTL8169_XID_RTL8169S  0x00800000UL // RTL_GIGA_MAC_VER_02
#define RTL8169_XID_RTL8168B  0x38000000UL // RTL_GIGA_MAC_VER_17
#define RTL8169_XID_RTL8168CP 0x3C800000UL // RTL_GIGA_MAC_VER_24
#define RTL8169_XID_RTL8168DP 0x28B00000UL // RTL_GIGA_MAC_VER_31
#define RTL8169_XID_RTL8168EP 0x50200000UL // RTL_GIGA_MAC_VER_51
#define RTL8169_XID_RTL8117   0x54B00000UL // RTL_GIGA_MAC_VER_53

/*
 * Receive Configuration bits
 */
#define RTL8169_RCR_AAP       0x0001 // Accept All Packets with Destination Address
#define RTL8169_RCR_APM       0x0002 // Accept Physical Match Packets
#define RTL8169_RCR_AMP       0x0004 // Accept Multicast Packets
#define RTL8169_RCR_ABP       0x0008 // Accept Broadcast Packets
#define RTL8169_RCR_9356      0x0040 // EEPROM is 9356
#define RTL8169_RCR_MXDMA     0x0700 // Unlimited DMA Burst
#define RTL8169_RCR_RXFTH     0xE000 // No Rx threshold
#define RTL8169_RCR_DEFAULT   0xE70F // Default RX config

/*
 * PHY Status bits
 */
#define RTL8169_PHY_STATUS    0x73 // Link up, full duplex, 1Gbit, flow control ON

/*
 * C+ Command bits
 */
#define RTL8169_CPCR_RXCSUM   0x20 // Receive Checksum Offload Enabled
#define RTL8169_CPCR_RXVLAN   0x40 // Receive VLAN De-tagging Enabled

/*
 * Common Descriptor flags
 */
#define RTL8169_DESC_OWN      0x80000000UL // Descriptor owned by RTL8169
#define RTL8169_DESC_EOR      0x40000000UL // End of Descriptor Ring
#define RTL8169_DESC_FS       0x20000000UL // First Segment Descriptor
#define RTL8169_DESC_LS       0x10000000UL // Last Segment Descriptor

/*
 * TX Descriptor flags
 */
#define RTL8169_DESC_LGSEN    0x08000000UL // Enable Large Send Offload
#define RTL8169_DESC_TXSTA    0x70000000UL // EOR | FS | LS

/*
 * RX Descriptor flags
 */
#define RTL8169_DESC_PAM      0x04000000UL // Physical Address Matched
#define RTL8169_DESC_BAR      0x02000000UL // Broadcast Address Received
#define RTL8169_DESC_RSV1     0x00800000UL // Reserved (Always 1)
#define RTL8169_DESC_UDP      0x00040000UL // UDP/IP Received
#define RTL8169_DESC_TCP      0x00020000UL // TCP/IP Received
#define RTL8169_DESC_RXSTA    0x34820000UL // FS | LS | PAM | TCP

/*
 * PHY registers
 */
#define RTL8169_PHY_BMCR      0x00
#define RTL8169_PHY_BMSR      0x01
#define RTL8169_PHY_ID1       0x02
#define RTL8169_PHY_ID2       0x03
#define RTL8169_PHY_GBCR      0x09
#define RTL8169_PHY_GBSR      0x0A
#define RTL8169_PHY_GBESR     0x0F

/*
 * EEPROM pins
 */
#define RTL8169_EEPROM_DOU    0x01 // EEPROM Data out
#define RTL8169_EEPROM_DIN    0x02 // EEPROM Data in
#define RTL8169_EEPROM_CLK    0x04 // EEPROM Clock
#define RTL8169_EEPROM_SEL    0x08 // EEPROM Chip select
#define RTL8169_EEMODE_PRG    0x80 // EEPROM Programming mode

/*
 * Size constants
 */
#define RTL8169_MAX_FIFO_SIZE 0x0400 // 1024 FIFO entries
#define RTL8169_MAC_SIZE      0x0006
#define RTL8169_RMS           0x1FFF // RX Packet Maximum Size: 8191
#define RTL8169_MTPS          0x003B // TX Packet Maximum Size: 7552

typedef struct {
    uint32_t addr;
    uint32_t addr_h;
    uint32_t index;
} rtl8169_ring_t;

typedef struct {
    uint32_t pins;
    uint32_t addr;
    uint32_t word;
    uint32_t cbit;
    uint32_t read;
} rtl8169_at93c56_t;

typedef struct {
    rvvm_pci_func_t* func;
    tap_dev_t*       tap;

    // EEPROM (Used to retreive MAC address)
    rtl8169_at93c56_t eeprom;

    // RX / TX / High priority TX queues
    rtl8169_ring_t rx;
    rtl8169_ring_t tx;
    rtl8169_ring_t txp;

    // RTL8169 registers
    uint32_t cr;
    uint32_t imr;
    uint32_t isr;
    uint32_t phydr;
    uint32_t phyar;

    // Frame segmentation reassembly buffer
    uint8_t  seg_buff[0x1000];
    uint32_t seg_size;

    // Cleanup region counter
    uint32_t cleanup;
} rtl8169_dev_t;

/*
 * Ring handling
 */

static inline rvvm_addr_t rtl8169_ring_addr(const rtl8169_ring_t* ring)
{
    return atomic_load_uint32_relax(&ring->addr) | (((uint64_t)atomic_load_uint32_relax(&ring->addr_h)) << 32);
}

static uint8_t* rtl8169_ring_prepare_desc(rtl8169_dev_t* rtl8169, rtl8169_ring_t* ring)
{
    uint8_t*    desc = NULL;
    rvvm_addr_t base = rtl8169_ring_addr(ring);
    uint32_t    curr = atomic_load_uint32_relax(&ring->index);
    uint32_t    next = 0;
    uint32_t    flag = 0;
    while (true) {
        desc = rvvm_pci_get_dma(rtl8169->func, base + (curr << 4), 0x10);
        if (unlikely(!desc)) {
            rvvm_debug("rtl8169: descriptor dma error");
            return NULL;
        }
        flag = read_uint32_le(desc);
        if (unlikely(!(flag & RTL8169_DESC_OWN))) {
            // End of TX ring / TX ring full
            rvvm_pci_end_dma(rtl8169->func, desc);
            return NULL;
        }
        next = curr + 1;
        if (unlikely((flag & RTL8169_DESC_EOR) || curr >= RTL8169_MAX_FIFO_SIZE)) {
            // RX ring wrapped
            next = 0;
        }
        if (atomic_cas_uint32(&ring->index, curr, next)) {
            // Claimed RX descriptor
            break;
        } else {
            // Retry
            rvvm_pci_end_dma(rtl8169->func, desc);
            desc = NULL;
        }
    }
    return desc;
}

static void rtl8169_ring_reset(rtl8169_ring_t* ring)
{
    atomic_store_uint32_relax(&ring->addr, 0);
    atomic_store_uint32_relax(&ring->addr_h, 0);
    atomic_store_uint32_relax(&ring->index, 0);
}

static void rtl8169_ring_suspend(rvvm_snapshot_t* snap, rtl8169_ring_t* ring)
{
    rvvm_snapshot_field(snap, ring->addr);
    rvvm_snapshot_field(snap, ring->addr_h);
    rvvm_snapshot_field(snap, ring->index);
}

/*
 * Interrupt handling
 */

static void rtl8169_update_irqs(rtl8169_dev_t* rtl8169)
{
    uint32_t isr = atomic_load_uint32_relax(&rtl8169->isr);
    uint32_t imr = atomic_load_uint32_relax(&rtl8169->imr);
    rvvm_pci_set_irq(rtl8169->func, 0, !!(isr & imr));
}

static void rtl8169_interrupt(rtl8169_dev_t* rtl8169, size_t irq)
{
    uint32_t irqs = 1U << irq;
    atomic_or_uint32(&rtl8169->isr, irqs);
    if (irqs & atomic_load_uint32_relax(&rtl8169->imr)) {
        rvvm_pci_raise_irq(rtl8169->func, 0);
    }
}

/*
 * EEPROM handling (For MAC retrieval)
 */

static uint16_t rtl8169_at93c56_read_word(rtl8169_dev_t* rtl8169, uint8_t addr)
{
    switch (addr) {
        case 0x00: // Device ID
            return 0x8129;
        case 0x07: // MAC words
        case 0x08:
        case 0x09: {
            uint8_t mac[6] = {0};
            tap_get_mac(rtl8169->tap, mac);
            return read_uint16_le_m(mac + ((addr - 7) << 1));
        }
    }
    return 0;
}

static void rtl8169_at93c56_write_pins(rtl8169_dev_t* rtl8169, uint8_t pins)
{
    rtl8169_at93c56_t* eeprom = &rtl8169->eeprom;
    if (pins & RTL8169_EEMODE_PRG) {
        uint32_t prev = atomic_load_uint32_relax(&eeprom->pins);
        if ((pins & ~prev) & RTL8169_EEPROM_CLK) {
            // Clock pulled high
            uint32_t cbit = atomic_load_uint32_relax(&eeprom->cbit);
            uint32_t addr = atomic_load_uint32_relax(&eeprom->addr);
            if (atomic_load_uint32_relax(&eeprom->read)) {
                // Push data bits
                uint32_t word = atomic_load_uint32_relax(&eeprom->word);
                if (!cbit) {
                    word = rtl8169_at93c56_read_word(rtl8169, addr);
                    atomic_store_uint32_relax(&eeprom->word, word);
                }
                if (word & (0x8000 >> cbit)) {
                    pins |= RTL8169_EEPROM_DOU;
                } else {
                    pins &= ~RTL8169_EEPROM_DOU;
                }
                if (cbit >= 15) {
                    atomic_store_uint32_relax(&eeprom->cbit, 0);
                    atomic_store_uint32_relax(&eeprom->addr, addr + 1);
                } else {
                    atomic_store_uint32_relax(&eeprom->cbit, cbit + 1);
                }
            } else {
                // Get starting addr, ignore command (Act as readonly eeprom)
                if (cbit >= 3) {
                    addr = (addr << 1) | !!(pins & RTL8169_EEPROM_DIN);
                    atomic_store_uint32_relax(&eeprom->addr, addr);
                }
                if (cbit >= 11) {
                    atomic_store_uint32_relax(&eeprom->cbit, 0);
                    atomic_store_uint32_relax(&eeprom->read, true);
                } else {
                    atomic_store_uint32_relax(&eeprom->cbit, cbit + 1);
                }
            }
        }
        if (!(pins & RTL8169_EEPROM_SEL)) {
            // End of transfer, request addr next time
            atomic_store_uint32_relax(&eeprom->addr, 0);
            atomic_store_uint32_relax(&eeprom->cbit, 0);
            atomic_store_uint32_relax(&eeprom->read, false);
        }
    }
    atomic_store_uint32_relax(&rtl8169->eeprom.pins, pins);
}

static void rtl8169_at93c56_reset(rtl8169_at93c56_t* eeprom)
{
    atomic_store_uint32_relax(&eeprom->pins, 0);
    atomic_store_uint32_relax(&eeprom->addr, 0);
    atomic_store_uint32_relax(&eeprom->word, 0);
    atomic_store_uint32_relax(&eeprom->cbit, 0);
    atomic_store_uint32_relax(&eeprom->read, 0);
}

static void rtl8169_at93c56_suspend(rvvm_snapshot_t* snap, rtl8169_at93c56_t* eeprom)
{
    rvvm_snapshot_field(snap, eeprom->pins);
    rvvm_snapshot_field(snap, eeprom->addr);
    rvvm_snapshot_field(snap, eeprom->word);
    rvvm_snapshot_field(snap, eeprom->cbit);
    rvvm_snapshot_field(snap, eeprom->read);
}

/*
 * PHY handling (For link state detection)
 */

static uint16_t rtl8169_phy_read(uint32_t reg)
{
    switch (reg) {
        case RTL8169_PHY_BMCR:
            return 0x1140; // Full-duplex 1Gbps, Auto-Negotiation Enabled
        case RTL8169_PHY_BMSR:
            return 0x796D; // Link is up; Supports GBESR
        case RTL8169_PHY_ID1:
            return 0x001C; // Realtek
        case RTL8169_PHY_ID2:
            return 0xC910; // Generic 1 GBps PHY
        case RTL8169_PHY_GBCR:
            return 0x0200; // Advertise 1000BASE-T Full duplex
        case RTL8169_PHY_GBSR:
            return 0x3800; // Link partner is capable of 1000BASE-T Full duplex
        case RTL8169_PHY_GBESR:
            return 0xA000; // 1000BASE-T Full duplex capable
    }
    return 0;
}

static void rtl8169_phy_handle(rtl8169_dev_t* rtl8169, uint32_t cmd)
{
    uint32_t reg = (cmd >> 16) & 0x1F;
    uint32_t val = ((cmd & 0xFFFF0000) ^ 0x80000000) | rtl8169_phy_read(reg);
    atomic_store_uint32_relax(&rtl8169->phyar, val);
}

static void rtl8169_phy_eri_handle(rtl8169_dev_t* rtl8169, uint32_t cmd)
{
    uint32_t reg = cmd & 0x0FFF;
    uint32_t val = ((cmd & 0xFFFF0000) ^ 0x80000000);
    atomic_store_uint32_relax(&rtl8169->phydr, rtl8169_phy_read(reg));
    atomic_store_uint32_relax(&rtl8169->phyar, val);
}

static void rtl8169_phy_ocp_handle(rtl8169_dev_t* rtl8169, uint32_t cmd)
{
    uint32_t reg = (cmd >> 16) & 0x1F;
    uint32_t val = ((cmd & 0xFFFF0000) ^ 0x80000000);
    atomic_store_uint32_relax(&rtl8169->phydr, rtl8169_phy_read(reg));
    atomic_store_uint32_relax(&rtl8169->phyar, val);
}

/*
 * Transmit / Receive
 */

static bool rtl8169_feed_rx(void* net_dev, const void* pkt_data, size_t pkt_size)
{
    rtl8169_dev_t* rtl8169 = net_dev;
    if (likely(atomic_load_uint32_relax(&rtl8169->cr) & RTL8169_CR_RE)) {
        // Receiver enabled, prepare RX descriptor
        uint8_t* desc = rtl8169_ring_prepare_desc(rtl8169, &rtl8169->rx);
        if (likely(desc)) {
            uint32_t    flag = read_uint32_le(desc);
            rvvm_addr_t addr = read_uint64_le(desc + 8);
            size_t      size = flag & 0x3FFF;
            uint8_t*    ptr  = rvvm_pci_get_dma(rtl8169->func, addr, size);
            if (likely(ptr && size >= pkt_size + 4)) {
                memcpy(ptr, pkt_data, pkt_size);
                memset(ptr + pkt_size, 0, 4); // Append fake CRC32
            } else {
                rvvm_debug("rtl8169: rx packet dma error");
            }
            rvvm_pci_end_dma(rtl8169->func, ptr);
            atomic_store_uint32_le(desc, (flag & RTL8169_DESC_EOR) | RTL8169_DESC_RXSTA | (pkt_size + 4));
            rvvm_pci_end_dma(rtl8169->func, desc);
            rtl8169_interrupt(rtl8169, RTL8169_IRQ_ROK);
            return true;
        }
    }
    return false;
}

// Reassemble transmitted segmented frame
static void rtl8169_tx_segmented(rtl8169_dev_t* rtl8169, void* seg_ptr, size_t seg_size, uint32_t flag)
{
    uint32_t size = 0;
    if (flag & RTL8169_DESC_FS) {
        // Start assembling a new packet
        atomic_store_uint32_relax(&rtl8169->seg_size, 0);
    } else {
        size = atomic_load_uint32_relax(&rtl8169->seg_size);
    }
    if (size + seg_size <= sizeof(rtl8169->seg_buff)) {
        memcpy(rtl8169->seg_buff + size, seg_ptr, seg_size);
        size += seg_size;
        if (flag & RTL8169_DESC_LS) {
            // Last segment found
            tap_send(rtl8169->tap, rtl8169->seg_buff, size);
            atomic_store_uint32_relax(&rtl8169->seg_size, -1);
        } else {
            atomic_store_uint32_relax(&rtl8169->seg_size, size);
        }
    } else {
        // Transmit error
        rtl8169_interrupt(rtl8169, RTL8169_IRQ_TER);
        atomic_store_uint32_relax(&rtl8169->seg_size, -1);
    }
}

static void rtl8169_tx_doorbell(rtl8169_dev_t* rtl8169, rtl8169_ring_t* ring)
{
    bool tx_irq = false;
    if (likely(atomic_load_uint32_relax(&rtl8169->cr) & RTL8169_CR_TE)) {
        // Transmitter enabled, prepare TX descriptor
        uint8_t* desc = NULL;
        while ((desc = rtl8169_ring_prepare_desc(rtl8169, ring))) {
            uint32_t    flag = read_uint32_le(desc);
            rvvm_addr_t addr = read_uint64_le(desc + 8);
            size_t      size = flag & 0x3FFF;
            void*       ptr  = rvvm_pci_get_dma(rtl8169->func, addr, size);
            if (likely(ptr)) {
                if ((flag & RTL8169_DESC_FS) && (flag & RTL8169_DESC_LS)) {
                    // Normal contiguous frame
                    tap_send(rtl8169->tap, ptr, size);
                } else {
                    // Segmented frame
                    rtl8169_tx_segmented(rtl8169, ptr, size, flag);
                }
                rvvm_pci_end_dma(rtl8169->func, ptr);
            } else {
                rvvm_debug("rtl8169: tx packet dma error");
                atomic_store_uint32_relax(&rtl8169->seg_size, -1);
            }
            atomic_store_uint32_le(desc, flag & RTL8169_DESC_TXSTA);
            rvvm_pci_end_dma(rtl8169->func, desc);
            tx_irq = true;
        }
        if (tx_irq) {
            rtl8169_interrupt(rtl8169, RTL8169_IRQ_TOK);
        }
    }
}

/*
 * Device frontend
 */

static void rtl8169_reset(rvvm_reg_dev_t* dev)
{
    rtl8169_dev_t* rtl8169 = rvvm_region_data(dev);

    // Reset registers
    atomic_store_uint32_relax(&rtl8169->cr, 0);
    atomic_store_uint32_relax(&rtl8169->imr, 0);
    atomic_store_uint32_relax(&rtl8169->isr, 0);
    atomic_store_uint32_relax(&rtl8169->phydr, 0);
    atomic_store_uint32_relax(&rtl8169->phyar, 0);
    atomic_store_uint32_relax(&rtl8169->seg_size, 0);
    rtl8169_update_irqs(rtl8169);

    // Reset rings
    rtl8169_ring_reset(&rtl8169->rx);
    rtl8169_ring_reset(&rtl8169->tx);
    rtl8169_ring_reset(&rtl8169->txp);

    // Reset EEPROM
    rtl8169_at93c56_reset(&rtl8169->eeprom);
}

static void rtl8169_suspend(rvvm_reg_dev_t* dev, rvvm_snapshot_t* snap, bool resume)
{
    if (snap) {
        rtl8169_dev_t* rtl8169 = rvvm_region_data(dev);

        // Pause NIC RX, snapshot registers
        uint32_t cr = atomic_swap_uint32(&rtl8169->cr, 0);
        rvvm_snapshot_field(snap, cr);
        rvvm_snapshot_field(snap, rtl8169->imr);
        rvvm_snapshot_field(snap, rtl8169->isr);
        rvvm_snapshot_field(snap, rtl8169->phydr);
        rvvm_snapshot_field(snap, rtl8169->phyar);
        rtl8169_update_irqs(rtl8169);

        // Snapshot rings
        rtl8169_ring_suspend(snap, &rtl8169->rx);
        rtl8169_ring_suspend(snap, &rtl8169->tx);
        rtl8169_ring_suspend(snap, &rtl8169->txp);

        // Snapshot EEPROM
        rtl8169_at93c56_suspend(snap, &rtl8169->eeprom);

        // Resume
        atomic_swap_uint32(&rtl8169->cr, cr);
    }
    UNUSED(resume);
}

static void rtl8169_pci_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    rtl8169_dev_t* rtl8169 = rvvm_region_data(dev);
    uint32_t       val     = 0;

    switch (off & ~0x03) {
        case RTL8169_REG_IDR0:
        case RTL8169_REG_IDR4: {
            uint8_t mac[6] = {0};
            tap_get_mac(rtl8169->tap, mac);
            val = read_uint32_le(mac + (off & ~0x03));
            break;
        }
        case RTL8169_REG_IMR:
            val  = atomic_load_uint32_relax(&rtl8169->imr);
            val |= atomic_load_uint32_relax(&rtl8169->isr) << 16;
            break;
        case RTL8169_REG_CR32:
            val = atomic_load_uint32_relax(&rtl8169->cr) << 24;
            break;
        case RTL8169_REG_TCR:
            val = RTL8169_TCR_DEFAULT | RTL8169_XID_RTL8168B;
            break;
        case RTL8169_REG_RCR:
            val = RTL8169_RCR_DEFAULT;
            break;
        case RTL8169_REG_9346:
            val = atomic_load_uint32_relax(&rtl8169->eeprom.pins);
            break;
        case RTL8169_REG_ERIDR:
        case RTL8169_REG_OCPDR:
            val = atomic_load_uint32_relax(&rtl8169->phydr);
            break;
        case RTL8169_REG_PHYAR:
        case RTL8169_REG_ERIAR:
        case RTL8169_REG_EPHAR:
        case RTL8169_REG_OCPAR:
            val = atomic_load_uint32_relax(&rtl8169->phyar);
            break;
        case RTL8169_REG_PHYS:
            val = RTL8169_PHY_STATUS;
            break;
        case RTL8169_REG_TXDA1:
            val = atomic_load_uint32_relax(&rtl8169->tx.addr);
            break;
        case RTL8169_REG_TXDA2:
            val = atomic_load_uint32_relax(&rtl8169->tx.addr_h);
            break;
        case RTL8169_REG_TXHA1:
            val = atomic_load_uint32_relax(&rtl8169->txp.addr);
            break;
        case RTL8169_REG_TXHA2:
            val = atomic_load_uint32_relax(&rtl8169->txp.addr_h);
            break;
        case RTL8169_REG_CPCR:
            val = RTL8169_CPCR_RXCSUM | RTL8169_CPCR_RXVLAN;
            break;
        case RTL8169_REG_RXDA1:
            val = atomic_load_uint32_relax(&rtl8169->rx.addr);
            break;
        case RTL8169_REG_RXDA2:
            val = atomic_load_uint32_relax(&rtl8169->rx.addr_h);
            break;
        case RTL8169_REG_RMS32:
            val = RTL8169_RMS << 16;
            break;
        case RTL8169_REG_MTPS:
            val = RTL8169_MTPS;
            break;
    }

    write_uint32_le(&val, val);
    memcpy(data, ((uint8_t*)&val) + (off & 0x03), size);
}

static void rtl8169_pci_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    rtl8169_dev_t* rtl8169 = rvvm_region_data(dev);
    uint32_t       val     = 0;

    if (likely(size == 2)) {
        val = read_uint16_le(data);
    } else if (likely(size == 1)) {
        val = read_uint8(data);
    } else {
        val = read_uint32_le(data);
    }

    switch (off) {
        case RTL8169_REG_IDR0:
        case RTL8169_REG_IDR4: {
            uint8_t mac[6] = {0};
            tap_get_mac(rtl8169->tap, mac);
            memcpy(mac + off, data, EVAL_MIN(size, 6 - off));
            tap_set_mac(rtl8169->tap, mac);
            break;
        }
        case RTL8169_REG_IMR:
            atomic_store_uint32_relax(&rtl8169->imr, (uint16_t)val);
            rtl8169_update_irqs(rtl8169);
            break;
        case RTL8169_REG_ISR:
            if (atomic_and_uint32(&rtl8169->isr, ~val) & val) {
                rtl8169_update_irqs(rtl8169);
            }
            break;
        case RTL8169_REG_CR:
            atomic_store_uint32_relax(&rtl8169->cr, val & RTL8169_CR_RW);
            if (val & RTL8169_CR_RST) {
                rtl8169_reset(dev);
            }
            break;
        case RTL8169_REG_TPOLL:
            if (val & RTL8169_TPOLL_HPQ) {
                rtl8169_tx_doorbell(rtl8169, &rtl8169->txp);
            }
            if (val & RTL8169_TPOLL_NPQ) {
                rtl8169_tx_doorbell(rtl8169, &rtl8169->tx);
            }
            if (val & RTL8169_TPOLL_FSW) {
                rtl8169_interrupt(rtl8169, RTL8169_IRQ_SWI);
            }
            break;
        case RTL8169_REG_9346:
            rtl8169_at93c56_write_pins(rtl8169, val);
            break;
        case RTL8169_REG_TXDA1:
            atomic_store_uint32_relax(&rtl8169->tx.addr, val & ~0xFFU);
            break;
        case RTL8169_REG_TXDA2:
            atomic_store_uint32_relax(&rtl8169->tx.addr_h, val);
            break;
        case RTL8169_REG_TXHA1:
            atomic_store_uint32_relax(&rtl8169->txp.addr, val & ~0xFFU);
            break;
        case RTL8169_REG_TXHA2:
            atomic_store_uint32_relax(&rtl8169->txp.addr_h, val);
            break;
        case RTL8169_REG_RXDA1:
            atomic_store_uint32_relax(&rtl8169->rx.addr, val & ~0xFFU);
            break;
        case RTL8169_REG_RXDA2:
            atomic_store_uint32_relax(&rtl8169->rx.addr_h, val);
            break;
        case RTL8169_REG_PHYAR:
        case RTL8169_REG_EPHAR:
            rtl8169_phy_handle(rtl8169, val);
            break;
        case RTL8169_REG_ERIAR:
            rtl8169_phy_eri_handle(rtl8169, val);
            break;
        case RTL8169_REG_OCPAR:
            rtl8169_phy_ocp_handle(rtl8169, val);
            break;
    }
}

static void rtl8169_cleanup(rvvm_reg_dev_t* dev)
{
    rtl8169_dev_t* rtl8169 = rvvm_region_data(dev);
    // The device has 2 regions
    if (++rtl8169->cleanup == 2) {
        tap_close(rtl8169->tap);
        free(rtl8169);
    }
}

static rvvm_reg_type_t rtl8169_type = {
    .name     = "rtl8169",
    .read     = rtl8169_pci_read,
    .write    = rtl8169_pci_write,
    .reset    = rtl8169_reset,
    .suspend  = rtl8169_suspend,
    .cleanup  = rtl8169_cleanup,
    .min_size = 1,
    .max_size = 4,
};

RVVM_PUBLIC rvvm_pci_func_t* rvvm_rtl8169_init(rvvm_machine_t* machine, tap_dev_t* tap, rvvm_pci_addr_t addr)
{
    rtl8169_dev_t* rtl8169 = safe_new_obj(rtl8169_dev_t);
    tap_net_dev_t  nic     = {
        .net_dev = rtl8169,
        .feed_rx = rtl8169_feed_rx,
    };

    rtl8169->tap = tap;
    tap_attach(tap, &nic);
    if (rtl8169->tap == NULL) {
        rvvm_error("Failed to create TAP device!");
        free(rtl8169);
        return NULL;
    }

    rvvm_reg_desc_t rtl8169_io = {
        .size = 0x100,
        .data = rtl8169,
        .type = &rtl8169_type,
        .attr = RVVM_REG_ATTR_PIO,
    };
    rvvm_reg_desc_t rtl8169_mmio = {
        .size = 0x1000,
        .data = rtl8169,
        .type = &rtl8169_type,
        .attr = RVVM_REG_ATTR_BAR64,
    };
    rvvm_pci_func_desc_t rtl8169_desc = {
        .vendor_id  = 0x10EC, // Realtek
        .device_id  = 0x8168, // RTL8168 Gigabit NIC
        .class_code = 0x0200, // Ethernet
        .irq_pin    = RVVM_PCI_PIN_INTA,
        .bar[0]     = &rtl8169_io,
        .bar[2]     = &rtl8169_mmio,
    };

    rvvm_pci_func_t* func = rvvm_pci_func_init(machine, &rtl8169_desc, addr);
    if (func) {
        // Successfully plugged in
        rtl8169->func = func;
    }
    return func;
}

#endif
