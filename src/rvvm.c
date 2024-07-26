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
#include "rvvm_isolation.h"
#include "riscv_hart.h"
#include "riscv_mmu.h"
#include "riscv_cpu.h"
#include "vector.h"
#include "utils.h"
#include "mem_ops.h"
#include "threading.h"
#include "spinlock.h"
#include "elf_load.h"

static spinlock_t global_lock = SPINLOCK_INIT;
static vector_t(rvvm_machine_t*) global_machines = {0};
static cond_var_t* builtin_eventloop_cond;
static thread_ctx_t* builtin_eventloop_thread;
static bool builtin_eventloop_enabled = true;
static bool builtin_eventloop_running = false;

#ifdef USE_FDT
static void rvvm_init_fdt(rvvm_machine_t* machine)
{
    machine->fdt = fdt_node_create(NULL);
    fdt_node_add_prop_u32(machine->fdt, "#address-cells", 2);
    fdt_node_add_prop_u32(machine->fdt, "#size-cells", 2);
    fdt_node_add_prop_str(machine->fdt, "model", "RVVM v"RVVM_VERSION);
    fdt_node_add_prop(machine->fdt, "compatible", "lekkit,rvvm\0riscv-virtio\0", 25);

    struct fdt_node* chosen = fdt_node_create("chosen");
    uint8_t rng_buffer[64] = {0};
    rvvm_randombytes(rng_buffer, sizeof(rng_buffer));
    fdt_node_add_prop(chosen, "rng-seed", rng_buffer, sizeof(rng_buffer));
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
        fdt_node_add_prop(cpu, "compatible", "lekkit,rvvm\0riscv\0", 18);
        fdt_node_add_prop_u32(cpu, "clock-frequency", 3000000000);
#ifdef USE_RV64
        if (vector_at(machine->harts, i)->rv64) {
#ifdef USE_FPU
            fdt_node_add_prop_str(cpu, "riscv,isa", "rv64imafdc_zicsr_zifencei");
#else
            fdt_node_add_prop_str(cpu, "riscv,isa", "rv64imac_zicsr_zifencei");
#endif
            fdt_node_add_prop_str(cpu, "mmu-type", "riscv,sv39");
        } else {
#endif
#ifdef USE_FPU
            fdt_node_add_prop_str(cpu, "riscv,isa", "rv32imafdc_zicsr_zifencei");
#else
            fdt_node_add_prop_str(cpu, "riscv,isa", "rv32imac_zicsr_zifencei");
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

static void rvvm_prepare_fdt(rvvm_machine_t* machine)
{
    if (rvvm_get_opt(machine, RVVM_OPT_HW_IMITATE)) {
        fdt_node_add_prop_str(machine->fdt, "model", "PlasmaArc Five");
        fdt_node_add_prop_str(machine->fdt, "compatible", "lekkit,arc7xx");

        vector_foreach(machine->harts, i) {
            struct fdt_node* cpus = fdt_node_find(machine->fdt, "cpus");
            struct fdt_node* cpu = fdt_node_find_reg(cpus, "cpu", i);
            fdt_node_add_prop(cpu, "compatible", "lekkit,arc5xx\0riscv\0", 20);
        }
    }
}
#endif

#define RVVM_POWER_OFF   0
#define RVVM_POWER_ON    1
#define RVVM_POWER_RESET 2

static size_t rvvm_dtb_addr(rvvm_machine_t* machine, size_t dtb_size)
{
    return align_size_down(machine->mem.size > dtb_size ? machine->mem.size - dtb_size : 0, 8);
}

static rvvm_addr_t rvvm_pass_dtb(rvvm_machine_t* machine)
{
    if (rvvm_get_opt(machine, RVVM_OPT_DTB_ADDR)) {
        // API user manually passes DTB
        return rvvm_get_opt(machine, RVVM_OPT_DTB_ADDR);
    } else if (machine->dtb_file) {
        // Load DTB from file
        uint32_t dtb_size = rvfilesize(machine->dtb_file);
        size_t dtb_off = rvvm_dtb_addr(machine, dtb_size);
        if (dtb_size < machine->mem.size) {
            rvread(machine->dtb_file, machine->mem.data + dtb_off, machine->mem.size - dtb_off, 0);
            rvvm_info("Loaded DTB at 0x%08"PRIxXLEN", size %u", machine->mem.begin + dtb_off, dtb_size);
            return machine->mem.begin + dtb_off;
        }
    } else {
        // Generate DTB
#ifdef USE_FDT
        rvvm_prepare_fdt(machine);
        uint32_t dtb_size = fdt_size(machine->fdt);
        size_t dtb_off = rvvm_dtb_addr(machine, dtb_size);
        if (fdt_serialize(machine->fdt, machine->mem.data + dtb_off, machine->mem.size - dtb_off, 0)) {
            rvvm_info("Generated DTB at 0x%08"PRIxXLEN", size %u", (phys_addr_t)(machine->mem.begin + dtb_off), dtb_size);
            return machine->mem.begin + dtb_off;
        }
#else
        rvvm_error("This build doesn't support FDT generation");
        return 0;
#endif
    }

    rvvm_error("Device tree does not fit in RAM!");
    return 0;
}

static bool rvvm_reset_machine_state(rvvm_machine_t* machine)
{
    atomic_store_uint32(&machine->power_state, RVVM_POWER_ON);
    // Call reset callback
    if (machine->on_reset && !machine->on_reset(machine, machine->reset_data, true)) {
        return false;
    }
    // Reset devices
    vector_foreach(machine->mmio, i) {
        rvvm_mmio_dev_t *dev = &vector_at(machine->mmio, i);
        if (dev->type && dev->type->reset) dev->type->reset(dev);
    }
    // Load bootrom, kernel, dtb into RAM if needed
    bool elf = !rvvm_get_opt(machine, RVVM_OPT_HW_IMITATE);
    if (machine->bootrom_file) {
        bin_objcopy(machine->bootrom_file, machine->mem.data, machine->mem.size, elf);
    }
    if (machine->kernel_file) {
        size_t kernel_offset = machine->rv64 ? 0x200000 : 0x400000;
        size_t kernel_size = machine->mem.size > kernel_offset ? machine->mem.size - kernel_offset : 0;
        bin_objcopy(machine->kernel_file, machine->mem.data + kernel_offset, kernel_size, elf);
    }
    rvvm_addr_t dtb_addr = rvvm_pass_dtb(machine);
    // Reset CPUs
    rvtimer_init(&machine->timer, 10000000); // 10 MHz timer
    vector_foreach(machine->harts, i) {
        rvvm_hart_t* vm = vector_at(machine->harts, i);
        vm->timer = machine->timer;
        // a0 register & mhartid csr contain hart ID
        vm->csr.hartid = i;
        vm->registers[REGISTER_X10] = i;
        // a1 register contains FDT address
        vm->registers[REGISTER_X11] = dtb_addr;
        // Jump to RESET_PC
        vm->registers[REGISTER_PC] = rvvm_get_opt(machine, RVVM_OPT_RESET_PC);
        riscv_switch_priv(vm, PRIVILEGE_MACHINE);
        riscv_jit_flush_cache(vm);
    }
    return true;
}

static void* rvvm_eventloop(void* manual)
{
    if (!manual && rvvm_getarg_int("noisolation") < 1) {
        rvvm_restrict_this_thread();
    }
    /*
     * The eventloop runs in a separate thread if it's enabled,
     * and returns on any machine shutdown if ran manually.
     */
    while (true) {
        spin_lock_slow(&global_lock);
        if (vector_size(global_machines) == 0 || builtin_eventloop_enabled == !!manual) {
            builtin_eventloop_running = false;
            spin_unlock(&global_lock);
            break;
        }
        vector_foreach_back(global_machines, m) {
            rvvm_machine_t* machine = vector_at(global_machines, m);
            uint32_t power_state = atomic_load_uint32(&machine->power_state);

            if (power_state == RVVM_POWER_ON) {
                vector_foreach(machine->harts, i) {
                    rvvm_hart_t* vm = vector_at(machine->harts, i);
                    // Wake hart thread to check timer interrupt.
                    if ((vm->csr.ie & INTERRUPT_MTIMER) && rvtimer_pending(&vm->timer)) {
                        riscv_hart_check_timer(vector_at(machine->harts, i));
                    }
                    if (rvvm_get_opt(machine, RVVM_OPT_MAX_CPU_CENT) < 100) {
                        uint32_t preempt = 10 - ((10 * rvvm_get_opt(machine, RVVM_OPT_MAX_CPU_CENT) + 9) / 100);
                        riscv_hart_preempt(vm, preempt);
                    }
                }

                vector_foreach(machine->mmio, i) {
                    rvvm_mmio_dev_t* dev = &vector_at(machine->mmio, i);
                    if (dev->type && dev->type->update) {
                        // Update device
                        dev->type->update(dev);
                    }
                }
            } else {
                // The machine was shut down or reset
                vector_foreach(machine->harts, i) {
                    riscv_hart_pause(vector_at(machine->harts, i));
                }
                // Call reset/poweroff handler
                if (power_state == RVVM_POWER_RESET && rvvm_reset_machine_state(machine)) {
                    rvvm_info("Machine %p resetting", machine);
                    vector_foreach(machine->harts, i) {
                        riscv_hart_spawn(vector_at(machine->harts, i));
                    }
                } else {
                    if (machine->on_reset) {
                        machine->on_reset(machine, machine->reset_data, false);
                    }
                    rvvm_info("Machine %p shutting down", machine);
                    atomic_store_uint32(&machine->running, false);
                    vector_erase(global_machines, m);
                    if (manual) {
                        spin_unlock(&global_lock);
                        return NULL;
                    }
                }
            }
        }
        spin_unlock(&global_lock);
        condvar_wait(builtin_eventloop_cond, 10);
    }

    return NULL;
}

static void reap_running_machines()
{
    // Check for any leftover machines (Invalid API usage)
    while (true) {
        rvvm_machine_t* machine = NULL;
        spin_lock(&global_lock);
        vector_foreach(global_machines, m) {
            machine = vector_at(global_machines, m);
            break;
        }
        spin_unlock(&global_lock);
        if (machine == NULL) break;
        rvvm_warn("Reaping leftover machine %p", (void*)machine);
        rvvm_free_machine(machine);
    }
    // Join on the eventloop thread
    condvar_wake(builtin_eventloop_cond);
    thread_join(builtin_eventloop_thread);
    vector_free(global_machines);
}

static void setup_eventloop()
{
    DO_ONCE({
        builtin_eventloop_cond = condvar_create();
        call_at_deinit(reap_running_machines);
    });
    if (builtin_eventloop_enabled && vector_size(global_machines) && !builtin_eventloop_running) {
        builtin_eventloop_running = true;
        thread_join(builtin_eventloop_thread);
        builtin_eventloop_thread = thread_create(rvvm_eventloop, NULL);
    }
    if (!builtin_eventloop_enabled && builtin_eventloop_running) {
        condvar_wake(builtin_eventloop_cond);
    }
}

PUBLIC bool rvvm_mmio_none(rvvm_mmio_dev_t* dev, void* dest, size_t offset, uint8_t size)
{
    UNUSED(dev);
    UNUSED(offset);
    memset(dest, 0, size);
    return true;
}

PUBLIC rvvm_machine_t* rvvm_create_machine(rvvm_addr_t mem_base, size_t mem_size, size_t hart_count, bool rv64)
{
#ifndef USE_RV64
    if (rv64) {
        rvvm_error("RV64 is disabled in this RVVM build");
        return NULL;
    }
#endif
    if (hart_count == 0) {
        rvvm_error("Creating machine with no harts at all... What are you even??");
        return NULL;
    }
    if (hart_count > 1024) {
        rvvm_error("Invalid machine core count");
        return NULL;
    }
    if (!rv64 && mem_size > (1U << 30)) {
        // Workaround for SBI/Linux hangs on incorrect machine config
        rvvm_warn("Creating RV32 machine with >1G of RAM is likely to break, fixing");
        mem_size = 1U << 30;
    }

    rvvm_machine_t* machine = safe_new_obj(rvvm_machine_t);
    machine->rv64 = rv64;
    if (!riscv_init_ram(&machine->mem, mem_base, mem_size)) {
        free(machine);
        return NULL;
    }

    // Default options
#ifdef USE_JIT
    rvvm_set_opt(machine, RVVM_OPT_JIT, !rvvm_has_arg("nojit"));
    rvvm_set_opt(machine, RVVM_OPT_JIT_HARWARD, rvvm_has_arg("rvjit_harward"));
    if (rvvm_getarg_size("jitcache")) {
        rvvm_set_opt(machine, RVVM_OPT_JIT_CACHE, rvvm_getarg_size("jitcache"));
    } else {
        size_t jit_cache = 16 << 20;
        if (mem_size >= (512U << 20)) jit_cache = 32 << 20;
        if (mem_size >= (1U << 30))   jit_cache = 64 << 20;
        // Default 16M-64M JIT cache per hart (depends on RAM)
        rvvm_set_opt(machine, RVVM_OPT_JIT_CACHE, jit_cache);
    }
#endif
    rvvm_set_opt(machine, RVVM_OPT_MAX_CPU_CENT, 100);
    rvvm_set_opt(machine, RVVM_OPT_RESET_PC, mem_base);

    for (size_t i=0; i<hart_count; ++i) {
        vector_push_back(machine->harts, riscv_hart_init(machine));
    }
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
    riscv_jit_mark_dirty_mem(machine, dest, size);
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
    riscv_jit_mark_dirty_mem(machine, addr, size);
    return machine->mem.data + (addr - machine->mem.begin);
}

PUBLIC void rvvm_flush_icache(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size)
{
    // WIP, issue a total cache flush on all harts
    // Needs improvements in RVJIT
    UNUSED(addr);
    UNUSED(size);
    spin_lock_slow(&global_lock);
    vector_foreach(machine->harts, i) {
        riscv_jit_flush_cache(vector_at(machine->harts, i));
    }
    spin_unlock(&global_lock);
}

PUBLIC plic_ctx_t* rvvm_get_plic(rvvm_machine_t* machine)
{
    return machine->plic;
}

PUBLIC void rvvm_set_plic(rvvm_machine_t* machine, plic_ctx_t* plic)
{
    if (plic) machine->plic = plic;
}

PUBLIC pci_bus_t* rvvm_get_pci_bus(rvvm_machine_t* machine)
{
    return machine->pci_bus;
}

PUBLIC void rvvm_set_pci_bus(rvvm_machine_t* machine, pci_bus_t* pci_bus)
{
    if (pci_bus) machine->pci_bus = pci_bus;
}

PUBLIC i2c_bus_t* rvvm_get_i2c_bus(rvvm_machine_t* machine)
{
    return machine->i2c_bus;
}

PUBLIC void rvvm_set_i2c_bus(rvvm_machine_t* machine, i2c_bus_t* i2c_bus)
{
    if (i2c_bus) machine->i2c_bus = i2c_bus;
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

PUBLIC void rvvm_set_cmdline(rvvm_machine_t* machine, const char* str)
{
#ifdef USE_FDT
    free(machine->cmdline);
    machine->cmdline = NULL;
    rvvm_append_cmdline(machine, str);
#else
    UNUSED(machine);
    UNUSED(str);
#endif
}

PUBLIC void rvvm_append_cmdline(rvvm_machine_t* machine, const char* str)
{
#ifdef USE_FDT
    size_t cmd_len = machine->cmdline ? rvvm_strlen(machine->cmdline) : 0;
    size_t append_len = rvvm_strlen(str);
    char* tmp = safe_new_arr(char, cmd_len + append_len + (cmd_len ? 2 : 1));
    if (cmd_len) {
        memcpy(tmp, machine->cmdline, cmd_len);
        tmp[cmd_len++] = ' ';
    }
    memcpy(tmp + cmd_len, str, append_len);
    free(machine->cmdline);
    machine->cmdline = tmp;
    struct fdt_node* chosen = fdt_node_find(machine->fdt, "chosen");
    fdt_node_del_prop(chosen, "bootargs");
    fdt_node_add_prop_str(chosen, "bootargs", machine->cmdline);
#else
    UNUSED(machine);
    UNUSED(str);
#endif
}

PUBLIC rvvm_addr_t rvvm_get_opt(rvvm_machine_t* machine, uint32_t opt)
{
    if (opt < RVVM_MAX_OPTS) {
        return atomic_load_uint64_ex(&machine->opts[opt], ATOMIC_RELAXED);
    }
    switch (opt) {
        case RVVM_OPT_MEM_BASE: return machine->mem.begin;
        case RVVM_OPT_MEM_SIZE: return machine->mem.size;
        case RVVM_OPT_HART_COUNT: return vector_size(machine->harts);
    }
    return 0;
}

PUBLIC bool rvvm_set_opt(rvvm_machine_t* machine, uint32_t opt, rvvm_addr_t val)
{
    if (opt >= RVVM_MAX_OPTS) return false;
    atomic_store_uint64_ex(&machine->opts[opt], val, ATOMIC_RELAXED);
    return true;
}

PUBLIC void rvvm_set_reset_handler(rvvm_machine_t* machine, rvvm_reset_handler_t handler, void* data)
{
    machine->on_reset = handler;
    machine->reset_data = data;
}

static bool file_reopen_check_size(rvfile_t** dest, const char* path, size_t size)
{
    rvclose(*dest);
    if (path) {
        *dest = rvopen(path, 0);
        if (*dest == NULL) {
            rvvm_error("Could not open file %s", path);
            return false;
        }
        if (rvfilesize(*dest) > size) {
            rvvm_error("File %s doesn't fit in RAM", path);
            rvclose(*dest);
            *dest = NULL;
            return false;
        }
    } else {
        *dest = NULL;
    }
    return true;
}

PUBLIC bool rvvm_load_bootrom(rvvm_machine_t* machine, const char* path)
{
    return file_reopen_check_size(&machine->bootrom_file, path, machine->mem.size);
}

PUBLIC bool rvvm_load_kernel(rvvm_machine_t* machine, const char* path)
{
    size_t kernel_offset = machine->rv64 ? 0x200000 : 0x400000;
    size_t kernel_size = machine->mem.size > kernel_offset ? machine->mem.size - kernel_offset : 0;
    return file_reopen_check_size(&machine->kernel_file, path, kernel_size);
}

PUBLIC bool rvvm_load_dtb(rvvm_machine_t* machine, const char* path)
{
    return file_reopen_check_size(&machine->dtb_file, path, machine->mem.size >> 1);
}

PUBLIC bool rvvm_dump_dtb(rvvm_machine_t* machine, const char* path)
{
#ifdef USE_FDT
    rvfile_t* file = rvopen(path, RVFILE_RW | RVFILE_CREAT | RVFILE_TRUNC);
    if (file) {
        size_t size = fdt_size(rvvm_get_fdt_root(machine));
        void* buffer = safe_calloc(size, 1);
        size = fdt_serialize(rvvm_get_fdt_root(machine), buffer, size, 0);
        rvwrite(file, buffer, size, 0);
        rvclose(file);
        free(buffer);
        return true;
    }
#else
    UNUSED(machine);
    UNUSED(path);
    rvvm_error("This build doesn't support FDT generation");
#endif
    return false;
}

PUBLIC bool rvvm_start_machine(rvvm_machine_t* machine)
{
    if (atomic_swap_uint32(&machine->running, true)) {
        return false;
    }

    spin_lock_slow(&global_lock);

    if (!rvvm_machine_powered(machine)) {
        rvvm_reset_machine_state(machine);
    }
    vector_foreach(machine->harts, i) {
        riscv_hart_prepare(vector_at(machine->harts, i));
    }
    vector_foreach(machine->harts, i) {
        riscv_hart_spawn(vector_at(machine->harts, i));
    }

    // Register the machine as running
    vector_push_back(global_machines, machine);
    setup_eventloop();
    spin_unlock(&global_lock);
    return true;
}

PUBLIC bool rvvm_pause_machine(rvvm_machine_t* machine)
{
    if (!atomic_swap_uint32(&machine->running, false)) {
        return false;
    }

    spin_lock_slow(&global_lock);

    vector_foreach(machine->harts, i) {
        riscv_hart_pause(vector_at(machine->harts, i));
    }

    vector_foreach(global_machines, i) {
        if (vector_at(global_machines, i) == machine) {
            vector_erase(global_machines, i);
            break;
        }
    }
    spin_unlock(&global_lock);
    return true;
}

PUBLIC void rvvm_reset_machine(rvvm_machine_t* machine, bool reset)
{
    // Handled by eventloop
    atomic_store_uint32(&machine->power_state, reset ? RVVM_POWER_RESET : RVVM_POWER_OFF);

    // For singlethreaded VMs, returns from riscv_hart_run()
    if (vector_size(machine->harts) == 1) {
        riscv_hart_queue_pause(vector_at(machine->harts, 0));
    }
    condvar_wake(builtin_eventloop_cond);
}

PUBLIC bool rvvm_machine_powered(rvvm_machine_t* machine)
{
    return atomic_load_uint32(&machine->power_state) != RVVM_POWER_OFF;
}

static void rvvm_cleanup_mmio(rvvm_mmio_dev_t* dev)
{
    rvvm_info("Removing MMIO device \"%s\"", dev->type ? dev->type->name : "null");
    // Either device implements it's own cleanup routine,
    // or we free it's data buffer
    if (dev->type && dev->type->remove)
        dev->type->remove(dev);
    else
        free(dev->data);
}

PUBLIC void rvvm_free_machine(rvvm_machine_t* machine)
{
    rvvm_pause_machine(machine);

    // Clean up devices in reversed order, something may reference older devices
    vector_foreach_back(machine->mmio, i) {
        rvvm_cleanup_mmio(&vector_at(machine->mmio, i));
    }

    vector_foreach(machine->harts, i) {
        riscv_hart_free(vector_at(machine->harts, i));
    }

    vector_free(machine->harts);
    vector_free(machine->mmio);
    riscv_free_ram(&machine->mem);
    rvclose(machine->bootrom_file);
    rvclose(machine->kernel_file);
    rvclose(machine->dtb_file);
#ifdef USE_FDT
    fdt_node_free(machine->fdt);
    free(machine->cmdline);
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

// Returns addr if zone is free
static rvvm_addr_t rvvm_mmio_zone_check(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size)
{
    if (addr >= machine->mem.begin && (addr + size) <= (machine->mem.begin + machine->mem.size)) {
        addr = machine->mem.begin + machine->mem.size;
    }

    vector_foreach(machine->mmio, i) {
        struct rvvm_mmio_dev_t *dev = &vector_at(machine->mmio, i);
        if (dev->data == NULL && dev->mapping == NULL && dev->type == NULL) {
            // This is a leftover device which was removed from running VM
            rvvm_detach_mmio(machine, i, true);
        } else if (addr >= dev->addr && (addr + size) <= (dev->addr + dev->size)) {
            addr = dev->addr + dev->size;
        }
    }

    return addr;
}

// Regions of size 0 are ignored (those are non-IO placeholders)
PUBLIC rvvm_addr_t rvvm_mmio_zone_auto(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size)
{
    rvvm_addr_t ret = addr;
    if (size == 0) return addr;

    do {
        ret = rvvm_mmio_zone_check(machine, addr, size);
    } while (ret != addr);

    return ret;
}

PUBLIC rvvm_mmio_handle_t rvvm_attach_mmio(rvvm_machine_t* machine, const rvvm_mmio_dev_t* mmio)
{
    rvvm_mmio_dev_t dev = *mmio;
    dev.machine = machine;
    if (mmio->min_op_size > mmio->max_op_size || mmio->min_op_size > 8) {
        rvvm_warn("MMIO device \"%s\" has invalid op sizes: min %u, max %u",
                  mmio->type ? mmio->type->name : "null", mmio->min_op_size, mmio->max_op_size);
        rvvm_cleanup_mmio(&dev);
        return RVVM_INVALID_MMIO;
    }
    if (rvvm_mmio_zone_auto(machine, mmio->addr, mmio->size) != mmio->addr) {
        rvvm_warn("Cannot attach MMIO device \"%s\" to occupied region 0x%08"PRIx64"",
                  mmio->type ? mmio->type->name : "null", mmio->addr);
        rvvm_cleanup_mmio(&dev);
        return RVVM_INVALID_MMIO;
    }
    bool was_running = rvvm_pause_machine(machine);
    // Normalize access properties: Power of two, default 1 - 8 bytes
    dev.min_op_size = dev.min_op_size ? bit_next_pow2(dev.min_op_size) : 1;
    dev.max_op_size = dev.max_op_size ? bit_next_pow2(dev.max_op_size) : 8;
    vector_push_back(machine->mmio, dev);
    rvvm_mmio_handle_t ret = vector_size(machine->mmio) - 1;
    rvvm_info("Attached MMIO device at 0x%08"PRIx64", type \"%s\"",
              dev.addr, dev.type ? dev.type->name : "null");
    if (was_running) rvvm_start_machine(machine);
    return ret;
}

PUBLIC void rvvm_detach_mmio(rvvm_machine_t* machine, rvvm_mmio_handle_t handle, bool cleanup)
{
    if (handle >= 0 && (size_t)handle < vector_size(machine->mmio)) {
        bool was_running = rvvm_pause_machine(machine);
        struct rvvm_mmio_dev_t* dev = &vector_at(machine->mmio, handle);

        // Keep the placeholder device in vector so that the handles remain valid
        if (cleanup) rvvm_cleanup_mmio(dev);

        // It's a shared memory mapping, flush each hart TLB
        if (dev->mapping) {
            vector_foreach(machine->harts, i) {
                rvvm_hart_t* vm = vector_at(machine->harts, i);
                riscv_tlb_flush(vm);
            }
        }

        dev->data = NULL;
        dev->type = NULL;
        dev->mapping = NULL;
        dev->read = rvvm_mmio_none;
        dev->write = rvvm_mmio_none;

        // Tearing the device from running machine leaves a dummy range
        // Experimentally confirmed this actually happens on real boards
        if (!rvvm_machine_powered(machine)) dev->size = 0;

        if (was_running) rvvm_start_machine(machine);
    }
}

PUBLIC void rvvm_enable_builtin_eventloop(bool enabled)
{
    spin_lock_slow(&global_lock);
    builtin_eventloop_enabled = enabled;
    setup_eventloop();
    spin_unlock(&global_lock);
}

PUBLIC void rvvm_run_eventloop()
{
    rvvm_enable_builtin_eventloop(false);
    rvvm_eventloop((void*)(size_t)1);
}

//
// Userland emulation API (WIP)
//

PUBLIC rvvm_machine_t* rvvm_create_userland(bool rv64)
{
    rvvm_machine_t* machine = safe_new_obj(rvvm_machine_t);
    // Bypass entire process memory except the NULL page
    // RVVM expects mem.data to be non-NULL, let's leave that for now
    machine->mem.begin = 0x1000;
    machine->mem.size = (phys_addr_t)-0x1000ULL;
    machine->mem.data = (void*)0x1000;
    machine->rv64 = rv64;
    // I don't know what time CSR frequency userspace expects...
    rvtimer_init(&machine->timer, 1000000);
#ifdef USE_JIT
    rvvm_set_opt(machine, RVVM_OPT_JIT, true);
    rvvm_set_opt(machine, RVVM_OPT_JIT_HARWARD, true);
    rvvm_set_opt(machine, RVVM_OPT_JIT_CACHE, 16 << 20);
#endif
    return machine;
}

PUBLIC rvvm_cpu_handle_t rvvm_create_user_thread(rvvm_machine_t* machine)
{
    rvvm_hart_t* vm = riscv_hart_init(machine);
    riscv_hart_prepare(vm);
#ifdef USE_FPU
    // Initialize FPU by writing to status CSR
    maxlen_t mstatus = (FS_INITIAL << 13);
    riscv_csr_op(vm, 0x300, &mstatus, CSR_SETBITS);
#endif
#ifdef USE_JIT
    // Enable pointer optimization
    rvjit_set_native_ptrs(&vm->jit, true);
#endif
    riscv_switch_priv(vm, PRIVILEGE_USER);
    spin_lock_slow(&global_lock);
    vector_push_back(machine->harts, vm);
    spin_unlock(&global_lock);
    return (rvvm_cpu_handle_t)vm;
}

PUBLIC void rvvm_free_user_thread(rvvm_cpu_handle_t cpu)
{
    rvvm_hart_t* vm = (rvvm_hart_t*)cpu;
    spin_lock_slow(&global_lock);
    vector_foreach(vm->machine->harts, i) {
        if (vector_at(vm->machine->harts, i) == vm) {
            vector_erase(vm->machine->harts, i);
            riscv_hart_free(vm);
            spin_unlock(&global_lock);
            return;
        }
    }
    rvvm_fatal("Corrupted userland context!");
}

PUBLIC rvvm_addr_t rvvm_run_user_thread(rvvm_cpu_handle_t cpu)
{
    return riscv_hart_run_userland((rvvm_hart_t*)cpu);
}

PUBLIC rvvm_addr_t rvvm_read_cpu_reg(rvvm_cpu_handle_t cpu, size_t reg_id)
{
    rvvm_hart_t* vm = (rvvm_hart_t*)cpu;
    if (reg_id < (RVVM_REGID_X0 + 32)) {
        return vm->registers[reg_id - RVVM_REGID_X0];
#ifdef USE_FPU
    } else if (reg_id < (RVVM_REGID_F0 + 32)) {
        rvvm_addr_t ret;
        memcpy(&ret, &vm->fpu_registers[reg_id - RVVM_REGID_F0], sizeof(ret));
        return ret;
#endif
    } else if (reg_id == RVVM_REGID_PC) {
        return vm->registers[REGISTER_PC];
    } else if (reg_id == RVVM_REGID_CAUSE) {
        return vm->csr.cause[PRIVILEGE_USER];
    } else if (reg_id == RVVM_REGID_TVAL) {
        return vm->csr.tval[PRIVILEGE_USER];
    } else {
        rvvm_warn("Unknown register %d in rvvm_read_cpu_reg()!", (uint32_t)reg_id);
        return 0;
    }
}

PUBLIC void rvvm_write_cpu_reg(rvvm_cpu_handle_t cpu, size_t reg_id, rvvm_addr_t reg)
{
    rvvm_hart_t* vm = (rvvm_hart_t*)cpu;
    if (reg_id < (RVVM_REGID_X0 + 32)) {
        vm->registers[reg_id - RVVM_REGID_X0] = reg;
#ifdef USE_FPU
    } else if (reg_id < (RVVM_REGID_F0 + 32)) {
        memcpy(&vm->fpu_registers[reg_id - RVVM_REGID_F0], &reg, sizeof(reg));
#endif
    } else if (reg_id == RVVM_REGID_PC) {
        vm->registers[REGISTER_PC] = reg;
    } else if (reg_id == RVVM_REGID_CAUSE) {
        vm->csr.cause[PRIVILEGE_USER] = reg;
    } else if (reg_id == RVVM_REGID_TVAL) {
        vm->csr.tval[PRIVILEGE_USER] = reg;
    } else {
        rvvm_warn("Unknown register %d in rvvm_write_cpu_reg()!", (uint32_t)reg_id);
    }
}
