 /*
rvvm.c - RISC-V Virtual Machine
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

#include "rvvm.h"
#include "riscv_hart.h"
#include "riscv_mmu.h"
#include "vector.h"
#include "utils.h"
#include "mem_ops.h"
#include "threading.h"
#include "spinlock.h"

static spinlock_t global_lock;
static vector_t(rvvm_machine_t*) global_machines = {0};

static thread_handle_t builtin_eventloop_thread;
static bool builtin_eventloop_enabled;

static void* builtin_eventloop(void* arg)
{
    rvvm_machine_t* machine;
    rvvm_mmio_dev_t* dev;
    
    // The eventloop runs while its enabled/ran manually,
    // and there are any running machines
    while ((builtin_eventloop_enabled || arg) && vector_size(global_machines)) {
        sleep_ms(10);
        spin_lock(&global_lock);
        vector_foreach(global_machines, m) {
            machine = vector_at(global_machines, m);
            if (!atomic_load_uint32(&machine->running)) {
                // The machine was shut down
                vector_foreach(machine->harts, i) {
                    riscv_hart_pause(&vector_at(machine->harts, i));
                }
                vector_erase(global_machines, m);
                
                if (vector_size(global_machines) == 0) {
                    vector_free(global_machines);
                    spin_unlock(&global_lock);
                    return NULL;
                } else continue;
            }
            
            vector_foreach(machine->harts, i) {
                // Wake hart thread to check timer interrupt.
                riscv_hart_check_timer(&vector_at(machine->harts, i));
            }
            
            vector_foreach(machine->mmio, i) {
                dev = &vector_at(machine->mmio, i);
                if (dev->type && dev->type->update) {
                    // Update device
                    dev->type->update(dev);
                }
            }
        }
        spin_unlock(&global_lock);
    }
    return arg;
}

static void register_machine(rvvm_machine_t* machine)
{
    spin_lock(&global_lock);
    if (vector_size(global_machines) == 0) {
        vector_init(global_machines);
    }
    vector_push_back(global_machines, machine);
    if (builtin_eventloop_enabled && vector_size(global_machines) == 1) {
        builtin_eventloop_thread = thread_create(builtin_eventloop, NULL);
    }
    spin_unlock(&global_lock);
}

static void deregister_machine(rvvm_machine_t* machine)
{
    bool stop_thread = false;
    spin_lock(&global_lock);
    vector_foreach(global_machines, i) {
        if (vector_at(global_machines, i) == machine) {
            vector_erase(global_machines, i);
        }
    }
    if (vector_size(global_machines) == 0) {
        vector_free(global_machines);
        // prevent deadlock
        stop_thread = builtin_eventloop_enabled;
    }
    spin_unlock(&global_lock);
    
    if (stop_thread) {
        thread_signal_membarrier(builtin_eventloop_thread);
        thread_join(builtin_eventloop_thread);
    }
}

PUBLIC rvvm_machine_t* rvvm_create_machine(paddr_t mem_base, size_t mem_size, size_t hart_count, bool rv64)
{
    rvvm_hart_t* vm;
    rvvm_machine_t* machine = safe_calloc(sizeof(rvvm_machine_t), 1);
    if (!riscv_init_ram(&machine->mem, mem_base, mem_size)) {
        free(machine);
        return NULL;
    }
    vector_init(machine->harts);
    vector_init(machine->mmio);
    for (size_t i=0; i<hart_count; ++i) {
        vector_emplace_back(machine->harts);
        vm = &vector_at(machine->harts, i);
        riscv_hart_init(vm, rv64);
        vm->machine = machine;
        vm->mem = machine->mem;
        // a0 register & mhartid csr contain hart ID
        vm->csr.hartid = i;
        vm->registers[REGISTER_X10] = i;
        // Boot from ram base addr by default
        vm->registers[REGISTER_PC] = vm->mem.begin;
    }
    if (hart_count == 0)
        rvvm_warn("Creating machine with no harts at all... What are you even??");
    
    return machine;
}

PUBLIC bool rvvm_write_ram(rvvm_machine_t* machine, paddr_t dest, const void* src, size_t size)
{
    if (dest < machine->mem.begin
    || (dest - machine->mem.begin + size) > machine->mem.size) return false;
    memcpy(machine->mem.data + dest - machine->mem.begin, src, size);
    return true;
}

PUBLIC bool rvvm_read_ram(rvvm_machine_t* machine, void* dest, paddr_t src, size_t size)
{
    if (src < machine->mem.begin
    || (src - machine->mem.begin + size) > machine->mem.size) return false;
    memcpy(dest, machine->mem.data + src - machine->mem.begin, size);
    return true;
}

PUBLIC void rvvm_start_machine(rvvm_machine_t* machine)
{
    if (machine->running) return;
    machine->running = true;
    vector_foreach(machine->harts, i) {
        riscv_hart_spawn(&vector_at(machine->harts, i));
    }
    register_machine(machine);
}

PUBLIC void rvvm_pause_machine(rvvm_machine_t* machine)
{
    if (!machine->running) return;
    machine->running = false;
    vector_foreach(machine->harts, i) {
        riscv_hart_pause(&vector_at(machine->harts, i));
    }
    deregister_machine(machine);
}

PUBLIC void rvvm_free_machine(rvvm_machine_t* machine)
{
    rvvm_mmio_dev_t* dev;
    if (machine->running) return;
    
    vector_foreach(machine->mmio, i) {
        dev = &vector_at(machine->mmio, i);
        rvvm_info("Removing MMIO device \"%s\"", dev->type ? dev->type->name : "null");
        // Either device implements it's own cleanup routine,
        // or we free it's data buffer
        if (dev->type && dev->type->remove)
            dev->type->remove(dev);
        else
            free(dev->data);
    }
    
    vector_free(machine->harts);
    vector_free(machine->mmio);
    riscv_free_ram(&machine->mem);
    free(machine);
}

PUBLIC void rvvm_attach_mmio(rvvm_machine_t* machine, const rvvm_mmio_dev_t* mmio)
{
    rvvm_mmio_dev_t* dev;
    if (machine->running) return;
    vector_push_back(machine->mmio, *mmio);
    dev = &vector_at(machine->mmio, vector_size(machine->mmio)-1);
    dev->machine = machine;
    rvvm_info("Attached MMIO device at 0x%08"PRIxXLEN", type \"%s\"", dev->begin, dev->type ? dev->type->name : "null");
}

PUBLIC void rvvm_detach_mmio(rvvm_machine_t* machine, paddr_t mmio_addr)
{
    if (machine->running) return;
    vector_foreach(machine->mmio, i) {
        if (mmio_addr >= vector_at(machine->mmio, i).begin
         && mmio_addr <= vector_at(machine->mmio, i).end) {
             vector_erase(machine->mmio, i);
        }
    }
}

PUBLIC void rvvm_enable_builtin_eventloop(bool enabled)
{
    bool stop_thread = false;
    spin_lock(&global_lock);
    if (builtin_eventloop_enabled && !enabled && vector_size(global_machines)) {
        builtin_eventloop_enabled = false;
        stop_thread = true;
    } else if (!builtin_eventloop_enabled && enabled && vector_size(global_machines)) {
        builtin_eventloop_enabled = true;
        builtin_eventloop_thread = thread_create(builtin_eventloop, NULL);
    }
    spin_unlock(&global_lock);
    
    if (stop_thread) {
        thread_signal_membarrier(builtin_eventloop_thread);
        thread_join(builtin_eventloop_thread);
    }
}

PUBLIC void rvvm_run_eventloop()
{
    builtin_eventloop((void*)(size_t)1);
}

PUBLIC void rvvm_run_machine_singlethread(rvvm_machine_t* machine)
{
    if (machine->running) return;
    machine->running = true;
    // I don't have the slightest idea how the preemptive timer,
    // nor the async peripherals should work now,
    // but for dumb environments might suffice
    riscv_hart_run(&vector_at(machine->harts, 0));
}
