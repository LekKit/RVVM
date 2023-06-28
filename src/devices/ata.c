/*
ata.c - IDE/ATA disk controller
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

#include "ata.h"
#include "bit_ops.h"
#include "mem_ops.h"
#include "spinlock.h"
#include "utils.h"
#include "threading.h"

#ifdef USE_FDT
#include "fdtlib.h"
#endif

// Data registers
#define ATA_REG_DATA   0x00
#define ATA_REG_ERR    0x01 // or FEATURE
#define ATA_REG_NSECT  0x02
#define ATA_REG_LBAL   0x03
#define ATA_REG_LBAM   0x04
#define ATA_REG_LBAH   0x05
#define ATA_REG_DEVICE 0x06
#define ATA_REG_STATUS 0x07 // or CMD

// Control registers
#define ATA_REG_CTL     0x00 // or alternate STATUS
#define ATA_REG_DRVADDR 0x01

// 16-bit registers - needed for LBA48
#define ATA_REG_SHIFT 0
typedef uint16_t atareg_t;

// Error flags for ERR register
#define ATA_ERR_AMNF  (1 << 0)
#define ATA_ERR_TKZNF (1 << 1)
#define ATA_ERR_ABRT  (1 << 2)
#define ATA_ERR_MCR   (1 << 3)
#define ATA_ERR_IDNF  (1 << 4)
#define ATA_ERR_MC    (1 << 5)
#define ATA_ERR_UNC   (1 << 6)
#define ATA_ERR_BBK   (1 << 7)

// Flags for STATUS register
#define ATA_STATUS_ERR  (1 << 0)
#define ATA_STATUS_IDX  (1 << 1)
#define ATA_STATUS_CORR (1 << 2)
#define ATA_STATUS_DRQ  (1 << 3)
#define ATA_STATUS_SRV  (1 << 4) // or DSC aka Seek Complete, deprecated
#define ATA_STATUS_DF   (1 << 5)
#define ATA_STATUS_RDY  (1 << 6)
#define ATA_STATUS_BSY  (1 << 7)

// Flags for DRIVE/HEAD register
#define ATA_DRIVE_DRV (1 << 4)
#define ATA_DRIVE_LBA (1 << 6)

// Commands
#define ATA_CMD_IDENTIFY          0xEC
#define ATA_CMD_INITIALIZE_DEVICE_PARAMS 0x91
#define ATA_CMD_READ_SECTORS      0x20
#define ATA_CMD_WRITE_SECTORS     0x30
#define ATA_CMD_READ_DMA          0xC8
#define ATA_CMD_WRITE_DMA         0xCA
#define ATA_CMD_STANDBY_IMMEDIATE 0xE0
#define ATA_CMD_IDLE_IMMEDIATE    0xE1
#define ATA_CMD_STANDBY           0xE2
#define ATA_CMD_IDLE              0xE3
#define ATA_CMD_CHECK_POWER_MODE  0xE4
#define ATA_CMD_SLEEP             0xE6

// BMDMA registers
#define ATA_BMDMA_CMD    0x0
#define ATA_BMDMA_STATUS 0x2
#define ATA_BMDMA_PRDT   0x4

#define SECTOR_SIZE 512

/* CHS is not supported - it's dead anyway...
// Limits for C/H/S calculation
// Max sectors per track: 255, usually 63 because of six bit numberring. Starts from 1
#define MAX_SPT           63 // Sectors per track
#define MAX_HPC           16 // Heads per cyllinder/track
#define MAX_CYLLINDERS 65536 // 16-bit addressing limit
*/

#define DIV_ROUND_UP(x, d) (((x) + (d) - 1) / (d))

struct ata_dev
{
    struct {
        blkdev_t* blk;
        size_t size; // In sectors
        uint16_t bytes_to_rw;
        uint16_t sectcount;
        atareg_t lbal;
        atareg_t lbam;
        atareg_t lbah;
        atareg_t drive;
        atareg_t error;
        uint8_t status;
        uint8_t hob_shift;
        bool nien; // Interrupt disable
        uint8_t buf[SECTOR_SIZE];
    } drive[2];
    struct {
        paddr_t prdt_addr;
        spinlock_t lock;
        uint8_t cmd;
        uint8_t status;
    } dma_info;
    spinlock_t lock;
    uint8_t curdrive;
    pci_dev_t* pci_dev;
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
    pci_send_irq(ata->pci_dev, 0);
}

static void ata_clear_interrupt(struct ata_dev *ata) {
    pci_clear_irq(ata->pci_dev, 0);
}

static void ata_copy_id_string(uint8_t* buf, const char* str)
{
    // Reverse each byte pair since they are little-endian words
    size_t len = strlen(str);
    for (size_t i=0; i<len; ++i) {
        buf[i ^ 1ULL] = str[i];
    }
}

static void ata_cmd_identify(struct ata_dev *ata)
{
    uint8_t id_buf[SECTOR_SIZE] = {0};
    write_uint16_le(id_buf,         0x40); // Non-removable, ATA device
    write_uint16_le(id_buf + 2,   0xFFFF); // Logical cylinders
    write_uint16_le(id_buf + 6,     0x10); // Sectors per track
    write_uint16_le(id_buf + 12,    0x3F); // Logical heads
    write_uint16_le(id_buf + 44,     0x4); // Number of bytes available in READ/WRITE LONG cmds
    write_uint16_le(id_buf + 98,   0x300); // Capabilities - LBA supported, DMA supported
    write_uint16_le(id_buf + 100, 0x4000); // Capabilities - bit 14 needs to be set as required by ATA/ATAPI-5 spec
    write_uint16_le(id_buf + 102,  0x400); // PIO data transfer cycle timing mode
    write_uint16_le(id_buf + 106,    0x7); // Fields 54-58, 64-70 and 88 are valid
    write_uint16_le(id_buf + 108, 0xFFFF); // Logical cylinders
    write_uint16_le(id_buf + 110,   0x10); // Logical heads
    write_uint16_le(id_buf + 112,   0x3F); // Sectors per track
    // Capacity in sectors
    write_uint16_le(id_buf + 114,  ata->drive[ata->curdrive].size);
    write_uint16_le(id_buf + 116,  ata->drive[ata->curdrive].size >> 16);
    write_uint16_le(id_buf + 120,  ata->drive[ata->curdrive].size);
    write_uint16_le(id_buf + 122,  ata->drive[ata->curdrive].size >> 16);
    write_uint16_le(id_buf + 128,    0x3); // Advanced PIO modes supported
    write_uint16_le(id_buf + 134,    0x1); // PIO transfer cycle time without flow control
    write_uint16_le(id_buf + 136,    0x1); // PIO transfer cycle time with IORDY flow control
    write_uint16_le(id_buf + 160,  0x100); // ATA major version
    write_uint16_le(id_buf + 176, 0x80FF); // UDMA mode 7 active, All UDMA modes supported

    // Serial Number
    ata_copy_id_string(id_buf + 20, "DEADBEEF            ");
    // Firmware Revision
    ata_copy_id_string(id_buf + 46, "R948    ");
    // Model Number
    ata_copy_id_string(id_buf + 54, "IDE HDD                                 ");

    memcpy(ata->drive[ata->curdrive].buf, id_buf, sizeof(id_buf));
    ata->drive[ata->curdrive].bytes_to_rw = sizeof(id_buf);
    ata->drive[ata->curdrive].status = ATA_STATUS_RDY | ATA_STATUS_SRV | ATA_STATUS_DRQ;
    ata->drive[ata->curdrive].sectcount = 1;
    ata_send_interrupt(ata);
}

static void ata_cmd_initialize_device_params(struct ata_dev *ata)
{
    // CHS translation is not supported
    ata->drive[ata->curdrive].status |= ATA_STATUS_ERR;
    ata->drive[ata->curdrive].error |= ATA_ERR_ABRT;
}

// Reads the data to buffer
static bool ata_read_buf(struct ata_dev *ata)
{
    if (!blk_read(ata->drive[ata->curdrive].blk,
                  ata->drive[ata->curdrive].buf,
                  SECTOR_SIZE, BLKDEV_CURPOS)) {
        return false;
    }

    ata->drive[ata->curdrive].bytes_to_rw = SECTOR_SIZE;
    ata_send_interrupt(ata);
    return true;
}

// Writes the data from buffer to drive
static bool ata_write_buf(struct ata_dev *ata)
{
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
    // Sector count of 0 means 256
    if (ata->drive[ata->curdrive].sectcount == 0) {
        ata->drive[ata->curdrive].sectcount = 256;
    }

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
    // Sector count of 0 means 256
    if (ata->drive[ata->curdrive].sectcount == 0) {
        ata->drive[ata->curdrive].sectcount = 256;
    }

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
    // Sector count of 0 means 256
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
    // Sector count of 0 means 256
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
    ata->drive[ata->curdrive].sectcount = 0xff; // Always active
    ata_send_interrupt(ata);
}

static void ata_handle_cmd(struct ata_dev *ata, uint8_t cmd)
{
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

static bool ata_data_mmio_read_handler(rvvm_mmio_dev_t* device, void* data, size_t offset, uint8_t size)
{
    struct ata_dev* ata = (struct ata_dev*)device->data;
    offset >>= ATA_REG_SHIFT;
    spin_lock(&ata->lock);
    switch (offset) {
        case ATA_REG_DATA:
            if (ata->drive[ata->curdrive].bytes_to_rw >= size) {
                uint8_t* addr = ata->drive[ata->curdrive].buf + SECTOR_SIZE - ata->drive[ata->curdrive].bytes_to_rw;
                memcpy(data, addr, size);

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
                memset(data, 0, size);
            }
            break;
        case ATA_REG_ERR:
            // OSDev says that this register is 16-bit,
            // but there's no address stored so this seems wrong
            if (size == 2) {
                write_uint16_le(data, ata->drive[ata->curdrive].error);
            } else {
                write_uint8(data, ata->drive[ata->curdrive].error);
            }
            break;
        case ATA_REG_NSECT:
            write_uint8(data, ata->drive[ata->curdrive].sectcount >> ata->drive[ata->curdrive].hob_shift);
            break;
        case ATA_REG_LBAL:
            write_uint8(data, ata->drive[ata->curdrive].lbal >> ata->drive[ata->curdrive].hob_shift);
            break;
        case ATA_REG_LBAM:
            write_uint8(data, ata->drive[ata->curdrive].lbam >> ata->drive[ata->curdrive].hob_shift);
            break;
        case ATA_REG_LBAH:
            write_uint8(data, ata->drive[ata->curdrive].lbah >> ata->drive[ata->curdrive].hob_shift);
            break;
        case ATA_REG_DEVICE:
            write_uint8(data, ata->drive[ata->curdrive].drive | (1 << 5) | (1 << 7));
            break;
        case ATA_REG_STATUS:
            write_uint8(data, ata->drive[ata->curdrive].status);
            ata_clear_interrupt(ata);
            break;
        default:
            memset(data, 0, size);
            break;
    }
    spin_unlock(&ata->lock);

    return true;
}

static bool ata_data_mmio_write_handler(rvvm_mmio_dev_t* device, void* data, size_t offset, uint8_t size)
{
    struct ata_dev* ata = (struct ata_dev*)device->data;
    offset >>= ATA_REG_SHIFT;
    spin_lock(&ata->lock);
    switch (offset) {
        case ATA_REG_DATA:
            if (ata->drive[ata->curdrive].bytes_to_rw >= size) {
                uint8_t* addr = ata->drive[ata->curdrive].buf + SECTOR_SIZE - ata->drive[ata->curdrive].bytes_to_rw;
                memcpy(addr, data, size);

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
        case ATA_REG_ERR: // Features - ignore
            break;
        case ATA_REG_NSECT:
            ata->drive[ata->curdrive].sectcount <<= 8;
            ata->drive[ata->curdrive].sectcount |= read_uint8(data);
            break;
        case ATA_REG_LBAL:
            ata->drive[ata->curdrive].lbal <<= 8;
            ata->drive[ata->curdrive].lbal |= read_uint8(data);
            break;
        case ATA_REG_LBAM:
            ata->drive[ata->curdrive].lbam <<= 8;
            ata->drive[ata->curdrive].lbam |= read_uint8(data);
            break;
        case ATA_REG_LBAH:
            ata->drive[ata->curdrive].lbah <<= 8;
            ata->drive[ata->curdrive].lbah |= read_uint8(data);
            break;
        case ATA_REG_DEVICE:
            ata->curdrive = bit_check(read_uint8(data), 4) ? 1 : 0;
            ata->drive[ata->curdrive].drive = read_uint8(data);
            break;
        case ATA_REG_STATUS: // Command
            // Not sure when error is cleared.
            // Spec says that it contains status of the last command executed
            ata->drive[ata->curdrive].error = 0;
            ata->drive[ata->curdrive].status &= ~ATA_STATUS_ERR;
            ata_handle_cmd(ata, read_uint8(data));
            break;
    }
    spin_unlock(&ata->lock);

    return true;
}

static bool ata_ctl_mmio_read_handler(rvvm_mmio_dev_t* device, void* data, size_t offset, uint8_t size)
{
    struct ata_dev* ata = (struct ata_dev*)device->data;
    UNUSED(size);
    offset >>= ATA_REG_SHIFT;
    spin_lock(&ata->lock);
    switch (offset) {
        case ATA_REG_CTL: // Alternate STATUS
            write_uint8(data, ata->drive[ata->curdrive].status);
            ata_clear_interrupt(ata);
            break;
        case ATA_REG_DRVADDR: // TODO: Seems that Linux doesn't use this
            break;
    }
    spin_unlock(&ata->lock);

    return true;
}

static bool ata_ctl_mmio_write_handler(rvvm_mmio_dev_t* device, void* data, size_t offset, uint8_t size)
{
    struct ata_dev* ata = (struct ata_dev*)device->data;
    UNUSED(size);
    offset >>= ATA_REG_SHIFT;
    spin_lock(&ata->lock);
    switch (offset) {
        case ATA_REG_CTL: // Device control
            ata->drive[ata->curdrive].nien = bit_check(read_uint8(data), 1);
            ata->drive[ata->curdrive].hob_shift = bit_check(read_uint8(data), 7) ? 8 : 0;
            if (bit_check(read_uint8(data), 2)) {
                // Soft reset
                ata->drive[ata->curdrive].bytes_to_rw = 0;
                ata->drive[ata->curdrive].lbal = 1; // Sectors start from 1
                ata->drive[ata->curdrive].lbah = 0;
                ata->drive[ata->curdrive].lbam = 0;
                ata->drive[ata->curdrive].sectcount = 1;
                ata->drive[ata->curdrive].drive = 0;
                if (ata->drive[ata->curdrive].blk != NULL) {
                    ata->drive[ata->curdrive].error = ATA_ERR_AMNF; // AMNF means OK here...
                    ata->drive[ata->curdrive].status = ATA_STATUS_RDY | ATA_STATUS_SRV;
                } else {
                    ata->drive[ata->curdrive].error = 0;
                    ata->drive[ata->curdrive].status = 0;
                }
            }
            break;
        case ATA_REG_DRVADDR: // TODO: Seems that Linux doesn't use this
            break;
    }
    spin_unlock(&ata->lock);

    return true;
}

static void ata_data_remove(rvvm_mmio_dev_t* device)
{
    struct ata_dev *ata = (struct ata_dev*)device->data;
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
    // Dummy remove, cleanup happends in ata_data_remove()
    UNUSED(device);
}

static rvvm_mmio_type_t ata_ctl_dev_type = {
    .name = "ata_ctl",
    .remove = ata_remove_dummy,
};

static struct ata_dev* ata_create(const char* image_path, bool rw)
{
    blkdev_t* blk = blk_open(image_path, rw ? BLKDEV_RW : 0);
    if (blk == NULL) return NULL;
    struct ata_dev* ata = safe_new_obj(struct ata_dev);
    ata->drive[0].blk = blk;
    ata->drive[0].size = DIV_ROUND_UP(blk_getsize(blk), SECTOR_SIZE);
    // Slave drives aren't supported
    return ata;
}

PUBLIC bool ata_init_pio(rvvm_machine_t* machine, rvvm_addr_t data_base_addr, rvvm_addr_t ctl_base_addr, const char* image_path, bool rw)
{
    struct ata_dev* ata = ata_create(image_path, rw);
    if (ata == NULL) return false;

    rvvm_mmio_dev_t ata_data = {
        .addr = data_base_addr,
        .size = ((ATA_REG_STATUS + 1) << ATA_REG_SHIFT),
        .data = ata,
        .read = ata_data_mmio_read_handler,
        .write = ata_data_mmio_write_handler,
        .type = &ata_data_dev_type,
        .min_op_size = 1,
        .max_op_size = 2,
    };
    rvvm_attach_mmio(machine, &ata_data);

    rvvm_mmio_dev_t ata_ctl = {
        .addr = ctl_base_addr,
        .size = ((ATA_REG_DRVADDR + 1) << ATA_REG_SHIFT),
        .data = ata,
        .read = ata_ctl_mmio_read_handler,
        .write = ata_ctl_mmio_write_handler,
        .type = &ata_ctl_dev_type,
        .min_op_size = 1,
        .max_op_size = 1,
    };
    rvvm_attach_mmio(machine, &ata_ctl);
#ifdef USE_FDT
    uint32_t reg_cells[8];
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
    fdt_node_add_child(rvvm_get_fdt_soc(machine), ata_node);
#endif
    return true;
}

#ifdef USE_PCI

static rvvm_mmio_type_t ata_bmdma_dev_type = {
    .name = "ata_bmdma",
    .remove = ata_remove_dummy,
};

static void ata_process_prdt(struct ata_dev* ata)
{
    bool is_read = bit_check(ata->dma_info.cmd, 3);
    size_t to_process = ata->drive[ata->curdrive].sectcount * SECTOR_SIZE;
    blkdev_t* blk = ata->drive[ata->curdrive].blk;
    size_t processed = 0;
    uint8_t* buf;
    uint32_t prd_physaddr, prd_sectcount, buf_size;
    // According to spec, maximum amount of PRDT entries is 65536
    // This should prevent malicious guests from hanging up the thread
    for (size_t i=0; i<65536; ++i) {
        // Read PRD
        buf = pci_get_dma_ptr(ata->pci_dev, ata->dma_info.prdt_addr, 8);
        if (buf == NULL)  goto err;
        prd_physaddr = read_uint32_le_m(buf);
        prd_sectcount = read_uint32_le_m(buf + 4);

        buf_size = prd_sectcount & 0xffff;
        // Value 0 means size of 64K
        if (buf_size == 0) {
            buf_size = 64 * 1024;
        }

        buf = pci_get_dma_ptr(ata->pci_dev, prd_physaddr, buf_size);
        if (buf == NULL)  goto err;

        // Read/write data to/from RAM
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

        // If bit 31 is set, this is the last PRD
        if (bit_check(prd_sectcount, 31)) {
            if (processed != to_process) {
                goto err;
            }

            break;
        }

        // All good, advance the pointer
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

static void* ata_worker(void* data)
{
    struct ata_dev* ata = (struct ata_dev*)data;
    spin_lock(&ata->dma_info.lock);
    ata_process_prdt(ata);
    spin_unlock(&ata->dma_info.lock);
    return NULL;
}

static bool ata_bmdma_mmio_read_handler(rvvm_mmio_dev_t* device, void* data, size_t offset, uint8_t size)
{
    struct ata_dev* ata = (struct ata_dev*)device->data;
    switch (offset) {
        case ATA_BMDMA_CMD:
            if (size != 1) return false;
            write_uint8(data, ata->dma_info.cmd);
            break;
        case ATA_BMDMA_STATUS:
            if (size != 1) return false;
            write_uint8(data, ata->dma_info.status
                           | (ata->drive[0].blk != NULL) << 5
                           | (ata->drive[1].blk != NULL) << 6);
            break;
        case ATA_BMDMA_PRDT:
            if (size != 4) return false;
            write_uint32_le(data, ata->dma_info.prdt_addr);
            break;
        default:
            // Secondary controller not supported now
            return false;
    }

    return true;
}

static bool ata_bmdma_mmio_write_handler(rvvm_mmio_dev_t* device, void* data, size_t offset, uint8_t size)
{
    struct ata_dev* ata = (struct ata_dev*)device->data;
    bool process_prdt;
    switch (offset) {
        case ATA_BMDMA_CMD:
            if (size != 1) return false;
            spin_lock(&ata->dma_info.lock);
            process_prdt = !(ata->dma_info.cmd & 1) && (read_uint8(data) & 1);
            ata->dma_info.cmd = read_uint8(data);
            spin_unlock(&ata->dma_info.lock);
            if (process_prdt) {
                thread_create_task(ata_worker, ata);
            }
            break;
        case ATA_BMDMA_STATUS:
            if (size != 1) return false;
            spin_lock(&ata->dma_info.lock);
            ata->dma_info.status &= ~(read_uint8(data) & 6);
            if (!bit_check(ata->dma_info.status, 2)) {
                ata_clear_interrupt(ata);
            }
            spin_unlock(&ata->dma_info.lock);
            break;
        case ATA_BMDMA_PRDT:
            if (size != 4) return false;
            spin_lock(&ata->dma_info.lock);
            ata->dma_info.prdt_addr = read_uint32_le(data);
            spin_unlock(&ata->dma_info.lock);
            break;
        default:
            // Secondary controller not supported
            return false;
    }

    return true;
}

static bool ata_ctl_read_primary(rvvm_mmio_dev_t* device, void* memory_data, size_t offset, uint8_t size)
{
    offset -= 2;
    return ata_ctl_mmio_read_handler(device, memory_data, offset, size);
}

static bool ata_ctl_write_primary(rvvm_mmio_dev_t* device, void* memory_data, size_t offset, uint8_t size)
{
    offset -= 2;
    return ata_ctl_mmio_write_handler(device, memory_data, offset, size);
}

/* No support for secondary BAR
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
*/

PUBLIC pci_dev_t* ata_init_pci(pci_bus_t* pci_bus, const char* image_path, bool rw)
{
    struct ata_dev* ata = ata_create(image_path, rw);
    if (ata == NULL) return NULL;

    pci_dev_desc_t ata_desc = {
        .func[0] = {
            .vendor_id = 0x8086,  // Intel (ata-generic kernel driver refuses to load with other vendors)
            .device_id = 0x8c88,  // 9 Series Series Chipset Family SATA Controller [IDE Mode]
            .class_code = 0x0101, // Mass Storage, IDE
            .prog_if = 0x85,      // PCI native mode-only controller, supports bus mastering
            .irq_pin = PCI_IRQ_PIN_INTA,
            .bar[0] = {
                .size = 4096,
                .min_op_size = 1,
                .max_op_size = 2,
                .read = ata_data_mmio_read_handler,
                .write = ata_data_mmio_write_handler,
                .data = ata,
                .type = &ata_data_dev_type,
            },
            .bar[1] = {
                .size = 4096,
                .min_op_size = 1,
                .max_op_size = 1,
                .read = ata_ctl_read_primary,
                .write = ata_ctl_write_primary,
                .data = ata,
                .type = &ata_ctl_dev_type,
            },
/* No support for secondary BAR
            .bar[2] = {
                .size = 4096,
                .min_op_size = 1,
                .max_op_size = 2,
                .read = ata_data_read_secondary,
                .write = ata_data_write_secondary,
            },
            .bar[3] = {
                .size = 4096,
                .min_op_size = 1,
                .max_op_size = 1,
                .read = ata_ctl_read_secondary,
                .write = ata_ctl_write_secondary,
            },
*/
            .bar[4] = {
                .size = 16,
                .min_op_size = 1,
                .max_op_size = 4,
                .read = ata_bmdma_mmio_read_handler,
                .write = ata_bmdma_mmio_write_handler,
                .data = ata,
                .type = &ata_bmdma_dev_type,
            }
        }
    };

    pci_dev_t* pci_dev = pci_bus_add_device(pci_bus, &ata_desc);
    if (pci_dev) ata->pci_dev = pci_dev;
    return pci_dev;
}

#else
PUBLIC pci_dev_t* ata_init_pci(pci_bus_t* pci_bus, const char* image_path, bool rw) { UNUSED(pci_bus); UNUSED(image_path); UNUSED(rw); return NULL; }
#endif

PUBLIC bool ata_init_auto(rvvm_machine_t* machine, const char* image_path, bool rw)
{
#ifdef USE_PCI
    pci_bus_t* pci_bus = rvvm_get_pci_bus(machine);
    return pci_bus && ata_init_pci(pci_bus, image_path, rw);
#else
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, ATA_DATA_DEFAULT_MMIO, 0x2000);
    return ata_init_pio(machine, addr, addr + 0x1000, image_path, rw);
#endif
}
