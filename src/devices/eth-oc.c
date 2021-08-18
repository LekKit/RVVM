/*
eth-oc.c - OpenCores Ethernet MAC controller
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

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
#include "plic.h"
#include "bit_ops.h"
#include "mem_ops.h"
#include "threading.h"
#include "spinlock.h"
#include "tap.h"

#include <string.h>
#include <inttypes.h>

/* Device registers */
#define ETHOC_MODER 0x00
#define ETHOC_INT_SRC 0x04
#define ETHOC_INT_MASK 0x08
#define ETHOC_IPGT 0x0C
#define ETHOC_IPGR1 0x10
#define ETHOC_IPGR2 0x14
#define ETHOC_PACKETLEN 0x18
#define ETHOC_COLLCONF 0x1C
#define ETHOC_TX_BD_NUM 0x20
#define ETHOC_CTRLMODER 0x24
#define ETHOC_MIIMODER 0x28
#define ETHOC_MIICOMMAND 0x2C
#define ETHOC_MIIADDRESS 0x30
#define ETHOC_MIITX_DATA 0x34
#define ETHOC_MIIRX_DATA 0x38
#define ETHOC_MIISTATUS 0x3C
#define ETHOC_MAC_ADDR0 0x40
#define ETHOC_MAC_ADDR1 0x44
#define ETHOC_ETH_HASH0_ADR 0x48
#define ETHOC_ETH_HASH1_ADR 0x4C
#define ETHOC_TXCTRL 0x50

/* MODER fields */
#define ETHOC_MODER_RECSMALL (1 << 16)
#define ETHOC_MODER_PAD (1 << 15)
#define ETHOC_MODER_HUGEN (1 << 14)
#define ETHOC_MODER_CRCEN (1 << 13)
#define ETHOC_MODER_DLYCRCEN (1 << 12)
#define ETHOC_MODER_RST (1 << 11)
#define ETHOC_MODER_FULLD (1 << 10)
#define ETHOC_MODER_EXDFREN (1 << 9)
#define ETHOC_MODER_NOBCKOF (1 << 8)
#define ETHOC_MODER_LOOPBCK (1 << 7)
#define ETHOC_MODER_IFG (1 << 6)
#define ETHOC_MODER_PRO (1 << 5)
#define ETHOC_MODER_IAM (1 << 4)
#define ETHOC_MODER_BRO (1 << 3)
#define ETHOC_MODER_NOPRE (1 << 2)
#define ETHOC_MODER_TXEN (1 << 1)
#define ETHOC_MODER_RXEN (1 << 0)

/* Interrupt numbers */
#define ETHOC_INT_RXC 6 /* control frame received */
#define ETHOC_INT_TXC 5 /* control frame transmitted */
#define ETHOC_INT_BUSY 4 /* buffer received and discarded */
#define ETHOC_INT_RXE 3 /* receive error */
#define ETHOC_INT_RXB 2 /* frame received */
#define ETHOC_INT_TXE 1 /* transmit error */
#define ETHOC_INT_TXB 0 /* buffer transmitted */

/* CTRLMODER fields */
#define ETHOC_CTRLMODER_TXFLOW (1 << 2)
#define ETHOC_CTRLMODER_RXFLOW (1 << 1)
#define ETHOC_CTRLMODER_PASSALL (1 << 0)

/* MIIMODER fields */
#define ETHOC_MIIMODER_MIIMRST (1 << 9)
#define ETHOC_MIIMODER_MIINOPRE (1 << 8)
/* CLKDIV in the lower 8 bits */

/* MIICOMMAND fields */
#define ETHOC_MIICOMMAND_WCTRLDATA (1 << 2)
#define ETHOC_MIICOMMAND_RSTAT (1 << 1)
#define ETHOC_MIICOMMAND_SCANSTAT (1 << 0)

/* MIISTATUS fields */
#define ETHOC_MIISTATUS_NVALID (1 << 2)
#define ETHOC_MIISTATUS_BUSY (1 << 1)
#define ETHOC_MIISTATUS_LINKFAIL (1 << 0)

/* TXCTRL field */
#define ETHOC_TXCTRL_TXPAUSERQ (1 << 16)

/* Buffer Descriptor */
struct bd
{
    uint32_t data;
    uint32_t ptr; /* pointers are 32-bit */
};

/* Transmission BD fields */
#define ETHOC_TXBD_RD (1 << 15)
#define ETHOC_BD_IRQ (1 << 14)
#define ETHOC_BD_WR (1 << 13)
#define ETHOC_TXBD_PAD (1 << 12)
#define ETHOC_TXBD_CRC (1 << 11)
#define ETHOC_TXBD_UR (1 << 8)
#define ETHOC_TXBD_RL (1 << 3)
#define ETHOC_TXBD_LC (1 << 2)
#define ETHOC_TXBD_DF (1 << 1)
#define ETHOC_TXBD_CS (1 << 0)

/* Receive BD fields */
#define ETHOC_RXBD_E (1 << 15)
#define ETHOC_RXBD_M (1 << 7)
#define ETHOC_RXBD_OR (1 << 6)
#define ETHOC_RXBD_IS (1 << 5)
#define ETHOC_RXBD_DN (1 << 4)
#define ETHOC_RXBD_TL (1 << 3)
#define ETHOC_RXBD_SF (1 << 2)
#define ETHOC_RXBD_CRC (1 << 1)
#define ETHOC_RXBD_LC (1 << 0)

/* max BD count */
#define ETHOC_BD_BUFSIZ 1024

/* BD register start address */
#define ETHOC_BD_ADDR 0x400

#define MII_REG_BMCR 0
#define MII_REG_BMSR 1
#define MII_REG_PHYIDR1 2
#define MII_REG_PHYIDR2 3

struct mdio {
    struct tap_dev *dev;
    uint8_t phyid;
};

static uint16_t mdio_read(struct mdio *mdio, uint8_t phy, uint8_t reg) {
    if (mdio->phyid != phy) {
        return 0;
    }

    switch (reg) {
        case MII_REG_BMSR:
            /* Link is up */
            return tap_is_up(mdio->dev) ? (1 << 2) : 0;
        case MII_REG_PHYIDR2:
        case MII_REG_PHYIDR1:
            /* PHY ID */
            return 0;

        default:
            return 0;
    }
}

static void mdio_write(struct mdio *mdio, uint8_t phy, uint8_t reg, uint16_t val) {
    if (mdio->phyid != phy) {
        return;
    }

    switch (reg) {
        default:
            UNUSED(val);
            break;
    }
}

struct ethoc_dev
{
    struct bd bdbuf[ETHOC_BD_BUFSIZ / sizeof(struct bd)];
    struct mdio mdio;
    struct tap_pollevent_cb pollev;
    rvvm_machine_t* machine; /* Machine to send IRQ to, also used as memory to send/recv packets */
    void *intc_data;
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
    /* IDK what HASH0/1 registers are for...
     * Judging from Linux driver source, they're used with multicast filtering */
    uint32_t hash[2];
    uint32_t txctrl;
    uint8_t macaddr[6];
};

static bool ethoc_interrupt(struct ethoc_dev *eth, uint8_t int_num)
{
    eth->int_src |= (1 << int_num);
    if (!(eth->int_mask & (1 << int_num))) {
        return false;
    }

    plic_send_irq(eth->machine, eth->intc_data, eth->irq);
    return true;
}

static void ethoc_reset(struct ethoc_dev *eth)
{
    eth->moder = ETHOC_MODER_PAD | ETHOC_MODER_CRCEN;
    eth->int_src = 0;
    eth->int_mask = 0;
    eth->packetlen = 0x400600;
    eth->collconf = 0xf003f;
    eth->tx_bd_num = 0x40;
    eth->ctrlmoder = 0;
    eth->miimoder = 0x64;
    eth->miiaddress = 0;
    eth->miitx_data = 0;
    eth->miirx_data = 0;
    eth->miistatus = 0;
    memset(eth->macaddr, '\0', sizeof(eth->macaddr));
    eth->hash[0] = 0;
    eth->hash[1] = 0;
    eth->txctrl = 0;
}

static bool ethoc_data_mmio_read(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    if (offset < 0x400 && (offset % 4 != 0 || size != 4))
    {
        /* TODO: misalign */
        return false;
    }

#if 0
    printf("ETHOC MMIO offset: 0x%02x size: %d read\n", offset, size);
#endif
    struct ethoc_dev *eth = (struct ethoc_dev *) device->data;
    uint32_t *data = (uint32_t*) memory_data;

    spin_lock(&eth->pollev.lock);
    switch (offset)
    {
        case ETHOC_MODER:
            *data = eth->moder;
            break;
        case ETHOC_INT_SRC:
            *data = eth->int_src;
            break;
        case ETHOC_INT_MASK:
            *data = eth->int_mask;
            break;
        case ETHOC_IPGT:
            /* ignore */
            break;
        case ETHOC_IPGR1:
            /* ignore */
            break;
        case ETHOC_IPGR2:
            /* ignore */
            break;
        case ETHOC_PACKETLEN:
            *data = eth->packetlen;
            break;
        case ETHOC_COLLCONF:
            *data = eth->collconf;
            break;
        case ETHOC_TX_BD_NUM:
            *data = eth->tx_bd_num;
            break;
        case ETHOC_CTRLMODER:
            *data = eth->ctrlmoder;
            break;
        case ETHOC_MIIMODER:
            *data = eth->miimoder;
            break;
        case ETHOC_MIICOMMAND:
            *data = 0;
            break;
        case ETHOC_MIIADDRESS:
            *data = eth->miiaddress;
            break;
        case ETHOC_MIITX_DATA:
            *data = eth->miitx_data;
            break;
        case ETHOC_MIIRX_DATA:
            *data = eth->miirx_data;
            break;
        case ETHOC_MIISTATUS:
            *data = eth->miistatus;
            break;
        case ETHOC_MAC_ADDR0:
            tap_get_mac(&eth->pollev.dev, eth->macaddr);
            *data = eth->macaddr[5]
                | (eth->macaddr[4] << 8)
                | (eth->macaddr[3] << 16)
                | ((uint32_t)eth->macaddr[4]) << 24;
            break;
        case ETHOC_MAC_ADDR1:
            tap_get_mac(&eth->pollev.dev, eth->macaddr);
            *data = eth->macaddr[1] | (eth->macaddr[0] << 8);
            break;
        case ETHOC_ETH_HASH0_ADR:
            *data = eth->hash[0];
            break;
        case ETHOC_ETH_HASH1_ADR:
            *data = eth->hash[1];
            break;
        case ETHOC_TXCTRL:
            *data = eth->txctrl;
            break;
        default:
            if (offset < ETHOC_BD_ADDR || offset + size >= ETHOC_BD_ADDR + ETHOC_BD_BUFSIZ) {
                goto err;
            }

            memcpy(memory_data, (uint8_t*)&eth->bdbuf + offset - ETHOC_BD_ADDR, size);
    }

    spin_unlock(&eth->pollev.lock);
    return true;
err:
    spin_unlock(&eth->pollev.lock);
    return false;
}

static bool ethoc_data_mmio_write(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    if (offset < 0x400 && (offset % 4 != 0 || size != 4))
    {
        /* TODO: misalign */
        return false;
    }

#if 0
    printf("ETHOC MMIO offset: 0x%02x size: %d write val: 0x%08X\n", offset, size, *(uint32_t*) memory_data);
#endif
    struct ethoc_dev *eth = (struct ethoc_dev *) device->data;
    uint32_t *data = (uint32_t*) memory_data;
    bool wake = false;

    spin_lock(&eth->pollev.lock);
    switch (offset)
    {
        case ETHOC_MODER:
            {
                bool prev_rx = !!(eth->moder & ETHOC_MODER_RXEN);
                bool prev_tx = !!(eth->moder & ETHOC_MODER_TXEN);

                eth->moder = *data;

                if (!prev_rx && eth->moder & ETHOC_MODER_RXEN) {
                    eth->cur_rxbd = eth->tx_bd_num;
                    wake = true;
                }

                if (!prev_tx && eth->moder & ETHOC_MODER_TXEN) {
                    eth->cur_txbd = 0;
                    wake = true;
                }
            }
            break;
        case ETHOC_INT_SRC:
            /* Bits are cleared by writing 1 to them */
            eth->int_src &= ~*data;

            if ((eth->int_src & eth->int_mask) != 0) {
                plic_send_irq(eth->machine, eth->intc_data, eth->irq);
            }
            break;
        case ETHOC_INT_MASK:
            eth->int_mask = *data;

            if ((eth->int_src & eth->int_mask) != 0) {
                plic_send_irq(eth->machine, eth->intc_data, eth->irq);
            }
            break;
        case ETHOC_IPGT:
            /* ignore */
            break;
        case ETHOC_IPGR1:
            /* ignore */
            break;
        case ETHOC_IPGR2:
            /* ignore */
            break;
        case ETHOC_PACKETLEN:
            eth->packetlen = *data;
            break;
        case ETHOC_COLLCONF:
            eth->collconf = *data;
            break;
        case ETHOC_TX_BD_NUM:
            eth->tx_bd_num = *data;
            break;
        case ETHOC_CTRLMODER:
            eth->ctrlmoder = *data;
            break;
        case ETHOC_MIIMODER:
            eth->miimoder = *data;
            break;
        case ETHOC_MIICOMMAND:
            if (*data & ETHOC_MIICOMMAND_RSTAT) {
                eth->miirx_data = mdio_read(&eth->mdio, eth->miiaddress & 0x1f, (eth->miiaddress >> 8) & 0x1f) & 0xffff;
            } else if (*data & ETHOC_MIICOMMAND_WCTRLDATA) {
                mdio_write(&eth->mdio, eth->miiaddress & 0x1f, (eth->miiaddress >> 8) & 0x1f, eth->miitx_data & 0xffff);
            }
            break;
        case ETHOC_MIIADDRESS:
            eth->miiaddress = *data;
            break;
        case ETHOC_MIITX_DATA:
            eth->miitx_data = *data;
            break;
        case ETHOC_MIIRX_DATA:
            /* R/O, but was R/W in older spec */
            /* eth->miirx_data = *data; */
            break;
        case ETHOC_MIISTATUS:
            eth->miistatus = *data;
            break;
        case ETHOC_MAC_ADDR0:
            eth->macaddr[5] = *data & 0xff;
            eth->macaddr[4] = (*data >> 8) & 0xff;
            eth->macaddr[3] = (*data >> 16) & 0xff;
            eth->macaddr[2] = (*data >> 24) & 0xff;
            tap_set_mac(&eth->pollev.dev, eth->macaddr);
            break;
        case ETHOC_MAC_ADDR1:
            eth->macaddr[1] = *data & 0xff;
            eth->macaddr[0] = (*data >> 8) & 0xff;
            tap_set_mac(&eth->pollev.dev, eth->macaddr);
            break;
        case ETHOC_ETH_HASH0_ADR:
            eth->hash[0] = *data;
            break;
        case ETHOC_ETH_HASH1_ADR:
            eth->hash[1] = *data;
            break;
        case ETHOC_TXCTRL:
            eth->txctrl = *data;
            break;
        default:
            if (offset < ETHOC_BD_ADDR || offset + size >= ETHOC_BD_ADDR + ETHOC_BD_BUFSIZ) {
                goto err;
            }

            memcpy((uint8_t*)&eth->bdbuf + offset - ETHOC_BD_ADDR, memory_data, size);
            /* Write BD might be modified, so wake the tap thread */
            if (offset + size >= eth->tx_bd_num) {
                wake = true;
            }
    }

    spin_unlock(&eth->pollev.lock);
    if (wake) tap_wake(&eth->pollev);
    return true;
err:
    spin_unlock(&eth->pollev.lock);
    return false;
}

static void ethoc_pollevent(int poll_status, void *arg)
{
    struct ethoc_dev *eth = (struct ethoc_dev *) arg;
    //printf("in pollevent\n");

    spin_lock(&eth->pollev.lock);

    if (poll_status & TAPPOLL_IN && eth->moder & ETHOC_MODER_RXEN) {
        /* Some data arrived */
        uint32_t prevbd = eth->cur_rxbd;
        struct bd *bd;
        do {
            bd = &eth->bdbuf[eth->cur_rxbd];

            if (bd->data & ETHOC_BD_WR || eth->cur_rxbd == ETHOC_BD_BUFSIZ / sizeof(struct bd)) {
                eth->cur_rxbd = eth->tx_bd_num;
            } else {
                ++eth->cur_rxbd;
            }

            if (prevbd == eth->cur_rxbd) {
                /* No free buffers when receiving a frame - ignore it.
                 * Hopefully it will be read later... */
                goto err_read;
            }
        } while (!(bd->data & ETHOC_RXBD_E));

        bd->data &= ~ETHOC_RXBD_E;

        void* buffer = safe_malloc(1536);
        ptrdiff_t read = tap_recv(&eth->pollev.dev, buffer, 1536);
        if (read < 0) {
            /* Set Invalid Symbol flag on error - there's no generic error flag, but
                * this is close enough */
            bd->data |= ETHOC_RXBD_IS;
            ethoc_interrupt(eth, ETHOC_INT_TXE);
        } else {
            if (rvvm_write_ram(eth->machine, bd->ptr, buffer, read)) {
                bd->data |= (read & 0xffff) << 16;
            } else {
                /* Where does this thing point to? Anyway, set some error flag... */
                bd->data |= ETHOC_RXBD_OR;
                ethoc_interrupt(eth, ETHOC_INT_TXE);
            }
        }
        free(buffer);

        //printf("rx bd: %d read: %zd\n", eth->cur_rxbd, read);

        if (read > (eth->packetlen & 0xffff)) {
            bd->data |= ETHOC_RXBD_TL;
            ethoc_interrupt(eth, ETHOC_INT_TXE);
        } else if (!(eth->moder & ETHOC_MODER_PAD) && !(eth->moder & ETHOC_MODER_RECSMALL) && read < ((eth->packetlen >> 16) & 0xffff)) {
            bd->data |= ETHOC_RXBD_SF;
            ethoc_interrupt(eth, ETHOC_INT_TXE);
        }

        if (bd->data & ETHOC_BD_IRQ) {
            ethoc_interrupt(eth, ETHOC_INT_RXB);
        }
    }
err_read:

    if (poll_status & TAPPOLL_OUT && eth->moder & ETHOC_MODER_TXEN) {
        /* Ready to send something */
        struct bd *bd = &eth->bdbuf[eth->cur_txbd];
        if (!(bd->data & ETHOC_TXBD_RD)) {
            /* Nothing to send */
            goto err_send;
        }

        //printf("tx bd: %d bd num: %d to write: %d\n", eth->cur_txbd, eth->tx_bd_num, (bd->data >> 16) & 0xffff);

        if (bd->data & ETHOC_BD_WR || eth->cur_txbd == eth->tx_bd_num) {
            eth->cur_txbd = 0;
        } else {
            ++eth->cur_txbd;
        }

        uint16_t to_write = (bd->data >> 16) & 0xffff;
        void* buffer = safe_malloc(to_write);
        if (rvvm_read_ram(eth->machine, buffer, bd->ptr, to_write)) {
            ptrdiff_t written = tap_send(&eth->pollev.dev, buffer, to_write);
            bd->data &= ~ETHOC_TXBD_RD;
            if (written < 0) {
                bd->data |= ETHOC_TXBD_RL;
                ethoc_interrupt(eth, ETHOC_INT_TXE);
            } else if (written < to_write) {
                bd->data |= ETHOC_TXBD_UR;
                ethoc_interrupt(eth, ETHOC_INT_TXE);
            }
        } else {
            bd->data &= ~ETHOC_TXBD_RD;
            bd->data |= ETHOC_TXBD_CS;
            ethoc_interrupt(eth, ETHOC_INT_TXE);
        }
        free(buffer);

        if (bd->data & ETHOC_BD_IRQ) {
            ethoc_interrupt(eth, ETHOC_INT_TXB);
        }
    }
err_send:
    spin_unlock(&eth->pollev.lock);
    return;
}

static int ethoc_pollevent_check(void *arg) {
    struct ethoc_dev *eth = (struct ethoc_dev *) arg;
    int ret = 0;
    spin_lock(&eth->pollev.lock);

    if (eth->moder & ETHOC_MODER_TXEN) {
        /* Set OUT flag only if we have something to send */
        struct bd *bd = &eth->bdbuf[eth->cur_txbd];
        if (bd->data & ETHOC_TXBD_RD) {
            ret |= TAPPOLL_OUT;
        }
    }

    if (eth->moder & ETHOC_MODER_RXEN) {
        ret |= TAPPOLL_IN;
    }

    spin_unlock(&eth->pollev.lock);
    return ret;
}

static rvvm_mmio_type_t ethoc_dev_type = {
    .name = "ethernet_oc",
};

void ethoc_init(rvvm_machine_t* machine, paddr_t base_addr, void* intc_data, uint32_t irq)
{
    struct ethoc_dev* eth = (struct ethoc_dev*)safe_calloc(sizeof(struct ethoc_dev), 1);
    if (eth == NULL) {
        return;
    }

    int err = tap_open(NULL/*tap_name*/, &eth->pollev.dev);
    if (err < 0) {
        free(eth);
        return;
    }

    memset(&eth->bdbuf, '\0', sizeof(eth->bdbuf));
    ethoc_reset(eth);

    eth->intc_data = intc_data;
    eth->irq = irq;
    eth->machine = machine;

    if (!tap_pollevent_init(&eth->pollev, eth, ethoc_pollevent_check, ethoc_pollevent)) {
        free(eth);
        return;
    }

    eth->mdio.phyid = 0;
    eth->mdio.dev = &eth->pollev.dev;

    rvvm_mmio_dev_t ethoc_dev = {0};
    ethoc_dev.min_op_size = 4;
    ethoc_dev.max_op_size = 4;
    ethoc_dev.read = ethoc_data_mmio_read;
    ethoc_dev.write = ethoc_data_mmio_write;
    ethoc_dev.type = &ethoc_dev_type;
    ethoc_dev.begin = base_addr;
    ethoc_dev.end = base_addr + 0x800;
    ethoc_dev.data = eth;
    rvvm_attach_mmio(machine, &ethoc_dev);
    
    // TODO: this leaks thread handle, needs proper managing
    thread_create(tap_workthread, &eth->pollev);
}

#endif
