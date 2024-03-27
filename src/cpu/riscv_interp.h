/*
riscv_interp.h - RISC-V Template interpreter
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.

Alternatively, the contents of this file may be used under the terms
of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or any later version.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef RISCV_INTERP_H
#define RISCV_INTERP_H

#include "riscv_compressed.h"

NOINLINE void riscv_jit_finalize(rvvm_hart_t* vm);

static forceinline void riscv_emulate(rvvm_hart_t *vm, const uint32_t instruction)
{
#ifdef USE_JIT
    if (unlikely(vm->jit_compiling)) {
        // If we hit non-compilable instruction or cross page boundaries,
        // the block is finalized.
        if (vm->block_ends
        || (vm->jit.virt_pc >> MMU_PAGE_SHIFT) != (vm->registers[REGISTER_PC] >> MMU_PAGE_SHIFT)) {
            riscv_jit_finalize(vm);
        }
        vm->block_ends = true;
    }
#endif
    riscv_emulate_insn(vm, instruction);
}

/*
 * Optimized dispatch loop that does not fetch each instruction,
 * and invokes MMU on page change instead.
 * This gains us about 40-60% more performance depending on workload.
 * Attention: Any TLB flush must clear vm->wait_event to
 * restart dispatch loop, otherwise it will continue executing current page
 */
TSAN_SUPPRESS void riscv_run_interpreter(rvvm_hart_t* vm)
{
    size_t inst_ptr = 0;  // Updated before any read
    uint32_t instruction = 0;
    // page_addr should always mismatch pc by at least 1 page before execution
    xlen_t page_addr = vm->registers[REGISTER_PC] + 0x1000;

    // Execute instructions loop until some event occurs (interrupt, trap)
    while (likely(vm->wait_event)) {
        xlen_t inst_addr = vm->registers[REGISTER_PC];
        if (likely(inst_addr - page_addr < 0xFFD)) {
            instruction = read_uint32_le_m((vmptr_t)(size_t)(inst_ptr + TLB_VADDR(inst_addr)));
        } else if (likely(riscv_fetch_inst(vm, inst_addr, &instruction))) {
            // Update pointer to the current page in real memory
            // If we are executing code from MMIO, direct memory fetch fails
            const xlen_t vpn = vm->registers[REGISTER_PC] >> 12;
            inst_ptr = vm->tlb[vpn & TLB_MASK].ptr;
            page_addr = vm->tlb[vpn & TLB_MASK].e << 12;
        } else break;
        vm->registers[REGISTER_ZERO] = 0;
        riscv_emulate(vm, instruction);
    }
}

#endif
