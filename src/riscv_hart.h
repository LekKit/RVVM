/*
riscv_hart.h - RISC-V Hardware Thread
Copyright (C) 2021  LekKit <github.com/LekKit>

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

#ifndef RISCV_HART_H
#define RISCV_HART_H

#include "rvvm.h"

/*
 * Hart context creation, disposal
 */

// Create hart context
rvvm_hart_t* riscv_hart_init(rvvm_machine_t* machine);

// Prepare harts before spawning any of them
void riscv_hart_prepare(rvvm_hart_t* vm);

// Free hart context
void riscv_hart_free(rvvm_hart_t* vm);

/*
 * Hart operations, may be called on any thread
 */

// Makes vCPU return from riscv_run_till_event() to check for IRQs,
// or after flushing pages overlapping PC (optimization quirk)
void riscv_restart_dispatch(rvvm_hart_t* vm);

// Signals interrupt to the hart
void riscv_interrupt(rvvm_hart_t* vm, bitcnt_t irq);

// Clears interrupt in IP csr of the hart
void riscv_interrupt_clear(rvvm_hart_t* vm, bitcnt_t irq);

// Hart interrupts that are raised externally
static inline uint64_t riscv_interrupts_raised(rvvm_hart_t* vm)
{
    return atomic_load_uint64_ex(&vm->pending_irqs, ATOMIC_RELAXED);
}

// Signal the vCPU to check for timer interrupts
void riscv_hart_check_timer(rvvm_hart_t* vm);

// Preempt the hart vCPU thread from consuming CPU for preempt_ms
void riscv_hart_preempt(rvvm_hart_t* vm, uint32_t preempt_ms);

/*
 * Hart operations, may be called ONLY on hart thread
 */

// Hart interrupts that are pending & enabled by ie CSR
static inline uint64_t riscv_interrupts_pending(rvvm_hart_t* vm)
{
    return (riscv_interrupts_raised(vm) | vm->csr.ip) & vm->csr.ie;
}

// Check interrupts after writing to ie/ip/status CSRs, or after sret/mret
void riscv_hart_check_interrupts(rvvm_hart_t* vm);

// Correctly applies side-effects of switching privileges
void riscv_switch_priv(rvvm_hart_t* vm, uint8_t priv_mode);

// Correctly applies side-effects of switching XLEN
void riscv_update_xlen(rvvm_hart_t* vm);

// Traps the hart. Should be the last operation before returning to dispatch.
void riscv_trap(rvvm_hart_t* vm, bitcnt_t cause, maxlen_t tval);

/*
 * Running the hart
 */

// Executes the machine hart in a current thread
// Returns upon receiving EXT_EVENT_PAUSE
void riscv_hart_run(rvvm_hart_t* vm);

// Execute a userland thread context in current thread
// Returns trap cause upon any CPU trap
rvvm_addr_t riscv_hart_run_userland(rvvm_hart_t* vm);

// Spawns hart vCPU thread, returns immediately
void riscv_hart_spawn(rvvm_hart_t *vm);

// Requests the hart to be paused as soon as possible
void riscv_hart_queue_pause(rvvm_hart_t* vm);

// Pauses hart in a consistent state, terminates vCPU thread
void riscv_hart_pause(rvvm_hart_t* vm);

#endif
