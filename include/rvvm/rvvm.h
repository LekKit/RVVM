/*
<rvvm/rvvm.h> - RVVM Public API
Copyright (C) 2020-2025 LekKit <github.com/LekKit>
                        cerg2010cerg2010 <github.com/cerg2010cerg2010>
                        Mr0maks <mr.maks0443@gmail.com>
                        KotB <github.com/0xCatPKG>
                        X512 <github.com/X547>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _RVVM_PUBLIC_API_H
#define _RVVM_PUBLIC_API_H

#include <rvvm/rvvm_base.h>

#include <rvvm/rvvm_irq.h>

RVVM_EXTERN_C_BEGIN

/**
 * @defgroup rvvm_machine_api RVVM Machine API
 * @addtogroup rvvm_machine_api
 * @{
 */

/*
 * Machine options (Settable)
 */

#define RVVM_OPT_NONE         0x00 /**< Invalid option                                         */
#define RVVM_OPT_RESET_PC     0x01 /**< Physical jump address at reset, defaults to 0x80000000 */
#define RVVM_OPT_FDT_ADDR     0x02 /**< Physical FDT address at reset for manual FDT loading   */
#define RVVM_OPT_TIME_FREQ    0x03 /**< Machine timer frequency, 10Mhz by default              */
#define RVVM_OPT_HW_IMITATE   0x04 /**< Imitate traits or identity of physical hardware        */
#define RVVM_OPT_MAX_CPU_CENT 0x05 /**< Maximum CPU load % per guest/host CPUs                 */
#define RVVM_OPT_JIT          0x06 /**< Enable JIT                                             */
#define RVVM_OPT_JIT_CACHE    0x07 /**< Amount of per-cpu JIT cache (In bytes)                 */
#define RVVM_OPT_JIT_HARVARD  0x08 /**< No dirty code tracking, explicit ifence, slower        */

/*
 * Machine options (Special or read-only)
 */

#define RVVM_OPT_MEM_BASE     0x80000001U /**< Physical RAM base address, Default: 0x80000000 */
#define RVVM_OPT_MEM_SIZE     0x80000002U /**< Physical RAM size                              */
#define RVVM_OPT_CPU_COUNT    0x80000003U /**< Amount of CPUs                                 */

/**
 * MMIO access handler
 * \param dev    Device handle
 * \param dest   Pointer to load/store data
 * \param offset Offset within device region, always aligned to size
 * \param size   Access size
 * \return True if access was handled by this device
 */
typedef bool (*rvvm_mmio_handler_t)(rvvm_mmio_dev_t* dev, void* dest, size_t offset, uint8_t size);

/**
 * Check librvvm ABI compatibility via rvvm_check_abi(RVVM_ABI_VERSION)
 * \return True if librvvm supports the requested ABI
 */
RVVM_PUBLIC bool rvvm_check_abi(int abi);

/**
 * Creates a new virtual machine
 *
 * \param mem_size   Amount of memory (in bytes), should be page-aligned
 * \param hart_count Amount of HARTs (cores), should be >=1
 * \param isa        String describing the CPU instruction set, or NULL to pick rv64
 * \return Valid machine handle, or NULL on failure
 */
RVVM_PUBLIC rvvm_machine_t* rvvm_create_machine(size_t mem_size, size_t hart_count, const char* isa);

/**
 * Set a new kernel cmdline passed to the guest kernel
 */
RVVM_PUBLIC void rvvm_set_cmdline(rvvm_machine_t* machine, const char* str);

/**
 * Append to the kernel cmdline
 */
RVVM_PUBLIC void rvvm_append_cmdline(rvvm_machine_t* machine, const char* str);

/**
 * Load machine firmware, which is executed from RVVM_OPT_RESET_PC
 */
RVVM_PUBLIC bool rvvm_load_firmware(rvvm_machine_t* machine, const char* path);

/**
 * Load kernel payload, which is usually the next stage after the firmware
 */
RVVM_PUBLIC bool rvvm_load_kernel(rvvm_machine_t* machine, const char* path);

/**
 * Load a custom Flattened Device Tree, which is passed to guest at reset
 */
RVVM_PUBLIC bool rvvm_load_fdt(rvvm_machine_t* machine, const char* path);

/**
 * Dump generated Flattened Device Tree to file
 */
RVVM_PUBLIC bool rvvm_dump_fdt(rvvm_machine_t* machine, const char* path);

/**
 * Get machine option value
 */
RVVM_PUBLIC rvvm_addr_t rvvm_get_opt(rvvm_machine_t* machine, uint32_t opt);

/**
 * Set machine option
 */
RVVM_PUBLIC bool rvvm_set_opt(rvvm_machine_t* machine, uint32_t opt, rvvm_addr_t val);

/**
 * Powers up or resumes a paused machine, returns immediately
 *
 * \return Machine start success, false if it was already running
 */
RVVM_PUBLIC bool rvvm_start_machine(rvvm_machine_t* machine);

/**
 * Pauses the machine (Stops the vCPUs)
 *
 * \return Machine pause success, false if it wasn't running yet
 */
RVVM_PUBLIC bool rvvm_pause_machine(rvvm_machine_t* machine);

/**
 * Reset the machine (Continues running if it was powered)
 */
RVVM_PUBLIC void rvvm_reset_machine(rvvm_machine_t* machine, bool reset);

/**
 * Check whether machine is currently running
 */
RVVM_PUBLIC bool rvvm_machine_running(rvvm_machine_t* machine);

/**
 * Check whether machine is powered on (Even when it's paused)
 */
RVVM_PUBLIC bool rvvm_machine_powered(rvvm_machine_t* machine);

/**
 * Full machine state cleanup (Frees memory, attached devices, internal structures)
 *
 * \warning After this call, none of the handles previously attached to this machine are valid
 */
RVVM_PUBLIC void rvvm_free_machine(rvvm_machine_t* machine);

/**
 * Run the event loop in the calling thread, returns when any machine is paused or powered off
 */
RVVM_PUBLIC void rvvm_run_eventloop(void);

/** @}*/

/**
 * @defgroup rvvm_device_api RVVM Device API
 * @addtogroup rvvm_device_api
 * @{
 */

/**
 * Dummy MMIO handler: Reads zeros, ignores writes, never faults
 */
RVVM_PUBLIC bool rvvm_mmio_none(rvvm_mmio_dev_t* dev, void* dest, size_t offset, uint8_t size);

/**
 * MMIO device class information and handlers (Cleanup, reset, serialize)
 */
typedef struct {
    /** Device identifier name */
    const char* name;

    /** Called to free device state (LIFO order), dev->data is simply freed if this is NULL */
    void (*remove)(rvvm_mmio_dev_t* dev);

    /** Called periodically from event thread */
    void (*update)(rvvm_mmio_dev_t* dev);

    /** Called on machine reset */
    void (*reset)(rvvm_mmio_dev_t* dev);

    /** Called to serialize/deserialize device state on a paused machine */
    void (*suspend)(rvvm_mmio_dev_t* dev, rvvm_snapshot_t* snap, bool resume);

} rvvm_mmio_type_t;

/**
 * MMIO device region description
 */
struct rvvm_mmio_dev_t {
    /** MMIO region address in machine physical memory */
    rvvm_addr_t addr;

    /** MMIO region size, zero means a placeholder */
    size_t size;

    /** Private device data, free()'d on removal if dev->type->remove is NULL */
    void* data;

    /**
     * Directly mapped host memory region.
     * If this is non-NULL, read/write handlers are only called on page dirtying
     */
    void* mapping;

    /** Owner machine handle, filled on attach */
    rvvm_machine_t* machine;

    /** Device class description */
    const rvvm_mmio_type_t* type;

    /** Called on MMIO region read if non-NULL */
    rvvm_mmio_handler_t read;

    /** Called on MMIO region write if non-NULL */
    rvvm_mmio_handler_t write;

    /** Minimum MMIO operation size allowed */
    uint8_t min_op_size;

    /** Maximum MMIO operation size allowed */
    uint8_t max_op_size;
};

/**
 * Writes data to machine physical memory
 */
RVVM_PUBLIC bool rvvm_write_ram(rvvm_machine_t* machine, rvvm_addr_t dest, const void* src, size_t size);

/**
 * Reads data from machine physical memory
 */
RVVM_PUBLIC bool rvvm_read_ram(rvvm_machine_t* machine, void* dest, rvvm_addr_t src, size_t size);

/**
 * Directly access machine physical memory (DMA)
 *
 * \return Pointer to machine DMA region, or NULL on failure
 */
RVVM_PUBLIC void* rvvm_get_dma_ptr(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);

/**
 * Get usable address for a MMIO region
 *
 * \return Usable physical memory address, which is equal to addr if the requested region is free
 */
RVVM_PUBLIC rvvm_addr_t rvvm_mmio_zone_auto(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);

/**
 * Attach MMIO device to the machine by it's description, free it's state on failure
 *
 * \param   machine   Machine to attach the device to
 * \param   mmio_desc MMIO region description, gets copied internally
 * \return  Attached MMIO region handle, or NULL on failure
 * \warning Dereferencing an attached rvvm_mmio_dev_t* should be done thread-safely using atomics/fences
 */
RVVM_PUBLIC rvvm_mmio_dev_t* rvvm_attach_mmio(rvvm_machine_t* machine, const rvvm_mmio_dev_t* mmio_desc);

/**
 * Detach (pull out) MMIO device from the owning machine, free it's state
 */
RVVM_PUBLIC void rvvm_remove_mmio(rvvm_mmio_dev_t* mmio_dev);

/**
 * Clean up MMIO device which is not attached to any machine
 */
RVVM_PUBLIC void rvvm_cleanup_mmio_desc(const rvvm_mmio_dev_t* mmio_desc);

/**
 * Get wired interrupt controller handle of this machine or NULL
 */
RVVM_PUBLIC rvvm_intc_t* rvvm_get_intc(rvvm_machine_t* machine);
RVVM_PUBLIC void         rvvm_set_intc(rvvm_machine_t* machine, rvvm_intc_t* intc);

/**
 * Get PCI rooot complex handle of this machine or NULL
 */
RVVM_PUBLIC pci_bus_t* rvvm_get_pci_bus(rvvm_machine_t* machine);
RVVM_PUBLIC void       rvvm_set_pci_bus(rvvm_machine_t* machine, pci_bus_t* pci_bus);

/**
 * Get I2C bus handle of this machine, or NULL
 */
RVVM_PUBLIC i2c_bus_t* rvvm_get_i2c_bus(rvvm_machine_t* machine);
RVVM_PUBLIC void       rvvm_set_i2c_bus(rvvm_machine_t* machine, i2c_bus_t* i2c_bus);

/**
 * Get root FDT node (For FDT device description generation)
 */
RVVM_PUBLIC rvvm_fdt_node_t* rvvm_get_fdt_root(rvvm_machine_t* machine);

/**
 * Get /soc FDT node (For FDT device description generation)
 */
RVVM_PUBLIC rvvm_fdt_node_t* rvvm_get_fdt_soc(rvvm_machine_t* machine);

/** @}*/

/**
 * @defgroup rvvm_irq_legacy Legacy interrrupt backport
 * @addtogroup rvvm_irq_legacy
 * @{
 */

/**
 * Sends an MSI IRQ by issuing a 32-bit little-endian memory write
 */
RVVM_PUBLIC bool rvvm_send_msi_irq(rvvm_machine_t* machine, rvvm_addr_t addr, uint32_t val);

/**
 * Attach an MSI IRQ controller, semantically almost same as rvvm_attach_mmio(), not removable
 */
RVVM_PUBLIC bool rvvm_attach_msi_target(rvvm_machine_t* machine, const rvvm_mmio_dev_t* mmio_desc);

#define rvvm_alloc_irq(intc)                       rvvm_irq_alloc(intc, 0)
#define rvvm_send_irq(intc, irq)                   rvvm_irq_send(intc, irq)
#define rvvm_raise_irq(intc, irq)                  rvvm_irq_raise(intc, irq)
#define rvvm_lower_irq(intc, irq)                  rvvm_irq_lower(intc, irq)
#define rvvm_fdt_intc_phandle(intc)                rvvm_irq_fdt_phandle(intc)
#define rvvm_fdt_describe_irq(fdt, intc, irq)      rvvm_irq_fdt_describe(fdt, intc, irq)
#define rvvm_fdt_irq_cells(intc, irq, cells, size) rvvm_irq_fdt_cells(intc, irq, cells, size)

/** @}*/

/**
 * @defgroup rvvm_userland_api RVVM Userland Emulation API
 * @addtogroup rvvm_userland_api
 * @{
 */

#define RVVM_REGID_X0                              0
#define RVVM_REGID_F0                              32
#define RVVM_REGID_PC                              1024
#define RVVM_REGID_CAUSE                           1025
#define RVVM_REGID_TVAL                            1026

/**
 * Create a userland process context
 */
RVVM_PUBLIC rvvm_machine_t* rvvm_create_userland(const char* isa);

/**
 * Flush instruction cache for a specified memory range
 */
RVVM_PUBLIC void rvvm_flush_icache(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);

/**
 * Create userland process thread
 */
RVVM_PUBLIC rvvm_cpu_t* rvvm_create_user_thread(rvvm_machine_t* machine);

/**
 * Destroy userland process thread
 */
RVVM_PUBLIC void rvvm_free_user_thread(rvvm_cpu_t* thread);

/**
 * Run a userland thread until a trap happens
 *
 * \return Returns trap cause. PC points to faulty instruction upon return
 */
RVVM_PUBLIC rvvm_addr_t rvvm_run_user_thread(rvvm_cpu_t* thread);

/**
 * Read thread context register
 */
RVVM_PUBLIC rvvm_addr_t rvvm_read_cpu_reg(rvvm_cpu_t* thread, size_t reg_id);

/**
 * Write thread context register
 */
RVVM_PUBLIC void rvvm_write_cpu_reg(rvvm_cpu_t* thread, size_t reg_id, rvvm_addr_t reg);

/** @}*/

RVVM_EXTERN_C_END

#endif
