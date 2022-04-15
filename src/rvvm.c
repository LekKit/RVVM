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

static spinlock_t global_lock = SPINLOCK_INIT;
static vector_t(rvvm_machine_t*) global_machines = {0};

static thread_handle_t builtin_eventloop_thread;
static bool builtin_eventloop_enabled = true;

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
        fdt_node_add_prop(cpu, "compatible", "rvvm\0riscv\0", 11);
        fdt_node_add_prop_u32(cpu, "clock-frequency", 3000000000);
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
    machine->fdt_soc = soc;
}

static void rvvm_gen_dtb(rvvm_machine_t* machine)
{
    if (machine->cmdline) {
        struct fdt_node* chosen = fdt_node_find(machine->fdt, "chosen");
        fdt_node_add_prop_str(chosen, "bootargs", machine->cmdline);
        free(machine->cmdline);
        machine->cmdline = NULL;
    }

    if (machine->dtb_addr) {
        rvvm_info("DTB already specified, skipping FDT generation");
    } else {
        machine->dtb_addr = machine->mem.begin + (machine->mem.size >> 1);
        size_t dtb_size = fdt_serialize(machine->fdt, machine->mem.data + (machine->mem.size >> 1), machine->mem.size >> 1, 0);
        if (dtb_size) {
            rvvm_info("Generated DTB at 0x%08"PRIxXLEN", size %u", machine->dtb_addr, (uint32_t)dtb_size);
        } else {
            rvvm_error("Generated DTB does not fit in RAM!");
        }
    }
}
#endif

#define RVVM_POWER_ON    0
#define RVVM_POWER_RESET 1
#define RVVM_POWER_OFF   2

static void rvvm_reset_machine_state(rvvm_machine_t* machine)
{
    machine->power_state = RVVM_POWER_ON;
#ifdef USE_FDT
    rvvm_gen_dtb(machine);
#endif
    rvtimer_init(&machine->timer, 10000000); // 10 MHz timer
    vector_foreach(machine->harts, i) {
        rvvm_hart_t* vm = &vector_at(machine->harts, i);
        vm->timer = machine->timer;
        // a0 register & mhartid csr contain hart ID
        vm->csr.hartid = i;
        vm->registers[REGISTER_X10] = i;
        // a1 register contains FDT address
        if (machine->dtb_addr) vm->registers[REGISTER_X11] = machine->dtb_addr;
        // Boot from ram base addr by default
        vm->registers[REGISTER_PC] = vm->mem.begin;
        riscv_switch_priv(vm, PRIVILEGE_MACHINE);
    }
    machine->dtb_addr = 0;
}

static void* builtin_eventloop(void* arg)
{
    rvvm_machine_t* machine;
    rvvm_mmio_dev_t* dev;
    
    // The eventloop runs while its enabled/ran manually,
    // and there are any running machines
    while (builtin_eventloop_enabled || arg) {
        spin_lock_slow(&global_lock);
        if (vector_size(global_machines) == 0) {
            thread_detach(builtin_eventloop_thread);
            builtin_eventloop_thread = NULL;
            spin_unlock(&global_lock);
            break;
        }
        vector_foreach(global_machines, m) {
            machine = vector_at(global_machines, m);
            if (machine->power_state != RVVM_POWER_ON) {
                // The machine was shut down or reset
                vector_foreach(machine->harts, i) {
                    riscv_hart_pause(&vector_at(machine->harts, i));
                }
                // Call reset/poweroff handler
                bool do_reset = machine->power_state == RVVM_POWER_RESET;
                if (machine->on_reset) {
                    do_reset &= machine->on_reset(machine, machine->reset_data, do_reset);
                }
                if (do_reset) {
                    rvvm_info("Machine %p resetting", machine);
                    rvvm_reset_machine_state(machine);
                    vector_foreach(machine->harts, i) {
                        riscv_hart_spawn(&vector_at(machine->harts, i));
                    }
                } else {
                    rvvm_info("Machine %p shutting down", machine);
                    machine->running = false;
                    vector_erase(global_machines, m);
                    break;
                }
            }
            
            vector_foreach(machine->harts, i) {
                // Wake hart thread to check timer interrupt.
                if (rvtimer_pending(&vector_at(machine->harts, i).timer)) {
                    riscv_hart_check_timer(&vector_at(machine->harts, i));
                }
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
        sleep_ms(10);
    }

    return arg;
}

PUBLIC rvvm_machine_t* rvvm_create_machine(rvvm_addr_t mem_base, size_t mem_size, size_t hart_count, bool rv64)
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
    vector_init(machine->harts);
    vector_init(machine->mmio);
    for (size_t i=0; i<hart_count; ++i) {
        vector_emplace_back(machine->harts);
        vm = &vector_at(machine->harts, i);
        riscv_hart_init(vm, rv64);
        vm->machine = machine;
        vm->mem = machine->mem;
    }
    machine->power_state = RVVM_POWER_OFF;
#ifdef USE_FDT
    rvvm_init_fdt(machine);
#endif
    return machine;
}

PUBLIC bool rvvm_write_ram(rvvm_machine_t* machine, rvvm_addr_t dest, const void* src, size_t size)
{
    if (dest < machine->mem.begin
    || (dest - machine->mem.begin + size) > machine->mem.size) return false;
    memcpy(machine->mem.data + (dest - machine->mem.begin), src, size);
    return true;
}

PUBLIC bool rvvm_read_ram(rvvm_machine_t* machine, void* dest, rvvm_addr_t src, size_t size)
{
    if (src < machine->mem.begin
    || (src - machine->mem.begin + size) > machine->mem.size) return false;
    memcpy(dest, machine->mem.data + (src - machine->mem.begin), size);
    return true;
}

PUBLIC void* rvvm_get_dma_ptr(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size)
{
    if (addr < machine->mem.begin
    || (addr - machine->mem.begin + size) > machine->mem.size) return NULL;
    return machine->mem.data + (addr - machine->mem.begin);
}

PUBLIC bool rvvm_mmio_none(rvvm_mmio_dev_t* dev, void* dest, size_t offset, uint8_t size)
{
    UNUSED(dev);
    UNUSED(dest);
    UNUSED(offset);
    UNUSED(size);
    return true;
}

PUBLIC struct fdt_node* rvvm_get_fdt_root(rvvm_machine_t* machine)
{
#ifdef USE_FDT
    return machine->fdt;
#else
    UNUSED(machine);
    return NULL;
#endif
}

PUBLIC struct fdt_node* rvvm_get_fdt_soc(rvvm_machine_t* machine)
{
#ifdef USE_FDT
    return machine->fdt_soc;
#else
    UNUSED(machine);
    return NULL;
#endif
}


PUBLIC void rvvm_set_dtb_addr(rvvm_machine_t* machine, rvvm_addr_t dtb_addr)
{
    machine->dtb_addr = dtb_addr;
}

PUBLIC void rvvm_cmdline_set(rvvm_machine_t* machine, const char* str)
{
#ifdef USE_FDT
    free(machine->cmdline);
    machine->cmdline = NULL;
    rvvm_cmdline_append(machine, str);
#else
    UNUSED(machine);
    UNUSED(str);
#endif
}

PUBLIC void rvvm_cmdline_append(rvvm_machine_t* machine, const char* str)
{
#ifdef USE_FDT
    size_t cmd_len = machine->cmdline ? strlen(machine->cmdline) : 0;
    size_t append_len = strlen(str);
    char* tmp = safe_calloc(sizeof(char), cmd_len + append_len + 2);
    if (machine->cmdline) memcpy(tmp, machine->cmdline, cmd_len);
    memcpy(tmp + cmd_len, str, append_len);
    tmp[cmd_len + append_len] = ' ';
    tmp[cmd_len + append_len + 1] = 0;
    free(machine->cmdline);
    machine->cmdline = tmp;
#else
    UNUSED(machine);
    UNUSED(str);
#endif
}

PUBLIC void rvvm_set_reset_handler(rvvm_machine_t* machine, rvvm_reset_handler_t handler, void* data)
{
    machine->on_reset = handler;
    machine->reset_data = data;
}

PUBLIC void rvvm_start_machine(rvvm_machine_t* machine)
{
    spin_lock_slow(&global_lock);
    if (atomic_swap_uint32(&machine->running, true)) {
        spin_unlock(&global_lock);
        return;
    }

    if (machine->power_state == RVVM_POWER_OFF) rvvm_reset_machine_state(machine);
    vector_foreach(machine->harts, i) {
        riscv_hart_spawn(&vector_at(machine->harts, i));
    }

    if (vector_size(global_machines) == 0) {
        vector_init(global_machines);
    }
    vector_push_back(global_machines, machine);
    if (builtin_eventloop_enabled && builtin_eventloop_thread == NULL) {
        builtin_eventloop_thread = thread_create(builtin_eventloop, NULL);
    }
    spin_unlock(&global_lock);
}

PUBLIC void rvvm_pause_machine(rvvm_machine_t* machine)
{
    thread_handle_t stop_thread = NULL;
    spin_lock_slow(&global_lock);
    if (!atomic_swap_uint32(&machine->running, false)) {
        spin_unlock(&global_lock);
        return;
    }

    vector_foreach(machine->harts, i) {
        riscv_hart_pause(&vector_at(machine->harts, i));
    }

    vector_foreach(global_machines, i) {
        if (vector_at(global_machines, i) == machine) {
            vector_erase(global_machines, i);
        }
    }
    if (vector_size(global_machines) == 0) {
        vector_free(global_machines);
        // prevent deadlock
        stop_thread = builtin_eventloop_thread;
        builtin_eventloop_thread = NULL;
    }
    spin_unlock(&global_lock);

    if (stop_thread) {
        thread_signal_membarrier(stop_thread);
        thread_join(stop_thread);
    }
}

PUBLIC void rvvm_reset_machine(rvvm_machine_t* machine, bool reset)
{
    spin_lock(&global_lock);
    // Handled by eventloop
    machine->power_state = reset ? RVVM_POWER_RESET : RVVM_POWER_OFF;

    // For singlethreaded VMs, returns from riscv_hart_run()
    if (vector_size(machine->harts) == 1) {
        riscv_hart_queue_pause(&vector_at(machine->harts, 0));
    }
    spin_unlock(&global_lock);
}

PUBLIC bool rvvm_machine_powered_on(rvvm_machine_t* machine)
{
    return machine->power_state != RVVM_POWER_OFF;
}

PUBLIC void rvvm_free_machine(rvvm_machine_t* machine)
{
    rvvm_mmio_dev_t* dev;
    rvvm_pause_machine(machine);
    
    vector_foreach(machine->harts, i) {
        riscv_hart_free(&vector_at(machine->harts, i));
    }

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

// Regions of size 0 are ignored (those are non-IO placeholders)
PUBLIC rvvm_addr_t rvvm_mmio_zone_auto(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size)
{
    for (size_t i=0; i<64; ++i) {
        if (size && addr >= machine->mem.begin && (addr + size) <= (machine->mem.begin + machine->mem.size)) {
            addr = machine->mem.begin + machine->mem.size;
            continue;
        }

        vector_foreach(machine->mmio, i) {
            struct rvvm_mmio_dev_t *dev = &vector_at(machine->mmio, i);
            if (size && addr >= dev->addr && (addr + size) <= (dev->addr + dev->size)) {
                addr = dev->addr + dev->size;
                continue;
            }
        }

        return addr;
    }

    rvvm_warn("Cannot find free MMIO range!");
    return addr + 0x1000;
}

PUBLIC rvvm_mmio_handle_t rvvm_attach_mmio(rvvm_machine_t* machine, const rvvm_mmio_dev_t* mmio)
{
    rvvm_mmio_dev_t* dev;
    if (machine->running) return RVVM_INVALID_MMIO;
    if (rvvm_mmio_zone_auto(machine, mmio->addr, mmio->size) != mmio->addr) {
        rvvm_warn("Cannot attach MMIO device \"%s\" to occupied region 0x%08"PRIx64"", mmio->type ? mmio->type->name : "null", mmio->addr);
        return RVVM_INVALID_MMIO;
    }
    vector_push_back(machine->mmio, *mmio);
    rvvm_mmio_handle_t ret = vector_size(machine->mmio) - 1;
    dev = &vector_at(machine->mmio, ret);
    dev->machine = machine;
    rvvm_info("Attached MMIO device at 0x%08"PRIx64", type \"%s\"", dev->addr, dev->type ? dev->type->name : "null");
    return ret;
}

PUBLIC void rvvm_detach_mmio(rvvm_machine_t* machine, rvvm_addr_t mmio_addr)
{
    if (machine->running) return;
    vector_foreach(machine->mmio, i) {
        struct rvvm_mmio_dev_t *dev = &vector_at(machine->mmio, i);
        if (mmio_addr >= dev->addr
         && mmio_addr < (dev->addr + dev->size)) {
            /* do not remove the machine from vector so that the handles
             * remain valid */
            dev->size = 0;
        }
    }
}

PUBLIC void rvvm_enable_builtin_eventloop(bool enabled)
{
    thread_handle_t stop_thread = NULL;
    spin_lock_slow(&global_lock);
    if (builtin_eventloop_enabled != enabled) {
        builtin_eventloop_enabled = enabled;
        if (!enabled) {
            stop_thread = builtin_eventloop_thread;
            builtin_eventloop_thread = NULL;
        } else if (builtin_eventloop_thread == NULL) {
            builtin_eventloop_thread = thread_create(builtin_eventloop, NULL);
        }
    }
    spin_unlock(&global_lock);
    
    if (stop_thread) {
        thread_signal_membarrier(stop_thread);
        thread_join(stop_thread);
    }
}

PUBLIC void rvvm_run_eventloop()
{
    rvvm_enable_builtin_eventloop(false);
    builtin_eventloop((void*)(size_t)1);
}

PUBLIC void rvvm_run_machine_singlethread(rvvm_machine_t* machine)
{
    if (machine->running) return;
    machine->running = true;
    if (machine->power_state != RVVM_POWER_ON) rvvm_reset_machine_state(machine);
    // I don't have the slightest idea how the preemptive timer,
    // nor the async peripherals should work now,
    // but for dumb environments might suffice
    riscv_hart_run(&vector_at(machine->harts, 0));
}
