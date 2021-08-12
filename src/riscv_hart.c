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

#include "riscv_hart.h"
#include "riscv_mmu.h"
#include "threading.h"
#include "atomics.h"
#include "bit_ops.h"

void riscv_hart_init(rvvm_hart_t* vm)
{
    memset(vm, 0, sizeof(rvvm_hart_t));
    riscv_tlb_flush(vm);
    rvtimer_init(&vm->timer, 10000000); // 10 MHz timer
    vm->priv_mode = PRIVILEGE_MACHINE;
    vm->csr.edeleg[PRIVILEGE_HYPERVISOR] = 0xFFFFFFFF;
    vm->csr.ideleg[PRIVILEGE_HYPERVISOR] = 0xFFFFFFFF;
    
}

// Stub
static void riscv_run_till_event(rvvm_hart_t* vm)
{
    UNUSED(vm);
}

void riscv_hart_run(rvvm_hart_t* vm)
{
    uint32_t events;
    atomic_store_uint32(&vm->wait_event, HART_RUNNING);
#ifdef USE_SJLJ
    setjmp(vm->unwind);
#endif

    while (true) {
        riscv_run_till_event(vm);
        atomic_store_uint32(&vm->wait_event, HART_RUNNING);
#ifndef USE_SJLJ
        if (vm->trap) {
            vm->registers[REGISTER_PC] = vm->csr.tvec[vm->priv_mode] & (~3ULL);
            vm->trap = false;
        }
#endif

        vm->csr.ip |= atomic_swap_uint32(&vm->pending_irqs, 0);
        events = atomic_swap_uint32(&vm->pending_events, 0);

        if ((events & EXT_EVENT_TIMER) && rvtimer_pending(&vm->timer)) {
            vm->csr.ip |= (1 << INTERRUPT_MTIMER);
        }

        if (events & EXT_EVENT_PAUSE) {
            return;
        }

        riscv_handle_irqs(vm, false);
    }
}

void riscv_switch_priv(rvvm_hart_t* vm, uint8_t priv_mode)
{
    // true if one is S/U, other is M/H
    bool mmu_toggle = (vm->priv_mode & 2) != (priv_mode & 2);
    vm->priv_mode = priv_mode;
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
    // Target privilege mode
    uint8_t priv = PRIVILEGE_MACHINE;
    // Delegate to lower privilege mode if needed
    while (vm->csr.edeleg[priv] & (1 << cause)) priv--;
    // Write exception info
    vm->csr.epc[priv] = vm->registers[REGISTER_PC];
    vm->csr.cause[priv] = cause;
    vm->csr.tval[priv] = tval;
    // Modify exception stack in csr.status
    riscv_trap_priv_helper(vm, priv);
    // Jump to trap vector, switch to target priv
    vm->registers[REGISTER_PC] = vm->csr.tvec[priv] & (~3ULL);
    vm->trap = true;
    riscv_switch_priv(vm, priv);
#ifdef USE_SJLJ
    longjmp(vm->unwind, 1);
#endif
}

static const uint16_t irq_mask_high[PRIVILEGES_MAX] = {
    0xEEE, 0xCCC, 0x888, 0x0
};

static inline uint32_t riscv_irq_mask(rvvm_hart_t* vm, bool wfi) {
    uint32_t ret = irq_mask_high[vm->priv_mode];
    if (((1 << vm->priv_mode) & vm->csr.status) || wfi)
        ret |= (0x111 << vm->priv_mode);

    return ret;
}

static inline maxlen_t riscv_cause_irq_mask(rvvm_hart_t* vm)
{
    if (vm->rv64) {
        return 0x8000000000000000ULL;
    } else {
        return 0x80000000U;
    }
}

bool riscv_handle_irqs(rvvm_hart_t* vm, bool wfi)
{
    // IRQs that are pending, enabled by mie and allowed by privilege mode & mstatus
    uint32_t irqs = vm->csr.ip & vm->csr.ie & riscv_irq_mask(vm, wfi);
    if (unlikely(irqs)) {
        for (int i=11; i>=0; --i) {
            if (irqs & (1 << i)) {
                // Target privilege mode
                uint8_t priv = PRIVILEGE_MACHINE;
                // Delegate to lower privilege mode if needed
                while (vm->csr.edeleg[priv] & (1 << i)) priv--;
                // Write exception info
                vm->csr.epc[priv] = vm->registers[REGISTER_PC];
                if (wfi) vm->csr.epc[priv] += 4;
                vm->csr.cause[priv] = i | riscv_cause_irq_mask(vm);
                vm->csr.tval[priv] = 0;
                // Modify exception stack in csr.status
                riscv_trap_priv_helper(vm, priv);
                // Jump to trap vector, switch to target priv
                if (vm->csr.tvec[priv] & 1) {
                    vm->registers[REGISTER_PC] = (vm->csr.tvec[priv] & (~3ULL)) + (i << 2);
                } else {
                    vm->registers[REGISTER_PC] = vm->csr.tvec[priv] & (~3ULL);
                }
                riscv_switch_priv(vm, priv);
#ifdef USE_SJLJ
                longjmp(vm->unwind, 1);
#endif
                return true;
            }
        }
    }
    return false;
}

void riscv_restart_dispatch(rvvm_hart_t* vm)
{
#ifdef USE_SJLJ
    if (vm->wait_event != HART_STOPPED) longjmp(vm->unwind, 1);
#else
    atomic_store_uint32(&vm->wait_event, HART_STOPPED);
#endif
}

static void* riscv_hart_run_wrap(void* ptr)
{
    riscv_hart_run((rvvm_hart_t*)ptr);
    return NULL;
}

void riscv_hart_spawn(rvvm_hart_t *vm)
{
    vm->thread = thread_create(riscv_hart_run_wrap, (void*)vm);
}

static void riscv_hart_notify(rvvm_hart_t* vm)
{
    atomic_store_uint32(&vm->wait_event, HART_STOPPED);
    // Explicitly sync memory with the hart thread, wake from WFI sleep
    thread_signal_membarrier(vm->thread);
}

void riscv_interrupt(rvvm_hart_t* vm, bitcnt_t irq)
{
    atomic_or_uint32(&vm->pending_irqs, 1U << irq);
    riscv_hart_notify(vm);
}

void riscv_hart_check_timer(rvvm_hart_t* vm)
{
    atomic_or_uint32(&vm->pending_events, 1U << EXT_EVENT_TIMER);
    riscv_hart_notify(vm);
}

void riscv_hart_pause(rvvm_hart_t* vm)
{
    atomic_or_uint32(&vm->pending_events, 1U << EXT_EVENT_PAUSE);
    riscv_hart_notify(vm);
    thread_join(vm->thread);
}

#if 0
// Serialized interrupt delivery

#define HART_CLAIMED 2

static void riscv_hart_notify_claim(rvvm_hart_t* vm)
{
    // Expects the hart to be currently running (vm->wait_event == 1)
    // If not, wait until the previous event has been processed
    while (!atomic_cas_uint32(&vm->wait_event, HART_RUNNING, HART_CLAIMED));
}

static void riscv_hart_notify(rvvm_hart_t* vm)
{
    atomic_store_uint32(&vm->wait_event, HART_STOPPED);
    // Explicitly sync memory with the hart thread
    thread_signal_membarrier(vm->thread);
}

void riscv_interrupt_ext(rvvm_hart_t* vm, bitcnt_t irq)
{
    riscv_hart_notify_claim(vm);
    atomic_or_uint32(&vm->pending_irqs, 1U << irq);
    riscv_hart_notify(vm);
}

#endif
