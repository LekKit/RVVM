/*
rvvmlib.h - RISC-V Virtual Machine Public API
Copyright (C) 2021  LekKit <github.com/LekKit>
                    cerg2010cerg2010 <github.com/cerg2010cerg2010>
                    Mr0maks <mr.maks0443@gmail.com>
                    KotB <github.com/0xCatPKG>
                    X547 <github.com/X547>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.

Alternatively, the contents of this file may be used under the terms
of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or any later version.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef RVVMLIB_H
#define RVVMLIB_H

#ifdef __cplusplus
#define RVVM_EXTERN_C_BEGIN extern "C" {
#define RVVM_EXTERN_C_END }
#else
#define RVVM_EXTERN_C_BEGIN
#define RVVM_EXTERN_C_END
#endif

RVVM_EXTERN_C_BEGIN

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(_WIN32) && defined(RVVMLIB_SHARED)
#define PUBLIC __declspec(dllimport)
#elif defined(_WIN32) && defined(USE_LIB)
#define PUBLIC __declspec(dllexport)
#elif __GNUC__ >= 4 && (defined(RVVMLIB_SHARED) || defined(USE_LIB))
#define PUBLIC __attribute__((visibility("default")))
#else
// It's a static lib or rvvm-cli
#define PUBLIC
#endif

#ifndef RVVM_VERSION
//! Version string
#define RVVM_VERSION "0.7-git"
#endif

//! Increments on each API/ABI breakage
#define RVVM_ABI_VERSION 7

//! Default memory base address
#define RVVM_DEFAULT_MEMBASE 0x80000000

/*
 * Configurable machine options
 */

#define RVVM_OPT_NONE           0
#define RVVM_OPT_JIT            1 //!< Enable JIT
#define RVVM_OPT_JIT_CACHE      2 //!< Amount of per-core JIT cache (In bytes)
#define RVVM_OPT_JIT_HARVARD    3 //!< No dirty code tracking, explicit ifence, slower
#define RVVM_OPT_VERBOSITY      4 //!< Verbosity level of internal logic
#define RVVM_OPT_HW_IMITATE     5 //!< Imitate traits or identity of physical hardware
#define RVVM_OPT_MAX_CPU_CENT   6 //!< Max CPU load % per guest/host CPUs
#define RVVM_OPT_RESET_PC       7 //!< Physical jump address at reset, defaults to mem_base
#define RVVM_OPT_DTB_ADDR       8 //!< Pass DTB address if non-zero, omits FDT generation
#define RVVM_MAX_OPTS           9

/*
 * Readonly or special machine options
 */

#define RVVM_OPT_MEM_BASE       0x80000001U //!< Physical RAM base address
#define RVVM_OPT_MEM_SIZE       0x80000002U //!< Physical RAM size
#define RVVM_OPT_HART_COUNT     0x80000003U //!< Amount of harts

//! Physical memory address or similar opaque type
typedef uint64_t rvvm_addr_t;

//! Machine handle
typedef struct rvvm_machine_t rvvm_machine_t;

//! MMIO region handle
typedef struct rvvm_mmio_dev_t rvvm_mmio_dev_t;

//! FDT node for Device Tree generation
struct fdt_node;

typedef struct plic    plic_ctx_t;
typedef struct pci_bus pci_bus_t;
typedef struct i2c_bus i2c_bus_t;

//! MMIO device-type specific information and handlers
typedef struct {
    //! Called to free device state (LIFO order), dev->data is simply freed if this is NULL
    void (*remove)(rvvm_mmio_dev_t* dev);

    //! Called periodically from event thread
    void (*update)(rvvm_mmio_dev_t* dev);

    //! Called on machine reset
    void (*reset)(rvvm_mmio_dev_t* dev);

    /*
     * TODO
     * void (*suspend)(rvvm_mmio_dev_t* dev, rvvm_state_t* state);
     * void (*resume)(rvvm_mmio_dev_t* dev, rvvm_state_t* state);
     */

    const char* name; //!< Human-readable device name
} rvvm_mmio_type_t;

//! MMIO read/write handler, offset is always aligned to operation size
typedef bool (*rvvm_mmio_handler_t)(rvvm_mmio_dev_t* dev, void* dest, size_t offset, uint8_t size);

//! Dummy MMIO read/write: Reads zeros, ignores writes, never faults
PUBLIC bool rvvm_mmio_none(rvvm_mmio_dev_t* dev, void* dest, size_t offset, uint8_t size);

//! MMIO region description
struct rvvm_mmio_dev_t {
    rvvm_addr_t addr;        //!< MMIO region address in machine physical memory
    size_t      size;        //!< MMIO region size, zero means a device placeholder
    void*       data;        //!< Device-specific data pointer, freed on removal if type->remove is NULL
    void*       mapping;     //!< Directly mapped host memory region, read/write called on dirtying if non-NULL
    rvvm_machine_t* machine; //!< Owner machine handle

    //! Device class specific operations & info, may be NULL
    const rvvm_mmio_type_t* type;

    //! Called on MMIO region read if non-NULL
    rvvm_mmio_handler_t read;

    //! Called on MMIO region write if non-NULL
    rvvm_mmio_handler_t write;

    uint8_t min_op_size; //!< Minimum MMIO operation size allowed
    uint8_t max_op_size; //!< Maximum MMIO operation size allowed
};

/*
 * Machine API
 */

//! \brief Creates a new virtual machine
//! \param mem_base   Memory base address, usually RVVM_DEFAULT_MEMBASE
//! \param mem_size   Amount of memory (in bytes), should be page-aligned
//! \param hart_count Amount of HARTs (cores)
//! \param rv64       Enables 64-bit RISC-V, otherwise 32-bit machine is created
//! \return Valid machine handle, or NULL on failure
PUBLIC rvvm_machine_t* rvvm_create_machine(rvvm_addr_t mem_base, size_t mem_size, size_t hart_count, bool rv64);

//! \brief  Writes data to machine physical memory
PUBLIC bool rvvm_write_ram(rvvm_machine_t* machine, rvvm_addr_t dest, const void* src, size_t size);

//! \brief  Reads data from machine physical memory
PUBLIC bool rvvm_read_ram(rvvm_machine_t* machine, void* dest, rvvm_addr_t src, size_t size);

//! \brief  Directly access machine physical memory (DMA)
//! \return Pointer to machine DMA region, or NULL on failure
PUBLIC void* rvvm_get_dma_ptr(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);

/*
 * Get/Set default PLIC, PCI bus for this machine
 * Newly created ones are selected automatically
 */
PUBLIC plic_ctx_t* rvvm_get_plic(rvvm_machine_t* machine);
PUBLIC void        rvvm_set_plic(rvvm_machine_t* machine, plic_ctx_t* plic);
PUBLIC pci_bus_t*  rvvm_get_pci_bus(rvvm_machine_t* machine);
PUBLIC void        rvvm_set_pci_bus(rvvm_machine_t* machine, pci_bus_t* pci_bus);
PUBLIC i2c_bus_t*  rvvm_get_i2c_bus(rvvm_machine_t* machine);
PUBLIC void        rvvm_set_i2c_bus(rvvm_machine_t* machine, i2c_bus_t* i2c_bus);

//! \brief Get root FDT node for FDT generation
PUBLIC struct fdt_node* rvvm_get_fdt_root(rvvm_machine_t* machine);

//! \brief Get /soc FDT node for FDT generation
PUBLIC struct fdt_node* rvvm_get_fdt_soc(rvvm_machine_t* machine);

//! \brief Set a kernel cmdline for a manually loaded kernel
PUBLIC void rvvm_set_cmdline(rvvm_machine_t* machine, const char* str);

//! \brief Append to the kernel cmdline for a manually loaded kernel
PUBLIC void rvvm_append_cmdline(rvvm_machine_t* machine, const char* str);

//! \brief Load M-mode firmware (bootrom), which is executed from RAM base on reset
PUBLIC bool rvvm_load_bootrom(rvvm_machine_t* machine, const char* path);

//! \brief Load S-mode payload (kernel), which is usually the next stage after OpenSBI
PUBLIC bool rvvm_load_kernel(rvvm_machine_t* machine, const char* path);

//! \brief Load a custom Device Tree blob, which is passed to guest at reset.
PUBLIC bool rvvm_load_dtb(rvvm_machine_t* machine, const char* path);

//! \brief Dump generated Device Tree to a file
PUBLIC bool rvvm_dump_dtb(rvvm_machine_t* machine, const char* path);

//! \brief Get machine option value
PUBLIC rvvm_addr_t rvvm_get_opt(rvvm_machine_t* machine, uint32_t opt);

//! \brief Set machine option
PUBLIC bool rvvm_set_opt(rvvm_machine_t* machine, uint32_t opt, rvvm_addr_t val);

//! \brief  Powers up or resumes a paused machine
//! \return Machine start success, false if it was already running
PUBLIC bool rvvm_start_machine(rvvm_machine_t* machine);

//! \brief  Pauses the machine, stops the vCPUs
//! \return Machine pause success, false if it wasn't running
PUBLIC bool rvvm_pause_machine(rvvm_machine_t* machine);

//! \brief  Reset the machine (Continues running if it was powered)
PUBLIC void rvvm_reset_machine(rvvm_machine_t* machine, bool reset);

//! \brief  Returns true if the machine is running and not paused
PUBLIC bool rvvm_machine_running(rvvm_machine_t* machine);

//! \brief  Returns true if the machine is powered on (Even when it's paused)
PUBLIC bool rvvm_machine_powered(rvvm_machine_t* machine);

//! \brief   Complete cleanup (Frees memory, attached devices, other structures)
//! \warning After return, none of the handles previously attached to this machine are valid
PUBLIC void rvvm_free_machine(rvvm_machine_t* machine);

//! \brief  Get usable MMIO zone
//! \return Usable region in physical memory, which is equal to addr if possible
PUBLIC rvvm_addr_t rvvm_mmio_zone_auto(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);

//! \brief   Attach MMIO device to the machine by it's description, free it's state on failure
//! \param   mmio MMIO region description, doesn't need to be kept
//! \return  Valid MMIO region handle, or NULL on failure
//! \warning Dereferencing rvvm_mmio_dev_t* is only safe when machine is paused/powered off
PUBLIC rvvm_mmio_dev_t* rvvm_attach_mmio(rvvm_machine_t* machine, const rvvm_mmio_dev_t* mmio);

//! \brief Detach MMIO device from the owning machine, free it's state
PUBLIC void rvvm_remove_mmio(rvvm_mmio_dev_t* mmio);

//! \brief Run the event loop in the calling thread, returns when any machine stops
PUBLIC void rvvm_run_eventloop();

/*
 * Userland Emulation API (WIP)
 */

//! Thread handle
typedef struct rvvm_hart_t rvvm_hart_t;

//! Base X0 register index
#define RVVM_REGID_X0    0

//! Base F0 FPU register index, FPU regs are operated in binary form
#define RVVM_REGID_F0    32

//! Program counter register
#define RVVM_REGID_PC    1024

//! CSR cause register
#define RVVM_REGID_CAUSE 1025

//! CSR tval register
#define RVVM_REGID_TVAL  1026

//! \brief Create a userland process context
PUBLIC rvvm_machine_t* rvvm_create_userland(bool rv64);

//! \brief Flush instruction cache for a specified memory range
PUBLIC void rvvm_flush_icache(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);

//! \brief Create userland process thread
PUBLIC rvvm_hart_t* rvvm_create_user_thread(rvvm_machine_t* machine);

//! \brief Destroy userland process thread
PUBLIC void rvvm_free_user_thread(rvvm_hart_t* thread);

//! \brief  Run a userland thread until a trap happens.
//! \return Returns trap cause. PC points to faulty instruction upon return.
PUBLIC rvvm_addr_t rvvm_run_user_thread(rvvm_hart_t* thread);

//! \brief Read thread context register
PUBLIC rvvm_addr_t rvvm_read_cpu_reg(rvvm_hart_t* thread, size_t reg_id);

//! \brief Write thread context register
PUBLIC void rvvm_write_cpu_reg(rvvm_hart_t* thread, size_t reg_id, rvvm_addr_t reg);

RVVM_EXTERN_C_END

#endif
