/*
riscv_hart.c - RISC-V Hardware Thread
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

#include "rvvm_isolation.h"
#include "riscv_hart.h"
#include "riscv_mmu.h"
#include "riscv_csr.h"
#include "riscv_priv.h"
#include "riscv_cpu.h"
#include "threading.h"
#include "atomics.h"
#include "bit_ops.h"

rvvm_hart_t* riscv_hart_init(rvvm_machine_t* machine)
{
    rvvm_hart_t* vm = safe_new_obj(rvvm_hart_t);
    vm->wfi_cond = condvar_create();
    vm->machine = machine;
    vm->mem = machine->mem;
    vm->rv64 = machine->rv64;
    vm->priv_mode = PRIVILEGE_MACHINE;
    // Delegate exceptions from M to S
    vm->csr.edeleg[PRIVILEGE_HYPERVISOR] = 0xFFFFFFFF;
    vm->csr.ideleg[PRIVILEGE_HYPERVISOR] = 0xFFFFFFFF;

    if (vm->rv64) {
#ifdef USE_RV64
        // 0x2A00000000 for H-mode
        vm->csr.status = 0xA00000000;
        vm->csr.isa = CSR_MISA_RV64;
#else
        rvvm_warn("Requested RV64 in RV32-only build");
#endif
    } else {
        vm->csr.isa = CSR_MISA_RV32;
    }

    riscv_tlb_flush(vm);
    DO_ONCE(riscv_csr_global_init());
    return vm;
}

void riscv_hart_free(rvvm_hart_t* vm)
{
#ifdef USE_JIT
    if (vm->jit_enabled) rvjit_ctx_free(&vm->jit);
#endif
    condvar_free(vm->wfi_cond);
    free(vm);
}

void riscv_hart_run(rvvm_hart_t* vm)
{
    rvvm_info("Hart %p started", vm);

    while (true) {
        // Allow hart to run
        atomic_store_uint32_ex(&vm->wait_event, HART_RUNNING, ATOMIC_RELAXED);

        // Handle events
        vm->csr.ip |= atomic_swap_uint32(&vm->pending_irqs, 0);
        uint32_t events = atomic_swap_uint32(&vm->pending_events, 0);

        if ((vm->csr.ip & (1U << INTERRUPT_MTIMER)) && !rvtimer_pending(&vm->timer)) {
            riscv_interrupt_clear(vm, INTERRUPT_MTIMER);
        }

        if (unlikely(events)) {
            if (events & EXT_EVENT_PAUSE) {
                rvvm_info("Hart %p stopped", vm);
                return;
            } else if (events & EXT_EVENT_PREEMPT) {
                sleep_ms(atomic_swap_uint32(&vm->preempt_ms, 0));
            }
        }

        riscv_handle_irqs(vm, false);

        // Run the hart
        riscv_run_till_event(vm);
        if (vm->trap) {
            vm->registers[REGISTER_PC] = vm->trap_pc;
            vm->trap = false;
        }
    }
}

rvvm_addr_t riscv_hart_run_userland(rvvm_hart_t* vm)
{
    vm->user_traps = true;
    atomic_store_uint32(&vm->wait_event, HART_RUNNING);
    riscv_run_till_event(vm);
    if (vm->trap) {
        vm->registers[REGISTER_PC] = vm->trap_pc;
        vm->trap = false;
    }
    return vm->csr.cause[PRIVILEGE_USER];
}

#ifdef USE_RV64
void riscv_update_xlen(rvvm_hart_t* vm)
{
    bool rv64 = false;
    switch (vm->priv_mode) {
        case PRIVILEGE_MACHINE:
            rv64 = !!(vm->csr.isa & CSR_MISA_RV64);
            break;
        case PRIVILEGE_HYPERVISOR:
            rv64 = bit_check(vm->csr.status, 37);
            break;
        case PRIVILEGE_SUPERVISOR:
            rv64 = bit_check(vm->csr.status, 35);
            break;
        case PRIVILEGE_USER:
            rv64 = bit_check(vm->csr.status, 33);
            break;
    }

    if (vm->rv64 != rv64) {
        vm->rv64 = rv64;
#ifdef USE_JIT
        rvjit_set_rv64(&vm->jit, rv64);
        riscv_jit_flush_cache(vm);
#endif
        riscv_restart_dispatch(vm);
    }
}
#endif

void riscv_switch_priv(rvvm_hart_t* vm, uint8_t priv_mode)
{
    // true if one is S/U, other is M/H
    bool mmu_toggle = (vm->priv_mode & 2) != (priv_mode & 2);
    vm->priv_mode = priv_mode;
#ifdef USE_RV64
    riscv_update_xlen(vm);
#endif

    // May unwind to dispatch
    if (mmu_toggle) riscv_tlb_flush(vm);
}

// Save current priv to xPP, xIE to xPIE, disable interrupts for target priv
static void riscv_trap_priv_helper(rvvm_hart_t* vm, uint8_t target_priv)
{
    switch (target_priv) {
        case PRIVILEGE_MACHINE:
            vm->csr.status = bit_replace(vm->csr.status, 11, 2, vm->priv_mode);
            vm->csr.status = bit_replace(vm->csr.status, 7, 1, bit_cut(vm->csr.status, 3, 1));
            vm->csr.status = bit_replace(vm->csr.status, 3, 1, 0);
            break;
        case PRIVILEGE_HYPERVISOR:
            vm->csr.status = bit_replace(vm->csr.status, 9, 2, vm->priv_mode);
            vm->csr.status = bit_replace(vm->csr.status, 6, 1, bit_cut(vm->csr.status, 2, 1));
            vm->csr.status = bit_replace(vm->csr.status, 2, 1, 0);
            break;
        case PRIVILEGE_SUPERVISOR:
            vm->csr.status = bit_replace(vm->csr.status, 8, 1, vm->priv_mode);
            vm->csr.status = bit_replace(vm->csr.status, 5, 1, bit_cut(vm->csr.status, 1, 1));
            vm->csr.status = bit_replace(vm->csr.status, 1, 1, 0);
            break;
        case PRIVILEGE_USER:
            vm->csr.status = bit_replace(vm->csr.status, 4, 1, bit_cut(vm->csr.status, 0, 1));
            vm->csr.status = bit_replace(vm->csr.status, 0, 1, 0);
            break;
    }
}

void riscv_trap(rvvm_hart_t* vm, bitcnt_t cause, maxlen_t tval)
{
    vm->trap = true;
    if (cause < TRAP_ENVCALL_UMODE || cause > TRAP_ENVCALL_MMODE) {
        riscv_jit_discard(vm);
    }
    if (vm->user_traps) {
        // Defer usermode trap
        vm->csr.cause[PRIVILEGE_USER] = cause;
        vm->csr.tval[PRIVILEGE_USER] = tval;
        vm->trap_pc = vm->registers[REGISTER_PC];
    } else {
        // Target privilege mode
        uint8_t priv = PRIVILEGE_MACHINE;
        // Delegate to lower privilege mode if needed
        while ((priv > vm->priv_mode) && (vm->csr.edeleg[priv] & (1 << cause))) priv--;
        // Write exception info
        vm->csr.epc[priv] = vm->registers[REGISTER_PC];
        vm->csr.cause[priv] = cause;
        vm->csr.tval[priv] = tval;
        // Modify exception stack in csr.status
        riscv_trap_priv_helper(vm, priv);
        // Jump to trap vector, switch to target priv
        vm->trap_pc = vm->csr.tvec[priv] & (~3ULL);
        riscv_switch_priv(vm, priv);
    }
    riscv_restart_dispatch(vm);
}

static inline maxlen_t riscv_cause_irq_mask(rvvm_hart_t* vm)
{
#ifdef USE_RV64
    if (vm->rv64) {
        return 0x8000000000000000ULL;
    } else {
        return 0x80000000U;
    }
#else
    UNUSED(vm);
    return 0x80000000U;
#endif
}

bool riscv_handle_irqs(rvvm_hart_t* vm, bool wfi)
{
    // IRQs that are pending & enabled by mie
    uint32_t pending_irqs = vm->csr.ip & vm->csr.ie;
    if (unlikely(pending_irqs)) {
        // Target privilege mode
        uint8_t priv = PRIVILEGE_MACHINE;
        uint32_t irqs = 0;
        // Delegate pending IRQs from M to the highest possible mode
        do {
            irqs = pending_irqs & ~vm->csr.ideleg[priv];
            pending_irqs &= vm->csr.ideleg[priv];
            if (irqs) break;
        } while (--priv);
        // Skip if target priv < current priv, or equal and IRQs are disabled in status CSR
        if (vm->priv_mode > priv) return false;
        if (vm->priv_mode == priv && !((1 << vm->priv_mode) & vm->csr.status)) return false;

        for (int i=11; i>=0; --i) {
            if (irqs & (1 << i)) {
                // Modify exception stack in csr.status
                riscv_trap_priv_helper(vm, priv);
                // Discard unfinished JIT block
                riscv_jit_discard(vm);
                // Switch privilege
                riscv_switch_priv(vm, priv);
                // Write exception info
                vm->csr.epc[priv] = vm->registers[REGISTER_PC];
                if (wfi) vm->csr.epc[priv] += 4;
                vm->csr.cause[priv] = i | riscv_cause_irq_mask(vm);
                vm->csr.tval[priv] = 0;
                // Jump to trap vector
                if (vm->csr.tvec[priv] & 1) {
                    vm->registers[REGISTER_PC] = (vm->csr.tvec[priv] & (~3ULL)) + (i << 2);
                } else {
                    vm->registers[REGISTER_PC] = vm->csr.tvec[priv] & (~3ULL);
                }
                return true;
            }
        }
    }
    return false;
}

void riscv_restart_dispatch(rvvm_hart_t* vm)
{
    atomic_store_uint32_ex(&vm->wait_event, HART_STOPPED, ATOMIC_RELAXED);
}

static void* riscv_hart_run_wrap(void* ptr)
{
    if (rvvm_getarg_int("noisolation") < 1) {
        rvvm_restrict_this_thread();
    }
    riscv_hart_run((rvvm_hart_t*)ptr);
    return NULL;
}

void riscv_hart_prepare(rvvm_hart_t *vm)
{
#ifdef USE_JIT
    if (!vm->jit_enabled && rvvm_get_opt(vm->machine, RVVM_OPT_JIT)) {
        vm->jit_enabled = rvjit_ctx_init(&vm->jit, rvvm_get_opt(vm->machine, RVVM_OPT_JIT_CACHE));

        if (vm->jit_enabled) {
            rvjit_set_rv64(&vm->jit, vm->rv64);
            if (!rvvm_get_opt(vm->machine, RVVM_OPT_JIT_HARWARD)) {
                rvjit_init_memtracking(&vm->jit, vm->mem.size);
            }
        } else {
            rvvm_set_opt(vm->machine, RVVM_OPT_JIT, false);
            rvvm_warn("RVJIT failed to initialize, falling back to interpreter");
        }
    }
#else
    UNUSED(vm);
#endif
}

void riscv_hart_spawn(rvvm_hart_t *vm)
{
    atomic_store_uint32(&vm->pending_events, 0);
    vm->thread = thread_create(riscv_hart_run_wrap, (void*)vm);
}

static void riscv_hart_notify(rvvm_hart_t* vm)
{
    atomic_store_uint32(&vm->wait_event, HART_STOPPED);
    // Wake from WFI sleep
    condvar_wake(vm->wfi_cond);
}

void riscv_interrupt(rvvm_hart_t* vm, bitcnt_t irq)
{
    if (~atomic_or_uint32(&vm->pending_irqs, 1U << irq) & (1U << irq)) {
        riscv_hart_notify(vm);
    }
}

void riscv_interrupt_clear(rvvm_hart_t* vm, bitcnt_t irq)
{
    // Discard pending irq
    atomic_and_uint32(&vm->pending_irqs, ~(1U << irq));
#ifdef USE_RV64
    atomic_and_uint64(&vm->csr.ip, ~(1U << irq));
#else
    atomic_and_uint32(&vm->csr.ip, ~(1U << irq));
#endif
    //rvvm_info("Hart %p сleared irq %d\n", vm, irq);
}

void riscv_hart_check_timer(rvvm_hart_t* vm)
{
    // The hard thread checks if the timer is actually pending
    atomic_or_uint32(&vm->pending_irqs, 1U << INTERRUPT_MTIMER);
    atomic_store_uint32(&vm->wait_event, HART_STOPPED);
}

void riscv_hart_preempt(rvvm_hart_t* vm, uint32_t preempt_ms)
{
    if (!preempt_ms) return;
    atomic_store_uint32(&vm->preempt_ms, preempt_ms);
    atomic_or_uint32(&vm->pending_events, EXT_EVENT_PREEMPT);
    atomic_store_uint32(&vm->wait_event, HART_STOPPED);
}

void riscv_hart_pause(rvvm_hart_t* vm)
{
    atomic_or_uint32(&vm->pending_events, EXT_EVENT_PAUSE);
    riscv_hart_notify(vm);

    // Clear vm->thread before freeing it
    thread_ctx_t* thread = vm->thread;
    vm->thread = NULL;
    thread_join(thread);
}

void riscv_hart_queue_pause(rvvm_hart_t* vm)
{
    atomic_or_uint32(&vm->pending_events, EXT_EVENT_PAUSE);
    riscv_hart_notify(vm);
}
