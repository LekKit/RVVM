/*
rvvmlib.h - RISC-V Virtual Machine Public API
Copyright (C) 2021  LekKit <github.com/LekKit>
                    cerg2010cerg2010 <github.com/cerg2010cerg2010>
                    Mr0maks <mr.maks0443@gmail.com>
                    KotB <github.com/0xCatPKG>

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

#ifndef RVVMLIB_H
#define RVVMLIB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef USE_LIB
#if defined(__GNUC__) || defined(__llvm__) || defined(__INTEL_COMPILER)
#define PUBLIC        __attribute__((visibility("default")))
#define HIDDEN        __attribute__((visibility("hidden")))
#elif defined(_WIN32)
#define PUBLIC        __declspec(dllexport)
#define HIDDEN
#endif
#endif

#ifndef PUBLIC
#define PUBLIC
#define HIDDEN
#endif

#define RVVM_ABI_VERSION     3
#define RVVM_DEFAULT_MEMBASE 0x80000000

typedef struct rvvm_machine_t rvvm_machine_t;

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

PUBLIC bool rvvm_mmio_none(rvvm_mmio_dev_t* dev, void* dest, size_t offset, uint8_t size);

struct rvvm_mmio_dev_t {
    rvvm_addr_t addr;         // MMIO region address in physical memory
    size_t size;              // Size of the MMIO region, size zero means a device placeholder
    void* data;               // Device-specific data, or pointer to memory (for native memory regions)
    rvvm_machine_t* machine;  // Parent machine
    rvvm_mmio_type_t* type;   // Device-specific operations & info

    // MMIO operations handlers, if these aren't NULL then this is a MMIO device
    // Hint: setting read to NULL and write to rvvm_mmio_none makes memory mapping read-only, etc
    rvvm_mmio_handler_t read;
    rvvm_mmio_handler_t write;

    // MMIO operations attributes (alignment & min op size, max op size), always power of 2
    // Any non-conforming operation is fixed on the fly before the handlers are invoked.
    uint8_t min_op_size; // Dictates alignment as well
    uint8_t max_op_size;
};

typedef bool (*rvvm_reset_handler_t)(rvvm_machine_t* machine, void* data, bool reset);

// Memory starts at 0x80000000 by default, machine boots from there as well
PUBLIC rvvm_machine_t* rvvm_create_machine(rvvm_addr_t mem_base, size_t mem_size, size_t hart_count, bool rv64);

// Copy to/from physical memory (returns true on success)
PUBLIC bool rvvm_write_ram(rvvm_machine_t* machine, rvvm_addr_t dest, const void* src, size_t size);
PUBLIC bool rvvm_read_ram(rvvm_machine_t* machine, void* dest, rvvm_addr_t src, size_t size);

// Directly access physical memory (returns non-NULL on success)
PUBLIC void* rvvm_get_dma_ptr(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);

// Get FDT nodes for FDT generation
PUBLIC struct fdt_node* rvvm_get_fdt_root(rvvm_machine_t* machine);
PUBLIC struct fdt_node* rvvm_get_fdt_soc(rvvm_machine_t* machine);

// Pass physical address of Device Tree Binary (skips FDT generation)
// To revert this, pass dtb_addr = 0
PUBLIC void rvvm_set_dtb_addr(rvvm_machine_t* machine, rvvm_addr_t dtb_addr);

// Manipulate cmdline passed to guest kernel in FDT prop /chosen/bootargs
PUBLIC void rvvm_cmdline_set(rvvm_machine_t* machine, const char* str);
PUBLIC void rvvm_cmdline_append(rvvm_machine_t* machine, const char* str);

// Set up handler & userdata to be called when the VM performs reset/shutdown
// Returning false cancels reset
PUBLIC void rvvm_set_reset_handler(rvvm_machine_t* machine, rvvm_reset_handler_t handler, void* data);

// Load bootrom, kernel binaries into RAM (handle reset as well)
PUBLIC bool rvvm_load_bootrom(rvvm_machine_t* machine, const char* path);
PUBLIC bool rvvm_load_kernel(rvvm_machine_t* machine, const char* path);
PUBLIC bool rvvm_load_dtb(rvvm_machine_t* machine, const char* path);

// Spawns CPU threads and continues VM execution
PUBLIC void rvvm_start_machine(rvvm_machine_t* machine);

// Stops the CPUs, everything is frozen upon return
PUBLIC void rvvm_pause_machine(rvvm_machine_t* machine);

// Reset/shutdown the VM
PUBLIC void rvvm_reset_machine(rvvm_machine_t* machine, bool reset);

// Returns true if the machine is powered on (even when it's paused)
PUBLIC bool rvvm_machine_powered_on(rvvm_machine_t* machine);

// Complete cleanup (frees memory, devices data, VM structures)
PUBLIC void rvvm_free_machine(rvvm_machine_t* machine);

// Get near mmio zone if the one specified is busy (before attaching device, for example)
// Returns addr if the specified zone is usable
PUBLIC rvvm_addr_t rvvm_mmio_zone_auto(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);

// Connect devices to the machine (only when it's stopped!)
PUBLIC rvvm_mmio_handle_t rvvm_attach_mmio(rvvm_machine_t* machine, const rvvm_mmio_dev_t* mmio);
PUBLIC void rvvm_detach_mmio(rvvm_machine_t* machine, rvvm_addr_t mmio_addr, bool cleanup);

// Manipulate attached mmio zone by handle, may be done on a running VM
PUBLIC rvvm_mmio_dev_t* rvvm_get_mmio(rvvm_machine_t* machine, rvvm_mmio_handle_t handle);

/*
 * Allows to disable the internal eventloop thread and
 * offload it somewhere. For self-contained VMs this
 * should be used in main thread.
 */
PUBLIC void rvvm_enable_builtin_eventloop(bool enabled);
PUBLIC void rvvm_run_eventloop(); // Returns when all VMs are stopped

/*
 * For hosts with no threading support, executes a single hart in the current thread.
 * Returns when the VM shuts down itself.
 */
PUBLIC void rvvm_run_machine_singlethread(rvvm_machine_t* machine);

#endif
