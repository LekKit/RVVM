 /*
rvvm.h - RISC-V Virtual Machine
Copyright (C) 2021  LekKit <github.com/LekKit>
                    cerg2010cerg2010 <github.com/cerg2010cerg2010>
                    Mr0maks <mr.maks0443@gmail.com>

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

#ifndef RVVM_H
#define RVVM_H
//#define USE_SJLJ
#include "rvvm_types.h"
#include "rvtimer.h"
#include "compiler.h"
#include "threading.h"
#include "vector.h"
#include "utils.h"

#ifdef USE_SJLJ
// Useful for unwinding from JITed code, can be disabled for interpreter
#include <setjmp.h>
#endif

#define RVVM_ABI_VERSION 2
#define TLB_SIZE         256  // Always nonzero, power of 2 (32, 64..)

enum
{
    REGISTER_ZERO,
    REGISTER_X0 = REGISTER_ZERO,
    REGISTER_X1,
    REGISTER_X2,
    REGISTER_X3,
    REGISTER_X4,
    REGISTER_X5,
    REGISTER_X6,
    REGISTER_X7,
    REGISTER_X8,
    REGISTER_X9,
    REGISTER_X10,
    REGISTER_X11,
    REGISTER_X12,
    REGISTER_X13,
    REGISTER_X14,
    REGISTER_X15,
    REGISTER_X16,
    REGISTER_X17,
    REGISTER_X18,
    REGISTER_X19,
    REGISTER_X20,
    REGISTER_X21,
    REGISTER_X22,
    REGISTER_X23,
    REGISTER_X24,
    REGISTER_X25,
    REGISTER_X26,
    REGISTER_X27,
    REGISTER_X28,
    REGISTER_X29,
    REGISTER_X30,
    REGISTER_X31,
    REGISTER_PC,
    REGISTERS_MAX
};

#define FPU_REGISTERS_MAX REGISTERS_MAX

enum
{
    PRIVILEGE_USER,
    PRIVILEGE_SUPERVISOR,
    PRIVILEGE_HYPERVISOR,
    PRIVILEGE_MACHINE,
    PRIVILEGES_MAX
};

#define INTERRUPT_USOFTWARE    0x0
#define INTERRUPT_SSOFTWARE    0x1
#define INTERRUPT_MSOFTWARE    0x3
#define INTERRUPT_UTIMER       0x4
#define INTERRUPT_STIMER       0x5
#define INTERRUPT_MTIMER       0x7
#define INTERRUPT_UEXTERNAL    0x8
#define INTERRUPT_SEXTERNAL    0x9
#define INTERRUPT_MEXTERNAL    0xB

// Internal events delivered to the hart
#define EXT_EVENT_TIMER        0x1 // Check timecmp for irq
#define EXT_EVENT_PAUSE        0x2 // Pause the hart in a consistent state

#define TRAP_INSTR_MISALIGN    0x0
#define TRAP_INSTR_FETCH       0x1
#define TRAP_ILL_INSTR         0x2
#define TRAP_BREAKPOINT        0x3
#define TRAP_LOAD_MISALIGN     0x4
#define TRAP_LOAD_FAULT        0x5
#define TRAP_STORE_MISALIGN    0x6
#define TRAP_STORE_FAULT       0x7
#define TRAP_ENVCALL_UMODE     0x8
#define TRAP_ENVCALL_SMODE     0x9
#define TRAP_ENVCALL_MMODE     0xB
#define TRAP_INSTR_PAGEFAULT   0xC
#define TRAP_LOAD_PAGEFAULT    0xD
#define TRAP_STORE_PAGEFAULT   0xF

#define RVVM_DEFAULT_MEMBASE   0x80000000

typedef struct rvvm_hart_t rvvm_hart_t;
typedef struct rvvm_machine_t rvvm_machine_t;
typedef struct rvvm_mmio_dev_t rvvm_mmio_dev_t;

typedef void (*riscv_inst_t)(rvvm_hart_t *vm, const uint32_t instruction);
typedef void (*riscv_inst_c_t)(rvvm_hart_t *vm, const uint16_t instruction);

// Decoder moved to hart struct, allows to switch extensions per-hart
typedef struct {
    riscv_inst_t opcodes[512];
    riscv_inst_c_t opcodes_c[32];
} rvvm_decoder_t;

/* 
 * Address translation cache
 * In future, it would be nice to verify if cache-line alignment
 * gives any profit (entries scattered between cachelines waste L1)
 */
typedef struct {
    // Pointer to page (with vaddr subtracted? faster tlb translation)
    vmptr_t ptr;
    // Virtual page number per each op type (vaddr >> 12)
    vaddr_t r;
    vaddr_t w;
    vaddr_t e;
    // Make entry size a power of 2 (32 or 16 bytes)
#if defined(HOST_64BIT) && !defined(USE_RV64)
    vaddr_t align[3];
#endif
} rvvm_tlb_entry_t;

typedef struct {
    void (*init)(rvvm_mmio_dev_t* dev, bool reset);
    void (*remove)(rvvm_mmio_dev_t* dev);
    void (*update)(rvvm_mmio_dev_t* dev);
    /* TODO
     * void (*suspend)(rvvm_mmio_dev_t* dev, rvvm_state_t* state);
     * void (*resume)(rvvm_mmio_dev_t* dev, rvvm_state_t* state);
     */
    const char* name;
} rvvm_mmio_type_t;

typedef bool (*rvvm_mmio_handler_t)(rvvm_mmio_dev_t* dev, void* dest, paddr_t offset, uint8_t size);

struct rvvm_mmio_dev_t {
    paddr_t begin;  // First usable address in physical memory
    paddr_t end;    // Last usable address in physical memory
    void* data;     // Pointer to memory (for native memory regions) or device-specific data
    rvvm_machine_t* machine;  // Parent machine
    rvvm_mmio_type_t* type;   // Device-specific operations & info
    // MMIO operations handlers, if these aren't NULL then this is a MMIO device
    // Hint: setting read to NULL and write to rvvm_mmio_none makes memory mapping read-only
    rvvm_mmio_handler_t read;
    rvvm_mmio_handler_t write;
    // MMIO operations attributes (alignment & min op size, max op size), always power of 2
    // Any non-conforming operation is fixed on the fly before the handlers are invoked.
    uint8_t min_op_size; // Dictates alignment as well
    uint8_t max_op_size;
};

typedef struct {
    paddr_t begin;  // First usable address in physical memory
    paddr_t size;   // Memory amount (since the region may be empty)
    vmptr_t data;   // Pointer to memory data
} rvvm_ram_t;

typedef struct {
    // Virtual page number per each op type (vaddr >> 12)
    vaddr_t r;
    vaddr_t w;
    vaddr_t e;
    // Physical address of the page mapped to the device
    paddr_t phys;
    // The device itself
    const rvvm_mmio_dev_t* mmio;
} rvvm_mmio_tlb_t;

struct rvvm_hart_t {
    uint32_t wait_event;
    maxlen_t registers[REGISTERS_MAX];
#ifdef USE_FPU
    double fpu_registers[REGISTERS_MAX];
#endif
    
    // We want short offsets from vmptr to tlb
    rvvm_tlb_entry_t tlb[TLB_SIZE];
    rvvm_decoder_t decoder;
    rvvm_ram_t mem;
    rvvm_machine_t* machine;
    paddr_t root_page_table;
    uint8_t mmu_mode;
    uint8_t priv_mode;
    bool rv64;
    bool trap;
    maxlen_t trap_pc;
    
    struct {
        maxlen_t hartid;
        maxlen_t isa;
        maxlen_t status;
        maxlen_t edeleg[PRIVILEGES_MAX];
        maxlen_t ideleg[PRIVILEGES_MAX];
        maxlen_t ie;
        maxlen_t tvec[PRIVILEGES_MAX];
        maxlen_t scratch[PRIVILEGES_MAX];
        maxlen_t epc[PRIVILEGES_MAX];
        maxlen_t cause[PRIVILEGES_MAX];
        maxlen_t tval[PRIVILEGES_MAX];
        maxlen_t ip;
        maxlen_t fcsr;
    } csr;
    maxlen_t lrsc_cas;
    bool lrsc;
    
    thread_handle_t thread;
    rvtimer_t timer;
    uint32_t pending_irqs;
    uint32_t pending_events;
#ifdef USE_SJLJ
    jmp_buf unwind;
#endif
    // Cacheline alignment
    uint8_t align[64];
};

struct rvvm_machine_t {
    rvvm_ram_t mem;
    vector_t(rvvm_hart_t) harts;
    vector_t(rvvm_mmio_dev_t) mmio;
    bool running;
};

// Memory starts at 0x80000000 by default, machine boots from there as well
PUBLIC rvvm_machine_t* rvvm_create_machine(paddr_t mem_base, size_t mem_size, size_t hart_count, bool rv64);

// Directly access physical memory (returns true on success)
PUBLIC bool rvvm_write_ram(rvvm_machine_t* machine, paddr_t dest, const void* src, size_t size);
PUBLIC bool rvvm_read_ram(rvvm_machine_t* machine, void* dest, paddr_t src, size_t size);

// Spawns CPU threads and continues VM execution
PUBLIC void rvvm_start_machine(rvvm_machine_t* machine);
// Stops the CPUs, everything is frozen upon return
PUBLIC void rvvm_pause_machine(rvvm_machine_t* machine);
// Complete cleanup (frees memory, devices data, VM structures)
PUBLIC void rvvm_free_machine(rvvm_machine_t* machine);

// Connect devices to the machine (only when it's stopped!)
PUBLIC void rvvm_attach_mmio(rvvm_machine_t* machine, const rvvm_mmio_dev_t* mmio);
PUBLIC void rvvm_detach_mmio(rvvm_machine_t* machine, paddr_t mmio_addr);

/*
 * Allows to disable the internal eventloop thread and
 * offload it somewhere. For self-contained VMs this
 * should be used in main thread.
 */
PUBLIC void rvvm_enable_builtin_eventloop(bool enabled);
PUBLIC void rvvm_run_eventloop(); // Returns when all VMs are stopped

/*
 * For hosts with no threading support, or self-contained executables,
 * executes a single hart in the current thread.
 * Returns when the VM shuts down itself.
 */
PUBLIC void rvvm_run_machine_singlethread(rvvm_machine_t* machine);

#endif
