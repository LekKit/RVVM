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

#define HART_STOPPED 0
#define HART_RUNNING 1

// Set up initial hart context
void riscv_hart_init(rvvm_hart_t* vm, bool rv64);

/* Hart-thread routines */

/*
 * Executes the hart in a current thread
 * Returns upon receiving EXT_EVENT_PAUSE
 */
void riscv_hart_run(rvvm_hart_t* vm);

// Correctly applies side-effects of switching privileges
void riscv_switch_priv(rvvm_hart_t* vm, uint8_t priv_mode);

// Correctly applies side-effects of switching XLEN
void riscv_update_xlen(rvvm_hart_t* vm);

/*
 * Traps the hart
 * Should be the last operation before returning to dispatch
 * JIT may unwind to dispatch afterwards
 */ 
void riscv_trap(rvvm_hart_t* vm, bitcnt_t cause, maxlen_t tval);

// Used for wfi, returns true if an interrupt was caught
bool riscv_handle_irqs(rvvm_hart_t* vm, bool wfi);

// Used in tlb flush routines to reset page_addr in dispatch
void riscv_restart_dispatch(rvvm_hart_t* vm);

// Requests the hart to be paused as soon as possible
void riscv_hart_queue_pause(rvvm_hart_t* vm);

/* External-thread routines */

// Spawns thread for hart execution, returns immediately
void riscv_hart_spawn(rvvm_hart_t *vm);

// Signals interrupt to the hart, may be called anywhere
void riscv_interrupt(rvvm_hart_t* vm, bitcnt_t irq_mask);

// Clears interrupt in IP csr of the hart, may be called anywhere
void riscv_interrupt_clear(rvvm_hart_t* vm, bitcnt_t irq_mask);

// Forces hart to check timecmp register for interrupts
void riscv_hart_check_timer(rvvm_hart_t* vm);

// Pauses hart in a consistent state, terminates executing thread
// This function is blocking
void riscv_hart_pause(rvvm_hart_t* vm);

#endif
