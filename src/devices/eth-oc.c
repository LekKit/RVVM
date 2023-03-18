/*
eth-oc.c - OpenCores Ethernet MAC controller
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>
                    LekKit <github.com/LekKit>

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

#include "eth-oc.h"
#include "tap_api.h"
#include "mem_ops.h"
#include "spinlock.h"
#include "utils.h"

#ifdef USE_FDT
#include "fdtlib.h"
#endif

// Device registers
#define ETHOC_MODER         0x00 // Mode
#define ETHOC_INT_SRC       0x04 // Interrupt Source
#define ETHOC_INT_MASK      0x08 // Interrupt Mask
#define ETHOC_IPGT          0x0C // Inter-packet Gap
#define ETHOC_IPGR1         0x10
#define ETHOC_IPGR2         0x14
#define ETHOC_PACKETLEN     0x18 // Min/Max Packet Length
#define ETHOC_COLLCONF      0x1C // Collision & Retry Configuration
#define ETHOC_TX_BD_NUM     0x20 // Number of TX BD (Max 0x80)
#define ETHOC_CTRLMODER     0x24 // Control Module Mode
#define ETHOC_MIIMODER      0x28 // MII Mode
#define ETHOC_MIICOMMAND    0x2C // MII Command
#define ETHOC_MIIADDRESS    0x30 // MII Address
#define ETHOC_MIITX_DATA    0x34 // MII TX Data
#define ETHOC_MIIRX_DATA    0x38 // MII RX Data
#define ETHOC_MIISTATUS     0x3C // MII Status
#define ETHOC_MAC_ADDR0     0x40 // Four LSB bytes of MAC Address
#define ETHOC_MAC_ADDR1     0x44 // Two MSB bytes of MAC Address
#define ETHOC_ETH_HASH0_ADR 0x48
#define ETHOC_ETH_HASH1_ADR 0x4C
#define ETHOC_TXCTRL        0x50

// MODER fields
#define ETHOC_MODER_DMAEN    (1 << 17) // DMA Enable
#define ETHOC_MODER_RECSMALL (1 << 16) // Receive Small Packets (<MINFL)
#define ETHOC_MODER_PAD      (1 << 15) // Padding enabled
#define ETHOC_MODER_HUGEN    (1 << 14) // Huge Packets Enable (<=64kB)
#define ETHOC_MODER_CRCEN    (1 << 13) // TX appends CRC to every frame
#define ETHOC_MODER_DLYCRCEN (1 << 12) // CRC calculation starts 4 bytes after the SFD
#define ETHOC_MODER_RST      (1 << 11) // Reset MAC
#define ETHOC_MODER_FULLD    (1 << 10) // Full Duplex
#define ETHOC_MODER_EXDFREN  (1 << 9)  // MAC waits for the carrier indefinitely
#define ETHOC_MODER_NOBCKOF  (1 << 8)  // No Backoff
#define ETHOC_MODER_LOOPBCK  (1 << 7)  // Loopback (TX is looped back to the RX)
#define ETHOC_MODER_IFG      (1 << 6)  // Interframe Gap
#define ETHOC_MODER_PRO      (1 << 5)  // Promiscuous (Receive any fram)
#define ETHOC_MODER_IAM      (1 << 4)  // IAM (Use hashtable to check address)
#define ETHOC_MODER_BRO      (1 << 3)  // Reject all broadcast frames
#define ETHOC_MODER_NOPRE    (1 << 2)  // No Preamble
#define ETHOC_MODER_TXEN     (1 << 1)  // Transmit Enable
#define ETHOC_MODER_RXEN     (1 << 0)  // Receive Enable

// Interrupt numbers
#define ETHOC_INT_RXC  0x6 // Control frame received
#define ETHOC_INT_TXC  0x5 // control frame transmitted
#define ETHOC_INT_BUSY 0x4 // Buffer received and discarded
#define ETHOC_INT_RXE  0x3 // Receive error
#define ETHOC_INT_RXB  0x2 // Frame received
#define ETHOC_INT_TXE  0x1 // Transmit error
#define ETHOC_INT_TXB  0x0 // Frame transmitted

// CTRLMODER fields
#define ETHOC_CTRLMODER_TXFLOW  (1 << 2) // Transmit Flow Control (Allow PAUSE)
#define ETHOC_CTRLMODER_RXFLOW  (1 << 1) // Receive Flow Control (Block on PAUSE)
#define ETHOC_CTRLMODER_PASSALL (1 << 0) // Pass all receive frames

// MIIMODER fields
#define ETHOC_MIIMODER_MIIMRST  (1 << 10) // Reset MIIM Module
#define ETHOC_MIIMODER_MIINOPRE (1 << 8)  // No Preamble
// CLKDIV in the lower 8 bits

// MIICOMMAND fields
#define ETHOC_MIICOMMAND_WCTRLDATA (1 << 2) // Write control data
#define ETHOC_MIICOMMAND_RSTAT     (1 << 1) // Read status
#define ETHOC_MIICOMMAND_SCANSTAT  (1 << 0) // Scan status

// MIISTATUS fields
#define ETHOC_MIISTATUS_NVALID   (1 << 2)
#define ETHOC_MIISTATUS_BUSY     (1 << 1)
#define ETHOC_MIISTATUS_LINKFAIL (1 << 0)

// TXCTRL field
#define ETHOC_TXCTRL_TXPAUSERQ (1 << 16)

// Generic BD fields
#define ETHOC_BD_IRQ   (1 << 14) // Send IRQ after BD operation
#define ETHOC_BD_WRAP  (1 << 13) // This is the last BD in table

// Transmit BD fields
#define ETHOC_TXBD_RD  (1 << 15) // TX BD Ready
#define ETHOC_TXBD_PAD (1 << 12) // Pad short packets
#define ETHOC_TXBD_CRC (1 << 11) // Add CRC at the end of packet
#define ETHOC_TXBD_UR  (1 << 8)
#define ETHOC_TXBD_RL  (1 << 3)
#define ETHOC_TXBD_LC  (1 << 2)
#define ETHOC_TXBD_DF  (1 << 1)
#define ETHOC_TXBD_CS  (1 << 0)

// Receive BD fields
#define ETHOC_RXBD_E   (1 << 15) // RX BD Empty
#define ETHOC_RXBD_M   (1 << 7)
#define ETHOC_RXBD_OR  (1 << 6)  // Memory Overrun
#define ETHOC_RXBD_IS  (1 << 5)
#define ETHOC_RXBD_DN  (1 << 4)
#define ETHOC_RXBD_TL  (1 << 3)
#define ETHOC_RXBD_SF  (1 << 2)
#define ETHOC_RXBD_CRC (1 << 1)
#define ETHOC_RXBD_LC  (1 << 0)

// Max BD count
#define ETHOC_BD_COUNT  0x80
#define ETHOC_BD_BUFSIZ 0x400

// BD register start address
#define ETHOC_BD_ADDR   0x400

#define MII_REG_BMCR    0
#define MII_REG_BMSR    1
#define MII_REG_PHYIDR1 2
#define MII_REG_PHYIDR2 3

// Buffer Descriptor
struct ethoc_bd
{
    uint32_t data;
    uint32_t ptr;
};

struct ethoc_dev
{
    struct ethoc_bd bdbuf[ETHOC_BD_COUNT];
    tap_dev_t* tap;
    spinlock_t lock;
    spinlock_t rx_lock;

    rvvm_machine_t* machine;
    plic_ctx_t* plic;
    uint32_t irq;
    uint32_t cur_txbd;
    uint32_t cur_rxbd;

    uint32_t moder;
    uint32_t int_src;
    uint32_t int_mask;
    uint32_t packetlen;
    uint32_t collconf;
    uint32_t tx_bd_num;
    uint32_t ctrlmoder;
    uint32_t miimoder;
    uint32_t miiaddress;
    uint32_t miitx_data;
    uint32_t miirx_data;
    uint32_t miistatus;
    // IDK what HASH0/1 registers are for...
    // Judging from Linux driver source, they're used with multicast filtering
    uint32_t hash[2];
    uint32_t txctrl;
    uint8_t macaddr[6];
};

static void ethoc_interrupt(struct ethoc_dev *eth, uint8_t int_num)
{
    uint32_t irqs = atomic_or_uint32(&eth->int_src, (1 << int_num)) | (1 << int_num);
    if (irqs & atomic_load_uint32(&eth->int_mask)) plic_send_irq(eth->plic, eth->irq);
}

static void ethoc_process_tx(struct ethoc_dev *eth)
{
    // Loop until the queue is drained
    for (size_t i=0; i<ETHOC_BD_COUNT; ++i) {
        struct ethoc_bd* txbd = &eth->bdbuf[eth->cur_txbd];
        if (!(eth->moder & ETHOC_MODER_TXEN) || !(txbd->data & ETHOC_TXBD_RD)) {
            // Nothing to send
            return;
        }

        size_t size = (txbd->data >> 16) & 0xFFFF;
        void* dma = rvvm_get_dma_ptr(eth->machine, txbd->ptr, size);
        if (dma) {
            int ret = tap_send(eth->tap, dma, size);
            if (ret > 0) {
                // Success
                txbd->data &= ~ETHOC_TXBD_RD;
                if (txbd->data & ETHOC_BD_IRQ) ethoc_interrupt(eth, ETHOC_INT_TXB);
            } else {
                // Transmit error
                txbd->data = (txbd->data & ~ETHOC_TXBD_RD) | ETHOC_TXBD_RL;
                ethoc_interrupt(eth, ETHOC_INT_TXE);
            }
        } else {
            // DMA Error
            txbd->data = (txbd->data & ~ETHOC_TXBD_RD) | ETHOC_TXBD_CS;
            ethoc_interrupt(eth, ETHOC_INT_TXE);
        }

        if (txbd->data & ETHOC_BD_WRAP || eth->cur_txbd == eth->tx_bd_num) {
            eth->cur_txbd = 0;
        } else {
            eth->cur_txbd++;
        }
    }
}

static bool ethoc_feed_rx(void* net_dev, const void* data, size_t size)
{
    struct ethoc_dev* eth = (struct ethoc_dev*)net_dev;

    // Receiver disabled
    if (!(atomic_load_uint32(&eth->moder) & ETHOC_MODER_RXEN)) return false;

    spin_lock(&eth->rx_lock);
    struct ethoc_bd* rxbd = &eth->bdbuf[eth->cur_rxbd];
    uint32_t flags = atomic_load_uint32(&rxbd->data);
    if (!(flags & ETHOC_RXBD_E)) {
        // Ring overrun
        spin_unlock(&eth->rx_lock);
        return false;
    }
    flags &= ~ETHOC_RXBD_E;

    size_t f_size = size + 4;
    uint32_t size_lim = atomic_load_uint32(&eth->packetlen);
    uint8_t* dma = rvvm_get_dma_ptr(eth->machine, atomic_load_uint32(&rxbd->ptr), f_size);
    if (dma == NULL || f_size > (size_lim & 0xFFFF)) {
        // DMA Error
        atomic_store_uint32(&rxbd->data, flags | ETHOC_RXBD_OR);
        spin_unlock(&eth->rx_lock);
        ethoc_interrupt(eth, ETHOC_INT_RXE);
        return false;
    }

    memcpy(dma, data, size);
    memset(dma + size, 0, 4); // Append bogus CRC32 FCS
    atomic_store_uint32(&rxbd->data, (f_size & 0xFFFF) << 16 | (flags & 0xFFFF));

    if ((flags & ETHOC_BD_WRAP) || eth->cur_rxbd == ETHOC_BD_COUNT) {
        eth->cur_rxbd = eth->tx_bd_num;
    } else {
        eth->cur_rxbd++;
    }

    spin_unlock(&eth->rx_lock);
    if (flags & ETHOC_BD_IRQ) ethoc_interrupt(eth, ETHOC_INT_RXB);
    return true;
}

static bool ethoc_data_mmio_read(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    struct ethoc_dev* eth = (struct ethoc_dev*)dev->data;
    UNUSED(size);

    spin_lock(&eth->lock);
    switch (offset) {
        case ETHOC_MODER:
            write_uint32_le_m(data, atomic_load_uint32(&eth->moder));
            break;
        case ETHOC_INT_SRC:
            write_uint32_le_m(data, atomic_load_uint32(&eth->int_src));
            break;
        case ETHOC_INT_MASK:
            write_uint32_le_m(data, atomic_load_uint32(&eth->int_mask));
            break;
        case ETHOC_IPGT:
        case ETHOC_IPGR1:
        case ETHOC_IPGR2:
            // ignore
            write_uint32_le_m(data, 0);
            break;
        case ETHOC_PACKETLEN:
            write_uint32_le_m(data, atomic_load_uint32(&eth->packetlen));
            break;
        case ETHOC_COLLCONF:
            write_uint32_le_m(data, eth->collconf);
            break;
        case ETHOC_TX_BD_NUM:
            write_uint32_le_m(data, eth->tx_bd_num);
            break;
        case ETHOC_CTRLMODER:
            write_uint32_le_m(data, eth->ctrlmoder);
            break;
        case ETHOC_MIIMODER:
            write_uint32_le_m(data, eth->miimoder);
            break;
        case ETHOC_MIICOMMAND:
            write_uint32_le_m(data, 0);
            break;
        case ETHOC_MIIADDRESS:
            write_uint32_le_m(data, eth->miiaddress);
            break;
        case ETHOC_MIITX_DATA:
            write_uint32_le_m(data, eth->miitx_data);
            break;
        case ETHOC_MIIRX_DATA:
            write_uint32_le_m(data, eth->miirx_data);
            break;
        case ETHOC_MIISTATUS:
            write_uint32_le_m(data, eth->miistatus);
            break;
        case ETHOC_MAC_ADDR0:
            tap_get_mac(eth->tap, eth->macaddr);
            write_uint32_le_m(data, read_uint32_be_m(eth->macaddr + 2));
            break;
        case ETHOC_MAC_ADDR1:
            tap_get_mac(eth->tap, eth->macaddr);
            write_uint32_le_m(data, read_uint16_be_m(eth->macaddr));
            break;
        case ETHOC_ETH_HASH0_ADR:
            write_uint32_le_m(data, eth->hash[0]);
            break;
        case ETHOC_ETH_HASH1_ADR:
            write_uint32_le_m(data, eth->hash[1]);
            break;
        case ETHOC_TXCTRL:
            write_uint32_le_m(data, eth->txctrl);
            break;
        default:
            if (offset >= ETHOC_BD_ADDR && offset < ETHOC_BD_ADDR + ETHOC_BD_BUFSIZ) {
                size_t bdid = (offset - ETHOC_BD_ADDR) >> 3;
                struct ethoc_bd* bd = &eth->bdbuf[bdid];
                if (offset & 4) {
                    write_uint32_le_m(data, atomic_load_uint32(&bd->ptr));
                } else {
                    write_uint32_le_m(data, atomic_load_uint32(&bd->data));
                }
            } else {
                write_uint32_le_m(data, 0);
            }
            break;
    }

    spin_unlock(&eth->lock);
    return true;
}

static bool ethoc_data_mmio_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    struct ethoc_dev* eth = (struct ethoc_dev*)dev->data;
    UNUSED(size);

    spin_lock(&eth->lock);
    switch (offset) {
        case ETHOC_MODER: {
                uint32_t new_moder = read_uint32_le_m(data);
                if (eth->tx_bd_num == 0) new_moder &= ~ETHOC_MODER_TXEN;
                if (eth->tx_bd_num >= ETHOC_BD_COUNT) new_moder &= ~ETHOC_MODER_RXEN;
                uint32_t prev_moder = atomic_swap_uint32(&eth->moder, new_moder);
                if ((prev_moder ^ new_moder) & ETHOC_MODER_RXEN) {
                    // Toggled RX
                    spin_lock(&eth->rx_lock);
                    eth->cur_rxbd = eth->tx_bd_num;
                    spin_unlock(&eth->rx_lock);
                }

                if ((prev_moder ^ new_moder) & ETHOC_MODER_TXEN) {
                    // Toggled TX
                    eth->cur_txbd = 0;
                    ethoc_process_tx(eth);
                }
                break;
            }
        case ETHOC_INT_SRC:
            // Bits are cleared by writing 1 to them
            atomic_and_uint32(&eth->int_src, ~read_uint32_le_m(data));
            break;
        case ETHOC_INT_MASK:
            atomic_store_uint32(&eth->int_mask, read_uint32_le_m(data));

            if (atomic_load_uint32(&eth->int_src) & read_uint32_le_m(data)) {
                plic_send_irq(eth->plic, eth->irq);
            }
            break;
        case ETHOC_IPGT:
        case ETHOC_IPGR1:
        case ETHOC_IPGR2:
            // Ignore
            break;
        case ETHOC_PACKETLEN:
            atomic_store_uint32(&eth->packetlen, read_uint32_le_m(data));
            break;
        case ETHOC_COLLCONF:
            eth->collconf = read_uint32_le_m(data);
            break;
        case ETHOC_TX_BD_NUM:
            eth->tx_bd_num = EVAL_MIN(read_uint32_le_m(data), ETHOC_BD_COUNT);
            break;
        case ETHOC_CTRLMODER:
            eth->ctrlmoder = read_uint32_le_m(data);
            break;
        case ETHOC_MIIMODER:
            eth->miimoder = read_uint32_le_m(data);
            break;
        case ETHOC_MIICOMMAND:
            if (read_uint32_le_m(data) & ETHOC_MIICOMMAND_RSTAT) {
                if ((eth->miiaddress & 0x1f) == 0                     // PHY id 0
                && ((eth->miiaddress >> 8) & 0x1f) == MII_REG_BMSR) { // Link is up
                    eth->miirx_data = (1 << 2);
                } else {
                    eth->miirx_data = 0;
                }
            }
            break;
        case ETHOC_MIIADDRESS:
            eth->miiaddress = read_uint32_le_m(data);
            break;
        case ETHOC_MIITX_DATA:
            eth->miitx_data = read_uint32_le_m(data);
            break;
        case ETHOC_MIIRX_DATA:
            // RO, but was RW in older spec
            // eth->miirx_data = read_uint32_le_m(data);
            break;
        case ETHOC_MIISTATUS:
            eth->miistatus = read_uint32_le_m(data);
            break;
        case ETHOC_MAC_ADDR0:
            write_uint32_be_m(eth->macaddr + 2, read_uint32_le_m(data));
            tap_set_mac(eth->tap, eth->macaddr);
            break;
        case ETHOC_MAC_ADDR1:
            write_uint16_be_m(eth->macaddr, read_uint32_le_m(data));
            tap_set_mac(eth->tap, eth->macaddr);
            break;
        case ETHOC_ETH_HASH0_ADR:
            eth->hash[0] = read_uint32_le_m(data);
            break;
        case ETHOC_ETH_HASH1_ADR:
            eth->hash[1] = read_uint32_le_m(data);
            break;
        case ETHOC_TXCTRL:
            eth->txctrl = read_uint32_le_m(data);
            break;
        default:
            if (offset >= ETHOC_BD_ADDR && offset < ETHOC_BD_ADDR + ETHOC_BD_BUFSIZ) {
                size_t bdid = (offset - ETHOC_BD_ADDR) >> 3;
                struct ethoc_bd* bd = &eth->bdbuf[bdid];
                if (offset & 4) {
                    atomic_store_uint32(&bd->ptr, read_uint32_le_m(data));
                } else {
                    atomic_store_uint32(&bd->data, read_uint32_le_m(data));
                }

                // TX BD might be modified
                if (bdid < eth->tx_bd_num) {
                    ethoc_process_tx(eth);
                }
            }
            break;
    }

    spin_unlock(&eth->lock);
    return true;
}

static void ethoc_reset(rvvm_mmio_dev_t* dev)
{
    struct ethoc_dev* eth = dev->data;
    spin_lock(&eth->lock);
    memset(&eth->bdbuf, 0, sizeof(eth->bdbuf));
    eth->moder = ETHOC_MODER_PAD | ETHOC_MODER_CRCEN;
    eth->int_src = 0;
    eth->int_mask = 0;
    eth->packetlen = 0x3C0600;
    eth->collconf = 0xf003f;
    eth->tx_bd_num = 0x40;
    eth->ctrlmoder = 0;
    eth->miimoder = 0x64;
    eth->miiaddress = 0;
    eth->miitx_data = 0;
    eth->miirx_data = 0;
    eth->miistatus = 0;
    memset(eth->macaddr, 0, sizeof(eth->macaddr));
    eth->hash[0] = 0;
    eth->hash[1] = 0;
    eth->txctrl = 0;
    spin_unlock(&eth->lock);
}

static void ethoc_remove(rvvm_mmio_dev_t* device)
{
    struct ethoc_dev *eth = (struct ethoc_dev *) device->data;
    tap_close(eth->tap);
    free(eth);
}

static rvvm_mmio_type_t ethoc_dev_type = {
    .name = "ethernet_oc",
    .remove = ethoc_remove,
    .reset = ethoc_reset,
};

PUBLIC void ethoc_init(rvvm_machine_t* machine, rvvm_addr_t base_addr, plic_ctx_t* plic, uint32_t irq)
{
    struct ethoc_dev* eth = (struct ethoc_dev*)safe_calloc(sizeof(struct ethoc_dev), 1);
    tap_net_dev_t tap_net = {
        .net_dev = eth,
        .feed_rx = ethoc_feed_rx,
    };

    eth->plic = plic;
    eth->irq = irq;
    eth->machine = machine;

    eth->tap = tap_open(&tap_net);
    if (eth->tap == NULL) {
        rvvm_error("Failed to create TAP device!");
        free(eth);
        return;
    }

    rvvm_mmio_dev_t ethoc_dev = {
        .min_op_size = 4,
        .max_op_size = 4,
        .read = ethoc_data_mmio_read,
        .write = ethoc_data_mmio_write,
        .type = &ethoc_dev_type,
        .addr = base_addr,
        .size = 0x800,
        .data = eth,
    };
    rvvm_attach_mmio(machine, &ethoc_dev);

#ifdef USE_FDT
    struct fdt_node* ethoc = fdt_node_create_reg("ethernet", base_addr);
    fdt_node_add_prop_reg(ethoc, "reg", base_addr, 0x800);
    fdt_node_add_prop_str(ethoc, "compatible", "opencores,ethoc");
    fdt_node_add_prop_u32(ethoc, "interrupt-parent", plic_get_phandle(plic));
    fdt_node_add_prop_u32(ethoc, "interrupts", irq);
    fdt_node_add_child(rvvm_get_fdt_soc(machine), ethoc);
#endif
}

PUBLIC void ethoc_init_auto(rvvm_machine_t* machine)
{
    plic_ctx_t* plic = rvvm_get_plic(machine);
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, ETHOC_DEFAULT_MMIO, 0x800);
    ethoc_init(machine, addr, plic, plic_alloc_irq(plic));
}

#endif
