/*
rtl8169.c - Realtek RTL8169 NIC
Copyright (C) 2022  LekKit <github.com/LekKit>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifdef USE_NET

#include "rtl8169.h"
#include "tap_api.h"
#include "pci-bus.h"
#include "mem_ops.h"
#include "bit_ops.h"
#include "spinlock.h"
#include "utils.h"

#define RTL8169_REG_IDR0  0x0  // ID Register 0-3 (For MAC Address)
#define RTL8169_REG_IDR4  0x4  // ID Register 4-5
#define RTL8169_REG_MAR0  0x8  // Multicast Address Register 0-3
#define RTL8169_REG_MAR4  0xC  // Multicast Address Register 4-7
#define RTL8169_REG_DTCR1 0x10 // Dump Tally Counter Command Register (64-byte alignment)
#define RTL8169_REG_DTCR2 0x14
#define RTL8169_REG_TXDA1 0x20 // Transmit Descriptors Address (64-bit, 256-byte alignment)
#define RTL8169_REG_TXDA2 0x24
#define RTL8169_REG_TXHA1 0x28 // Transmit High Priority Descriptors Address (64-bit, 256-byte alignment)
#define RTL8169_REG_TXHA2 0x2C
#define RTL8169_REG_CR    0x37 // Command Register
#define RTL8169_REG_TPOLL 0x38 // Transmit Priority Polling
#define RTL8169_REG_IMR   0x3C // Interrupt Mask
#define RTL8169_REG_ISR   0x3E // Interrupt Status
#define RTL8169_REG_TCR   0x40 // Transmit Configuration Register
#define RTL8169_REG_RCR   0x44 // Receive Configuration Register
#define RTL8169_REG_TCTR  0x48 // Timer Counter Register
#define RTL8169_REG_MPC   0x4C // Missed Packet Counter
#define RTL8169_REG_9346  0x50 // 93C46 Command Register, CFG 0-2
#define RTL8169_REG_CFG3  0x54 // Configuration Register 3-5
#define RTL8169_REG_TINT  0x58 // Timer Interrupt Register
#define RTL8169_REG_PHYAR 0x60 // PHY Access Register
#define RTL8169_REG_TBIR0 0x64 // TBI Control and Status Register
#define RTL8169_REG_TBANR 0x68 // TBI Auto-Negotiation Advertisement Register
#define RTL8169_REG_PHYS  0x6C // PHY Status Register
#define RTL8169_REG_RMS   0xDA // RX Packet Maximum Size
#define RTL8169_REG_C_CR  0xE0 // C+ Command Register
#define RTL8169_REG_RXDA1 0xE4 // Receive Descriptor Address (64-bit, 256-byte alignment)
#define RTL8169_REG_RXDA2 0xE8
#define RTL8169_REG_MTPS  0xEC // TX Packet Maximum Size

#define RTL8169_CR_TE  0x04 // Transmitter Enable
#define RTL8169_CR_RE  0x08 // Receiver Enable
#define RTL8169_CR_RW  0x0C // R/W Register bits mask
#define RTL8169_CR_RST 0x10 // Reset

#define RTL8169_TPOLL_FSW 0x01 // Forced Software Interrupt
#define RTL8169_TPOLL_NPQ 0x40 // Normal Priority Queue Polling
#define RTL8169_TPOLL_HPQ 0x40 // High Priority Queue Polling

#define RTL8169_IRQ_ROK 0x0  // Receive OK
#define RTL8169_IRQ_RER 0x1  // Receiver Error
#define RTL8169_IRQ_TOK 0x2  // Transmit OK
#define RTL8169_IRQ_TER 0x3  // Transmitter Error
#define RTL8169_IRQ_RDU 0x4  // RX Descriptor Unavailable
#define RTL8169_IRQ_LCG 0x5  // Link Change
#define RTL8169_IRQ_FOV 0x6  // RX FIFO Overflow
#define RTL8169_IRQ_TDU 0x7  // TX Descriptor Unavailable
#define RTL8169_IRQ_SWI 0x10 // Software Interrupt

#define RTL8169_PHY_BMCR  0x0
#define RTL8169_PHY_BMSR  0x1
#define RTL8169_PHY_ID1   0x2
#define RTL8169_PHY_ID2   0x3
#define RTL8169_PHY_GBCR  0x9
#define RTL8169_PHY_GBSR  0xA
#define RTL8169_PHY_GBESR 0xF

#define RTL8169_DESC_OWN 0x80000000
#define RTL8169_DESC_EOR 0x40000000
#define RTL8169_DESC_FS  0x20000000
#define RTL8169_DESC_LS  0x10000000
#define RTL8169_DESC_PAM 0x04000000
#define RTL8169_DESC_GRX 0x34000000 // Generic RX packet flags

#define RTL8169_EEPROM_DOU 0x01 // EEPROM Data out
#define RTL8169_EEPROM_DIN 0x02 // EEPROM Data in
#define RTL8169_EEPROM_CLK 0x04 // EEPROM Clock
#define RTL8169_EEPROM_SEL 0x08 // EEPROM Chip select
#define RTL8169_EEMODE_PRG 0x80 // EEPROM Programming mode

#define RTL8169_MAX_FIFO_SIZE 1024
#define RTL8169_MAC_SIZE 6

typedef struct {
    rvvm_addr_t addr;
    uint32_t index;
} rtl8169_ring_t;

// 93C56 16-bit word EEPROM emulation for reading MAC
typedef struct {
    uint8_t  pins;
    uint8_t  addr;
    uint16_t word;
    bitcnt_t cur_bit;
    bool     addr_ok;
} at93c56_state_t;

typedef struct {
    pci_dev_t* pci_dev;
    tap_dev_t* tap;
    at93c56_state_t eeprom;
    rtl8169_ring_t rx;
    rtl8169_ring_t tx;
    rtl8169_ring_t txp;
    spinlock_t lock;
    spinlock_t rx_lock;
    uint32_t cr;
    uint32_t phyar;
    uint32_t imr;
    uint32_t isr;
    uint8_t  mac[RTL8169_MAC_SIZE];
} rtl8169_dev_t;

static void rtl8169_reset(rvvm_mmio_dev_t* dev)
{
    rtl8169_dev_t* rtl8169 = dev->data;
    memset(&rtl8169->eeprom, 0, sizeof(at93c56_state_t));
    memset(&rtl8169->rx, 0, sizeof(rtl8169_ring_t));
    memset(&rtl8169->tx, 0, sizeof(rtl8169_ring_t));
    memset(&rtl8169->txp, 0, sizeof(rtl8169_ring_t));
    rtl8169->isr = 0;
    rtl8169->imr = 0;
    rtl8169->cr = 0;
    rtl8169->phyar = 0;
}

static void rtl8169_interrupt(rtl8169_dev_t* rtl8169, size_t irq)
{
    uint32_t irqs = atomic_or_uint32(&rtl8169->isr, (1U << irq)) | (1U << irq);
    if (irqs & atomic_load_uint32(&rtl8169->imr)) pci_send_irq(rtl8169->pci_dev, 0);
}

static uint32_t rtl8169_handle_phy(uint32_t cmd)
{
    uint32_t reg = (cmd >> 16) & 0x1F;
    cmd &= 0xFFFF0000;
    switch (reg) {
        case RTL8169_PHY_BMCR:
            cmd |= 0x0140; // Full-duplex 1Gbps
            break;
        case RTL8169_PHY_BMSR:
            cmd |= 0x796D; // Link is up; Supports GBESR
            break;
        case RTL8169_PHY_ID1:
            cmd |= 0x001C; // Realtek
            break;
        case RTL8169_PHY_ID2:
            cmd |= 0xC800; // Generic 1 GBps PHY
            break;
        case RTL8169_PHY_GBCR:
            cmd |= 0x0300; // Advertise 1000BASE-T Full/Half duplex
            break;
        case RTL8169_PHY_GBSR:
            cmd |= 0x3C00; // Link partner is capable of 1000BASE-T Full/Half duplex
            break;
        case RTL8169_PHY_GBESR:
            cmd |= 0x3000; // 1000BASE-T Full/Half duplex capable
            break;
        case 0x12:
            cmd |= 0x0200; // Advertise a 10 Gbps link (Use 0x0400 for 1 Gbps)
            break;
    }
    return cmd ^ 0x80000000;
}

static void rtl8169_93c56_read_word(rtl8169_dev_t* rtl8169)
{
    rtl8169->eeprom.word = 0;
    switch (rtl8169->eeprom.addr) {
        case 0x0: // Device ID
            rtl8169->eeprom.word = 0x8129;
            break;
        case 0x7: // MAC words
        case 0x8:
        case 0x9:
            tap_get_mac(rtl8169->tap, rtl8169->mac);
            rtl8169->eeprom.word = read_uint16_le_m(rtl8169->mac + ((rtl8169->eeprom.addr - 7) << 1));
            break;
        default: // Unknown
            rtl8169->eeprom.word = 0;
            break;
    }
}

static void rtl8169_93c56_pins_write(rtl8169_dev_t* rtl8169, uint8_t pins)
{
    if (pins & RTL8169_EEMODE_PRG) {
        if ((pins & RTL8169_EEPROM_CLK) && !(rtl8169->eeprom.pins & RTL8169_EEPROM_CLK)) {
            // Clock pulled high
            if (rtl8169->eeprom.addr_ok) {
                // Push data bits
                if (rtl8169->eeprom.cur_bit == 0) rtl8169_93c56_read_word(rtl8169);
                if (rtl8169->eeprom.word & (0x8000 >> rtl8169->eeprom.cur_bit)) {
                    pins |= RTL8169_EEPROM_DOU;
                } else {
                    pins &= ~RTL8169_EEPROM_DOU;
                }
                if (rtl8169->eeprom.cur_bit++ >= 15) {
                    rtl8169->eeprom.cur_bit = 0;
                    rtl8169->eeprom.addr++;
                }
            } else {
                // Get starting addr, ignore command (Act as readonly eeprom)
                if (rtl8169->eeprom.cur_bit >= 3) {
                    rtl8169->eeprom.addr <<= 1;
                    if (pins & RTL8169_EEPROM_DIN) rtl8169->eeprom.addr |= 1;
                }
                if (rtl8169->eeprom.cur_bit++ >= 11) {
                    rtl8169->eeprom.cur_bit = 0;
                    rtl8169->eeprom.addr_ok = true;
                }
            }
        }
        if (!(pins & RTL8169_EEPROM_SEL)) {
            // End of transfer, request addr next time
            rtl8169->eeprom.addr_ok = false;
            rtl8169->eeprom.addr = 0;
            rtl8169->eeprom.cur_bit = 0;
        }
    }
    rtl8169->eeprom.pins = pins;
}

static bool rtl8169_feed_rx(void* net_dev, const void* data, size_t size)
{
    rtl8169_dev_t* rtl8169 = net_dev;
    // Receiver disabled
    if (!(atomic_load_uint32(&rtl8169->cr) & RTL8169_CR_RE)) return false;

    spin_lock(&rtl8169->rx_lock);
    uint8_t* cmd = pci_get_dma_ptr(rtl8169->pci_dev, rtl8169->rx.addr + (rtl8169->rx.index << 4), 16);
    // FIFO DMA error
    if (cmd == NULL) {
        spin_unlock(&rtl8169->rx_lock);
        return false;
    }

    uint32_t flags = read_uint32_le(cmd);
    // FIFO overflow
    if (!(flags & RTL8169_DESC_OWN)) {
        spin_unlock(&rtl8169->rx_lock);
        rtl8169_interrupt(rtl8169, RTL8169_IRQ_FOV);
        return false;
    }

    rvvm_addr_t packet_addr = read_uint64_le(cmd + 8);
    size_t packet_size = flags & 0x3FFF;
    uint8_t* packet_ptr = pci_get_dma_ptr(rtl8169->pci_dev, packet_addr, packet_size);
    // Packet DMA error
    if (packet_ptr == NULL || packet_size < size + 4) {
        spin_unlock(&rtl8169->rx_lock);
        rtl8169_interrupt(rtl8169, RTL8169_IRQ_RER);
        return false;
    }

    memcpy(packet_ptr, data, size);
    memset(packet_ptr + size, 0, 4); // Append fake CRC32

    write_uint32_le(cmd, (flags & RTL8169_DESC_EOR) | RTL8169_DESC_GRX | (size + 4));
    rtl8169->rx.index++;
    if ((flags & RTL8169_DESC_EOR) || rtl8169->rx.index >= RTL8169_MAX_FIFO_SIZE) {
        rtl8169->rx.index = 0;
    }

    spin_unlock(&rtl8169->rx_lock);
    rtl8169_interrupt(rtl8169, RTL8169_IRQ_ROK);
    return true;
}

static void rtl8169_handle_tx(rtl8169_dev_t* rtl8169, rtl8169_ring_t* ring)
{
    size_t tx_id = ring->index;
    bool tx_irq = false;
    if (rtl8169->cr & RTL8169_CR_TE) do {
        uint8_t* cmd = pci_get_dma_ptr(rtl8169->pci_dev, ring->addr + (ring->index << 4), 16);
        // FIFO DMA error
        if (cmd == NULL) break;

        uint32_t flags = read_uint32_le(cmd);
        // Nothing to transmit
        if (!(flags & RTL8169_DESC_OWN)) break;

        rvvm_addr_t packet_addr = read_uint64_le(cmd + 8);
        size_t packet_size = flags & 0x3FFF;
        void* packet_ptr = pci_get_dma_ptr(rtl8169->pci_dev, packet_addr, packet_size);

        if (packet_ptr) tap_send(rtl8169->tap, packet_ptr, packet_size);

        write_uint32_le(cmd, flags & ~RTL8169_DESC_OWN);
        ring->index++;
        if (!!(flags & RTL8169_DESC_EOR) || ring->index >= RTL8169_MAX_FIFO_SIZE) {
            ring->index = 0;
        }

        tx_irq = true;
    } while (tx_id != ring->index);
    if (tx_irq) rtl8169_interrupt(rtl8169, RTL8169_IRQ_TOK);
}

static bool rtl8169_pci_read(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    rtl8169_dev_t* rtl8169 = dev->data;
    uint8_t tmp[4] = {0};
    spin_lock(&rtl8169->lock);
    switch (offset & (~0x3)) {
        case RTL8169_REG_IDR0:
            tap_get_mac(rtl8169->tap, rtl8169->mac);
            memcpy(tmp, rtl8169->mac, 4);
            break;
        case RTL8169_REG_IDR4:
            tap_get_mac(rtl8169->tap, rtl8169->mac);
            memcpy(tmp, rtl8169->mac + 4, 2);
            break;
        case RTL8169_REG_IMR:
            write_uint16_le(tmp, atomic_load_uint32(&rtl8169->imr));
            write_uint16_le(tmp + 2, atomic_load_uint32(&rtl8169->isr));
            break;
        case RTL8169_REG_CR - 3:
            write_uint8(tmp + 3, atomic_load_uint32(&rtl8169->cr));
            break;
        case RTL8169_REG_TCR:
            write_uint32_le(tmp, 0x3810700); // RTL8169S XID
            break;
        case RTL8169_REG_9346:
            tmp[0] = rtl8169->eeprom.pins;
            break;
        case RTL8169_REG_PHYAR:
            write_uint32_le(tmp, rtl8169->phyar);
            break;
        case RTL8169_REG_PHYS:
            write_uint32_le(tmp, 0x73); // 1Gbps Full/Half
            break;
        case RTL8169_REG_TXDA1:
            write_uint32_le(tmp, rtl8169->tx.addr);
            break;
        case RTL8169_REG_TXDA2:
            write_uint32_le(tmp, rtl8169->tx.addr >> 32);
            break;
        case RTL8169_REG_TXHA1:
            write_uint32_le(tmp, rtl8169->txp.addr);
            break;
        case RTL8169_REG_TXHA2:
            write_uint32_le(tmp, rtl8169->txp.addr >> 32);
            break;
        case RTL8169_REG_RXDA1:
            write_uint32_le(tmp, rtl8169->rx.addr);
            break;
        case RTL8169_REG_RXDA2:
            write_uint32_le(tmp, rtl8169->rx.addr >> 32);
            break;
        case RTL8169_REG_RMS:
            write_uint32_le(tmp, 0x1FFF);
            break;
        case RTL8169_REG_MTPS:
            write_uint32_le(tmp, 0x3B);
            break;
    }
    spin_unlock(&rtl8169->lock);
    memcpy(data, tmp + (offset & 0x3), size);
    //rvvm_warn("rtl8169 read  %08x from %08zx", read_uint32_le(tmp), offset & (~0x3));
    return true;
}

static bool rtl8169_pci_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    rtl8169_dev_t* rtl8169 = dev->data;
    //rvvm_warn("rtl8169 write %08x to   %08zx", read_uint32_le(data), offset);
    spin_lock(&rtl8169->lock);
    // I don't even know how to refactor this...
    if (offset == RTL8169_REG_TPOLL) {
        uint8_t flags = read_uint8(data);
        if (flags & RTL8169_TPOLL_HPQ) rtl8169_handle_tx(rtl8169, &rtl8169->txp);
        if (flags & RTL8169_TPOLL_NPQ) rtl8169_handle_tx(rtl8169, &rtl8169->tx);
        if (flags & RTL8169_TPOLL_FSW) rtl8169_interrupt(rtl8169, RTL8169_IRQ_SWI);
    } else if (offset == RTL8169_REG_CR) {
        atomic_store_uint32(&rtl8169->cr, read_uint8(data) & RTL8169_CR_RW);
        if (read_uint8(data) & RTL8169_CR_RST) rtl8169_reset(dev);
    } else if (offset < RTL8169_MAC_SIZE) {
        size_t size_clamp = (offset + size > RTL8169_MAC_SIZE)
                        ? (RTL8169_MAC_SIZE - offset) : size;
        memcpy(rtl8169->mac + offset, data, size_clamp);
        tap_set_mac(rtl8169->tap, rtl8169->mac);
    } else if (offset == RTL8169_REG_9346) rtl8169_93c56_pins_write(rtl8169, read_uint8(data));

    if (size >= 2) {
        switch (offset) {
            case RTL8169_REG_IMR:
                atomic_store_uint32(&rtl8169->imr, read_uint16_le(data));
                if (atomic_load_uint32(&rtl8169->isr) & atomic_load_uint32(&rtl8169->imr)) {
                    pci_send_irq(rtl8169->pci_dev, 0);
                }
                break;
            case RTL8169_REG_ISR:
                atomic_and_uint32(&rtl8169->isr, ~read_uint16_le(data));
                break;
        }
    }
    if (size >= 4) {
        switch (offset) {
            case RTL8169_REG_TXDA1:
                rtl8169->tx.addr = bit_replace(rtl8169->tx.addr, 0, 32, read_uint32_le(data) & ~0xFFU);
                break;
            case RTL8169_REG_TXDA2:
                rtl8169->tx.addr = bit_replace(rtl8169->tx.addr, 32, 32, read_uint32_le(data));
                break;
            case RTL8169_REG_TXHA1:
                rtl8169->txp.addr = bit_replace(rtl8169->txp.addr, 0, 32, read_uint32_le(data) & ~0xFFU);
                break;
            case RTL8169_REG_TXHA2:
                rtl8169->txp.addr = bit_replace(rtl8169->txp.addr, 32, 32, read_uint32_le(data));
                break;
            case RTL8169_REG_RXDA1:
                rtl8169->rx.addr = bit_replace(rtl8169->rx.addr, 0, 32, read_uint32_le(data) & ~0xFFU);
                break;
            case RTL8169_REG_RXDA2:
                rtl8169->rx.addr = bit_replace(rtl8169->rx.addr, 32, 32, read_uint32_le(data));
                break;
            case RTL8169_REG_PHYAR:
                rtl8169->phyar = rtl8169_handle_phy(read_uint32_le(data));
                break;
        }
    }
    spin_unlock(&rtl8169->lock);
    return true;
}

static void rtl8169_remove(rvvm_mmio_dev_t* dev)
{
    rtl8169_dev_t* rtl8169 = dev->data;
    tap_close(rtl8169->tap);
    free(rtl8169);
}

static rvvm_mmio_type_t rtl8169_type = {
    .name = "rtl8169",
    .remove = rtl8169_remove,
    .reset = rtl8169_reset,
};

PUBLIC pci_dev_t* rtl8169_init(pci_bus_t* pci_bus)
{
    rtl8169_dev_t* rtl8169 = safe_new_obj(rtl8169_dev_t);
    tap_net_dev_t tap_net = {
        .net_dev = rtl8169,
        .feed_rx = rtl8169_feed_rx,
    };

    rtl8169->tap = tap_open(&tap_net);
    if (rtl8169->tap == NULL) {
        rvvm_error("Failed to create TAP device!");
        free(rtl8169);
        return NULL;
    }

    pci_dev_desc_t rtl8169_desc = {
        .func[0] = {
            .vendor_id = 0x10EC,  // Realtek
            .device_id = 0x8169,  // RTL8169 Gigabit NIC
            .class_code = 0x0200, // Ethernet
            .irq_pin = PCI_IRQ_PIN_INTA,
            .bar[1] = {
                .size = 0x100,
                .min_op_size = 1,
                .max_op_size = 4,
                .read = rtl8169_pci_read,
                .write = rtl8169_pci_write,
                .data = rtl8169,
                .type = &rtl8169_type,
            },
        }
    };

    pci_dev_t* pci_dev = pci_bus_add_device(pci_bus, &rtl8169_desc);
    if (pci_dev) rtl8169->pci_dev = pci_dev;
    return pci_dev;
}

PUBLIC pci_dev_t* rtl8169_init_auto(rvvm_machine_t* machine)
{
    return rtl8169_init(rvvm_get_pci_bus(machine));
}

#endif
