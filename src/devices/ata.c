/*
ata.c - IDE/ATA disk controller
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>
                    LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

/*
 * TODO: Threaded task workers
 * TODO: Level-triggered interrupts, right now Linux complains "irq 14: nobody cared"
 * TODO: Snapshots
 * TODO: Primary/Secondary channels?
 * TODO: LBA48?
 */

#include <rvvm/rvvm_blk.h>
#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_pci.h>
#include <rvvm/rvvm_region.h>
#include <rvvm/rvvm_snapshot.h>

#include <util/bit_ops.h>
#include <util/locking.h>
#include <util/mem_ops.h>
#include <util/threading.h>
#include <util/utils.h>

PUSH_OPTIMIZATION_SIZE

/*
 * Useful resources:
 * - https://wiki.osdev.org/ATA_PIO_Mode
 * - https://wiki.osdev.org/ATA/ATAPI_using_DMA
 * - https://wiki.osdev.org/ATA_Command_Matrix
 * - https://www.manualslib.com/manual/574153/Hitachi-Hts548040m9at00.html?page=164#manual
 */

/*
 * ATA Data registers
 */
#define ATA_REG_DATA             0x00 // PIO Data Register
#define ATA_REG_ERROR            0x01 // Error Register (RO)
#define ATA_REG_SECTORS          0x02 // Number of sectors to read/write (0 means 256)
#define ATA_REG_LBAL             0x03 // LBAlow
#define ATA_REG_LBAM             0x04 // LBAmid
#define ATA_REG_LBAH             0x05 // LBAhigh
#define ATA_REG_DEVICE           0x06 // Device control register
#define ATA_REG_STATUS           0x07 // Status (RO)
#define ATA_REG_COMMAND          0x07 // Command (WO)

/*
 * Error register flags
 */
#define ATA_ERROR_AMNF           0x01 // Address mark not found (Actually used to detect drive after soft reset)
#define ATA_ERROR_ABRT           0x04 // Aborted command
#define ATA_ERROR_UNC            0x40 // Uncorrectable data error

/*
 * Slave drive selection bit (Slave drives not supported!)
 */
#define ATA_DEVICE_SLAVE         0x10

/*
 * Status register flags
 */
#define ATA_STATUS_ERR           0x01 // Error occured
#define ATA_STATUS_DRQ           0x08 // Data ready
#define ATA_STATUS_SRV           0x10 // Overlapped Service Request
#define ATA_STATUS_RDY           0x40 // Device ready

/*
 * Commands
 */
#define ATA_CMD_NOP              0x00 // No operation
#define ATA_CMD_READ_PIO         0x20 // Read Sectors (PIO)
#define ATA_CMD_WRITE_PIO        0x30 // Write Sectors (PIO)
#define ATA_CMD_DEV_DIAGNOSTIC   0x90 // Execute device diagnostic
#define ATA_CMD_INIT_DEV_PARAMS  0x91 // Set CHS addressing options, CHS not supported!
#define ATA_CMD_READ_DMA         0xC8 // Read DMA
#define ATA_CMD_WRITE_DMA        0xCA // Write DMA
#define ATA_CMD_CHECK_POWER_MODE 0xE4 // Check power mode
#define ATA_CMD_IDENTIFY         0xEC // Identify drive

/*
 * Control bits
 */
#define ATA_CONTROL_NIEN         0x01 // Interrupt disabled
#define ATA_CONTROL_RST          0x02 // Soft reset

/*
 * ATA BMDMA registers
 */
#define ATA_BMDMA_COMMAND        0x00
#define ATA_BMDMA_STATUS         0x02
#define ATA_BMDMA_PRDT           0x04

/*
 * ATA BMDMA command flags
 */
#define ATA_BMDMA_COMMAND_DMA    0x01 // Enable DMA mode
#define ATA_BMDMA_COMMAND_READ   0x08 // Perform DMA read

/*
 * ATA BMDMA status flags
 */
#define ATA_BMDMA_STATUS_IRQ     0x04 // DMA mode exited
#define ATA_BMDMA_STATUS_ERR     0x02 // DMA operation failed
#define ATA_BMDMA_STATUS_DMA     0x01 // DMA mode enabled

/*
 * Implementation constants
 */
#define ATA_SECTOR_SHIFT         9
#define ATA_SECTOR_SIZE          512

typedef struct {
    // Own PCI function handle
    rvvm_pci_func_t* func;

    // Block device handle
    rvvm_blk_dev_t* blk;

    // DMA lock
    rvvm_lock_t dma_lock;

    // Current LBA
    uint32_t lba;

    // ATA BMDMA
    uint32_t prdt_addr;
    uint32_t bmdma_command;
    uint32_t bmdma_status;

    // ATA generic
    uint32_t bytes;
    uint32_t sectors;
    uint32_t device;
    uint32_t error;
    uint32_t status;
    uint32_t control;

    uint8_t buf[ATA_SECTOR_SIZE];
    char    serial[16];
} ata_dev_t;

/*
 * ATA PIO handling
 */

static inline bool ata_drive_valid(ata_dev_t* ata)
{
    return !(atomic_load_uint32_relax(&ata->device) & ATA_DEVICE_SLAVE) && ata->blk;
}

static void ata_complete(ata_dev_t* ata, uint32_t status)
{
    atomic_store_uint32_relax(&ata->status, status);
    if (!(atomic_load_uint32_relax(&ata->control) & ATA_CONTROL_NIEN)) {
        rvvm_pci_send_irq(ata->func, 0);
        // rvvm_pci_raise_irq(ata->func, 0);
    }
}

static void ata_error(ata_dev_t* ata, uint32_t error)
{
    // No interrupt on error
    atomic_store_uint32_relax(&ata->error, atomic_load_uint32_relax(&ata->error) | error);
    atomic_store_uint32_relax(&ata->status, ATA_STATUS_RDY | ATA_STATUS_ERR);
}

static inline uint64_t ata_get_seek(ata_dev_t* ata)
{
    return ((uint64_t)atomic_load_uint32_relax(&ata->lba)) << ATA_SECTOR_SHIFT;
}

static void ata_copy_id_string(uint8_t* buf, const char* str, size_t size)
{
    // Reverse each byte pair since they are little-endian words, pad with spaces
    size_t len = rvvm_strnlen(str, size);
    memset(buf, ' ', size);
    for (size_t i = 0; i < len; ++i) {
        buf[i ^ 1ULL] = str[i];
    }
}

static void ata_cmd_identify(ata_dev_t* ata)
{
    uint8_t* id_buf = ata->buf;
    memset(id_buf, 0, ATA_SECTOR_SIZE);

    write_uint16_le(id_buf, 0x40);         // Non-removable, ATA device
    write_uint16_le(id_buf + 2, 0xFFFF);   // Logical cylinders
    write_uint16_le(id_buf + 6, 0x10);     // Sectors per track
    write_uint16_le(id_buf + 12, 0x3F);    // Logical heads
    write_uint16_le(id_buf + 44, 0x4);     // Number of bytes available in READ/WRITE LONG cmds
    write_uint16_le(id_buf + 98, 0x300);   // Capabilities - LBA supported, DMA supported
    write_uint16_le(id_buf + 100, 0x4000); // Capabilities - bit 14 needs to be set as required by ATA/ATAPI-5 spec
    write_uint16_le(id_buf + 102, 0x400);  // PIO data transfer cycle timing mode
    write_uint16_le(id_buf + 106, 0x7);    // Fields 54-58, 64-70 and 88 are valid
    write_uint16_le(id_buf + 108, 0xFFFF); // Logical cylinders
    write_uint16_le(id_buf + 110, 0x10);   // Logical heads
    write_uint16_le(id_buf + 112, 0x3F);   // Sectors per track

    // Capacity in sectors
    write_uint16_le(id_buf + 114, rvvm_blk_get_size(ata->blk) >> 9);
    write_uint16_le(id_buf + 116, rvvm_blk_get_size(ata->blk) >> 25);
    write_uint16_le(id_buf + 120, rvvm_blk_get_size(ata->blk) >> 9);
    write_uint16_le(id_buf + 122, rvvm_blk_get_size(ata->blk) >> 25);

    write_uint16_le(id_buf + 128, 0x3);    // Advanced PIO modes supported
    write_uint16_le(id_buf + 134, 0x1);    // PIO transfer cycle time without flow control
    write_uint16_le(id_buf + 136, 0x1);    // PIO transfer cycle time with IORDY flow control
    write_uint16_le(id_buf + 160, 0x100);  // ATA major version
    write_uint16_le(id_buf + 176, 0x80FF); // UDMA mode 7 active, All UDMA modes supported

    // Serial Number
    ata_copy_id_string(id_buf + 20, ata->serial, 20);
    // Firmware Revision
    ata_copy_id_string(id_buf + 46, "R2818", 8);
    // Model Number
    ata_copy_id_string(id_buf + 54, "Parallel ATA IDE HDD", 40);

    // Prepare data out without causing a sector read
    atomic_store_uint32_relax(&ata->bytes, ATA_SECTOR_SIZE);
    atomic_store_uint32_relax(&ata->sectors, 1);
    ata_complete(ata, ATA_STATUS_RDY | ATA_STATUS_DRQ);
}

static void ata_device_diagnostic(ata_dev_t* ata)
{
    // ATA device diagnostic
    atomic_store_uint32_relax(&ata->lba, 1);
    atomic_store_uint32_relax(&ata->sectors, 1);
    atomic_store_uint32_relax(&ata->error, 1);
    ata_complete(ata, ATA_STATUS_RDY);
}

static void ata_handle_cmd(ata_dev_t* ata, uint8_t cmd)
{
    atomic_store_uint32_relax(&ata->error, 0);
    switch (cmd) {
        case ATA_CMD_READ_PIO:
            if (rvvm_blk_seek_head(ata->blk, ata_get_seek(ata), RVVM_BLK_SEEK_SET) && //
                rvvm_blk_read_head(ata->blk, ata->buf, ATA_SECTOR_SIZE)) {
                atomic_store_uint32_relax(&ata->bytes, ATA_SECTOR_SIZE);
                ata_complete(ata, ATA_STATUS_RDY | ATA_STATUS_DRQ);
            } else {
                ata_error(ata, ATA_ERROR_UNC);
            }
            return;
        case ATA_CMD_WRITE_PIO:
            if (rvvm_blk_seek_head(ata->blk, ata_get_seek(ata), RVVM_BLK_SEEK_SET)) {
                atomic_store_uint32_relax(&ata->bytes, ATA_SECTOR_SIZE);
                ata_complete(ata, ATA_STATUS_RDY | ATA_STATUS_DRQ);
            } else {
                ata_error(ata, ATA_ERROR_UNC);
            }
            return;
        case ATA_CMD_READ_DMA:
        case ATA_CMD_WRITE_DMA:
            if (!rvvm_blk_seek_head(ata->blk, ata_get_seek(ata), RVVM_BLK_SEEK_SET)) {
                ata_error(ata, ATA_ERROR_UNC);
            }
            return;
        case ATA_CMD_CHECK_POWER_MODE:
            // Always active
            atomic_store_uint32_relax(&ata->sectors, 0xFF);
            ata_complete(ata, ATA_STATUS_RDY);
            return;
        case ATA_CMD_IDENTIFY:
            // Report drive information
            ata_cmd_identify(ata);
            return;
        case ATA_CMD_DEV_DIAGNOSTIC:
            ata_device_diagnostic(ata);
            return;
        default:
            rvvm_debug("Unknown ATA command 0x%02x", cmd);
            ata_complete(ata, ATA_STATUS_RDY);
            return;
    }
}

static void ata_pio_transfer(ata_dev_t* ata, bool is_write)
{
    uint32_t sectors = atomic_load_uint32_relax(&ata->sectors);
    if (sectors) {
        atomic_store_uint32_relax(&ata->sectors, sectors - 1);
        // Write sector
        if (is_write && !rvvm_blk_write_head(ata->blk, ata->buf, ATA_SECTOR_SIZE)) {
            // IO failed
            ata_error(ata, ATA_ERROR_UNC);
            return;
        }
        if (sectors > 1) {
            // Advance
            if (!is_write && !rvvm_blk_read_head(ata->blk, ata->buf, ATA_SECTOR_SIZE)) {
                // IO failed
                ata_error(ata, ATA_ERROR_UNC);
                return;
            }
            atomic_store_uint32_relax(&ata->bytes, ATA_SECTOR_SIZE);
            ata_complete(ata, ATA_STATUS_RDY | ATA_STATUS_DRQ);
        } else {
            // Completed
            ata_complete(ata, ATA_STATUS_RDY);
        }
    }
}

static void ata_data_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    ata_dev_t* ata = rvvm_region_data(dev);

    switch (off) {
        case ATA_REG_DATA:
            if (ata_drive_valid(ata)) {
                uint32_t bytes = atomic_load_uint32_relax(&ata->bytes);
                if (bytes >= size) {
                    memcpy(data, ata->buf + ATA_SECTOR_SIZE - bytes, size);
                    bytes -= size;
                    atomic_store_uint32_relax(&ata->bytes, bytes);
                    if (!bytes) {
                        ata_pio_transfer(ata, false);
                    }
                }
            }
            break;
        case ATA_REG_ERROR:
            if (ata_drive_valid(ata)) {
                write_uint8(data, atomic_load_uint32_relax(&ata->error));
            }
            break;
        case ATA_REG_SECTORS:
            write_uint8(data, atomic_load_uint32_relax(&ata->sectors));
            break;
        case ATA_REG_LBAL:
            write_uint8(data, atomic_load_uint32_relax(&ata->lba));
            break;
        case ATA_REG_LBAM:
            write_uint8(data, atomic_load_uint32_relax(&ata->lba) >> 8);
            break;
        case ATA_REG_LBAH:
            write_uint8(data, atomic_load_uint32_relax(&ata->lba) >> 16);
            break;
        case ATA_REG_DEVICE: {
            uint32_t val = (atomic_load_uint32_relax(&ata->lba) >> 24) & 0x0F;
            write_uint8(data, val | atomic_load_uint32_relax(&ata->device) | 0xE0);
            break;
        }
        case ATA_REG_STATUS:
            if (ata_drive_valid(ata)) {
                write_uint8(data, atomic_load_uint32_relax(&ata->status));
            }
            // rvvm_pci_lower_irq(ata->func, 0);
            break;
    }
}

static void ata_data_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    ata_dev_t* ata = rvvm_region_data(dev);

    switch (off) {
        case ATA_REG_DATA:
            if (ata_drive_valid(ata)) {
                uint32_t bytes = atomic_load_uint32_relax(&ata->bytes);
                if (bytes >= size) {
                    memcpy(ata->buf + ATA_SECTOR_SIZE - bytes, data, size);
                    bytes -= size;
                    atomic_store_uint32_relax(&ata->bytes, bytes);
                    if (!bytes) {
                        ata_pio_transfer(ata, true);
                    }
                }
            }
            break;
        case ATA_REG_SECTORS: {
            uint32_t val = read_uint8(data);
            if (!val) {
                // Byte 0x00 means 256 sectors
                val = 256;
            }
            atomic_store_uint32_relax(&ata->sectors, val);
            break;
        }
        case ATA_REG_LBAL: {
            uint32_t lba = atomic_load_uint32_relax(&ata->lba);
            atomic_store_uint32_relax(&ata->lba, (lba & ~0xFFUL) | read_uint8(data));
            break;
        }
        case ATA_REG_LBAM: {
            uint32_t lba = atomic_load_uint32_relax(&ata->lba);
            atomic_store_uint32_relax(&ata->lba, (lba & ~0xFF00UL) | (((uint32_t)read_uint8(data)) << 8));
            break;
        }
        case ATA_REG_LBAH: {
            uint32_t lba = atomic_load_uint32_relax(&ata->lba);
            atomic_store_uint32_relax(&ata->lba, (lba & ~0xFF0000UL) | (((uint32_t)read_uint8(data)) << 16));
            break;
        }
        case ATA_REG_DEVICE: {
            uint32_t lba = atomic_load_uint32_relax(&ata->lba);
            atomic_store_uint32_relax(&ata->device, read_uint8(data) & ATA_DEVICE_SLAVE);
            atomic_store_uint32_relax(&ata->lba, (lba & ~0xF000000UL) | (((uint32_t)read_uint8(data) & 0xF) << 24));
            break;
        }
        case ATA_REG_COMMAND:
            if (ata_drive_valid(ata)) {
                ata_handle_cmd(ata, read_uint8(data));
            }
            break;
    }
}

static void ata_ctl_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    ata_dev_t* ata = rvvm_region_data(dev);
    UNUSED(size && off);

    // Register index is ignored due to PIO/PCI interactions
    if (ata_drive_valid(ata)) {
        write_uint8(data, atomic_load_uint32_relax(&ata->status));
    }
}

static void ata_ctl_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    ata_dev_t* ata = rvvm_region_data(dev);
    uint32_t   val = read_uint8(data);
    UNUSED(size && off);

    // Register index is ignored due to PIO/PCI interactions
    if (ata_drive_valid(ata)) {
        atomic_store_uint32_relax(&ata->control, val & ATA_CONTROL_NIEN);
    }
    if (val & ATA_CONTROL_RST) {
        // Soft reset
        atomic_store_uint32_relax(&ata->bytes, 0);
        atomic_store_uint32_relax(&ata->lba, 1);
        atomic_store_uint32_relax(&ata->sectors, 1);
        atomic_store_uint32_relax(&ata->device, 0);
        atomic_store_uint32_relax(&ata->error, ATA_ERROR_AMNF);
        atomic_store_uint32_relax(&ata->status, ATA_STATUS_RDY);
    }
}

static void ata_data_cleanup(rvvm_reg_dev_t* dev)
{
    ata_dev_t* ata = rvvm_region_data(dev);
    if (ata) {
        rvvm_scoped_lock (&ata->dma_lock) {
            rvvm_blk_close(ata->blk);
        }
        free(ata);
    }
}

/*
 * ATA BMDMA
 */

static bool ata_process_io(ata_dev_t* ata, uint32_t addr, size_t size)
{
    void* buffer = rvvm_pci_get_dma(ata->func, addr, size);
    if (buffer) {
        if (atomic_load_uint32_relax(&ata->bmdma_command) & ATA_BMDMA_COMMAND_READ) {
            if (rvvm_blk_read_head(ata->blk, buffer, size) != size) {
                // IO error
                buffer = NULL;
            }
        } else {
            if (rvvm_blk_write_head(ata->blk, buffer, size) != size) {
                // IO error
                buffer = NULL;
            }
        }
        rvvm_pci_end_dma(ata->func, buffer);
    }
    return !!buffer;
}

static void ata_process_prdt(ata_dev_t* ata)
{
    rvvm_addr_t prdt_addr = atomic_load_uint32_relax(&ata->prdt_addr);
    rvvm_addr_t remaining = atomic_load_uint32_relax(&ata->sectors) << ATA_SECTOR_SHIFT;

    // According to spec, maximum amount of PRDT entries is 64k
    // This should prevent malicious guests from hanging up the thread
    for (size_t i = 0; remaining && i < 0x10000; ++i) {
        // Read PRD
        uint8_t* prdt = rvvm_pci_get_dma(ata->func, prdt_addr, 8);
        if (!prdt) {
            // DMA error
            break;
        }
        uint32_t addr = read_uint32_le_m(prdt);
        uint32_t prds = read_uint32_le_m(prdt + 4);
        rvvm_pci_end_dma(ata->func, prdt);

        uint32_t size = (uint16_t)prds;
        if (size == 0) {
            // Value 0 means 64K
            size = 0x10000;
        }
        if (size > remaining) {
            // Clamp buffer to size
            size = remaining;
        }

        // Read/write data to/from RAM
        if (!ata_process_io(ata, addr, size)) {
            // IO error
            break;
        }

        remaining -= size;

        if (prds >> 31) {
            // This is the last PRD
            break;
        }

        // All good, advance the pointer
        prdt_addr += 8;
    }

    atomic_store_uint32(&ata->bmdma_command, 0);

    if (!remaining) {
        // Everything OK
        atomic_store_uint32(&ata->bmdma_status, ATA_BMDMA_STATUS_IRQ);
    } else {
        // Error
        atomic_store_uint32(&ata->bmdma_status, ATA_BMDMA_STATUS_IRQ | ATA_BMDMA_STATUS_ERR);
    }

    ata_complete(ata, ATA_STATUS_RDY);
}

static void* ata_prdt_io_worker(void* arg)
{
    ata_dev_t* ata = arg;
    rvvm_scoped_lock (&ata->dma_lock) {
        ata_process_prdt(ata);
    }
    return NULL;
}

static void ata_bmdma_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    ata_dev_t* ata = rvvm_region_data(dev);

    switch (off) {
        case ATA_BMDMA_COMMAND:
            write_uint8(data, atomic_load_uint32_relax(&ata->bmdma_command));
            return;
        case ATA_BMDMA_STATUS:
            write_uint8(data, atomic_load_uint32_relax(&ata->bmdma_status) | ((ata->blk != NULL) << 5));
            return;
        case ATA_BMDMA_PRDT: {
            uint8_t buffer[4] = {0};
            write_uint32_le(buffer, atomic_load_uint32_relax(&ata->prdt_addr));
            memcpy(data, buffer, size);
            return;
        }
    }
}

static void ata_bmdma_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    ata_dev_t* ata = rvvm_region_data(dev);

    switch (off) {
        case ATA_BMDMA_COMMAND: {
            uint8_t cmd = read_uint8(data);
            atomic_and_uint32(&ata->bmdma_command, ATA_BMDMA_COMMAND_DMA);
            uint8_t prev_cmd = atomic_or_uint32(&ata->bmdma_command, cmd);
            if (!!(cmd & ATA_BMDMA_COMMAND_DMA) && !(prev_cmd & ATA_BMDMA_COMMAND_DMA)) {
                thread_create_task(ata_prdt_io_worker, ata);
            }
            break;
        }
        case ATA_BMDMA_STATUS:
            atomic_and_uint32(&ata->bmdma_status, ~(read_uint8(data) & (ATA_BMDMA_STATUS_DMA | ATA_BMDMA_STATUS_IRQ)));
            break;
        case ATA_BMDMA_PRDT: {
            uint8_t buffer[4] = {0};
            memcpy(buffer, data, size);
            atomic_store_uint32_relax(&ata->prdt_addr, read_uint32_le(buffer));
            break;
        }
    }
}

static const rvvm_reg_type_t ata_data_type = {
    .name     = "ata-data",
    .read     = ata_data_read,
    .write    = ata_data_write,
    .cleanup  = ata_data_cleanup,
    .min_size = 1,
    .max_size = 4,
};

static const rvvm_reg_type_t ata_ctl_type = {
    .name     = "ata-ctl",
    .read     = ata_ctl_read,
    .write    = ata_ctl_write,
    .min_size = 1,
    .max_size = 4,
};

static const rvvm_reg_type_t ata_bmdma_type = {
    .name     = "ata-bmdma",
    .read     = ata_bmdma_read,
    .write    = ata_bmdma_write,
    .min_size = 1,
    .max_size = 4,
};

static ata_dev_t* ata_create(rvvm_blk_dev_t* blk)
{
    ata_dev_t* ata = safe_new_obj(ata_dev_t);
    rvvm_randomserial(ata->serial, 12);
    ata->blk = blk;
    return ata;
}

RVVM_PUBLIC rvvm_pci_func_t* rvvm_ata_init(rvvm_machine_t* machine, /**/
                                           rvvm_blk_dev_t* blk,     /**/
                                           rvvm_pci_addr_t addr)
{
    ata_dev_t* ata_pri = ata_create(blk);
    ata_dev_t* ata_sec = ata_create(NULL);

    rvvm_reg_desc_t ata_pri_data = {
        .addr = 0x01F0,
        .size = 0x08,
        .data = ata_pri,
        .type = &ata_data_type,
        .attr = RVVM_REG_ATTR_PIO,
    };
    rvvm_reg_desc_t ata_pri_ctl = {
        .addr = 0x03F6,
        .size = 0x08,
        .data = ata_pri,
        .type = &ata_ctl_type,
        .attr = RVVM_REG_ATTR_PIO,
    };
    rvvm_reg_desc_t ata_sec_data = {
        .addr = 0x0170,
        .size = 0x08,
        .data = ata_sec,
        .type = &ata_data_type,
        .attr = RVVM_REG_ATTR_PIO,
    };
    rvvm_reg_desc_t ata_sec_ctl = {
        .addr = 0x0376,
        .size = 0x08,
        .data = ata_sec,
        .type = &ata_ctl_type,
        .attr = RVVM_REG_ATTR_PIO,
    };
    rvvm_reg_desc_t ata_bmdma = {
        .size = 0x10,
        .data = ata_pri,
        .type = &ata_bmdma_type,
        .attr = RVVM_REG_ATTR_PIO,
    };
    rvvm_pci_func_desc_t ata_desc = {
        .vendor_id  = 0x1179, // Toshiba
        .device_id  = 0x0102, // Extended IDE Controller
        .class_code = 0x0101, // Mass Storage, IDE
        .prog_iface = 0x85,   // PCI native mode-only controller, supports bus mastering
        .irq_pin    = RVVM_PCI_PIN_INTA,

        .bar[0] = &ata_pri_data,
        .bar[1] = &ata_pri_ctl,
        .bar[2] = &ata_sec_data,
        .bar[3] = &ata_sec_ctl,
        .bar[4] = &ata_bmdma,
    };

    rvvm_pci_func_t* func = rvvm_pci_func_init(machine, &ata_desc, addr);
    if (func) {
        // Successfully plugged in
        ata_pri->func = func;
        ata_sec->func = func;
    }
    return func;
}

POP_OPTIMIZATION_SIZE
