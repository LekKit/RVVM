/*
nvme.c - Non-Volatile Memory Express
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

#include "nvme.h"
#include "mem_ops.h"
#include "bit_ops.h"
#include "utils.h"
#include "spinlock.h"
#include "atomics.h"
#include "threading.h"
#include "blk_io.h"

#include <string.h>

// Controller Registers
#define NVME_CAP1  0x0   // Controller Capabilities
#define NVME_CAP2  0x4
#define NVME_VS    0x8   // Version
#define NVME_INTMS 0xC   // Interrupt Mask Set
#define NVME_INTMC 0x10  // Interrupt Mask Clear
#define NVME_CC    0x14  // Controller Configuration
#define NVME_CSTS  0x1C  // Controller Status
#define NVME_AQA   0x24  // Admin Queue Attributes
#define NVME_ASQ1  0x28  // Admin Submission Queue Base Address
#define NVME_ASQ2  0x2C
#define NVME_ACQ1  0x30  // Admin Completion Queue Base Address
#define NVME_ACQ2  0x34

// Queue IDs
#define ADMIN_SUBQ 0x0   // Admin Submission Queue
#define ADMIN_COMQ 0x1   // Admin Completion Queue

// Admin Command Set
#define A_RMIO_SUB 0x0   // Delete IO Submission Queue
#define A_MKIO_SUB 0x1   // Create IO Submission Queue
#define A_RMIO_COM 0x4   // Delete IO Completion Queue
#define A_MKIO_COM 0x5   // Create IO Completion Queue
#define A_IDENTIFY 0x6   // Identify
#define A_ABORTCMD 0x8   // Abort Command
#define A_SET_FEAT 0x9   // Set Features
#define A_GET_FEAT 0xA   // Get Features

// Admin Command Fields
#define IDENT_NS   0x0   // Identify Namespace
#define IDENT_CTRL 0x1   // Identify Controller
#define IDENT_NSLS 0x2   // Identify Namespace List
#define IDENT_NIDS 0x3   // Identify Namespace Descriptors

// NVM Command Set
#define NVM_FLUSH  0x0
#define NVM_WRITE  0x1
#define NVM_READ   0x2
#define NVM_WRITEZ 0x8   // Write Zeroes
#define NVM_DTSM   0x9   // Dataset Management

// Completion Queue Status Codes
#define SC_SUCCESS 0x0   // Successful Completion
#define SC_BAD_OP  0x1   // Invalid Command Opcode
#define SC_BAD_FIL 0x2   // Invalid Field in Command
#define SC_DT_ERR  0x4   // Data Transfer Error
#define SC_ABORT   0x7   // Command Abort Requested
#define SC_SQ_DEL  0x8   // Command Aborted due to SQ Deletion
#define SC_BAD_NS  0xB   // Invalid Namespace or Format
#define SC_BAD_QI 0x101  // Invalid Queue ID
#define SC_BAD_QS 0x102  // Invalid Queue Size

// Configurable constants
#define NVME_MQES 0xFFFF // Maximum Queue Entries Supported: 65536
#define NVME_CQR   0x1   // Contiguous Queues Required
#define NVME_TO    0xA   // Timeout: 5s
#define NVME_DSTRD 0x0   // Doorbell Stride (0 means 2-bit shift)
#define NVME_CSS   0x1   // Command Sets Supported (NVM Command Set)
#define NVME_MPMAX 0x0   // Max page size: 4K
#define NVME_V   0x10400 // NVMe v1.4
#define NVME_IOQES 0x46  // IO Queue Entry Sizes (16b:64b)
#define NVME_LBAS  0x9   // LBA Block Size Shift (512b blocks)
#define NVME_MAXQ  0x12  // Max Queues: 18 (Admin + IO, Submission & Completion)

typedef struct {
    rvvm_addr_t addr;
    spinlock_t lock;
    uint16_t size;
    uint16_t head;
    uint16_t tail;
} nvme_queue_t;

typedef struct {
    blkdev_t* blk;
    pci_dev_t* pci_dev;
    uint32_t threads;
    uint32_t conf;
    uint32_t irq_mask;
    nvme_queue_t queues[NVME_MAXQ];
} nvme_dev_t;

static void nvme_shutdown(nvme_dev_t* nvme)
{
    while (atomic_load_uint32(&nvme->threads));
    memset(nvme->queues, 0, sizeof(nvme->queues));
}

static void nvme_remove(rvvm_mmio_dev_t* dev)
{
    nvme_dev_t* nvme = (nvme_dev_t*)dev->data;
    nvme_shutdown(nvme);
    free(nvme);
}

static rvvm_mmio_type_t nvme_type = {
    .name = "nvme",
    .remove = nvme_remove,
};

static void nvme_complete_cmd(nvme_dev_t* nvme, size_t queue_id, uint16_t cid, uint16_t sf)
{
    size_t sub_q = queue_id & ~1;
    size_t com_q = queue_id | 1;

    spin_lock(&nvme->queues[com_q].lock);
    uint8_t* ptr = pci_get_dma_ptr(nvme->pci_dev, nvme->queues[com_q].addr + (nvme->queues[com_q].tail * 16), 16);
    if (nvme->queues[com_q].tail++ >= nvme->queues[com_q].size) nvme->queues[com_q].tail = 0;
    if (ptr) {
        uint8_t phase = (~read_uint16_le(ptr + 14)) & 1;
        write_uint32_le(ptr, (sf & 0x100) ? sf & 0xFF : 0);  // Command Specific
        write_uint32_le(ptr + 4, 0);                         // Reserved
        write_uint16_le(ptr + 8, nvme->queues[sub_q].head);  // SQ Head Pointer
        write_uint16_le(ptr + 10, sub_q >> 1);               // SQ Identifier
        write_uint16_le(ptr + 12, cid);                      // Command Identifier
        write_uint16_le(ptr + 14, (sf & 0xFF) << 1 | phase); // Phase Bit, Status Field
    }
    spin_unlock(&nvme->queues[com_q].lock);
    if (!(nvme->irq_mask & 1)) pci_send_irq(nvme->pci_dev, 0);
}

static void nvme_admin_cmd(nvme_dev_t* nvme, uint8_t* cmd)
{
    uint8_t opcode = cmd[0];
    uint16_t cid = read_uint16_le(cmd + 2);
    rvvm_addr_t dptr = read_uint64_le(cmd + 24);
    uint8_t* ptr;
    //rvvm_info("NVME Admin Command %02x", opcode);
    switch (opcode) {
        case A_IDENTIFY:
            //rvvm_info("NVME Identify %02x", cmd[40]);
            ptr = pci_get_dma_ptr(nvme->pci_dev, dptr, 0x1000);
            if (ptr == NULL) {
                nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_DT_ERR);
                break;
            }
            switch (cmd[40]) {
                case IDENT_NS:
                    memset(ptr, 0, 0x1000);
                    uint64_t lbas = blk_getsize(nvme->blk) >> NVME_LBAS;
                    write_uint64_le(ptr,      lbas);
                    write_uint64_le(ptr + 8,  lbas);
                    write_uint64_le(ptr + 16, lbas);
                    ptr[130] = NVME_LBAS;
                    nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_SUCCESS);
                    break;
                case IDENT_CTRL:
                    memset(ptr, 0, 0x1000);
                    write_uint16_le(ptr,     0x144d);        // PCI Vendor ID
                    write_uint16_le(ptr + 2, 0x144d);
                    strcpy((char*)ptr + 4,  "DEADBEEF");     // Serial Number
                    strcpy((char*)ptr + 24, "Virtual NVMe"); // Model Number
                    strcpy((char*)ptr + 64, "RVVM");         // Firmware Revision
                    write_uint32_le(ptr + 80, NVME_V);       // Version
                    ptr[111] = 1;    // Controller Type: I/O Controller
                    ptr[512] = 0x66; // Submission Queue Max/Cur Entry Size
                    ptr[513] = 0x44; // Completion Queue Max/Cur Entry Size
                    ptr[516] = 1;    // Number of Namespaces
                    ptr[520] = 0xC;  // Supports Write Zeroes, Dataset Management
                    strcpy((char*)ptr + 768, "nqn.2022.04.lekkit:nvme.rvvm"); // NVMe Qualified Name
                    nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_SUCCESS);
                    break;
                case IDENT_NSLS:
                    write_uint32_le(ptr, 0x1); // Namespace #1
                    nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_SUCCESS);
                    break;
                case IDENT_NIDS:
                    ptr[0] = 3;  // Namespace UUID
                    ptr[1] = 16; // UUID length
                    nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_SUCCESS);
                    break;
                default:
                    nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_BAD_FIL);
                    break;
            }
            break;
        case A_MKIO_SUB:
        case A_MKIO_COM: {
            size_t qid = (read_uint16_le(cmd + 40) << 1) + (opcode == A_MKIO_COM);
            uint16_t q_size = read_uint16_le(cmd + 42);
            //rvvm_info("NVME Create Queue %08x, size %04x", (uint32_t)qid, q_size);
            if (qid <= ADMIN_COMQ || qid > NVME_MAXQ) {
                nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_BAD_QI);
            } else if (q_size == 0) {
                nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_BAD_QS);
            } else {
                spin_lock(&nvme->queues[qid].lock);
                nvme->queues[qid].addr = dptr;
                nvme->queues[qid].size = q_size;
                nvme->queues[qid].head = 0;
                nvme->queues[qid].tail = 0;
                spin_unlock(&nvme->queues[qid].lock);
                nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_SUCCESS);
            }
            break;
        }
        case A_RMIO_SUB:
        case A_RMIO_COM: {
            size_t qid = (read_uint16_le(cmd + 40) << 1) + (opcode == A_RMIO_COM);
            //rvvm_info("NVME Delete Queue %08x", (uint32_t)qid);
            if (qid <= ADMIN_COMQ || qid > NVME_MAXQ) {
                nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_BAD_QI);
            } else {
                spin_lock(&nvme->queues[qid].lock);
                nvme->queues[qid].addr = 0;
                nvme->queues[qid].size = 0;
                nvme->queues[qid].head = 0;
                nvme->queues[qid].tail = 0;
                spin_unlock(&nvme->queues[qid].lock);
                nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_SUCCESS);
            }
            break;
        }
        case A_SET_FEAT:
        case A_GET_FEAT:
        case A_ABORTCMD: // Ignored, all the commands could be already executing
            nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_SUCCESS);
            break;
        default:
            rvvm_info("NVME unknown admin cmd %02x (head %04x tail %04x size %04x)", opcode,
                      nvme->queues[ADMIN_SUBQ].head, nvme->queues[ADMIN_SUBQ].tail, nvme->queues[ADMIN_SUBQ].size);
            nvme_complete_cmd(nvme, ADMIN_COMQ, cid, SC_BAD_OP);
            break;
    }
}

static bool nvme_io_rw(nvme_dev_t* nvme, size_t queue_id, uint16_t cid, rvvm_addr_t addr, uint64_t pos, size_t len, uint8_t opcode)
{
    void* buffer = pci_get_dma_ptr(nvme->pci_dev, addr, len);
    size_t ret;
    if (buffer == NULL) {
        nvme_complete_cmd(nvme, queue_id, cid, SC_DT_ERR);
        return false;
    }
    if (opcode == NVM_WRITE) {
        ret = blk_write(nvme->blk, buffer, len, pos);
    } else {
        ret = blk_read(nvme->blk, buffer, len, pos);
    }
    if (ret != len) {
        nvme_complete_cmd(nvme, queue_id, cid, SC_DT_ERR);
        return false;
    }
    return true;
}

static void nvme_io_cmd(nvme_dev_t* nvme, size_t queue_id, uint8_t* cmd)
{
    uint8_t opcode = cmd[0];
    uint16_t cid = read_uint16_le(cmd + 2);
    rvvm_addr_t prp1 = read_uint64_le(cmd + 24);
    rvvm_addr_t prp2 = read_uint64_le(cmd + 32);
    uint64_t pos = read_uint64_le(cmd + 40) << NVME_LBAS;
    size_t len = (((size_t)read_uint16_le(cmd + 48)) + 1) << NVME_LBAS;
    size_t tmp;
    uint8_t* ptr = NULL;

    switch (opcode) {
        case NVM_READ:
        case NVM_WRITE:
            // Read/write first page in PRP1
            tmp = 0x1000 - (prp1 & 0xFFF);
            if (len < tmp) tmp = len;
            if (!nvme_io_rw(nvme, queue_id, cid, prp1, pos, tmp, opcode)) return;
            pos += tmp;
            len -= tmp;
            if (len) {
                if (len <= 0x1000) {
                    // PRP2 points to second page
                    if (!nvme_io_rw(nvme, queue_id, cid, prp2, pos, len, opcode)) return;
                } else {
                    // PRP2 points to PRP list
                    tmp = 0;
                    prp1 = prp2;
                    prp2 &= 0xFF8;
                    while (len) {
                        if (tmp == 0 || prp2 == 0xFF8) {
                            if (tmp) prp2 = 0;
                            ptr = pci_get_dma_ptr(nvme->pci_dev, prp1 & ~0xFFFULL, 0x1000);
                            if (ptr == NULL) {
                                nvme_complete_cmd(nvme, queue_id, cid, SC_DT_ERR);
                                return;
                            }
                        }
                        prp1 = read_uint64_le(ptr + prp2);
                        tmp = len > 0x1000 ? 0x1000 : len;
                        if (!nvme_io_rw(nvme, queue_id, cid, prp1, pos, tmp, opcode)) return;
                        len -= tmp;
                        pos += tmp;
                        prp2 += 8;
                    }
                }
            }
            nvme_complete_cmd(nvme, queue_id, cid, SC_SUCCESS);
            break;
        case NVM_FLUSH:
            blk_sync(nvme->blk);
            nvme_complete_cmd(nvme, queue_id, cid, SC_SUCCESS);
            break;
        case NVM_WRITEZ:
            blk_trim(nvme->blk, pos, len);
            nvme_complete_cmd(nvme, queue_id, cid, SC_SUCCESS);
            break;
        case NVM_DTSM:
            if (cmd[44] & 0x4) {
                // Deallocate (TRIM)
                uint8_t ranges = cmd[40];
                uint8_t* range_def = pci_get_dma_ptr(nvme->pci_dev, prp1, ranges * 16);
                if (range_def) {
                    for (size_t i=0; i<=ranges; ++i) {
                        uint64_t len = ((uint64_t)read_uint32_le(range_def + 4)) << NVME_LBAS;
                        uint64_t pos = read_uint64_le(range_def + 8) << NVME_LBAS;
                        blk_trim(nvme->blk, pos, len);
                        range_def += 16;
                    }
                }
            }
            nvme_complete_cmd(nvme, queue_id, cid, SC_SUCCESS);
            break;
        default:
            rvvm_info("NVME unknown IO cmd %02x (queue %04x head %04x tail %04x size %04x)", opcode, (uint32_t)queue_id,
                      nvme->queues[queue_id].head, nvme->queues[queue_id].tail, nvme->queues[queue_id].size);
            nvme_complete_cmd(nvme, queue_id, cid, SC_BAD_OP);
            break;
    }
}

static void nvme_process_cmd(nvme_dev_t* nvme, size_t queue_id, uint8_t* cmd)
{
    if (queue_id == ADMIN_SUBQ) {
        nvme_admin_cmd(nvme, cmd);
    } else {
        nvme_io_cmd(nvme, queue_id, cmd);
    }
}

/*static void* nvme_cmd_worker(void** data)
{
    nvme_dev_t* nvme = data[0];
    size_t sub_q = (size_t)data[1];
    uint8_t* cmd = data[2];
    nvme_process_cmd(nvme, sub_q, cmd);
    atomic_sub_uint32(&nvme->threads, 1);
    return NULL;
}*/

static void* nvme_worker(void** data)
{
    nvme_dev_t* nvme = data[0];
    size_t sub_q = (size_t)data[1];

    while (nvme->queues[sub_q].head != nvme->queues[sub_q].tail) {
        uint8_t* cmd = pci_get_dma_ptr(nvme->pci_dev, nvme->queues[sub_q].addr + (nvme->queues[sub_q].head * 64), 64);
        if (cmd) {
            // Fully parallel IO is possible (NVMe doesn't impose any command ordering requirements)
            // Needs better task scheduler though

            //void* args[3] = {nvme, (void*)sub_q, cmd};
            //atomic_add_uint32(&nvme->threads, 1);
            //thread_create_task_va(nvme_cmd_worker, args, 3);
            nvme_process_cmd(nvme, sub_q, cmd);
        }
        if (nvme->queues[sub_q].head++ >= nvme->queues[sub_q].size) {
            nvme->queues[sub_q].head = 0;
        }
    }

    spin_unlock(&nvme->queues[sub_q].lock);
    atomic_sub_uint32(&nvme->threads, 1);
    return NULL;
}

static void nvme_doorbell(nvme_dev_t* nvme, size_t queue_id, uint16_t val)
{
    //rvvm_info("NVME Doorbell %08x %04x", (uint32_t)queue_id, val);
    size_t sub_q = queue_id & ~1;
    size_t com_q = queue_id | 1;

    if (queue_id == sub_q) {
        // Update submission queue tail
        nvme->queues[sub_q].tail = val;
        if (nvme->queues[sub_q].tail > nvme->queues[sub_q].size) {
            nvme->queues[sub_q].tail = 0;
            rvvm_info("NVME tail overrun (queue %04x head %04x tail %04x size %04x)", (uint32_t)queue_id,
                      nvme->queues[queue_id].head, val, nvme->queues[queue_id].size);
        }
        if (nvme->queues[sub_q].head != nvme->queues[sub_q].tail) {
            if (spin_try_lock(&nvme->queues[sub_q].lock)) {
                // Run queue worker
                void* args[2] = {nvme, (void*)sub_q};
                atomic_add_uint32(&nvme->threads, 1);
                thread_create_task_va(nvme_worker, args, 2);
            }
        }
    } else {
        // Update completion queue head
        nvme->queues[com_q].head = val;
        if (nvme->queues[com_q].head == nvme->queues[com_q].tail) {
            pci_clear_irq(nvme->pci_dev, 0);
        }
    }
}

static bool nvme_pci_read(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    nvme_dev_t* nvme = dev->data;
    switch (offset) {
        case NVME_CAP1:
            write_uint32_le(data, NVME_MQES | (NVME_CQR << 16) | (NVME_TO << 24));
            break;
        case NVME_CAP2:
            write_uint32_le(data, NVME_DSTRD | (NVME_CSS << 5) | (NVME_MPMAX << 20));
            break;
        case NVME_VS:
            write_uint32_le(data, NVME_V);
            break;
        case NVME_INTMS:
        case NVME_INTMC:
            write_uint32_le(data, nvme->irq_mask);
            break;
        case NVME_CC:
            write_uint32_le(data, (nvme->conf & 1) | (NVME_IOQES << 16));
            break;
        case NVME_CSTS:
            // CC.EN -> CSTS.EN
            // CC.SHN -> CSTS.SHST
            write_uint32_le(data, (nvme->conf & 1) | ((!!(nvme->conf & 0xC000)) << 3));
            break;
        case NVME_AQA:
            write_uint32_le(data, nvme->queues[ADMIN_SUBQ].size | (nvme->queues[ADMIN_COMQ].size <<  16));
            break;
        case NVME_ASQ1:
            write_uint32_le(data, nvme->queues[ADMIN_SUBQ].addr);
            break;
        case NVME_ASQ2:
            write_uint32_le(data, nvme->queues[ADMIN_SUBQ].addr >> 32);
            break;
        case NVME_ACQ1:
            write_uint32_le(data, nvme->queues[ADMIN_COMQ].addr);
            break;
        case NVME_ACQ2:
            write_uint32_le(data, nvme->queues[ADMIN_COMQ].addr >> 32);
            break;
        default:
            memset(data, 0, size);
            break;
    }
    //rvvm_info("NVME read  0x%08zx size 0x%d: 0x%08x", offset, size, read_uint32_le(data));
    return true;
}

static bool nvme_pci_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    nvme_dev_t* nvme = dev->data;
    UNUSED(size);
    //rvvm_info("NVME write 0x%08zx size 0x%d: 0x%08x", offset, size, read_uint32_le(data));
    switch (offset) {
        case NVME_INTMS:
            nvme->irq_mask |= read_uint32_le(data);
            break;
        case NVME_INTMC:
            nvme->irq_mask &= ~read_uint32_le(data);
            break;
        case NVME_CC:
            nvme->conf = read_uint32_le(data);
            if (nvme->conf & 0xC000) nvme_shutdown(nvme);
            break;
        case NVME_AQA:
            nvme->queues[ADMIN_SUBQ].size = bit_cut(read_uint32_le(data), 0, 12);
            nvme->queues[ADMIN_COMQ].size = bit_cut(read_uint32_le(data), 16, 12);
            break;
        case NVME_ASQ1:
            nvme->queues[ADMIN_SUBQ].addr = bit_replace(nvme->queues[ADMIN_SUBQ].addr, 12, 20, read_uint32_le(data) >> 12);
            break;
        case NVME_ASQ2:
            nvme->queues[ADMIN_SUBQ].addr = bit_replace(nvme->queues[ADMIN_SUBQ].addr, 32, 32, read_uint32_le(data));
            break;
        case NVME_ACQ1:
            nvme->queues[ADMIN_COMQ].addr = bit_replace(nvme->queues[ADMIN_COMQ].addr, 12, 20, read_uint32_le(data) >> 12);
            break;
        case NVME_ACQ2:
            nvme->queues[ADMIN_COMQ].addr = bit_replace(nvme->queues[ADMIN_COMQ].addr, 32, 32, read_uint32_le(data));
            break;
        default: {
            size_t queue_id = (offset - 0x1000) >> (NVME_DSTRD + 2);
            nvme_doorbell(nvme, queue_id, read_uint16_le(data));
            break;
        }
    }
    return true;
}

PUBLIC pci_dev_t* nvme_init_blk(pci_bus_t* pci_bus, blkdev_t* image)
{
    nvme_dev_t* nvme = safe_calloc(sizeof(nvme_dev_t), 1);
    nvme->blk = image;

    pci_dev_desc_t nvme_desc = {
        .func[0] = {
            .vendor_id = 0x144d,  // Samsung Electronics Co Ltd
            .device_id = 0xa809,  // NVMe SSD Controller 980
            .class_code = 0x0108, // Mass Storage, Non-Volatile memory controller
            .prog_if = 0x02,      // NVMe
            .irq_pin = PCI_IRQ_PIN_INTA,
            .bar[0] = {
                .addr = PCI_BAR_ADDR_64,
                .size = 0x4000,
                .min_op_size = 4,
                .max_op_size = 4,
                .read = nvme_pci_read,
                .write = nvme_pci_write,
                .data = nvme,
                .type = &nvme_type,
            }
        }
    };

    nvme->pci_dev = pci_bus_add_device(pci_bus, &nvme_desc);
    if (nvme->pci_dev == NULL) {
        free(nvme);
        return NULL;
    }
    return nvme->pci_dev;
}

PUBLIC pci_dev_t* nvme_init(pci_bus_t* pci_bus, const char* image_path, bool rw)
{
    blkdev_t* blk = blk_open(image_path, rw ? BLKDEV_RW : 0);
    if (blk == NULL) return NULL;
    return nvme_init_blk(pci_bus, blk);
}
