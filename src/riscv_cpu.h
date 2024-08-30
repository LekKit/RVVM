/*
riscv_cpu.h - RISC-V CPU Interfaces
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

#ifndef RISCV_CPU_H
#define RISCV_CPU_H

#include "rvvm.h"
#include "riscv_mmu.h"

// Run the vCPU in current thread
void riscv_run_till_event(rvvm_hart_t* vm);

// Trap the vCPU on illegal instruction
slow_path void riscv_illegal_insn(rvvm_hart_t* vm, const uint32_t insn);

// Internal interpreter ISA switching
void riscv32_run_interpreter(rvvm_hart_t* vm);
void riscv64_run_interpreter(rvvm_hart_t* vm);

/*
 * JIT infrastructure
 */

// Flush the JIT cache (Kinda like instruction cache)
void riscv_jit_flush_cache(rvvm_hart_t* vm);

// Discard the currently JITed block
static inline void riscv_jit_discard(rvvm_hart_t* vm)
{
#ifdef USE_JIT
    vm->jit_compiling = false;
#else
    UNUSED(vm);
#endif
}

// Finish the currently JITed block
static inline void riscv_jit_compile(rvvm_hart_t* vm)
{
#ifdef USE_JIT
    vm->block_ends = true;
#else
    UNUSED(vm);
#endif
}

// Mark the physical memory as dirty (Overwritten)
#ifdef USE_JIT
void riscv_jit_mark_dirty_mem(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);
#else
static inline void riscv_jit_mark_dirty_mem(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size) {
    UNUSED(machine);
    UNUSED(addr);
    UNUSED(size);
}
#endif

/*
 * Interpreter JIT glue
 */

slow_path bool riscv_jit_tlb_lookup(rvvm_hart_t* vm);

slow_path void riscv_jit_finalize(rvvm_hart_t* vm);

#endif
