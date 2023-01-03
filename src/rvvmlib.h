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
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(USE_LIB) && defined(_WIN32)
#define PUBLIC __declspec(dllexport)
#elif defined(RVVM_DLL) && defined(_WIN32)
#define PUBLIC __declspec(dllimport)
#elif defined(USE_LIB) && defined(__GNUC__)
#define PUBLIC __attribute__((visibility("default")))
#else
#define PUBLIC
#endif

#ifndef RVVM_VERSION
#define RVVM_VERSION "0.5"
#endif
#define RVVM_ABI_VERSION     3
#define RVVM_DEFAULT_MEMBASE 0x80000000

// ERRNO
typedef int rvvm_errno_t;

#define RVVM_OK ((rvvm_errno_t)0)

#undef _RV_ERRNO
//

typedef struct rvvm_machine_t rvvm_machine_t;

typedef struct plic    plic_ctx_t;
typedef struct pci_bus pci_bus_t;
typedef struct i2c_bus i2c_bus_t;

struct fdt_node;

typedef uint64_t rvvm_addr_t;

typedef struct rvvm_mmio_dev_t rvvm_mmio_dev_t;
typedef int rvvm_mmio_handle_t;

#define RVVM_INVALID_MMIO (-1)

typedef struct {
    void (*remove)(rvvm_mmio_dev_t* dev);
    void (*update)(rvvm_mmio_dev_t* dev);
    void (*reset)(rvvm_mmio_dev_t* dev);
    /* TODO
     * void (*suspend)(rvvm_mmio_dev_t* dev, rvvm_state_t* state);
     * void (*resume)(rvvm_mmio_dev_t* dev, rvvm_state_t* state);
     */
    const char* name;
} rvvm_mmio_type_t;

typedef bool (*rvvm_mmio_handler_t)(rvvm_mmio_dev_t* dev, void* dest, size_t offset, uint8_t size);

// Reads zeros, ignores writes, never faults
PUBLIC bool rvvm_mmio_none(rvvm_mmio_dev_t* dev, void* dest, size_t offset, uint8_t size);

struct rvvm_mmio_dev_t {
    rvvm_addr_t addr;        // MMIO region address in physical memory
    size_t      size;        // Size of the MMIO region, size zero means a device placeholder
    void*       data;        // Device-specific data, or pointer to memory (For native memory regions)
    rvvm_machine_t* machine; // Parent machine

    // Device class specific operations & info
    const rvvm_mmio_type_t* type;

    // MMIO operations handlers
    // If these aren't NULL then this is a MMIO device
    // If these are NULL, then the device data is directly mapped into guest memory
    // Hint: setting read to NULL and write to rvvm_mmio_none makes memory mapping read-only, etc
    rvvm_mmio_handler_t read;
    rvvm_mmio_handler_t write;

    // MMIO operations attributes (Alignment & min op size, max op size), always power of 2
    // Any non-conforming operation is fixed on the fly before the handlers are invoked.
    uint8_t min_op_size; // Dictates alignment as well
    uint8_t max_op_size;
};

typedef bool (*rvvm_reset_handler_t)(rvvm_machine_t* machine, void* data, bool reset);

// Memory starts at 0x80000000 by default, machine boots from there as well
PUBLIC rvvm_machine_t* rvvm_create_machine(rvvm_addr_t mem_base, size_t mem_size, size_t hart_count, bool rv64);

// Copy to/from physical memory (Returns true on success)
PUBLIC bool rvvm_write_ram(rvvm_machine_t* machine, rvvm_addr_t dest, const void* src, size_t size);
PUBLIC bool rvvm_read_ram(rvvm_machine_t* machine, void* dest, rvvm_addr_t src, size_t size);

// Directly access physical memory (Returns non-NULL on success)
PUBLIC void* rvvm_get_dma_ptr(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);

// Flush instruction cache for a specified physical/user memory range.
// This is useful for userspace emulation of syscalls like __riscv_flush_icache (Linux, etc).
// For machines, this is not needed unless your guest is broken or you bypass DMA APIs.
PUBLIC void rvvm_flush_icache(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);

// Get/Set default PLIC, PCI bus for this machine
// Newly created ones are selected automatically
PUBLIC plic_ctx_t* rvvm_get_plic(rvvm_machine_t* machine);
PUBLIC void        rvvm_set_plic(rvvm_machine_t* machine, plic_ctx_t* plic);
PUBLIC pci_bus_t*  rvvm_get_pci_bus(rvvm_machine_t* machine);
PUBLIC void        rvvm_set_pci_bus(rvvm_machine_t* machine, pci_bus_t* pci_bus);
PUBLIC i2c_bus_t*  rvvm_get_i2c_bus(rvvm_machine_t* machine);
PUBLIC void        rvvm_set_i2c_bus(rvvm_machine_t* machine, i2c_bus_t* i2c_bus);

// Get FDT nodes for FDT generation
PUBLIC struct fdt_node* rvvm_get_fdt_root(rvvm_machine_t* machine);
PUBLIC struct fdt_node* rvvm_get_fdt_soc(rvvm_machine_t* machine);

// Pass physical address of Device Tree Binary (Skips FDT generation)
// To revert this, pass dtb_addr = 0
PUBLIC void rvvm_set_dtb_addr(rvvm_machine_t* machine, rvvm_addr_t dtb_addr);

// Manipulate cmdline passed to guest kernel in FDT prop /chosen/bootargs
PUBLIC void rvvm_cmdline_set(rvvm_machine_t* machine, const char* str);
PUBLIC void rvvm_cmdline_append(rvvm_machine_t* machine, const char* str);

// Set up handler & userdata to be called when the VM performs reset/shutdown
// Returning false from handler cancels reset
PUBLIC void rvvm_set_reset_handler(rvvm_machine_t* machine, rvvm_reset_handler_t handler, void* data);

// Load bootrom, kernel, device tree binaries into RAM (Handles reset as well)
PUBLIC bool rvvm_load_bootrom(rvvm_machine_t* machine, const char* path);
PUBLIC bool rvvm_load_kernel(rvvm_machine_t* machine, const char* path);
PUBLIC bool rvvm_load_dtb(rvvm_machine_t* machine, const char* path);

// Dump generated device tree to a file
PUBLIC bool rvvm_dump_dtb(rvvm_machine_t* machine, const char* path);

// Spawns CPU threads and continues machine execution
// Returns false if the machine is already running
PUBLIC bool rvvm_start_machine(rvvm_machine_t* machine);

// Stops the CPUs, the machine is frozen upon return
// Returns false if the machine isn't yet running
PUBLIC bool rvvm_pause_machine(rvvm_machine_t* machine);

// Reset/shutdown the machine
PUBLIC void rvvm_reset_machine(rvvm_machine_t* machine, bool reset);

// Returns true if the machine is powered on (Even when it's paused)
PUBLIC bool rvvm_machine_powered_on(rvvm_machine_t* machine);

// Complete cleanup (Frees memory, devices data, VM structures)
PUBLIC void rvvm_free_machine(rvvm_machine_t* machine);

// Get near MMIO zone if the one specified is busy (Before attaching device, for example)
// Returns addr if the specified zone is usable
PUBLIC rvvm_addr_t rvvm_mmio_zone_auto(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);

// Connect MMIO devices to the machine
// Returns:
// - Success: Non-negative (>= 0) device handle
// - Invalid region: RVVM_INVALID_MMIO,
//   frees the device state as if the machine was shut down
PUBLIC rvvm_mmio_handle_t rvvm_attach_mmio(rvvm_machine_t* machine, const rvvm_mmio_dev_t* mmio);

// Detach MMIO device from the machine, optionally freeing the device state
PUBLIC void rvvm_detach_mmio(rvvm_machine_t* machine, rvvm_addr_t mmio_addr, bool cleanup);

// Manipulate attached MMIO device by handle, may be done on a running VM
// Returns:
// - Success: non-NULL pointer to the `rvvm_mmio_dev_t`
// - Invalid handle: NULL pointer
PUBLIC rvvm_mmio_dev_t* rvvm_get_mmio(rvvm_machine_t* machine, rvvm_mmio_handle_t handle);

// Allows to disable the internal eventloop thread and offload it somewhere
PUBLIC void rvvm_enable_builtin_eventloop(bool enabled);

// Returns when all machines are stopped
// For self-contained VMs this should be used in main thread
PUBLIC void rvvm_run_eventloop();

//
// Error reporting.
// Error reporting is done through the `errno` pattern,
// errno is thread-local, so you can observe errno changes
// only if it was changed in the same thread
//

// Set error code
PUBLIC void         rvvm_set_errno(rvvm_errno_t errno);

// Clear error code. Same as calling `rvvm_set_errno(RVVM_OK)`
PUBLIC void         rvvm_clear_errno();

// Get error code.
PUBLIC rvvm_errno_t rvvm_get_errno();

// Convert error to string.
//
// Returns:
// - Success: non-null pointer to the static nul-terminated string
// - Error: null pointer, indicates that error code is not defined
PUBLIC const char*  rvvm_errno_strerror(rvvm_errno_t errno);

//
// Userland Emulation API (WIP)
//

typedef void* rvvm_cpu_handle_t;

#define RVVM_REGID_X0    0
// FP registers are operated on in their binary form
#define RVVM_REGID_F0    32
// Reserved range for more kinds of registers
#define RVVM_REGID_PC    1024
#define RVVM_REGID_CAUSE 1025
#define RVVM_REGID_TVAL  1026

// Create a userland context
// The created machine interacts with host process memory directly,
// delegates traps to outside code. No harts are created initially.
PUBLIC rvvm_machine_t* rvvm_create_userland(bool rv64);

// Manage userland threads (Internally, they are RVVM harts)
PUBLIC rvvm_cpu_handle_t rvvm_create_user_thread(rvvm_machine_t* machine);
PUBLIC void rvvm_free_user_thread(rvvm_cpu_handle_t cpu);

// Run a userland thread until a trap happens. Returns trap cause.
PUBLIC rvvm_addr_t rvvm_run_user_thread(rvvm_cpu_handle_t cpu);

PUBLIC rvvm_addr_t rvvm_read_cpu_reg(rvvm_cpu_handle_t cpu, size_t reg_id);
PUBLIC void rvvm_write_cpu_reg(rvvm_cpu_handle_t cpu, size_t reg_id, rvvm_addr_t reg);

#ifdef __cplusplus
}
#endif

#endif
