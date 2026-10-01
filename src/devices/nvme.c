/*
nvme.c - Non-Volatile Memory Express
Copyright (C) 2022  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

/*
 * TODO: Threaded task workers
 * TODO: Snapshots
 */

#include <rvvm/rvvm_blk.h>
#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_pci.h>
#include <rvvm/rvvm_region.h>
#include <rvvm/rvvm_snapshot.h>

#include <util/atomics.h>
#include <util/bit_ops.h>
#include <util/mem_ops.h>
#include <util/threading.h>
#include <util/utils.h>

PUSH_OPTIMIZATION_SIZE

/*
 * NVMe Controller Registers
 */
#define NVME_REG_CAP1            0x00 // Controller Capabilities
#define NVME_REG_CAP2            0x04 // Controller Capabilities
#define NVME_REG_VS              0x08 // Version
#define NVME_REG_INTMS           0x0C // Interrupt Mask Set
#define NVME_REG_INTMC           0x10 // Interrupt Mask Clear
#define NVME_REG_CC              0x14 // Controller Configuration
#define NVME_REG_CSTS            0x1C // Controller Status
#define NVME_REG_AQA             0x24 // Admin Queue Attributes
#define NVME_REG_ASQ1            0x28 // Admin Submission Queue Base Address
#define NVME_REG_ASQ2            0x2C
#define NVME_REG_ACQ1            0x30 // Admin Completion Queue Base Address
#define NVME_REG_ACQ2            0x34

/*
 * NVMe Register constants
 */
#define NVME_CAP1_MQES           0x0000FFFF // Maximum Queue Entries Supported: 65536
#define NVME_CAP1_CQR            0x00010000 // Contiguous Queues Required
#define NVME_CAP1_TO             0xFF000000 // Timeout: Max

#define NVME_CAP2_DSTRD          0x00000000 // Doorbell Stride (0 means 2-bit shift)
#define NVME_CAP2_CSS            0x00000020 // Command Sets Supported (NVM Command Set)

#define NVME_VS_VERSION          0x00010400 // NVMe v1.4

#define NVME_CC_EN               0x00000001 // Enabled
#define NVME_CC_SHN              0x0000C000 // Shutdown Notification
#define NVME_CC_IOQES            0x00460000 // IO Queue Entry Sizes (16b:64b)

#define NVME_CSTS_RDY            0x00000001 // Ready
#define NVME_CSTS_SHST           0x00000008 // Shutdown complete

/*
 * NVMe Queue IDs
 */
#define NVME_QUEUE_ADMIN         0x00 // Admin Submission/Completion Queue IDs
#define NVME_QUEUE_IO            0x01 // IO Queues starting ID

/*
 * NVMe Submission Queue Entry definitions
 */
#define NVME_SQE_SIZE            0x40 // Completion Queue Entry Size
#define NVME_SQE_SIZE_SHIFT      0x06 // Completion Queue Entry Size Shift

#define NVME_SQE_CDW0            0x00 // Command Dword 0
#define NVME_SQE_CID             0x02 // Command Identifier
#define NVME_SQE_NSID            0x04 // Namespace Identifier
#define NVME_SQE_MPTR            0x10 // Metadata Pointer (Contiguous buffer or SGL descriptor)
#define NVME_SQE_DPTR            0x18 // Data Pointer (PRP1+PRP2 or SGL segment)
#define NVME_SQE_PRP1            0x18 // PRP1 (Pointer to first page)
#define NVME_SQE_PRP2            0x20 // PRP2 (Pointer to second page or PRP list)
#define NVME_SQE_CDW10           0x28 // Command Dword 10 (Command-Specific)
#define NVME_SQE_CDW11           0x2C // Command Dword 11 (Command-Specific)
#define NVME_SQE_CDW12           0x30 // Command Dword 12 (Command-Specific)
#define NVME_SQE_CDW13           0x34 // Command Dword 13 (Command-Specific)
#define NVME_SQE_CDW14           0x38 // Command Dword 14 (Command-Specific)
#define NVME_SQE_CDW15           0x3C // Command Dword 15 (Command-Specific)

#define NVME_CDW0_PRP            0x00000000 // PRPs are used for this transfer
#define NVME_CDW0_SGL_MPBUF      0x00004000 // SGLs are used for this transfer. MPTR contains a contiguous buffer.
#define NVME_CDW0_SGL_MPSGL      0x00008000 // PRPs are used for this transfer. MPTR contains an SGL descriptor.
#define NVME_CDW0_SGL_MASK       0x0000C000 // Mask to detect SGL usage

#define NVME_CDW0_CID_SHIFT      0x10 // Command Identifier (CID) shift from CDW0

/*
 * NVMe Completion Queue Entry definitions
 */
#define NVME_CQE_SIZE            0x10 // Completion Queue Entry Size
#define NVME_CQE_SIZE_SHIFT      0x04 // Completion Queue Entry Size Shift

#define NVME_CQE_CS              0x00 // Command Specific
#define NVME_CQE_RSVD            0x04 // Reserved
#define NVME_CQE_SQHD_SQID       0x08 // Submission Queue Head Pointer & Identifier
#define NVME_CQE_CID_PB_SF       0x0C // Command Identifier, Phase Bit, Status Field

#define NVME_CQE_PB_MASK         0x00010000 // Phase Bit
#define NVME_CQE_SF_SHIFT        0x11       // Status Shift

/*
 * NVMe Generic Status Codes - Admin command set
 */
#define NVME_SC_SUCCESS          0x00 // Successful Completion
#define NVME_SC_BAD_OPCODE       0x01 // Invalid Command Opcode
#define NVME_SC_BAD_FIELD        0x02 // Invalid Field in Command
#define NVME_SC_ID_CONFLICT      0x03 // Command ID Conflict
#define NVME_SC_DATA_ERR         0x04 // Data Transfer Error
#define NVME_SC_POWER_LOSS       0x05 // Command Aborted due to Power Loss
#define NVME_SC_INTERNAL_ERR     0x06 // Internal Error
#define NVME_SC_ABORTED          0x07 // Command Abort Requested
#define NVME_SC_SQ_DELETED       0x08 // Command Aborted due to SQ Deletion
#define NVME_SC_FUSE_FAIL        0x09 // Command Aborted due to Failed Fused Command
#define NVME_SC_FUSE_MISSING     0x0A // Command Aborted due to Missing Fused Command
#define NVME_SC_BAD_NAMESPACE    0x0B // Invalid Namespace or Format
#define NVME_SC_SEQUENCE_ERR     0x0C // Command Sequence Error
#define NVME_SC_BAD_SGL_DESC     0x0D // Invalid SGL Segment Descriptor
#define NVME_SC_BAD_SGL_NUM      0x0E // Invalid Number of SGL Descriptors
#define NVME_SC_BAD_SGL_DATA     0x0F // Invalid Data SGL Length
#define NVME_SC_BAD_SGL_META     0x10 // Invalid Metadata SGL Length
#define NVME_SC_BAD_SGL_TYPE     0x11 // Invalid SGL Descriptor Type
#define NVME_SC_INVALID_CMB_USE  0x12 // Invalid Use of Controller Memory Buffer
#define NVME_SC_BAD_PRP_OFFSET   0x13 // Invalid PRP Offset
#define NVME_SC_ATOMIC_UNIT      0x14 // Atomic Write Unit Exceeded
#define NVME_SC_PERM_DENIED      0x15 // Operation Denied
#define NVME_SC_BAD_SGL_OFFSET   0x16 // Invalid SGL Offset
#define NVME_SC_BAD_HOST_ID      0x18 // Host Identifier Inconsistent Format
#define NVME_SC_KEEP_ALIVE       0x19 // Keep Alive Timer Expired
#define NVME_SC_BAD_TIMER        0x1A // Keep Alive Timeout Invalid
#define NVME_SC_PREEMPTED        0x1B // Command Aborted due to Preempt and Abort
#define NVME_SC_SANITIZE_FAIL    0x1C // Sanitize Failed
#define NVME_SC_SANITIZE_NOW     0x1D // Sanitize In Progress
#define NVME_SC_BAD_SGL_GRAN     0x1E // Invalid SGL Data Block Granularity
#define NVME_SC_QUEUE_IN_CMB     0x1F // Command Not Supported for Queue in CMB
#define NVME_SC_NAMESPACE_WP     0x20 // Namespace is Write Protected
#define NVME_SC_INTERRUPTED      0x21 // Command Interrupted
#define NVME_SC_TRANSPORT_ERR    0x22 // Transient Transport Error

/*
 * NVMe Generic Status Codes - IO command set
 */
#define NVME_SC_LBA_RANGE        0x80 // LBA Out of Range
#define NVME_SC_NS_CAPACITY      0x81 // Capacity Exceeded
#define NVME_SC_NS_NOT_READY     0x82 // Namespace Not Ready
#define NVME_SC_RSV_CONFLICT     0x83 // Reservation Conflict
#define NVME_SC_FORMAT_NOW       0x84 // Format In Progress

/*
 * NVMe Command Specific Status Codes - Admin command set
 */
#define NVME_SC_BAD_CQ           0x0100 // Completion Queue Invalid
#define NVME_SC_BAD_QUEUE_ID     0x0101 // Invalid Queue Identifier
#define NVME_SC_BAD_QUEUE_SIZE   0x0102 // Invalid Queue Size
#define NVME_SC_ABORT_LIMIT      0x0103 // Abort Command Limit Exceeded
#define NVME_SC_AER_LIMIT        0x0105 // Asynchronous Evebt Request Limit Exceeded
#define NVME_SC_BAD_FW_SLOT      0x0106 // Invalid Firmware Slot
#define NVME_SC_BAD_FW_IMG       0x0107 // Invalid Firmware Image
#define NVME_SC_BAD_IRQ_VEC      0x0108 // Invalid Interrupt Vector
#define NVME_SC_BAD_LOG_PAGE     0x0109 // Invalid Log Page
#define NVME_SC_BAD_FORMAT       0x010A // Invalid Format
#define NVME_SC_FW_NEED_RST      0x010B // Firmware Activation Requires Conventional Reset
#define NVME_SC_BAD_QUEUE_DEL    0x010C // Invalid Queue Deletion
#define NVME_SC_FEAT_NOT_SAVEBL  0x010D // Feature Identifier Not Saveable
#define NVME_SC_FEAT_NOT_CHGBL   0x010E // Feature Not Changeable
#define NVME_SC_FEAT_NOT_NS      0x010F // Feature Not Namespace Specific
#define NVME_SC_FW_NEED_NRST     0x0110 // Firmware Activation Requires NVM Subsystem Reset
#define NVME_SC_FW_NEED_CRST     0x0111 // Firmware Activation Requires Controller Level Reset
#define NVME_SC_FW_NEED_MTO      0x0112 // Firmware Activation Requires Maximum Time Violation
#define NVME_SC_FW_PROHIBIT      0x0113 // Firmware Activation Prohibited
#define NVME_SC_OVERLAP_RANGE    0x0114 // Overlapping Range
#define NVME_SC_NS_NO_CAPACITY   0x0115 // Namespace Insufficient Capacity
#define NVME_SC_NS_ID_UNAVAIL    0x0116 // Namespace Identifier Unavailable
#define NVME_SC_NS_ATTACHED      0x0118 // Namespace Already Attached
#define NVME_SC_NS_PRIVATE       0x0119 // Namespace Is Private
#define NVME_SC_NS_NATTACHED     0x011A // Namespace Not Attached
#define NVME_SC_THIN_PROV_SUPP   0x011B // Thin Provisioning Not Supported
#define NVME_SC_BAD_CTRL_LIST    0x011C // Controller List Invalid
#define NVME_SC_SELF_TEST        0x011D // Device Self-test In Progress
#define NVME_SC_BOOT_PART_WP     0x011E // Boot Partition Write Prohibited
#define NVME_SC_BAD_CTRL_ID      0x011F // Invalid Controller Identifier
#define NVME_SC_BAD_CTRL_SEC     0x0120 // Invalid Secondary Controller State
#define NVME_SC_BAD_CTRL_RES     0x0121 // Invalid Number of Controller Resources
#define NVME_SC_BAD_RES_ID       0x0122 // Invalid Resource Identifier
#define NVME_SC_SANITIZE_PMR     0x0123 // Sanitize Prohibited While PMR Enabled
#define NVME_SC_ANA_GROUP_ID     0x0124 // ANA Group Identifier Invalid
#define NVME_SC_ANA_ATTACH       0x0125 // ANA Attach Failed

/*
 * NVMe Command Specific Status Codes - IO command set
 */
#define NVME_SC_BAD_ATTRS        0x0180 // Conflicting Attributes
#define NVME_SC_BAD_PROT         0x0181 // Invalid Protection Information
#define NVME_SC_READONLY         0x0182 // Attempted Write to Read Only Range

/*
 * NVMe Admin Command Set
 */
#define NVME_ADM_DELETE_IO_SQ    0x00 // Delete IO Submission Queue
#define NVME_ADM_CREATE_IO_SQ    0x01 // Create IO Submission Queue
#define NVME_ADM_GET_LOG_PAGE    0x02 // Get Log Page
#define NVME_ADM_DELETE_IO_CQ    0x04 // Delete IO Completion Queue
#define NVME_ADM_CREATE_IO_CQ    0x05 // Create IO Completion Queue
#define NVME_ADM_IDENTIFY        0x06 // Identify
#define NVME_ADM_ABORT           0x08 // Abort Command
#define NVME_ADM_SET_FEATURE     0x09 // Set Features
#define NVME_ADM_GET_FEATURE     0x0A // Get Features
#define NVME_ADM_ASYNC_EVENT_REQ 0x0C // Asynchronous Event Request
#define NVME_ADM_NAMESPACE_MGMT  0x0D // Namespace Management (Optional)
#define NVME_ADM_FIRMWARE_COMM   0x10 // Firmware Commit (Optional)
#define NVME_ADM_FIRMWARE_DOWN   0x11 // Firmware Image Download (Optional)
#define NVME_ADM_SELF_TEST       0x14 // Device Self-Test (Optional)
#define NVME_ADM_NAMESPACE_ATCH  0x15 // Namespace Attachment (Optional)
#define NVME_ADM_KEEP_ALIVE      0x18 // Keep Alive (Optional)
#define NVME_ADM_DIRECTIVE_SEND  0x19 // Directive Send (Optional)
#define NVME_ADM_DIRECTIVE_RECV  0x1A // Directive Receive (Optional)
#define NVME_ADM_VIRT_MGMT       0x1C // Virtualization Management (Optional)
#define NVME_ADM_NVME_MI_SEND    0x1D // NVMe-MI Send (Optional)
#define NVME_ADM_NVME_MI_RECV    0x1E // NVMe-MI Receive (Optional)
#define NVME_ADM_DBELL_BUFF_CFG  0x7C // Doorbell Buffer Config (Optional)
#define NVME_ADM_FORMAT_NVM      0x80 // Format NVM (Optional)
#define NVME_ADM_SECURITY_SEND   0x81 // Security Send (Optional)
#define NVME_ADM_SECURITY_RECV   0x82 // Security Receive (Optional)
#define NVME_ADM_SANITIZE        0x84 // Sanitize (Optional)
#define NVME_ADM_GET_LBA_STAT    0x86 // Get LBA Status (Optional)

/*
 * Create IO Completion Queue Dword 11 bits
 */
#define NVME_CQ_FLAGS_PC         0x01 // Physically Contiguous
#define NVME_CQ_FLAGS_IEN        0x02 // Interrupts Enabled

/*
 * NVMe Get Log Page - Log Page Identifiers
 */
#define NVME_LOG_ERROR           0x01 // Error Information
#define NVME_LOG_SMART           0x02 // SMART / Health Information
#define NVME_LOG_FIRMWARE_SLOT   0x03 // Firmware Slot Information
#define NVME_LOG_CHANGED_NS_LIST 0x04 // Changed Namespace List (Optional)
#define NVME_LOG_CMD_SUPPORTED   0x05 // Commands Supported and Effects (Optional)
#define NVME_LOG_SELF_TEST       0x06 // Device Self-Test (Optional)
#define NVME_LOG_TELEMETRY_HOST  0x07 // Telemetry Host-Initiated (Optional)
#define NVME_LOG_TELEMETRY_CTRL  0x08 // Telemetry Contoller-Initiated (Optional)
#define NVME_LOG_EGRP_INFO       0x09 // Endurance Group Information (Optional)
#define NVME_LOG_PRED_LAT_SET    0x0A // Predictable Latency Per NVM Set (Optional)
#define NVME_LOG_PRED_LAT_AGGR   0x0B // Predictable Latency Event Aggregate (Optional)
#define NVME_LOG_ASYMM_NS_ACCESS 0x0C // Asymmetric Namespace Access (Optional)
#define NVME_LOG_PERSIST_EVENT   0x0D // Persistent Event Log (Optional)
#define NVME_LOG_LBA_STATUS      0x0E // LBA Status Information (Optional)
#define NVME_LOG_EGRP_AGGR       0x0F // Endurance Group Event Aggregate (Optional)
#define NVME_LOG_DISCOVERY       0x70 // Discovery (Optional)
#define NVME_LOG_RSV_NOTIFY      0x80 // Reservation Notification (Optional)
#define NVME_LOG_SANITIZE_STAT   0x81 // Sanitize Status (Optional)

/*
 * NVMe Identify - CNS Identifiers
 */
#define NVME_CNS_NAMESPACE       0x00 // Identify Namespace
#define NVME_CNS_CONTROLLER      0x01 // Identify Controller
#define NVME_CNS_NSID_LIST       0x02 // Identify Namespace List
#define NVME_CNS_NSID_DESC       0x03 // Identify Namespace Descriptors
#define NVME_CNS_NVM_SET_LIST    0x04 // Identify NVM Set List (Optional)
#define NVME_CNS_ALLOC_NSID      0x10 // Identify Allocated Namespace ID List (Optional)
#define NVME_CNS_ALLOC_NAMESPACE 0x11 // Identify Allocated Namespace (Optional)
#define NVME_CNS_CTRL_NSID_LIST  0x12 // Identify Controller List attached to NSID (Optional)
#define NVME_CNS_CTRL_LIST       0x13 // Identify Controller List in the NVM subsystem (Optional)
#define NVME_CNS_CTRL_CAPS       0x14 // Identify Primary Controller Capabilities (Optional)
#define NVME_CNS_CTRL_SECONDARY  0x15 // Identify Secondary Controller List (Optional)
#define NVME_CNS_NS_GRANULARITY  0x15 // Identify Namespace Granularity List (Optional)
#define NVME_CNS_UUID_LIST       0x17 // Identify UUID List (Optional)

/*
 * NVMe Get/Set Feature - Feature Identifiers
 */
#define NVME_FEAT_ARBITRATION    0x01 // Arbitration
#define NVME_FEAT_POWER_MGMT     0x02 // Power Management
#define NVME_FEAT_LBA_RANGE      0x03 // LBA Range Type (Optional)
#define NVME_FEAT_TEMP_THRESH    0x04 // Temperature Threshold
#define NVME_FEAT_ERROR_RECOVER  0x05 // Error Recovery
#define NVME_FEAT_VOLATILE_WC    0x06 // Volatile Write Cache (Optional)
#define NVME_FEAT_NUM_QUEUES     0x07 // Number of Queues
#define NVME_FEAT_IRQ_COALESCE   0x08 // Interrupt Coalescing
#define NVME_FEAT_IRQ_VECTOR     0x09 // Interrupt Vector Configuration
#define NVME_FEAT_WR_ATOMIC      0x0A // Write Atomicity Normal
#define NVME_FEAT_ASYNC_EVENT    0x0B // Asynchronous Event Configuration
#define NVME_FEAT_AUTO_PWSTATE   0x0C // Autonomous Power State Transition (Optional)
#define NVME_FEAT_HOST_MEM_BUFF  0x0D // Host Memory Buffer (Optional)
#define NVME_FEAT_TIMESTAMP      0x0E // Timestamp (Optional)
#define NVME_FEAT_KPALIVE_TIMER  0x0F // Keep Alive Timer (Optional)
#define NVME_FEAT_HOST_THERMAL   0x10 // Host-Controlled Thrermal Management (Optional)
#define NVME_FEAT_NONOP_PWR_CFG  0x11 // Non-Operational Power State Config (Optional)
#define NVME_FEAT_RDRECOVER_LVL  0x12 // Read Recovery Level Config (Optional)
#define NVME_FEAT_PRED_LAT_CFG   0x13 // Predictable Latency Mode Config (Optional)
#define NVME_FEAT_PRED_LAT_WIN   0x14 // Predictable Latency Mode Window (Optional)
#define NVME_FEAT_LBA_STAT_INF   0x15 // LBA Status Information Attributes (Optional)
#define NVME_FEAT_HOST_BEHAVIOR  0x16 // Host Behavior Support (Optional)
#define NVME_FEAT_SANITIZE_CFG   0x17 // Sanitize Config (Optional)
#define NVME_FEAT_EGRP_EVT_CFG   0x18 // Endurance Group Event Configuration (Optional)
#define NVME_FEAT_SW_PROGRESS    0x80 // Software Progress Marker (Optional)
#define NVME_FEAT_HOST_IDENT     0x81 // Host Identifier (Optional)

/*
 * NVMe IO Command Set
 */
#define NVME_IO_FLUSH            0x00 // Flush buffers
#define NVME_IO_WRITE            0x01 // Write
#define NVME_IO_READ             0x02 // Read
#define NVME_IO_WRITE_UNC        0x04 // Write Uncorrectable (Optional)
#define NVME_IO_COMPARE          0x05 // Compare (Optional)
#define NVME_IO_WRITE_ZEROES     0x08 // Write Zeroes (Optional)
#define NVME_IO_DTSM             0x09 // Dataset Management (Optional)
#define NVME_IO_VERIFY           0x0C // Verify (Optional)
#define NVME_IO_RSV_REGISTER     0x0D // Reservation Register (Optional)
#define NVME_IO_RSV_REPORT       0x0E // Reservation Report (Optional)
#define NVME_IO_RSV_ACQUIRE      0x11 // Reservation Acquire (Optional)
#define NVME_IO_RSV_RELEASE      0x15 // Reservation Release (Optional)

/*
 * NVMe Implementation constants
 */
#define NVME_PAGE_SHIFT          0x0C // Page Size Shift (4kb pages)
#define NVME_LBA_SHIFT           0x09 // LBA Block Size Shift (512b logical blocks)
#define NVME_IO_QUEUES           0x10 // Max IO Queues (16)

#define NVME_PAGE_SIZE           (1ULL << NVME_PAGE_SHIFT)
#define NVME_PAGE_MASK           (NVME_PAGE_SIZE - 1ULL)
#define NVME_LBA_SIZE            (1ULL << NVME_LBA_SHIFT)
#define NVME_LBA_MASK            (NVME_LBA_SIZE - 1ULL)

typedef struct nvme_device nvme_dev_t;

typedef struct {
    // Queue address (Low/High)
    uint32_t addr_l;
    uint32_t addr_h;

    // Queue size
    uint32_t size;

    // Queue head/tail
    uint32_t head;
    uint32_t tail;

    // For submission queue: Completion queue ID
    // For completion queue: Interrupt configuration
    uint32_t data;
} nvme_queue_t;

struct nvme_device {
    // NVMe PCI function handle
    rvvm_pci_func_t* func;

    // NVMe backing block device
    rvvm_blk_dev_t* blk;

    // Submission queues
    nvme_queue_t sq[NVME_IO_QUEUES + NVME_QUEUE_IO];

    // Completion queues
    nvme_queue_t cq[NVME_IO_QUEUES + NVME_QUEUE_IO];

    uint32_t threads;

    // Controller Configuration
    uint32_t conf;

    // Masked interrupts bitmask
    uint32_t irq_mask;

    // Temperature Threshold
    uint32_t temp_thresh;

    // Serial number
    char serial[12];
};

typedef struct {
    rvvm_addr_t prp1;
    rvvm_addr_t prp2;
    size_t      size;
    size_t      cur;
} nvme_prp_ctx_t;

typedef struct {
    // Submission queue entry
    uint8_t* sqe;

    // PRP parser context
    nvme_prp_ctx_t prp;

    // Command information
    uint32_t sqhd_sqid;
    uint32_t cq_id;
} nvme_cmd_t;

static inline nvme_queue_t* nvme_get_sq(nvme_dev_t* nvme, uint32_t queue_id)
{
    if (likely(queue_id < STATIC_ARRAY_SIZE(nvme->sq))) {
        return &nvme->sq[queue_id];
    }
    return NULL;
}

static inline nvme_queue_t* nvme_get_cq(nvme_dev_t* nvme, uint32_t queue_id)
{
    if (likely(queue_id < STATIC_ARRAY_SIZE(nvme->cq))) {
        return &nvme->cq[queue_id];
    }
    return NULL;
}

static inline rvvm_addr_t nvme_queue_addr(const nvme_queue_t* queue)
{
    uint32_t low  = atomic_load_uint32_relax(&queue->addr_l);
    uint32_t high = atomic_load_uint32_relax(&queue->addr_h);
    return low | (((uint64_t)high) << 32);
}

static inline uint32_t nvme_queue_size(const nvme_queue_t* queue)
{
    return atomic_load_uint32_relax(&queue->size);
}

// Returns true on entry dequeue
static inline bool nvme_queue_dequeue(nvme_queue_t* queue, uint32_t* entry)
{
    uint32_t size = atomic_load_uint32_relax(&queue->size);
    uint32_t head = atomic_load_uint32_relax(&queue->head);
    uint32_t tail = atomic_load_uint32_relax(&queue->tail);
    uint32_t next = 0;
    do {
        if (head == tail || tail > size) {
            return false;
        }
        next = (head < size) ? head + 1 : 0;
    } while (!atomic_cas_uint32_loop(&queue->head, &head, next));
    *entry = head;
    return true;
}

// Returns true if queue was empty
static inline uint32_t nvme_queue_enqueue(nvme_queue_t* queue)
{
    uint32_t size = atomic_load_uint32_relax(&queue->size);
    uint32_t tail = atomic_load_uint32_relax(&queue->tail);
    uint32_t next = 0;
    do {
        next = (tail < size) ? tail + 1 : 0;
    } while (!atomic_cas_uint32_loop(&queue->tail, &tail, next));
    return tail;
}

static void nvme_queue_raise_irq(nvme_dev_t* nvme, nvme_queue_t* queue)
{
    uint32_t irq_reg = atomic_load_uint32_relax(&queue->data);
    if (likely(irq_reg & NVME_CQ_FLAGS_IEN)) {
        uint32_t irq_vec = irq_reg >> 16;
        if (!(atomic_load_uint32_relax(&nvme->irq_mask) & bit_set32(irq_vec))) {
            rvvm_pci_raise_irq(nvme->func, irq_vec);
        }
    }
}

static void nvme_queue_lower_irq(nvme_dev_t* nvme, nvme_queue_t* queue)
{
    uint32_t irq_vec = atomic_load_uint32_relax(&queue->data) >> 16;
    rvvm_pci_lower_irq(nvme->func, irq_vec);
}

static void nvme_queue_reset(nvme_queue_t* queue)
{
    atomic_store_uint32_relax(&queue->head, 0);
    atomic_store_uint32_relax(&queue->tail, 0);
}

static void nvme_queue_setup(nvme_queue_t* queue, rvvm_addr_t addr, uint32_t size, uint32_t data)
{
    atomic_store_uint32_relax(&queue->addr_l, addr & ~NVME_PAGE_MASK);
    atomic_store_uint32_relax(&queue->addr_h, addr >> 32);
    atomic_store_uint32_relax(&queue->size, size);
    atomic_store_uint32_relax(&queue->data, data);
    nvme_queue_reset(queue);
}

static void nvme_check_masked_irqs(nvme_dev_t* nvme, uint32_t mask)
{
    for (size_t i = 0; i < STATIC_ARRAY_SIZE(nvme->cq); ++i) {
        nvme_queue_t* queue = &nvme->cq[i];
        if (mask & bit_set32(queue->data >> 16)) {
            if (atomic_load_uint32_relax(&queue->head) != atomic_load_uint32_relax(&queue->tail)) {
                nvme_queue_raise_irq(nvme, queue);
            } else {
                nvme_queue_lower_irq(nvme, queue);
            }
        }
    }
}

static void nvme_reset(nvme_dev_t* nvme)
{
    // Wait for IO workers to halt
    while (atomic_load_uint32(&nvme->threads)) {
        rvvm_sched_yield();
    }
    // Reset queues
    for (size_t qid = 0; qid < STATIC_ARRAY_SIZE(nvme->sq); ++qid) {
        nvme_queue_t* queue = &nvme->sq[qid];
        nvme_queue_reset(queue);
        if (qid) {
            nvme_queue_setup(queue, 0, 0, 0);
        }
    }
    for (size_t qid = 0; qid < STATIC_ARRAY_SIZE(nvme->cq); ++qid) {
        nvme_queue_t* queue = &nvme->cq[qid];
        nvme_queue_lower_irq(nvme, queue);
        nvme_queue_reset(queue);
        if (qid) {
            nvme_queue_setup(queue, 0, 0, 0);
        }
    }
}

static void nvme_complete_cmd_cs(nvme_dev_t* nvme, nvme_cmd_t* cmd, uint32_t status, uint32_t cs)
{
    nvme_queue_t* queue = nvme_get_cq(nvme, cmd->cq_id);
    if (likely(queue)) {
        uint32_t    tail = nvme_queue_enqueue(queue);
        rvvm_addr_t addr = nvme_queue_addr(queue) + (tail << NVME_CQE_SIZE_SHIFT);
        uint8_t*    cqe  = rvvm_pci_get_dma(nvme->func, addr, NVME_CQE_SIZE);
        if (likely(cqe)) {
            uint32_t cmd_id = read_uint16_le(cmd->sqe + NVME_SQE_CID);
            uint32_t phase  = (~read_uint32_le(cqe + NVME_CQE_CID_PB_SF)) & NVME_CQE_PB_MASK;
            uint32_t cid_ps = cmd_id | phase | (status << NVME_CQE_SF_SHIFT);
            write_uint32_le(cqe + NVME_CQE_CS, cs);
            write_uint32_le(cqe + NVME_CQE_SQHD_SQID, cmd->sqhd_sqid);
            atomic_store_uint32_le(cqe + NVME_CQE_CID_PB_SF, cid_ps);
            rvvm_pci_end_dma(nvme->func, cqe);
            nvme_queue_raise_irq(nvme, queue);
        }
    }
}

static inline void nvme_complete_cmd(nvme_dev_t* nvme, nvme_cmd_t* cmd, uint32_t status)
{
    nvme_complete_cmd_cs(nvme, cmd, status, 0);
}

static inline rvvm_addr_t nvme_prepare_prp(nvme_cmd_t* cmd, size_t size)
{
    cmd->prp.prp1 = read_uint64_le(cmd->sqe + NVME_SQE_PRP1);
    cmd->prp.prp2 = read_uint64_le(cmd->sqe + NVME_SQE_PRP2);
    cmd->prp.size = size;
    cmd->prp.cur  = 0;
    return cmd->prp.prp1;
}

static inline size_t nvme_prp_avail(const nvme_cmd_t* cmd)
{
    return cmd->prp.size - cmd->prp.cur;
}

static size_t nvme_parse_prp_region(nvme_dev_t* nvme, nvme_cmd_t* cmd)
{
    nvme_prp_ctx_t* prp = &cmd->prp;
    size_t          len = NVME_PAGE_SIZE;

    if (prp->cur == 0) {
        // Consume the first page from PRP1, may be misaligned
        len = NVME_PAGE_SIZE - (prp->prp1 & NVME_PAGE_MASK);
        if (len >= prp->size) {
            // Single-page region
            return prp->size;
        } else if (prp->size <= len + NVME_PAGE_SIZE) {
            // PRP2 encodes second page address
            rvvm_addr_t page = prp->prp2 & ~NVME_PAGE_MASK;
            if (page == prp->prp1 + len) {
                // Contiguous two-page region
                return prp->size;
            } else {
                // Scattered two-page region
                prp->prp1 = page;
                return len;
            }
        }
    }

    // Process PRP list entries until we reach end of transfer
    rvvm_addr_t prp2 = prp->prp2 & ~7ULL;
    uint8_t*    dma  = NULL;
    while (nvme_prp_avail(cmd) > len) {
        if (!dma) {
            // Obtain DMA mapping of the PRP list
            dma = rvvm_pci_get_dma(nvme->func, prp2 & ~NVME_PAGE_MASK, NVME_PAGE_SIZE);
            if (!dma) {
                // PRP list DMA error
                rvvm_debug("NVMe PRP list DMA error at %#llx", (long long)prp2);
                break;
            }
        }
        if (!((prp2 + 8) & NVME_PAGE_MASK) && nvme_prp_avail(cmd) > len + NVME_PAGE_SIZE) {
            // Fetch next PRP list in the chain
            prp2 = read_uint64_le(dma + NVME_PAGE_SIZE - 8) & ~NVME_PAGE_MASK;
            rvvm_pci_end_dma(nvme->func, dma);
            dma = NULL;
        } else {
            // Fetch next PRP list entry
            rvvm_addr_t page = read_uint64_le(dma + (prp2 & NVME_PAGE_MASK));
            // Advance pointers
            prp2 += 8;
            if (page != (prp->prp1 + len)) {
                // Scattered region
                prp->prp1 = page;
                prp->prp2 = prp2;
                break;
            }
            len += NVME_PAGE_SIZE;
        }
    }
    rvvm_pci_end_dma(nvme->func, dma);

    return EVAL_MIN(len, nvme_prp_avail(cmd));
}

static void* nvme_get_prp_region(nvme_dev_t* nvme, nvme_cmd_t* cmd, size_t* size)
{
    rvvm_addr_t reg_addr = cmd->prp.prp1;
    size_t      reg_size = nvme_parse_prp_region(nvme, cmd);
    if (reg_size) {
        void* region  = rvvm_pci_get_dma(nvme->func, reg_addr, reg_size);
        cmd->prp.cur += reg_size;
        if (region) {
            *size = reg_size;
            return region;
        }
    }
    *size = 0;
    rvvm_debug("NVMe PRP region DMA error at %#llx", (long long)reg_addr);
    return NULL;
}

static void nvme_copy_to_prp(nvme_dev_t* nvme, nvme_cmd_t* cmd, const void* data, size_t size)
{
    while (true) {
        size_t reg_size = 0;
        void*  region   = nvme_get_prp_region(nvme, cmd, &reg_size);
        if (!region) {
            break;
        }
        size_t to_copy = EVAL_MIN(reg_size, size);
        if (to_copy) {
            memcpy(region, data, to_copy);
            region    = (void*)(((uint8_t*)region) + to_copy);
            reg_size -= to_copy;
            data      = (const void*)(((const uint8_t*)data) + to_copy);
            size     -= to_copy;
        }
        if (reg_size) {
            memset(region, 0, reg_size);
        }
        rvvm_pci_end_dma(nvme->func, region);
    }
}

static void nvme_create_io_sq(nvme_dev_t* nvme, nvme_cmd_t* cmd)
{
    rvvm_addr_t   sq_addr = nvme_prepare_prp(cmd, 0);
    uint32_t      sq_id   = read_uint16_le(cmd->sqe + NVME_SQE_CDW10);
    uint32_t      sq_size = read_uint16_le(cmd->sqe + NVME_SQE_CDW10 + 2);
    uint32_t      cq_flag = read_uint16_le(cmd->sqe + NVME_SQE_CDW11);
    uint32_t      cq_id   = read_uint16_le(cmd->sqe + NVME_SQE_CDW11 + 2);
    nvme_queue_t* sq      = nvme_get_sq(nvme, sq_id);
    nvme_queue_t* cq      = nvme_get_cq(nvme, cq_id);
    if (!(cq_flag & NVME_CQ_FLAGS_PC)) {
        // Non-contiguous queue
        nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_FIELD);
    } else if (!sq_size) {
        // Queue size invalid
        nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_QUEUE_SIZE);
    } else if (!sq_id || !sq || nvme_queue_size(sq)) {
        // Submission queue ID invalid or already in use
        nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_QUEUE_ID);
    } else if (!cq || !nvme_queue_size(cq)) {
        // Completion queue invalid
        nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_CQ);
    } else {
        nvme_queue_setup(sq, sq_addr, sq_size, cq_id);
        nvme_complete_cmd(nvme, cmd, NVME_SC_SUCCESS);
    }
}

static void nvme_create_io_cq(nvme_dev_t* nvme, nvme_cmd_t* cmd)
{
    rvvm_addr_t   cq_addr = nvme_prepare_prp(cmd, 0);
    uint32_t      cq_id   = read_uint16_le(cmd->sqe + NVME_SQE_CDW10);
    uint32_t      cq_size = read_uint16_le(cmd->sqe + NVME_SQE_CDW10 + 2);
    uint32_t      cq_flag = read_uint32_le(cmd->sqe + NVME_SQE_CDW11);
    nvme_queue_t* cq      = nvme_get_cq(nvme, cq_id);
    if (!(cq_flag & NVME_CQ_FLAGS_PC)) {
        // Non-contiguous queue
        nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_FIELD);
    } else if (!cq_size) {
        // Queue size invalid
        nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_QUEUE_SIZE);
    } else if (!cq_id || !cq || nvme_queue_size(cq)) {
        // Completion queue ID invalid or already in use
        nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_QUEUE_ID);
    } else {
        nvme_queue_lower_irq(nvme, cq);
        nvme_queue_setup(cq, cq_addr, cq_size, cq_flag);
        nvme_complete_cmd(nvme, cmd, NVME_SC_SUCCESS);
    }
}

static void nvme_delete_io_queue(nvme_dev_t* nvme, nvme_cmd_t* cmd, bool is_cq)
{
    size_t        queue_id = read_uint16_le(cmd->sqe + NVME_SQE_CDW10);
    nvme_queue_t* queue    = is_cq ? nvme_get_cq(nvme, queue_id) : nvme_get_sq(nvme, queue_id);
    if (!queue_id || !queue) {
        // Queue ID invalid
        nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_QUEUE_ID);
    } else {
        if (is_cq) {
            nvme_queue_lower_irq(nvme, queue);
        }
        nvme_queue_setup(queue, 0, 0, 0);
        nvme_complete_cmd(nvme, cmd, NVME_SC_SUCCESS);
    }
}

static void nvme_get_log_page(nvme_dev_t* nvme, nvme_cmd_t* cmd)
{
    uint8_t* buf = safe_new_arr(uint8_t, NVME_PAGE_SIZE);
    uint8_t  log = cmd->sqe[NVME_SQE_CDW10];
    switch (log) {
        case NVME_LOG_ERROR:
        case NVME_LOG_FIRMWARE_SLOT:
            break;
        case NVME_LOG_SMART:
            write_uint16_le_m(&buf[1], 315); // Temperature (In Kelvins)
            write_uint8(&buf[3], 94);        // Available Spare Percent
            write_uint8(&buf[4], 10);        // Available Spare Threshold
            break;
        default:
            rvvm_debug("NVMe log page %#04x unimplemented", log);
            nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_FIELD);
            safe_free(buf);
            return;
    }
    nvme_prepare_prp(cmd, read_uint32_le(&cmd->sqe[NVME_SQE_CDW10]) >> 16);
    nvme_copy_to_prp(nvme, cmd, buf, NVME_PAGE_SIZE);
    nvme_complete_cmd(nvme, cmd, NVME_SC_SUCCESS);
    safe_free(buf);
}

static void nvme_identify(nvme_dev_t* nvme, nvme_cmd_t* cmd)
{
    uint8_t* buf = safe_new_arr(uint8_t, NVME_PAGE_SIZE);
    uint8_t  idt = cmd->sqe[NVME_SQE_CDW10];
    switch (idt) {
        case NVME_CNS_NAMESPACE: {
            // Namespace usage
            uint64_t lbas = rvvm_blk_get_size(nvme->blk) >> NVME_LBA_SHIFT;
            write_uint64_le(&buf[0], lbas);
            write_uint64_le(&buf[8], lbas);
            write_uint64_le(&buf[16], lbas);

            // Namespace features
            buf[33]  = 0x09;           // Deallocated blocks read as zero; Supports Deallocate bit in Write Zeroes
            buf[130] = NVME_LBA_SHIFT; // LBA Format: 512b logical blocks
            break;
        }
        case NVME_CNS_CONTROLLER: {
            // Controller identification
            memcpy(&buf[4], nvme->serial, sizeof(nvme->serial)); // Serial Number
            rvvm_strlcpy((char*)&buf[24], "NVMe Storage", 40);   // Model
            rvvm_strlcpy((char*)&buf[64], "R2579", 8);           // Firmware Revision
            write_uint32_le(&buf[80], NVME_VS_VERSION);          // Version

            // Controller features
            buf[72]  = 0x02; // Recommended Arbitration Burst
            buf[111] = 0x01; // Controller Type: I/O Controller
            buf[512] = 0x66; // Submission Queue Max/Cur Entry Size
            buf[513] = 0x44; // Completion Queue Max/Cur Entry Size
            buf[516] = 0x01; // Number of Namespaces
            buf[520] = 0x04; // Supports Dataset Management (TRIM)
            buf[526] = 0x07; // Atomic Write Unit Normal: 4kb
            buf[528] = 0x07; // Atomic Write Unit Power Fail: 4kb

            // NVMe Qualified Name (Includes serial to distinguish targets)
            size_t nqn_off = rvvm_strlcpy((char*)&buf[768], "nqn.2022-04.lekkit:nvme:", 256);
            memcpy(&buf[768 + nqn_off], nvme->serial, sizeof(nvme->serial));
            break;
        }
        case NVME_CNS_NSID_LIST:
            write_uint32_le(buf, 0x01); // Namespace #1
            break;
        case NVME_CNS_NSID_DESC:
            buf[0] = 0x03; // Namespace uses UUID
            buf[1] = 0x10; // UUID length
            break;
        default:
            rvvm_debug("NVMe identify CNS %#04x unimplemented", idt);
            nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_FIELD);
            safe_free(buf);
            return;
    }
    nvme_prepare_prp(cmd, NVME_PAGE_SIZE);
    nvme_copy_to_prp(nvme, cmd, buf, NVME_PAGE_SIZE);
    nvme_complete_cmd(nvme, cmd, NVME_SC_SUCCESS);
    safe_free(buf);
}

static void nvme_handle_feature(nvme_dev_t* nvme, nvme_cmd_t* cmd, bool set)
{
    uint8_t  feature_id  = cmd->sqe[NVME_SQE_CDW10];
    uint32_t feature_val = 0;
    switch (feature_id) {
        case NVME_FEAT_ARBITRATION:
            feature_val = 0x07;
            break;
        case NVME_FEAT_NUM_QUEUES:
            feature_val = (NVME_IO_QUEUES - 1) | ((NVME_IO_QUEUES - 1) << 16);
            break;
        case NVME_FEAT_TEMP_THRESH:
            if (set) {
                atomic_store_uint32_relax(&nvme->temp_thresh, read_uint32_le(&cmd->sqe[NVME_SQE_CDW11]));
            } else {
                feature_val = atomic_load_uint32_relax(&nvme->temp_thresh);
            }
            break;
        case NVME_FEAT_POWER_MGMT:
        case NVME_FEAT_ERROR_RECOVER:
        case NVME_FEAT_VOLATILE_WC:
        case NVME_FEAT_IRQ_COALESCE:
        case NVME_FEAT_IRQ_VECTOR:
        case NVME_FEAT_WR_ATOMIC:
        case NVME_FEAT_ASYNC_EVENT:
            // Stubs
            break;
        default:
            rvvm_debug("NVMe feature %#04x unimplemented", feature_id);
            nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_FIELD);
            return;
    }
    if (set) {
        nvme_complete_cmd(nvme, cmd, NVME_SC_SUCCESS);
    } else {
        nvme_complete_cmd_cs(nvme, cmd, NVME_SC_SUCCESS, feature_val);
    }
}

static void nvme_admin_cmd(nvme_dev_t* nvme, nvme_cmd_t* cmd)
{
    uint8_t opcode = cmd->sqe[NVME_SQE_CDW0];
    switch (opcode) {
        case NVME_ADM_CREATE_IO_SQ:
            nvme_create_io_sq(nvme, cmd);
            return;
        case NVME_ADM_CREATE_IO_CQ:
            nvme_create_io_cq(nvme, cmd);
            return;
        case NVME_ADM_DELETE_IO_SQ:
        case NVME_ADM_DELETE_IO_CQ:
            nvme_delete_io_queue(nvme, cmd, opcode == NVME_ADM_DELETE_IO_CQ);
            return;
        case NVME_ADM_GET_LOG_PAGE:
            nvme_get_log_page(nvme, cmd);
            return;
        case NVME_ADM_IDENTIFY:
            nvme_identify(nvme, cmd);
            return;
        case NVME_ADM_ABORT:
            nvme_complete_cmd_cs(nvme, cmd, NVME_SC_SUCCESS, 1);
            return;
        case NVME_ADM_SET_FEATURE:
        case NVME_ADM_GET_FEATURE:
            nvme_handle_feature(nvme, cmd, opcode == NVME_ADM_DELETE_IO_CQ);
            return;
            return;
        case NVME_ADM_ASYNC_EVENT_REQ:
            // Nothing ever happens
            return;
        default:
            rvvm_debug("NVMe admin cmd %#04x unimplemented", opcode);
            nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_OPCODE);
            return;
    }
}

static void nvme_io_cmd(nvme_dev_t* nvme, nvme_cmd_t* cmd)
{
    uint8_t opcode = cmd->sqe[NVME_SQE_CDW0];
    switch (opcode) {
        case NVME_IO_READ:
        case NVME_IO_WRITE: {
            uint64_t pos = read_uint64_le(cmd->sqe + NVME_SQE_CDW10) << NVME_LBA_SHIFT;
            size_t   nlb = read_uint16_le(cmd->sqe + NVME_SQE_CDW12);
            nvme_prepare_prp(cmd, (nlb + 1) << NVME_LBA_SHIFT);
            while (nvme_prp_avail(cmd)) {
                size_t size = 0, tmp = 0;
                void*  buffer = nvme_get_prp_region(nvme, cmd, &size);
                if (buffer) {
                    if (opcode == NVME_IO_WRITE) {
                        tmp = rvvm_blk_write(nvme->blk, buffer, size, pos);
                    } else {
                        tmp = rvvm_blk_read(nvme->blk, buffer, size, pos);
                    }
                    rvvm_pci_end_dma(nvme->func, buffer);
                }
                if (tmp != size) {
                    nvme_complete_cmd(nvme, cmd, NVME_SC_DATA_ERR);
                    return;
                }
                pos += size;
            }
            nvme_complete_cmd(nvme, cmd, NVME_SC_SUCCESS);
            break;
        }
        case NVME_IO_FLUSH:
            rvvm_blk_sync(nvme->blk);
            nvme_complete_cmd(nvme, cmd, NVME_SC_SUCCESS);
            break;
        case NVME_IO_DTSM:
            if (cmd->sqe[NVME_SQE_CDW11] & 0x4) {
                // Deallocate (TRIM)
                size_t nr = cmd->sqe[NVME_SQE_CDW10];
                nvme_prepare_prp(cmd, (nr + 1) << 4);
                while (nvme_prp_avail(cmd)) {
                    size_t   size   = 0;
                    uint8_t* buffer = nvme_get_prp_region(nvme, cmd, &size);
                    if (buffer) {
                        for (size_t i = 0; i < size; i += 16) {
                            uint64_t trim_lba = read_uint32_le(buffer + i + 4);
                            uint64_t trim_pos = read_uint64_le(buffer + i + 8) << NVME_LBA_SHIFT;
                            rvvm_blk_trim(nvme->blk, trim_pos, trim_lba << NVME_LBA_SHIFT);
                        }
                        rvvm_pci_end_dma(nvme->func, buffer);
                    }
                }
            }
            nvme_complete_cmd(nvme, cmd, NVME_SC_SUCCESS);
            break;
        default:
            rvvm_debug("NVMe IO cmd %#04x unimplemented", opcode);
            nvme_complete_cmd(nvme, cmd, NVME_SC_BAD_OPCODE);
            break;
    }
}

static void nvme_run_cmd(nvme_dev_t* nvme, size_t sq_id, uint32_t sq_head)
{
    nvme_queue_t* sq       = &nvme->sq[sq_id];
    rvvm_addr_t   sqe_addr = nvme_queue_addr(sq) + (sq_head << NVME_SQE_SIZE_SHIFT);
    uint8_t*      sqe      = rvvm_pci_get_dma(nvme->func, sqe_addr, NVME_SQE_SIZE);

    if (likely(sqe)) {
        nvme_cmd_t cmd = {
            .sqe       = sqe,
            .sqhd_sqid = sq_head | (sq_id << 16),
            .cq_id     = sq->data,
        };
        if (sq_id) {
            nvme_io_cmd(nvme, &cmd);
        } else {
            nvme_admin_cmd(nvme, &cmd);
        }
        rvvm_pci_end_dma(nvme->func, cmd.sqe);
    }
}

static void* nvme_cmd_worker(void** data)
{
    nvme_dev_t* nvme = data[0];
    nvme_run_cmd(nvme, (size_t)data[1], (size_t)data[2]);
    atomic_sub_uint32(&nvme->threads, 1);
    return NULL;
}

static void nvme_drain_sq(nvme_dev_t* nvme, size_t sq_id)
{
    nvme_queue_t* sq   = &nvme->sq[sq_id];
    uint32_t      head = 0;
    while (nvme_queue_dequeue(sq, &head)) {
        if (sq_id) {
            void* args[3] = {
                nvme,
                (void*)sq_id,
                (void*)(size_t)head,
            };
            atomic_add_uint32(&nvme->threads, 1);
            thread_create_task_va(nvme_cmd_worker, args, 3);
        } else {
            nvme_run_cmd(nvme, sq_id, head);
        }
    }
}

static void nvme_doorbell(nvme_dev_t* nvme, uint32_t doorbell, uint16_t val)
{
    uint32_t queue_id = doorbell >> 1;
    if (doorbell & 1) {
        // Update completion queue head
        nvme_queue_t* cq = nvme_get_cq(nvme, queue_id);
        if (likely(cq)) {
            atomic_store_uint32_relax(&cq->head, val);
            if (atomic_load_uint32_relax(&cq->tail) == val) {
                nvme_queue_lower_irq(nvme, cq);
            }
        }
    } else {
        // Update submission queue tail
        nvme_queue_t* sq = nvme_get_sq(nvme, queue_id);
        if (likely(sq)) {
            atomic_store_uint32_relax(&sq->tail, val);
            nvme_drain_sq(nvme, queue_id);
        }
    }
}

static void nvme_pci_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    nvme_dev_t* nvme = rvvm_region_data(dev);
    uint32_t    val  = 0;
    UNUSED(size);

    switch (off) {
        case NVME_REG_CAP1:
            val = NVME_CAP1_MQES | NVME_CAP1_CQR | NVME_CAP1_TO;
            break;
        case NVME_REG_CAP2:
            val = NVME_CAP2_CSS;
            break;
        case NVME_REG_VS:
            val = NVME_VS_VERSION;
            break;
        case NVME_REG_INTMS:
        case NVME_REG_INTMC:
            val = atomic_load_uint32_relax(&nvme->irq_mask);
            break;
        case NVME_REG_CC: {
            uint32_t conf = atomic_load_uint32_relax(&nvme->conf);
            val           = (conf & NVME_CC_EN) | NVME_CC_IOQES;
            break;
        }
        case NVME_REG_CSTS: {
            uint32_t conf = atomic_load_uint32_relax(&nvme->conf);
            // CC.EN  -> CSTS.EN
            // CC.SHN -> CSTS.SHST
            val = (conf & NVME_CSTS_RDY);
            if (conf & NVME_CC_SHN) {
                // CC.SHN -> CSTS.SHST
                val |= NVME_CSTS_SHST;
            }
            break;
        }
        case NVME_REG_AQA:
            val  = atomic_load_uint32_relax(&nvme->sq[NVME_QUEUE_ADMIN].size);
            val |= atomic_load_uint32_relax(&nvme->cq[NVME_QUEUE_ADMIN].size) << 16;
            break;
        case NVME_REG_ASQ1:
            val = atomic_load_uint32_relax(&nvme->sq[NVME_QUEUE_ADMIN].addr_l);
            break;
        case NVME_REG_ASQ2:
            val = atomic_load_uint32_relax(&nvme->sq[NVME_QUEUE_ADMIN].addr_h);
            break;
        case NVME_REG_ACQ1:
            val = atomic_load_uint32_relax(&nvme->cq[NVME_QUEUE_ADMIN].addr_l);
            break;
        case NVME_REG_ACQ2:
            val = atomic_load_uint32_relax(&nvme->cq[NVME_QUEUE_ADMIN].addr_h);
            break;
    }

    write_uint32_le(data, val);
}

static void nvme_pci_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    nvme_dev_t* nvme = rvvm_region_data(dev);
    uint32_t    val  = read_uint32_le(data);
    UNUSED(size);

    if (likely(off >= 0x1000)) {
        // Doorbell write
        nvme_doorbell(nvme, (off - 0x1000) >> 2, val);
        return;
    }

    switch (off) {
        case NVME_REG_INTMS:
            atomic_or_uint32(&nvme->irq_mask, val);
            nvme_check_masked_irqs(nvme, val);
            break;
        case NVME_REG_INTMC:
            atomic_and_uint32(&nvme->irq_mask, ~val);
            nvme_check_masked_irqs(nvme, val);
            break;
        case NVME_REG_CC:
            atomic_store_uint32_relax(&nvme->conf, val);
            if ((val & NVME_CC_SHN) || !(val & NVME_CC_EN)) {
                // Shutdown the controller
                nvme_reset(nvme);
            }
            break;
        case NVME_REG_AQA:
            atomic_store_uint32_relax(&nvme->sq[NVME_QUEUE_ADMIN].size, bit_cut(val, 0, 12));
            atomic_store_uint32_relax(&nvme->cq[NVME_QUEUE_ADMIN].size, bit_cut(val, 16, 12));
            break;
        case NVME_REG_ASQ1:
            atomic_store_uint32_relax(&nvme->sq[NVME_QUEUE_ADMIN].addr_l, val & ~NVME_PAGE_MASK);
            break;
        case NVME_REG_ASQ2:
            atomic_store_uint32_relax(&nvme->sq[NVME_QUEUE_ADMIN].addr_h, val);
            break;
        case NVME_REG_ACQ1:
            atomic_store_uint32_relax(&nvme->cq[NVME_QUEUE_ADMIN].addr_l, val & ~NVME_PAGE_MASK);
            break;
        case NVME_REG_ACQ2:
            atomic_store_uint32_relax(&nvme->cq[NVME_QUEUE_ADMIN].addr_h, val);
            break;
    }
}

static void nvme_cleanup(rvvm_reg_dev_t* dev)
{
    nvme_dev_t* nvme = rvvm_region_data(dev);
    nvme_reset(nvme);
    rvvm_blk_close(nvme->blk);
    free(nvme);
}

static void nvme_queue_suspend(rvvm_snapshot_t* snap, nvme_queue_t* queue)
{
    rvvm_snapshot_field(snap, queue->addr_l);
    rvvm_snapshot_field(snap, queue->addr_h);
    rvvm_snapshot_field(snap, queue->size);
    rvvm_snapshot_field(snap, queue->head);
    rvvm_snapshot_field(snap, queue->tail);
    rvvm_snapshot_field(snap, queue->data);
}

static void nvme_suspend(rvvm_reg_dev_t* dev, rvvm_snapshot_t* snap, bool resume)
{
    nvme_dev_t* nvme = rvvm_region_data(dev);

    // Commands are processed by worker threads, which must be done before
    // the queues are read out or overwritten
    while (atomic_load_uint32(&nvme->threads)) {
        rvvm_sched_yield();
    }

    if (snap) {
        rvvm_snapshot_section(snap, "nvme");
        for (size_t qid = 0; qid < STATIC_ARRAY_SIZE(nvme->sq); ++qid) {
            nvme_queue_suspend(snap, &nvme->sq[qid]);
        }
        for (size_t qid = 0; qid < STATIC_ARRAY_SIZE(nvme->cq); ++qid) {
            nvme_queue_suspend(snap, &nvme->cq[qid]);
        }
        rvvm_snapshot_field(snap, nvme->conf);
        rvvm_snapshot_field(snap, nvme->irq_mask);
        rvvm_snapshot_field(snap, nvme->temp_thresh);
    }
    UNUSED(resume);
}

static rvvm_reg_type_t nvme_type = {
    .name     = "nvme",
    .read     = nvme_pci_read,
    .write    = nvme_pci_write,
    .suspend  = nvme_suspend,
    .cleanup  = nvme_cleanup,
    .min_size = 4,
    .max_size = 4,
};

RVVM_PUBLIC rvvm_pci_func_t* rvvm_nvme_init(rvvm_machine_t* machine, rvvm_blk_dev_t* blk, rvvm_pci_addr_t addr)
{
    nvme_dev_t* nvme = safe_new_obj(nvme_dev_t);

    // Enable IEN on Admin Completion Queue
    nvme->cq[NVME_QUEUE_ADMIN].data = NVME_CQ_FLAGS_IEN;

    nvme->blk = blk;
    rvvm_randomserial(nvme->serial, sizeof(nvme->serial));

    rvvm_reg_desc_t nvme_mmio = {
        .size = 0x4000,
        .data = nvme,
        .type = &nvme_type,
        .attr = RVVM_REG_ATTR_BAR64,
    };
    rvvm_pci_func_desc_t nvme_desc = {
        .vendor_id  = 0x1F31, // Nextorage
        .device_id  = 0x4512, // Nextorage NE1N NVMe SSD
        .class_code = 0x0108, // Mass Storage, Non-Volatile memory controller
        .prog_iface = 0x02,   // NVMe
        .irq_pin    = RVVM_PCI_PIN_INTA,
        .irq_vecs   = NVME_IO_QUEUES,
        .bar[0]     = &nvme_mmio,
    };

    rvvm_pci_func_t* func = rvvm_pci_func_init(machine, &nvme_desc, addr);
    if (func) {
        // Successfully plugged in
        nvme->func = func;
    }
    return func;
}

POP_OPTIMIZATION_SIZE
