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

#ifdef USE_FDT
static void rvvm_init_fdt(rvvm_machine_t* machine)
{
    machine->fdt = fdt_node_create(NULL);
    fdt_node_add_prop_u32(machine->fdt, "#address-cells", 2);
    fdt_node_add_prop_u32(machine->fdt, "#size-cells", 2);
    // Weird workaround for OpenSBI bug with string copy
    fdt_node_add_prop_str(machine->fdt, "compatible", "RVVM   ");
    fdt_node_add_prop_str(machine->fdt, "model", "RVVM   ");
    
    struct fdt_node* chosen = fdt_node_create("chosen");
    fdt_node_add_child(machine->fdt, chosen);
    
    struct fdt_node* memory = fdt_node_create_reg("memory", machine->mem.begin);
    fdt_node_add_prop_str(memory, "device_type", "memory");
    fdt_node_add_prop_reg(memory, "reg", machine->mem.begin, machine->mem.size);
    fdt_node_add_child(machine->fdt, memory);
    
    struct fdt_node* cpus = fdt_node_create("cpus");
    fdt_node_add_prop_u32(cpus, "#address-cells", 1);
    fdt_node_add_prop_u32(cpus, "#size-cells", 0);
    fdt_node_add_prop_u32(cpus, "timebase-frequency", 10000000);
    
    struct fdt_node* cpu_map = fdt_node_create("cpu-map");
    struct fdt_node* cluster = fdt_node_create("cluster0");
    
    // Attach all the nodes to the root node before getting phandles
    fdt_node_add_child(machine->fdt, cpus);
    
    vector_foreach(machine->harts, i) {
        struct fdt_node* cpu = fdt_node_create_reg("cpu", i);
        
        fdt_node_add_prop_str(cpu, "device_type", "cpu");
        fdt_node_add_prop_u32(cpu, "reg", i);
        fdt_node_add_prop_str(cpu, "compatible", "riscv");
#ifdef USE_RV64
        if (vector_at(machine->harts, i).rv64) {
#ifdef USE_FPU
            fdt_node_add_prop_str(cpu, "riscv,isa", "rv64imafdcsu");
#else
            fdt_node_add_prop_str(cpu, "riscv,isa", "rv64imacsu");
#endif
            fdt_node_add_prop_str(cpu, "mmu-type", "riscv,sv39");
        } else {
#endif
#ifdef USE_FPU
            fdt_node_add_prop_str(cpu, "riscv,isa", "rv32imafdcsu");
#else
            fdt_node_add_prop_str(cpu, "riscv,isa", "rv32imacsu");
#endif
            fdt_node_add_prop_str(cpu, "mmu-type", "riscv,sv32");
#ifdef USE_RV64
        }
#endif
        fdt_node_add_prop_str(cpu, "status", "okay");
        
        struct fdt_node* clic = fdt_node_create("interrupt-controller");
        fdt_node_add_prop_u32(clic, "#interrupt-cells", 1);
        fdt_node_add_prop(clic, "interrupt-controller", NULL, 0);
        fdt_node_add_prop_str(clic, "compatible", "riscv,cpu-intc");
        fdt_node_add_child(cpu, clic);
        
        fdt_node_add_child(cpus, cpu);
        
        char core_name[32] = "core";
        int_to_str_dec(core_name + 4, 20, i);
        struct fdt_node* core = fdt_node_create(core_name);
        fdt_node_add_prop_u32(core, "cpu", fdt_node_get_phandle(cpu));
        fdt_node_add_child(cluster, core);
    }

    fdt_node_add_child(cpu_map, cluster);
    fdt_node_add_child(cpus, cpu_map);
    
    struct fdt_node* soc = fdt_node_create("soc");
    fdt_node_add_prop_u32(soc, "#address-cells", 2);
    fdt_node_add_prop_u32(soc, "#size-cells", 2);
    fdt_node_add_prop_str(soc, "compatible", "simple-bus");
    fdt_node_add_prop(soc, "ranges", NULL, 0);
    
    fdt_node_add_child(machine->fdt, soc);
}

static void rvvm_gen_dtb(rvvm_machine_t* machine)
{
    vector_foreach(machine->harts, i) {
        paddr_t a1 = vector_at(machine->harts, i).registers[REGISTER_X11];
        if (a1 >= machine->mem.begin && a1 < (machine->mem.begin + machine->mem.size)) {
            rvvm_info("DTB already present in a1 register, skipping");
            return;
        }
    }
    paddr_t dtb_addr = machine->mem.begin + (machine->mem.size >> 1);
    size_t dtb_size = fdt_serialize(machine->fdt, machine->mem.data + (machine->mem.size >> 1), machine->mem.size >> 1, 0);
    if (dtb_size) {
        rvvm_info("Generated DTB at 0x%08"PRIxXLEN", size %u", dtb_addr, (uint32_t)dtb_size);
        vector_foreach(machine->harts, i) {
            vector_at(machine->harts, i).registers[REGISTER_X11] = dtb_addr;
        }
    } else {
        rvvm_error("Generated DTB does not fit in RAM!");
    }
}
#endif

PUBLIC rvvm_machine_t* rvvm_create_machine(paddr_t mem_base, size_t mem_size, size_t hart_count, bool rv64)
{
    rvvm_hart_t* vm;
    rvvm_machine_t* machine = safe_calloc(sizeof(rvvm_machine_t), 1);
    if (hart_count == 0) {
        rvvm_warn("Creating machine with no harts at all... What are you even??");
    }
    if (!rv64 && mem_size > (1U << 30)) {
        // Workaround for SBI/Linux hangs on incorrect machine config
        rvvm_warn("Creating RV32 machine with >1G of RAM is likely to break, fixing");
        mem_size = 1U << 30;
    }
    if (!riscv_init_ram(&machine->mem, mem_base, mem_size)) {
        free(machine);
        return NULL;
    }
    rvtimer_init(&machine->timer, 10000000); // 10 MHz timer
    vector_init(machine->harts);
    vector_init(machine->mmio);
    for (size_t i=0; i<hart_count; ++i) {
        vector_emplace_back(machine->harts);
        vm = &vector_at(machine->harts, i);
        riscv_hart_init(vm, rv64);
        vm->timer = machine->timer;
        vm->machine = machine;
        vm->mem = machine->mem;
        // a0 register & mhartid csr contain hart ID
        vm->csr.hartid = i;
        vm->registers[REGISTER_X10] = i;
        // Boot from ram base addr by default
        vm->registers[REGISTER_PC] = vm->mem.begin;
    }
#ifdef USE_FDT
    rvvm_init_fdt(machine);
#endif
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
#ifdef USE_FDT
    rvvm_gen_dtb(machine);
#endif
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
#ifdef USE_FDT
    fdt_node_free(machine->fdt);
#endif
    free(machine);
}

PUBLIC rvvm_mmio_dev_t* rvvm_get_mmio(rvvm_machine_t *machine, rvvm_mmio_handle_t handle)
{
    if (handle < 0 || (size_t)handle >= vector_size(machine->mmio)) {
        return NULL;
    }

    return &vector_at(machine->mmio, (size_t)handle);
}

PUBLIC rvvm_mmio_handle_t rvvm_attach_mmio(rvvm_machine_t* machine, const rvvm_mmio_dev_t* mmio)
{
    rvvm_mmio_dev_t* dev;
    if (machine->running) return RVVM_INVALID_MMIO;

    vector_push_back(machine->mmio, *mmio);
    rvvm_mmio_handle_t ret = vector_size(machine->mmio) - 1;
    dev = &vector_at(machine->mmio, ret);
    dev->machine = machine;
    rvvm_info("Attached MMIO device at 0x%08"PRIxXLEN", type \"%s\"", dev->begin, dev->type ? dev->type->name : "null");
    return ret;
}

PUBLIC void rvvm_detach_mmio(rvvm_machine_t* machine, paddr_t mmio_addr)
{
    if (machine->running) return;
    vector_foreach(machine->mmio, i) {
        struct rvvm_mmio_dev_t *dev = &vector_at(machine->mmio, i);
        if (mmio_addr >= dev->begin
         && mmio_addr <= dev->end) {
            /* do not remove the machine from vector so that the handles
             * remain valid */
            dev->begin = dev->end = 0;
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
#ifdef USE_FDT
    rvvm_gen_dtb(machine);
#endif
    // I don't have the slightest idea how the preemptive timer,
    // nor the async peripherals should work now,
    // but for dumb environments might suffice
    riscv_hart_run(&vector_at(machine->harts, 0));
}
