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
#include "rvtimer.h"

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
#define FEAT_NQES  0x7   // Number of Queues feature

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

#define NVME_PAGE_SIZE 0x1000ULL
#define NVME_PAGE_MASK 0xFFFULL
#define NVME_PRP2_END  0xFF8ULL

typedef struct {
    rvvm_addr_t addr;
    spinlock_t lock;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
} nvme_queue_t;

typedef struct {
    blkdev_t* blk;
    pci_dev_t* pci_dev;
    spinlock_t lock;
    uint32_t threads;
    uint32_t conf;
    uint32_t irq_mask;
    char serial[12];
    nvme_queue_t queues[NVME_MAXQ];
} nvme_dev_t;

typedef struct {
    rvvm_addr_t prp1;
    rvvm_addr_t prp2;
    uint8_t*    prp2_dma;
    size_t      prp2_off;
    size_t      size;
    size_t      cur;
} nvme_prp_ctx_t;

typedef struct {
    const uint8_t* ptr;
    nvme_queue_t*  queue;
    nvme_prp_ctx_t prp;
    uint16_t cmd_id;
    uint16_t sq_id;
    uint16_t sq_head;
    uint8_t  opcode;
} nvme_cmd_t;

static void nvme_shutdown(nvme_dev_t* nvme)
{
    while (atomic_load_uint32(&nvme->threads)) sleep_ms(1);
    rvvm_addr_t asq = nvme->queues[ADMIN_SUBQ].addr;
    rvvm_addr_t acq = nvme->queues[ADMIN_COMQ].addr;
    uint32_t asqs = nvme->queues[ADMIN_SUBQ].size;
    uint32_t acqs = nvme->queues[ADMIN_COMQ].size;
    memset(nvme->queues, 0, sizeof(nvme->queues));
    nvme->queues[ADMIN_SUBQ].addr = asq;
    nvme->queues[ADMIN_COMQ].addr = acq;
    nvme->queues[ADMIN_SUBQ].size = asqs;
    nvme->queues[ADMIN_COMQ].size = acqs;
}

static void nvme_remove(rvvm_mmio_dev_t* dev)
{
    nvme_dev_t* nvme = (nvme_dev_t*)dev->data;
    nvme_shutdown(nvme);
    blk_close(nvme->blk);
    free(nvme);
}

static rvvm_mmio_type_t nvme_type = {
    .name = "nvme",
    .remove = nvme_remove,
};

static void nvme_complete_cmd(nvme_dev_t* nvme, nvme_cmd_t* cmd, uint32_t sf)
{
    nvme_queue_t* queue = cmd->queue;
    spin_lock(&queue->lock);
    rvvm_addr_t addr = queue->addr + (queue->tail << 4);
    if (queue->tail++ >= queue->size) queue->tail = 0;
    spin_unlock(&queue->lock);

    uint8_t* ptr = pci_get_dma_ptr(nvme->pci_dev, addr, 16);
    if (ptr) {
        uint8_t phase = (~read_uint16_le(ptr + 14)) & 1;
        write_uint32_le(ptr,      sf >> 8);                  // Command Specific
        write_uint32_le(ptr + 4,  0);                        // Reserved
        write_uint16_le(ptr + 8,  cmd->sq_head);             // SQ Head Pointer
        write_uint16_le(ptr + 10, cmd->sq_id);               // SQ Identifier
        write_uint16_le(ptr + 12, cmd->cmd_id);              // Command Identifier
        atomic_fence();
        write_uint16_le(ptr + 14, (sf & 0xFF) << 1 | phase); // Phase Bit, Status Field
    }
    if (!(nvme->irq_mask & 1)) pci_send_irq(nvme->pci_dev, 0);
}

static size_t nvme_process_prp_chunk(nvme_dev_t* nvme, nvme_cmd_t* cmd)
{
    nvme_prp_ctx_t* prp = &cmd->prp;
    rvvm_addr_t addr = prp->prp1;
    size_t len = NVME_PAGE_SIZE;

    if (prp->cur >= prp->size) {
        // End of transfer
        return 0;
    }

    if (prp->cur == 0) {
        // Consume the first page, may be misaligned
        len = NVME_PAGE_SIZE - (prp->prp1 & NVME_PAGE_MASK);
        if (len < prp->size && prp->size <= NVME_PAGE_SIZE + len) {
            // PRP2 encodes second page address directly
            prp->prp1 = prp->prp2;
            if (prp->prp1 == addr + len) len += NVME_PAGE_SIZE;
            if (len >= prp->size) len = prp->size;
            prp->cur = len;
            return len;
        }
        if (len >= prp->size) {
            prp->cur = prp->size;
            return prp->size;
        }
    }

    while ((prp->cur + len) < prp->size) {
        // Process PRP2 entries until we reach end of transfer
        if (prp->prp2_dma == NULL) {
            prp->prp2_dma = pci_get_dma_ptr(nvme->pci_dev, prp->prp2, NVME_PAGE_SIZE);
        }
        if (prp->prp2_dma) {
            prp->prp1 = read_uint64_le_m(prp->prp2_dma + prp->prp2_off);
            prp->prp2_off += 8;
            if (prp->prp2_off >= NVME_PRP2_END) {
                prp->prp2 = read_uint64_le_m(prp->prp2_dma + NVME_PRP2_END);
                prp->prp2_off = 0;
                prp->prp2_dma = pci_get_dma_ptr(nvme->pci_dev, prp->prp2, NVME_PAGE_SIZE);
            }
        } else {
            // DMA error
            nvme_complete_cmd(nvme, cmd, SC_DT_ERR);
            return 0;
        }

        // Non-continuous page, split the chunk
        if (prp->prp1 != (addr + len)) break;
        len += NVME_PAGE_SIZE;
    }

    if ((prp->cur + len) > prp->size) {
        // Fixup length overrun
        len = prp->size - prp->cur;
    }

    prp->cur += len;
    return len;
}

static void* nvme_get_prp_chunk(nvme_dev_t* nvme, nvme_cmd_t* cmd, size_t* size)
{
    rvvm_addr_t addr = cmd->prp.prp1;
    *size = nvme_process_prp_chunk(nvme, cmd);
    if (*size == 0) return NULL;
    void* ret = pci_get_dma_ptr(nvme->pci_dev, addr, *size);
    if (ret == NULL) nvme_complete_cmd(nvme, cmd, SC_DT_ERR);
    return ret;
}

static bool nvme_write_prp(nvme_dev_t* nvme, nvme_cmd_t* cmd, const void* data, size_t size)
{
    const uint8_t* src = data;
    uint8_t* dest;
    size_t tmp_size;
    cmd->prp.size = size;
    while (cmd->prp.cur < cmd->prp.size) {
        dest = nvme_get_prp_chunk(nvme, cmd, &tmp_size);
        if (!dest) return false;
        memcpy(dest, src, tmp_size);
        src += tmp_size;
    }
    return true;
}

static void nvme_admin_cmd(nvme_dev_t* nvme, nvme_cmd_t* cmd)
{
    switch (cmd->opcode) {
        case A_IDENTIFY: {
            uint8_t* ptr = safe_calloc(NVME_PAGE_SIZE, sizeof(uint8_t));
            switch (cmd->ptr[40]) {
                case IDENT_NS: {
                    uint64_t lbas = blk_getsize(nvme->blk) >> NVME_LBAS;
                    write_uint64_le(ptr,      lbas);
                    write_uint64_le(ptr + 8,  lbas);
                    write_uint64_le(ptr + 16, lbas);
                    ptr[33] = 0x8; // Supports Deallocate bit in Write Zeros
                    ptr[130] = NVME_LBAS;
                    break;
                }
                case IDENT_CTRL: {
                    write_uint16_le(ptr,     0x144d); // PCI Vendor ID
                    write_uint16_le(ptr + 2, 0x144d);
                    memcpy(ptr + 4,  nvme->serial, sizeof(nvme->serial)); // Serial Number
                    rvvm_strlcpy((char*)ptr + 24, "NVMe Storage", 40);    // Model Number
                    rvvm_strlcpy((char*)ptr + 64, "R947", 8);             // Firmware Revision
                    write_uint32_le(ptr + 80, NVME_V); // Version
                    ptr[111] = 1;    // Controller Type: I/O Controller
                    ptr[512] = 0x66; // Submission Queue Max/Cur Entry Size
                    ptr[513] = 0x44; // Completion Queue Max/Cur Entry Size
                    ptr[516] = 1;    // Number of Namespaces
                    ptr[520] = 0xC;  // Supports Write Zeroes, Dataset Management
                    // NVMe Qualified Name (Includes serial to distinguish targets)
                    size_t nqn_off = rvvm_strlcpy((char*)ptr + 768, "nqn.2022-04.lekkit:nvme:", 256);
                    memcpy(ptr + 768 + nqn_off,  nvme->serial, sizeof(nvme->serial));
                    break;
                }
                case IDENT_NSLS:
                    write_uint32_le(ptr, 0x1); // Namespace #1
                    break;
                case IDENT_NIDS:
                    ptr[0] = 3;  // Namespace UUID
                    ptr[1] = 16; // UUID length
                    break;
                default:
                    nvme_complete_cmd(nvme, cmd, SC_BAD_FIL);
                    free(ptr);
                    return;
            }
            if (nvme_write_prp(nvme, cmd, ptr, NVME_PAGE_SIZE)) {
                nvme_complete_cmd(nvme, cmd, SC_SUCCESS);
            }
            free(ptr);
            break;
        }
        case A_MKIO_SUB:
        case A_MKIO_COM: {
            size_t q_id = (read_uint16_le(cmd->ptr + 40) << 1) + (cmd->opcode == A_MKIO_COM);
            uint16_t q_size = read_uint16_le(cmd->ptr + 42);
            if (q_id <= ADMIN_COMQ || q_id >= NVME_MAXQ) {
                nvme_complete_cmd(nvme, cmd, SC_BAD_QI);
            } else if (q_size == 0) {
                nvme_complete_cmd(nvme, cmd, SC_BAD_QS);
            } else {
                spin_lock(&nvme->queues[q_id].lock);
                nvme->queues[q_id].addr = cmd->prp.prp1;
                nvme->queues[q_id].size = q_size;
                nvme->queues[q_id].head = 0;
                nvme->queues[q_id].tail = 0;
                spin_unlock(&nvme->queues[q_id].lock);
                nvme_complete_cmd(nvme, cmd, SC_SUCCESS);
            }
            break;
        }
        case A_RMIO_SUB:
        case A_RMIO_COM: {
            size_t q_id = (read_uint16_le(cmd->ptr + 40) << 1) + (cmd->opcode == A_RMIO_COM);
            if (q_id <= ADMIN_COMQ || q_id >= NVME_MAXQ) {
                nvme_complete_cmd(nvme, cmd, SC_BAD_QI);
            } else {
                spin_lock(&nvme->queues[q_id].lock);
                nvme->queues[q_id].addr = 0;
                nvme->queues[q_id].size = 0;
                nvme->queues[q_id].head = 0;
                nvme->queues[q_id].tail = 0;
                spin_unlock(&nvme->queues[q_id].lock);
                nvme_complete_cmd(nvme, cmd, SC_SUCCESS);
            }
            break;
        }
        case A_SET_FEAT:
        case A_GET_FEAT:
            if (cmd->ptr[40] == FEAT_NQES) {
                nvme_complete_cmd(nvme, cmd, SC_SUCCESS | (NVME_MAXQ << 8));
            } else {
                nvme_complete_cmd(nvme, cmd, SC_BAD_FIL);
            }
            break;
        case A_ABORTCMD: // Ignored, all the commands could be already executing
            nvme_complete_cmd(nvme, cmd, SC_SUCCESS);
            break;
        default:
            //rvvm_info("NVMe unknown admin cmd %02x", cmd->opcode);
            nvme_complete_cmd(nvme, cmd, SC_BAD_OP);
            break;
    }
}

static void nvme_io_cmd(nvme_dev_t* nvme, nvme_cmd_t* cmd)
{
    uint64_t pos = read_uint64_le(cmd->ptr + 40) << NVME_LBAS;
    uint8_t* buffer;
    size_t   size, tmp;

    switch (cmd->opcode) {
        case NVM_READ:
        case NVM_WRITE:
            while (cmd->prp.cur < cmd->prp.size) {
                buffer = nvme_get_prp_chunk(nvme, cmd, &size);
                if (buffer == NULL) return;
                if (cmd->opcode == NVM_WRITE) {
                    tmp = blk_write(nvme->blk, buffer, size, pos);
                } else {
                    tmp = blk_read(nvme->blk, buffer, size, pos);
                }
                if (tmp != size) {
                    nvme_complete_cmd(nvme, cmd, SC_DT_ERR);
                    return;
                }
                pos += size;
            }
            nvme_complete_cmd(nvme, cmd, SC_SUCCESS);
            break;
        case NVM_FLUSH:
            blk_sync(nvme->blk);
            nvme_complete_cmd(nvme, cmd, SC_SUCCESS);
            break;
        case NVM_WRITEZ:
            blk_trim(nvme->blk, pos, cmd->prp.size);
            nvme_complete_cmd(nvme, cmd, SC_SUCCESS);
            break;
        case NVM_DTSM:
            if (cmd->ptr[44] & 0x4) {
                // Deallocate (TRIM)
                cmd->prp.size = (((size_t)cmd->ptr[40]) + 1) << 4;
                while (cmd->prp.cur < cmd->prp.size) {
                    buffer = nvme_get_prp_chunk(nvme, cmd, &size);
                    if (!buffer) return;
                    for (size_t i=0; i<size; i += 16) {
                        uint64_t trim_len = ((uint64_t)read_uint32_le(buffer + i + 4)) << NVME_LBAS;
                        uint64_t trim_pos = read_uint64_le(buffer + i + 8) << NVME_LBAS;
                        blk_trim(nvme->blk, trim_pos, trim_len);
                    }
                }
            }
            nvme_complete_cmd(nvme, cmd, SC_SUCCESS);
            break;
        default:
            //rvvm_info("NVMe unknown IO cmd %02x", cmd->opcode);
            nvme_complete_cmd(nvme, cmd, SC_BAD_OP);
            break;
    }
}

static void* nvme_cmd_worker(void** data)
{
    nvme_dev_t* nvme = data[0];
    size_t queue_id = (size_t)data[1];
    nvme_queue_t* queue = &nvme->queues[queue_id];
    nvme_cmd_t cmd = {
        .queue = &nvme->queues[queue_id + 1],
        .sq_id = queue_id >> 1,
        .sq_head = (size_t)data[2],
    };
    cmd.ptr = pci_get_dma_ptr(nvme->pci_dev, queue->addr + (cmd.sq_head << 6), 64);
    if (cmd.ptr) {
        // Parse & process NVMe command
        cmd.opcode = cmd.ptr[0];
        cmd.cmd_id = read_uint16_le(cmd.ptr + 2);
        cmd.prp.prp1 = read_uint64_le(cmd.ptr + 24);
        cmd.prp.prp2 = read_uint64_le(cmd.ptr + 32);
        cmd.prp.size = (((size_t)read_uint16_le(cmd.ptr + 48)) + 1) << NVME_LBAS;

        if (queue_id == ADMIN_SUBQ) {
            nvme_admin_cmd(nvme, &cmd);
        } else {
            nvme_io_cmd(nvme, &cmd);
        }
    }
    atomic_sub_uint32(&nvme->threads, 1);
    return NULL;
}

static void nvme_doorbell(nvme_dev_t* nvme, size_t queue_id, uint16_t val)
{
    nvme_queue_t* queue = &nvme->queues[queue_id];
    
    // Ignore attempts to overrun queue
    if (val > queue->size) return;

    spin_lock(&queue->lock);
    if (queue_id & 1) {
        // Update completion queue head
        queue->head = val;
        if (queue->tail == val) {
            pci_clear_irq(nvme->pci_dev, 0);
        }
    } else {
        queue->tail = val;
        while (queue->head != queue->tail) {
            void* args[3] = {nvme, (void*)queue_id, (void*)(size_t)queue->head};
            atomic_add_uint32(&nvme->threads, 1);
            thread_create_task_va(nvme_cmd_worker, args, 3);

            if (queue->head++ >= queue->size) queue->head = 0;
        }
    }
    spin_unlock(&queue->lock);
}

static bool nvme_pci_read(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    nvme_dev_t* nvme = dev->data;
    spin_lock(&nvme->lock);
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
            // CC.EN  -> CSTS.EN
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
    spin_unlock(&nvme->lock);
    return true;
}

static bool nvme_pci_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    nvme_dev_t* nvme = dev->data;
    UNUSED(size);
    if (likely(offset >= 0x1000)) {
        // Doorbell
        size_t queue_id = (offset - 0x1000) >> (NVME_DSTRD + 2);
        if (queue_id < NVME_MAXQ) nvme_doorbell(nvme, queue_id, read_uint16_le(data));
        return true;
    }
    spin_lock(&nvme->lock);
    switch (offset) {
        case NVME_INTMS:
            nvme->irq_mask |= read_uint32_le(data);
            break;
        case NVME_INTMC:
            nvme->irq_mask &= ~read_uint32_le(data);
            break;
        case NVME_CC:
            nvme->conf = read_uint32_le(data);
            // Shutdown or reset the controller
            if ((nvme->conf & 0xC000) || !(nvme->conf & 0x1))
                nvme_shutdown(nvme);
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
    }
    spin_unlock(&nvme->lock);
    return true;
}

PUBLIC pci_dev_t* nvme_init_blk(pci_bus_t* pci_bus, void* blk_dev)
{
    nvme_dev_t* nvme = safe_new_obj(nvme_dev_t);
    nvme->blk = blk_dev;
    rvvm_randomserial(nvme->serial, sizeof(nvme->serial));

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

    pci_dev_t* pci_dev = pci_bus_add_device(pci_bus, &nvme_desc);
    if (pci_dev) nvme->pci_dev = pci_dev;
    return pci_dev;
}

PUBLIC pci_dev_t* nvme_init(pci_bus_t* pci_bus, const char* image_path, bool rw)
{
    blkdev_t* blk = blk_open(image_path, rw ? BLKDEV_RW : 0);
    if (blk == NULL) return NULL;
    return nvme_init_blk(pci_bus, blk);
}

PUBLIC pci_dev_t* nvme_init_auto(rvvm_machine_t* machine, const char* image_path, bool rw)
{
    return nvme_init(rvvm_get_pci_bus(machine), image_path, rw);
}
