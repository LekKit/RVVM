/*
ata.h - ATA disk controller
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

#include "bit_ops.h"
#include "mem_ops.h"
#include "riscv.h"
#include "rvvm.h"
#include "ata.h"
#include "rvvm_types.h"
#include "spinlock.h"

/* Data registers */
#define ATA_REG_DATA 0x00
#define ATA_REG_ERR 0x01 /* or FEATURE */
#define ATA_REG_NSECT 0x02
#define ATA_REG_LBAL 0x03
#define ATA_REG_LBAM 0x04
#define ATA_REG_LBAH 0x05
#define ATA_REG_DEVICE 0x06
#define ATA_REG_STATUS 0x07 /* or CMD */

/* Control registers */
#define ATA_REG_CTL 0x00 /* or alternate STATUS */
#define ATA_REG_DRVADDR 0x01

/* 16-bit registers - needed for LBA48 */
#define ATA_REG_SHIFT 0
typedef uint16_t atareg_t;

/* Error flags for ERR register */
#define ATA_ERR_AMNF (1 << 0)
#define ATA_ERR_TKZNF (1 << 1)
#define ATA_ERR_ABRT (1 << 2)
#define ATA_ERR_MCR (1 << 3)
#define ATA_ERR_IDNF (1 << 4)
#define ATA_ERR_MC (1 << 5)
#define ATA_ERR_UNC (1 << 6)
#define ATA_ERR_BBK (1 << 7)

/* Flags for STATUS register */
#define ATA_STATUS_ERR (1 << 0)
#define ATA_STATUS_IDX (1 << 1)
#define ATA_STATUS_CORR (1 << 2)
#define ATA_STATUS_DRQ (1 << 3)
#define ATA_STATUS_SRV (1 << 4) /* or DSC aka Seek Complete, deprecated */
#define ATA_STATUS_DF (1 << 5)
#define ATA_STATUS_RDY (1 << 6)
#define ATA_STATUS_BSY (1 << 7)

/* Flags for DRIVE/HEAD register */
#define ATA_DRIVE_DRV (1 << 4)
#define ATA_DRIVE_LBA (1 << 6)

/* Commands */
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_INITIALIZE_DEVICE_PARAMS 0x91
#define ATA_CMD_READ_SECTORS 0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_READ_DMA 0xC8
#define ATA_CMD_WRITE_DMA 0xCA
#define ATA_CMD_STANDBY_IMMEDIATE 0xE0
#define ATA_CMD_IDLE_IMMEDIATE 0xE1
#define ATA_CMD_STANDBY 0xE2
#define ATA_CMD_IDLE 0xE3
#define ATA_CMD_CHECK_POWER_MODE 0xE4
#define ATA_CMD_SLEEP 0xE6

/* BMDMA registers */
#define ATA_BMDMA_CMD 0
#define ATA_BMDMA_STATUS 2
#define ATA_BMDMA_PRDT 4

#define SECTOR_SIZE 512

/* CHS is not supported - it's dead anyway... */
#if 0
/* Limits for C/H/S calculation */
/* max sectors per track: 255, usually 63 because of six bit numberring. Starts from 1 */
#define MAX_SPT 63 /* sectors per track */
#define MAX_HPC 16 /* heads per cyllinder/track */
#define MAX_CYLLINDERS 65536 /* 16-bit addressing limit */
#endif

#define DIV_ROUND_UP(x, d) (((x) + (d) - 1) / (d))

struct ata_dev
{
    struct {
        blkdev_t* blk;
        size_t size; /* in sectors */
        uint16_t bytes_to_rw;
        uint16_t sectcount;
        atareg_t lbal;
        atareg_t lbam;
        atareg_t lbah;
        atareg_t drive;
        atareg_t error;
        uint8_t status;
        uint8_t hob_shift;
        bool nien : 1; /* interrupt disable */
        uint8_t buf[SECTOR_SIZE];
    } drive[2];
    struct {
        paddr_t prdt_addr;
        spinlock_t lock;
        uint8_t cmd;
        uint8_t status;
    } dma_info;
    uint8_t curdrive;
    struct pci_func *func;
};

static uint64_t ata_get_lba(struct ata_dev *ata, bool is48bit)
{
    if (is48bit) {
        return bit_cut(ata->drive[ata->curdrive].lbal, 0, 8)
            | bit_cut(ata->drive[ata->curdrive].lbam, 0, 8) << 8
            | bit_cut(ata->drive[ata->curdrive].lbah, 0, 8) << 16
            | bit_cut(ata->drive[ata->curdrive].lbal, 8, 8) << 24
            | (uint64_t)bit_cut(ata->drive[ata->curdrive].lbam, 8, 8) << 32
            | (uint64_t)bit_cut(ata->drive[ata->curdrive].lbah, 8, 8) << 40;
    } else {
        return bit_cut(ata->drive[ata->curdrive].lbal, 0, 8)
            | bit_cut(ata->drive[ata->curdrive].lbam, 0, 8) << 8
            | bit_cut(ata->drive[ata->curdrive].lbah, 0, 8) << 16
            | bit_cut(ata->drive[ata->curdrive].drive, 0, 4) << 24;
    }
}

static void ata_send_interrupt(struct ata_dev *ata) {
    if (!ata->func || ata->drive[ata->curdrive].nien) {
        return;
    }

#ifdef USE_PCI
    pci_send_irq(ata->func);
#endif
}

static void ata_clear_interrupt(struct ata_dev *ata) {
    if (!ata->func) {
        return;
    }

#ifdef USE_PCI
    pci_clear_irq(ata->func);
#endif
}

static void ata_copy_id_string(uint16_t *pos, const char *str, size_t len)
{
    while (len) {
        *pos++ = (uint16_t)str[0] << 8 | str[1];
        str += 2;
        len -= 2;
    }
}

#ifdef USE_PCI
static void ata_process_prdt(struct ata_dev *ata, rvvm_machine_t *machine)
{
    bool is_read = bit_check(ata->dma_info.cmd, 3);
    size_t to_process = ata->drive[ata->curdrive].sectcount * SECTOR_SIZE;
    blkdev_t* blk = ata->drive[ata->curdrive].blk;
    size_t processed = 0;
    while (1) {
        /* Read PRD */
        uint32_t prd_physaddr;
        if (!rvvm_read_ram(machine, &prd_physaddr,
                    ata->dma_info.prdt_addr, sizeof(prd_physaddr)))
            goto err;
        uint32_t prd_sectcount;
        if (!rvvm_read_ram(machine, &prd_sectcount,
                    ata->dma_info.prdt_addr + 4, sizeof(prd_sectcount)))
            goto err;

        uint32_t buf_size = prd_sectcount & 0xffff;
        /* Value if 0 means size of 64K */
        if (buf_size == 0) {
            buf_size = 64 * 1024;
        }

        void *buf = rvvm_get_dma_ptr(machine, prd_physaddr, buf_size);
        if (!buf) goto err;

        /* Read/write data to/from RAM */
        if (is_read) {
            if (blk_read(blk, buf, buf_size, BLKDEV_CURPOS) != buf_size) {
                goto err;
            }
        } else {
            if (blk_write(blk, buf, buf_size, BLKDEV_CURPOS) != buf_size) {
                goto err;
            }
        }

        processed += buf_size;

        /* If bit 31 is set, this is the last PRD */
        if (bit_check(prd_sectcount, 31)) {
            if (processed != to_process) {
                goto err;
            }

            break;
        }

        /* All good, advance the pointer */
        ata->dma_info.prdt_addr += 8;
    }

    ata->dma_info.cmd &= ~(1 << 0);
    ata->dma_info.status |= (1 << 2);
    ata_send_interrupt(ata);
    return;

err:
    ata->dma_info.status |= (1 << 2) | (1 << 1);
    ata_send_interrupt(ata);
}
#endif

static void ata_cmd_identify(struct ata_dev *ata)
{
    uint16_t id_buf[SECTOR_SIZE / 2] = {
        [0] = (1 << 6), // non-removable, ATA device
        [1] = 65535, // logical cylinders
        [3] = 16, // logical heads
        [6] = 63, // sectors per track
        [22] = 4, // number of bytes available in READ/WRITE LONG cmds
        [47] = 0, // read-write multipe commands not implemented
        [49] = (1 << 9) | (1 << 8), // Capabilities - LBA supported, DMA supported
        [50] = (1 << 14), // Capabilities - bit 14 needs to be set as required by ATA/ATAPI-5 spec
        [51] = (4 << 8), // PIO data transfer cycle timing mode
        [53] = 1 | 2 | 4, // fields 54-58, 64-70 and 88 are valid
        [54] = 65535, // logical cylinders
        [55] = 16, // logical heads
        [56] = 63, // sectors per track
        // capacity in sectors
        [57] = ata->drive[ata->curdrive].size > 0xffffffff ? 0xffff : ata->drive[ata->curdrive].size & 0xffff,
        [58] = ata->drive[ata->curdrive].size > 0xffffffff ? 0xffff : ata->drive[ata->curdrive].size >> 16,
        [60] = ata->drive[ata->curdrive].size > 0xffffffff ? 0xffff : ata->drive[ata->curdrive].size & 0xffff,
        [61] = ata->drive[ata->curdrive].size > 0xffffffff ? 0xffff : ata->drive[ata->curdrive].size >> 16,
        [64] = 1 | 2, // advanced PIO modes supported
        [67] = 1, // PIO transfer cycle time without flow control
        [68] = 1, // PIO transfer cycle time with IORDY flow control
        [80] = 1 << 6, // ATA major version
        [88] = 1 << 5 | 1 << 13, // UDMA mode 5 supported & active
    };

    const char serial[20] = "IDE emulated disk   ";
    const char firmware[9] = "RVVM    ";
    const char model[] = VERSION"                                        ";

    ata_copy_id_string(id_buf + 10, serial, 20);
    ata_copy_id_string(id_buf + 23, firmware, 8);
    ata_copy_id_string(id_buf + 27, model, 40);

    memcpy(ata->drive[ata->curdrive].buf, id_buf, sizeof(id_buf));
    ata->drive[ata->curdrive].bytes_to_rw = sizeof(id_buf);
    ata->drive[ata->curdrive].status = ATA_STATUS_RDY | ATA_STATUS_SRV | ATA_STATUS_DRQ;
    ata->drive[ata->curdrive].sectcount = 1;
    ata_send_interrupt(ata);
}

static void ata_cmd_initialize_device_params(struct ata_dev *ata)
{
    /* CHS translation is not supported */
    ata->drive[ata->curdrive].status |= ATA_STATUS_ERR;
    ata->drive[ata->curdrive].error |= ATA_ERR_ABRT;
}

/* Reads the data to buffer */
static bool ata_read_buf(struct ata_dev *ata)
{
    //printf("ATA fill next sector\n");
    if (!blk_read(ata->drive[ata->curdrive].blk,
                  ata->drive[ata->curdrive].buf,
                  SECTOR_SIZE, BLKDEV_CURPOS)) {
        return false;
    }

    ata->drive[ata->curdrive].bytes_to_rw = SECTOR_SIZE;
    ata_send_interrupt(ata);
    return true;
}

/* Writes the data to buffer */
static bool ata_write_buf(struct ata_dev *ata)
{
    //printf("ATA write buf\n");
    if (!blk_write(ata->drive[ata->curdrive].blk,
                   ata->drive[ata->curdrive].buf,
                   SECTOR_SIZE, BLKDEV_CURPOS)) {
        return false;
    }

    ata_send_interrupt(ata);
    return true;
}

static void ata_cmd_read_sectors(struct ata_dev *ata)
{
    ata->drive[ata->curdrive].sectcount &= 0xff;
    /* Sector count of 0 means 256 */
    if (ata->drive[ata->curdrive].sectcount == 0) {
        ata->drive[ata->curdrive].sectcount = 256;
    }

    //printf("ATA read sectors count: %d offset: 0x%08"PRIx64"\n", ata->drive[ata->curdrive].sectcount, ata_get_lba(ata, false));

    ata->drive[ata->curdrive].status |= ATA_STATUS_DRQ | ATA_STATUS_RDY;
    if (!blk_seek(ata->drive[ata->curdrive].blk,
                  ata_get_lba(ata, false) * SECTOR_SIZE,
                  BLKDEV_SET)) {
        goto err;
    }

    if (!ata_read_buf(ata)) {
        goto err;
    }

    return;
err:
    ata->drive[ata->curdrive].status |= ATA_STATUS_ERR;
    ata->drive[ata->curdrive].error |= ATA_ERR_UNC;
}

static void ata_cmd_write_sectors(struct ata_dev *ata)
{
    ata->drive[ata->curdrive].sectcount &= 0xff;
    /* Sector count of 0 means 256 */
    if (ata->drive[ata->curdrive].sectcount == 0) {
        ata->drive[ata->curdrive].sectcount = 256;
    }

    //printf("ATA write sectors count: %d offset: 0x%08"PRIx64"\n", ata->drive[ata->curdrive].sectcount, ata_get_lba(ata, false));
    ata->drive[ata->curdrive].status |= ATA_STATUS_DRQ | ATA_STATUS_RDY;
    if (!blk_seek(ata->drive[ata->curdrive].blk,
                  ata_get_lba(ata, false) * SECTOR_SIZE,
                  BLKDEV_SET)) {
        goto err;
    }

    ata->drive[ata->curdrive].bytes_to_rw = SECTOR_SIZE;
    return;
err:
    ata->drive[ata->curdrive].status |= ATA_STATUS_ERR;
    ata->drive[ata->curdrive].error |= ATA_ERR_UNC;
}

static void ata_cmd_read_dma(struct ata_dev *ata)
{
    spin_lock(&ata->dma_info.lock);
    ata->drive[ata->curdrive].sectcount &= 0xff;
    /* Sector count of 0 means 256 */
    if (ata->drive[ata->curdrive].sectcount == 0) {
        ata->drive[ata->curdrive].sectcount = 256;
    }

    ata->drive[ata->curdrive].status |= ATA_STATUS_RDY;
    ata->drive[ata->curdrive].status &=
            ~(ATA_STATUS_BSY
            | ATA_STATUS_DF
            | ATA_STATUS_DRQ
            | ATA_STATUS_ERR);

    if (!blk_seek(ata->drive[ata->curdrive].blk,
                  ata_get_lba(ata, false) * SECTOR_SIZE,
                  BLKDEV_SET)) {
        spin_unlock(&ata->dma_info.lock);
        goto err;
    }
    spin_unlock(&ata->dma_info.lock);
    ata_send_interrupt(ata);
    return;
err:
    ata->drive[ata->curdrive].status |= ATA_STATUS_ERR;
    ata->drive[ata->curdrive].error |= ATA_ERR_UNC;
}

static void ata_cmd_write_dma(struct ata_dev *ata)
{
    spin_lock(&ata->dma_info.lock);
    ata->drive[ata->curdrive].sectcount &= 0xff;
    /* Sector count of 0 means 256 */
    if (ata->drive[ata->curdrive].sectcount == 0) {
        ata->drive[ata->curdrive].sectcount = 256;
    }

    ata->drive[ata->curdrive].status |= ATA_STATUS_RDY;
    ata->drive[ata->curdrive].status &=
            ~(ATA_STATUS_BSY
            | ATA_STATUS_DF
            | ATA_STATUS_DRQ
            | ATA_STATUS_ERR);

    if (!blk_seek(ata->drive[ata->curdrive].blk,
                  ata_get_lba(ata, false) * SECTOR_SIZE,
                  BLKDEV_SET)) {
        spin_unlock(&ata->dma_info.lock);
        goto err;
    }
    spin_unlock(&ata->dma_info.lock);
    ata_send_interrupt(ata);
    return;
err:
    ata->drive[ata->curdrive].status |= ATA_STATUS_ERR;
    ata->drive[ata->curdrive].error |= ATA_ERR_UNC;
}

static void ata_cmd_dummy_irq(struct ata_dev *ata)
{
    ata_send_interrupt(ata);
}

static void ata_cmd_check_power_mode(struct ata_dev *ata)
{
    ata->drive[ata->curdrive].sectcount = 0xff; /* always active */
    ata_send_interrupt(ata);
}

static void ata_handle_cmd(struct ata_dev *ata, uint8_t cmd)
{
    //printf("ATA command: 0x%02X\n", cmd);
    switch (cmd) {
        case ATA_CMD_IDENTIFY: ata_cmd_identify(ata); break;
        case ATA_CMD_INITIALIZE_DEVICE_PARAMS: ata_cmd_initialize_device_params(ata); break;
        case ATA_CMD_READ_SECTORS: ata_cmd_read_sectors(ata); break;
        case ATA_CMD_WRITE_SECTORS: ata_cmd_write_sectors(ata); break;
        case ATA_CMD_READ_DMA: ata_cmd_read_dma(ata); break;
        case ATA_CMD_WRITE_DMA: ata_cmd_write_dma(ata); break;
        case ATA_CMD_CHECK_POWER_MODE: ata_cmd_check_power_mode(ata); break;
        case ATA_CMD_SLEEP:
        case ATA_CMD_IDLE:
        case ATA_CMD_IDLE_IMMEDIATE:
        case ATA_CMD_STANDBY:
        case ATA_CMD_STANDBY_IMMEDIATE: ata_cmd_dummy_irq(ata); break;
        default: rvvm_info("ATA unknown cmd 0x%02x", cmd);
    }
}

static bool ata_data_mmio_read_handler(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    struct ata_dev *ata = (struct ata_dev *) device->data;

#if 0
    printf("ATA DATA MMIO offset: %d size: %d read\n", offset, size);
#endif
    if ((offset & ((1 << ATA_REG_SHIFT) - 1)) != 0) {
        /* TODO: misalign */
        return false;
    }

    offset >>= ATA_REG_SHIFT;

    /* DATA register is of any size, others are 1 byte r/w */
    if (size != 1 && offset != ATA_REG_DATA) {
        /* TODO: misalign */
        return false;
    }

    switch (offset) {
        case ATA_REG_DATA:
            if (ata->drive[ata->curdrive].bytes_to_rw != 0) {
                uint8_t *addr = ata->drive[ata->curdrive].buf + SECTOR_SIZE - ata->drive[ata->curdrive].bytes_to_rw;
#if 0
                if (size == 4) {
                    write_uint32_le(memory_data, read_uint32_le(addr));
                } else if (size == 2) {
                    write_uint16_le(memory_data, read_uint16_le(addr));
                }
#endif
                memcpy(memory_data, addr, size);

                ata->drive[ata->curdrive].bytes_to_rw -= size;
                if (ata->drive[ata->curdrive].bytes_to_rw == 0) {
                    ata->drive[ata->curdrive].status &= ~ATA_STATUS_DRQ;
                    if (--ata->drive[ata->curdrive].sectcount != 0) {
                        ata->drive[ata->curdrive].status |= ATA_STATUS_DRQ;
                        if (!ata_read_buf(ata)) {
                            ata->drive[ata->curdrive].status |= ATA_STATUS_ERR;
                            ata->drive[ata->curdrive].error |= ATA_ERR_UNC;
                        }
                    }
                }
            } else {
                memset(memory_data, '\0', size);
            }
            break;
        case ATA_REG_ERR:
            /* ERROR */

            /* OSDev says that this register is 16-bit,
             * but there's no address stored so this seems wrong */
            memcpy(memory_data, &ata->drive[ata->curdrive].error, size);
            break;
        case ATA_REG_NSECT:
            *(uint8_t*) memory_data = (ata->drive[ata->curdrive].sectcount >> ata->drive[ata->curdrive].hob_shift) & 0xff;
            break;
        case ATA_REG_LBAL:
            *(uint8_t*) memory_data = (ata->drive[ata->curdrive].lbal >> ata->drive[ata->curdrive].hob_shift) & 0xff;
            break;
        case ATA_REG_LBAM:
            *(uint8_t*) memory_data = (ata->drive[ata->curdrive].lbam >> ata->drive[ata->curdrive].hob_shift) & 0xff;
            break;
        case ATA_REG_LBAH:
            *(uint8_t*) memory_data = (ata->drive[ata->curdrive].lbah >> ata->drive[ata->curdrive].hob_shift) & 0xff;
            break;
        case ATA_REG_DEVICE:
            *(uint8_t*) memory_data = ata->drive[ata->curdrive].drive | (1 << 5) | (1 << 7);
            break;
        case ATA_REG_STATUS:
            /* STATUS */
            *(uint8_t*) memory_data = ata->drive[ata->curdrive].status;
            ata_clear_interrupt(ata);
            break;
    }

    return true;
}

static bool ata_data_mmio_write_handler(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    struct ata_dev *ata = (struct ata_dev *) device->data;

#if 0
    printf("ATA DATA MMIO offset: %d size: %d write val: 0x%02X\n", offset, size, *(uint8_t*) memory_data);
#endif
    if ((offset & ((1 << ATA_REG_SHIFT) - 1)) != 0) {
        /* TODO: misalign */
        return false;
    }

    offset >>= ATA_REG_SHIFT;

    /* DATA register is of any size, others are 1 byte r/w */
    if (size != 1 && offset != ATA_REG_DATA) {
        /* TODO: misalign */
        return false;
    }

    switch (offset) {
        case ATA_REG_DATA:
            {
                uint8_t *addr = ata->drive[ata->curdrive].buf + SECTOR_SIZE - ata->drive[ata->curdrive].bytes_to_rw;
                memcpy(addr, memory_data, size);

                ata->drive[ata->curdrive].bytes_to_rw -= size;
                if (ata->drive[ata->curdrive].bytes_to_rw == 0) {
                    ata->drive[ata->curdrive].status &= ~ATA_STATUS_DRQ;
                    if (--ata->drive[ata->curdrive].sectcount != 0) {
                        ata->drive[ata->curdrive].status |= ATA_STATUS_DRQ;
                        ata->drive[ata->curdrive].bytes_to_rw = SECTOR_SIZE;
                    }
                    if (!ata_write_buf(ata)) {
                        ata->drive[ata->curdrive].status |= ATA_STATUS_ERR;
                        ata->drive[ata->curdrive].error |= ATA_ERR_UNC;
                    }
                }
            }
            break;
        case ATA_REG_ERR:
            /* FEATURES - ignore */
            break;
        case ATA_REG_NSECT:
            ata->drive[ata->curdrive].sectcount <<= 8;
            ata->drive[ata->curdrive].sectcount |= *(uint8_t*) memory_data;
            break;
        case ATA_REG_LBAL:
            ata->drive[ata->curdrive].lbal <<= 8;
            ata->drive[ata->curdrive].lbal |= *(uint8_t*) memory_data;
            break;
        case ATA_REG_LBAM:
            ata->drive[ata->curdrive].lbam <<= 8;
            ata->drive[ata->curdrive].lbam |= *(uint8_t*) memory_data;
            break;
        case ATA_REG_LBAH:
            ata->drive[ata->curdrive].lbah <<= 8;
            ata->drive[ata->curdrive].lbah |= *(uint8_t*) memory_data;
            break;
        case ATA_REG_DEVICE:
            ata->curdrive = bit_check(*(uint8_t*) memory_data, 4) ? 1 : 0;
            ata->drive[ata->curdrive].drive = *(uint8_t*) memory_data;
            break;
        case ATA_REG_STATUS:
            /* Command */

            /* Not sure when error is cleared.
             * Spec says that it contains status of the last command executed */
            ata->drive[ata->curdrive].error = 0;
            ata->drive[ata->curdrive].status &= ~ATA_STATUS_ERR;
            ata_handle_cmd(ata, *(uint8_t*) memory_data);
            break;
    }

    return true;
}

static bool ata_ctl_mmio_read_handler(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    struct ata_dev *ata = (struct ata_dev *) device->data;

#if 0
    printf("ATA CTL MMIO offset: %d size: %d read\n", offset, size);
#endif
    if (size != 1 || (offset & ((1 << ATA_REG_SHIFT) - 1)) != 0) {
        /* TODO: misalign */
        return false;
    }

    offset >>= ATA_REG_SHIFT;

    switch (offset) {
        case ATA_REG_CTL:
            /* Alternate STATUS */
            *(uint8_t*) memory_data = ata->drive[ata->curdrive].status;
            ata_clear_interrupt(ata);
            break;
        case ATA_REG_DRVADDR:
            /* TODO: seems that Linux doesn't use this */
            break;
    }

    return true;
}

static bool ata_ctl_mmio_write_handler(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    struct ata_dev *ata = (struct ata_dev *) device->data;

#if 0
    printf("ATA CTL MMIO offset: %d size: %d write val: 0x%02X\n", offset, size, *(uint8_t*) memory_data);
#endif
    if (size != 1 || (offset & ((1 << ATA_REG_SHIFT) - 1)) != 0) {
        /* TODO: misalign */
        return false;
    }

    offset >>= ATA_REG_SHIFT;

    switch (offset) {
        case ATA_REG_CTL:
            /* Device control */
            ata->drive[ata->curdrive].nien = bit_check(*(uint8_t*) memory_data, 1);
            ata->drive[ata->curdrive].hob_shift = bit_check(*(uint8_t*) memory_data, 7) ? 8 : 0;
            if (bit_check(*(uint8_t*) memory_data, 2)) {
                /* Soft reset */
                ata->drive[ata->curdrive].bytes_to_rw = 0;
                ata->drive[ata->curdrive].lbal = 1; /* Sectors start from 1 */
                ata->drive[ata->curdrive].lbah = 0;
                ata->drive[ata->curdrive].lbam = 0;
                ata->drive[ata->curdrive].sectcount = 1;
                ata->drive[ata->curdrive].drive = 0;
                if (ata->drive[ata->curdrive].blk != NULL) {
                    ata->drive[ata->curdrive].error = ATA_ERR_AMNF; /* AMNF means OK here... */
                    ata->drive[ata->curdrive].status = ATA_STATUS_RDY | ATA_STATUS_SRV;
                } else {
                    ata->drive[ata->curdrive].error = 0;
                    ata->drive[ata->curdrive].status = 0;
                }
            }
            break;
        case ATA_REG_DRVADDR:
            /* TODO: seems that Linux doesn't use this */
            break;
    }

    return true;
}

#ifdef USE_PCI
static bool ata_bmdma_mmio_read_handler(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    struct ata_dev *ata = (struct ata_dev *) device->data;

#if 0
    printf("ATA BMDMA MMIO offset: %d size: %d read\n", offset, size);
#endif
    switch (offset)
    {
        case ATA_BMDMA_CMD:
            if (size != 1) return false;
            *(uint8_t*)memory_data = ata->dma_info.cmd;
            break;
        case ATA_BMDMA_STATUS:
            if (size != 1) return false;
            *(uint8_t*) memory_data = ata->dma_info.status
                | (ata->drive[0].blk != NULL) << 5
                | (ata->drive[1].blk != NULL) << 6;
            break;
        case ATA_BMDMA_PRDT:
            {
                if (size != 4) return false;
                *(uint32_t*)memory_data = (uint32_t) ata->dma_info.prdt_addr;
                break;
            }
        default:
            /* secondary controller not supported now */
            return false;
    }

    return true;
}


static void* ata_worker(void** data)
{
    struct ata_dev* ata = (struct ata_dev *)data[0];
    spin_lock(&ata->dma_info.lock);
    ata_process_prdt(ata, (rvvm_machine_t*)data[1]);
    spin_unlock(&ata->dma_info.lock);
    return NULL;
}

static bool ata_bmdma_mmio_write_handler(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    struct ata_dev *ata = (struct ata_dev *) device->data;
    bool process_prdt;

#if 0
    printf("ATA BMDMA MMIO offset: %d size: %d write val: 0x%02X\n", offset, size, *(uint8_t*) memory_data);
#endif
    switch (offset)
    {
        case ATA_BMDMA_CMD:
            if (size != 1) return false;
            spin_lock(&ata->dma_info.lock);
            process_prdt = !(ata->dma_info.cmd & 1) && (*(uint8_t*)memory_data & 1);
            ata->dma_info.cmd = *(uint8_t*)memory_data;
            spin_unlock(&ata->dma_info.lock);
            if (process_prdt) {
                if (rvvm_has_arg("noasync")) {
                    ata_process_prdt(ata, device->machine);
                } else {
                    void* req[2] = {ata, device->machine};
                    thread_create_task_va(ata_worker, req, 2);
                }
            }
            break;
        case ATA_BMDMA_STATUS:
            if (size != 1) return false;
            spin_lock(&ata->dma_info.lock);
            ata->dma_info.status &= ~(*(uint8_t*)memory_data & 6);
            if (!bit_check(ata->dma_info.status, 2)) {
                ata_clear_interrupt(ata);
            }
            spin_unlock(&ata->dma_info.lock);
            break;
        case ATA_BMDMA_PRDT:
            if (size != 4) return false;
            spin_lock(&ata->dma_info.lock);
            ata->dma_info.prdt_addr = (paddr_t)*(uint32_t*) memory_data;
            spin_unlock(&ata->dma_info.lock);
            break;
        default:
            /* secondary controller not supported now */
            return false;
    }

    return true;
}
#endif

static void ata_data_remove(rvvm_mmio_dev_t* device)
{
    struct ata_dev *ata = (struct ata_dev *) device->data;
    spin_lock(&ata->dma_info.lock);
    for (size_t i = 0; i < sizeof(ata->drive) / sizeof(ata->drive[0]); ++i) {
        if (ata->drive[i].blk != NULL) {
            blk_close(ata->drive[i].blk);
        }
    }
    spin_unlock(&ata->dma_info.lock);

    free(ata);
}

static rvvm_mmio_type_t ata_data_dev_type = {
    .name = "ata_data",
    .remove = ata_data_remove,
};

static void ata_remove_dummy(rvvm_mmio_dev_t* device)
{
    /* dummy remove */
    UNUSED(device);
}

static rvvm_mmio_type_t ata_ctl_dev_type = {
    .name = "ata_ctl",
    .remove = ata_remove_dummy,
};

void ata_init_pio(rvvm_machine_t* machine, paddr_t data_base_addr, paddr_t ctl_base_addr, blkdev_t* master, blkdev_t* slave)
{
    struct ata_dev *ata = (struct ata_dev*)safe_calloc(sizeof(struct ata_dev), 1);
    ata->drive[0].blk = master;
    ata->drive[0].size = master == NULL ? 0 : DIV_ROUND_UP(blk_getsize(ata->drive[0].blk), SECTOR_SIZE);
    if (ata->drive[0].size == 0) {
        ata->drive[0].blk = NULL;
    }
    ata->drive[1].blk = slave;
    ata->drive[1].size = slave == NULL ? 0 : DIV_ROUND_UP(blk_getsize(ata->drive[1].blk), SECTOR_SIZE);
    if (ata->drive[1].size == 0) {
        ata->drive[1].blk = NULL;
    }
    spin_init(&ata->dma_info.lock);

    rvvm_mmio_dev_t ata_data;
    ata_data.min_op_size = 1;
    ata_data.max_op_size = 2;
    ata_data.read = ata_data_mmio_read_handler;
    ata_data.write = ata_data_mmio_write_handler;
    ata_data.type = &ata_data_dev_type;
    ata_data.begin = data_base_addr;
    ata_data.end = data_base_addr + ((ATA_REG_STATUS + 1) << ATA_REG_SHIFT);
    ata_data.data = ata;
    rvvm_attach_mmio(machine, &ata_data);

    rvvm_mmio_dev_t ata_ctl;
    ata_ctl.min_op_size = 1,
    ata_ctl.max_op_size = 1,
    ata_ctl.read = ata_ctl_mmio_read_handler,
    ata_ctl.write = ata_ctl_mmio_write_handler,
    ata_ctl.type = &ata_ctl_dev_type,
    ata_ctl.begin = ctl_base_addr;
    ata_ctl.end = ctl_base_addr + ((ATA_REG_DRVADDR + 1) << ATA_REG_SHIFT);
    ata_ctl.data = ata;
    rvvm_attach_mmio(machine, &ata_ctl);

#ifdef USE_FDT
    uint32_t reg_cells[8];
    struct fdt_node* soc = fdt_node_find(machine->fdt, "soc");
    if (soc == NULL) {
        rvvm_warn("Missing soc node in FDT!");
        return;
    }

    reg_cells[0] = ((uint64_t)data_base_addr) >> 32;
    reg_cells[1] = data_base_addr;
    reg_cells[4] = ((uint64_t)ctl_base_addr) >> 32;
    reg_cells[5] = ctl_base_addr;
    reg_cells[2] = reg_cells[6] = 0;
    reg_cells[3] = reg_cells[7] = 0x1000;

    struct fdt_node* ata_node = fdt_node_create_reg("ata", data_base_addr);
    fdt_node_add_prop_cells(ata_node, "reg", reg_cells, 8);
    fdt_node_add_prop_str(ata_node, "compatible", "ata-generic");
    fdt_node_add_prop_u32(ata_node, "reg-shift", ATA_REG_SHIFT);
    fdt_node_add_prop_u32(ata_node, "pio-mode", 4);
    fdt_node_add_child(soc, ata_node);

    struct fdt_node* chosen = fdt_node_find(machine->fdt, "chosen");
    if (chosen == NULL) {
        rvvm_warn("Missing chosen node in FDT!");
        return;
    }
    fdt_node_add_prop_str(chosen, "bootargs", "root=/dev/sda rw");
#endif
}

#ifdef USE_PCI
static bool ata_data_read_primary(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    return ata_data_mmio_read_handler(device, memory_data, offset, size);
}

static bool ata_data_write_primary(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    return ata_data_mmio_write_handler(device, memory_data, offset, size);
}

static bool ata_ctl_read_primary(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    offset -= 2;
    return ata_ctl_mmio_read_handler(device, memory_data, offset, size);
}

static bool ata_ctl_write_primary(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    offset -= 2;
    return ata_ctl_mmio_write_handler(device, memory_data, offset, size);
}

#if 0
static bool ata_data_read_secondary(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    return ata_data_mmio_read_handler(device, memory_data, offset, size);
}

static bool ata_data_write_secondary(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    return ata_data_mmio_write_handler(device, memory_data, offset, size);
}

static bool ata_ctl_read_secondary(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    offset -= 2;
    return ata_ctl_mmio_read_handler(device, memory_data, offset, size);
}

static bool ata_ctl_write_secondary(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    offset -= 2;
    return ata_ctl_mmio_write_handler(device, memory_data, offset, size);
}
#endif

#endif

#ifdef USE_PCI
void ata_init_pci(rvvm_machine_t* machine, struct pci_bus *pci_bus, blkdev_t* master, blkdev_t* slave)
{
    assert(master != NULL || slave != NULL);
    struct ata_dev *ata = (struct ata_dev*)safe_calloc(sizeof(struct ata_dev), 1);
    ata->drive[0].blk = master;
    ata->drive[0].size = master == NULL ? 0 : DIV_ROUND_UP(blk_getsize(ata->drive[0].blk), SECTOR_SIZE);
    if (ata->drive[0].size == 0) {
        ata->drive[0].blk = NULL;
    }
    ata->drive[1].blk = slave;
    ata->drive[1].size = slave == NULL ? 0 : DIV_ROUND_UP(blk_getsize(ata->drive[1].blk), SECTOR_SIZE);
    if (ata->drive[1].size == 0) {
        ata->drive[1].blk = NULL;
    }
    spin_init(&ata->dma_info.lock);

    static struct pci_device_desc ata_desc = {
        .func[0] = {
            .vendor_id = 0x8086,
            .device_id = 0x1c3c, /* 6 Series/C200 Series Chipset Family IDE-r Controller */
            .class_code = 0x0101, /* IDE */
            .prog_if = 1 | 4, /* Do not use legacy mode */
            .irq_pin = 1, /* INTA, unused by now */
            .bar = {
                {
                    //.len = (ATA_REG_STATUS + 1) << ATA_REG_SHIFT,
                    .len = 4096,
                    .min_op_size = 1,
                    .max_op_size = 2,
                    .read = ata_data_read_primary,
                    .write = ata_data_write_primary,
                },
                {
                    //.len = (ATA_REG_DRVADDR + 1) << ATA_REG_SHIFT,
                    .len = 4096,
                    .min_op_size = 1,
                    .max_op_size = 1,
                    .read = ata_ctl_read_primary,
                    .write = ata_ctl_write_primary,
                },
#if 0
                {
                    //.len = (ATA_REG_STATUS + 1) << ATA_REG_SHIFT,
                    .len = 4096,
                    .min_op_size = 1,
                    .max_op_size = 2,
                    .read = ata_data_read_secondary,
                    .write = ata_data_write_secondary,
                },
                {
                    //.len = (ATA_REG_DRVADDR + 1) << ATA_REG_SHIFT,
                    .len = 4096,
                    .min_op_size = 1,
                    .max_op_size = 1,
                    .read = ata_ctl_read_secondary,
                    .write = ata_ctl_write_secondary,
                },
#else
                { 0 }, { 0 },
#endif
                {
                    .len = 16,
                    .min_op_size = 1,
                    .max_op_size = 4,
                    .read = ata_bmdma_mmio_read_handler,
                    .write = ata_bmdma_mmio_write_handler,
                }
            }
        }
    };

    struct pci_device *pci_dev = pci_bus_add_device(machine, pci_bus, &ata_desc, (void*) ata);

    ata->func = &pci_dev->func[0];
    struct rvvm_mmio_dev_t *mmio_dev = rvvm_get_mmio(machine, ata->func->bar_mapping[0]);
    if (!mmio_dev) {
        rvvm_warn("ATA BAR mapping 0 not found!");
        return;
    }
    mmio_dev->data = (void *) ata;
    /* for remove & update function */
    mmio_dev->type = &ata_data_dev_type;
    mmio_dev = rvvm_get_mmio(machine, pci_dev->func[0].bar_mapping[1]);
    if (!mmio_dev) {
        rvvm_warn("ATA BAR mapping 1 not found!");
        return;
    }
    mmio_dev->data = (void *) ata;
    mmio_dev->type = &ata_ctl_dev_type;
    mmio_dev = rvvm_get_mmio(machine, pci_dev->func[0].bar_mapping[4]);
    if (!mmio_dev) {
        rvvm_warn("ATA BAR mapping 4 not found!");
        return;
    }
    mmio_dev->data = (void *) ata;
    mmio_dev->type = &ata_ctl_dev_type;

#ifdef USE_FDT
    struct fdt_node* chosen = fdt_node_find(machine->fdt, "chosen");
    if (chosen == NULL) {
        rvvm_warn("Missing chosen node in FDT!");
        return;
    }
    fdt_node_add_prop_str(chosen, "bootargs", "root=/dev/sda rw");
#endif
}
#endif

