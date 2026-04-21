/*
rvvm.c - librvvm API & Core
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "feature_test.h" // IWYU pragma: keep

#include "elf_load.h"
#include "mem_ops.h"
#include "riscv_cpu.h"
#include "riscv_hart.h"
#include "riscv_mmu.h"
#include "rvvm.h"
#include "rvvm_isolation.h"
#include "spinlock.h"
#include "stacktrace.h"
#include "threading.h"
#include "utils.h"
#include "vector.h"

PUSH_OPTIMIZATION_SIZE

#define RVVM_POWER_OFF       0
#define RVVM_POWER_ON        1
#define RVVM_POWER_RESET     2

// Default memory base address
#define RVVM_DEFAULT_MEMBASE 0x80000000U

static spinlock_t                global_lock     = ZERO_INIT;
static vector_t(rvvm_machine_t*) global_machines = ZERO_INIT;
static bool                      global_manual   = false;

static spinlock_t    eventloop_lock   = ZERO_INIT;
static cond_var_t*   eventloop_cond   = NULL;
static thread_ctx_t* eventloop_thread = NULL;

void rvvm_append_isa_string(rvvm_machine_t* machine, const char* str)
{
#if defined(USE_FDT)
    vector_foreach (machine->harts, i) {
        struct fdt_node* cpus = fdt_node_find(rvvm_get_fdt_root(machine), "cpus");
        struct fdt_node* cpu  = fdt_node_find_reg(cpus, "cpu", i);
        // Get previous riscv,isa
        const char* isa_str = fdt_node_get_prop_data(cpu, "riscv,isa");
        size_t      isa_len = isa_str ? rvvm_strlen(isa_str) : 0;
        if (isa_str && rvvm_strfind(isa_str, str)) {
            // String already present
            return;
        }
        // Append riscv,isa
        size_t str_len = rvvm_strlen(str);
        size_t new_len = isa_len + str_len;
        char*  new_str = safe_new_arr(char, new_len + 1);
        if (isa_str) {
            memcpy(new_str, isa_str, isa_len);
        }
        memcpy(new_str + isa_len, str, str_len);
        // Extract riscv,isa-base
        char   base[16] = "rv64i";
        size_t base_len = ((size_t)rvvm_strfind(new_str, "i")) - ((size_t)new_str) + 1;
        if (base_len < sizeof(base)) {
            rvvm_strlcpy(base, new_str, base_len + 1);
        } else {
            // Fallback parsing
            base_len = rvvm_strlen(base);
        }
        // Extract riscv,isa-extensions
        size_t ext_let = ((size_t)rvvm_strfind(new_str, "_")) - ((size_t)new_str);
        size_t ext_len = 0;
        char*  ext_str = safe_new_arr(char, new_len + EVAL_MIN(ext_let, new_len) + 1);
        for (size_t n = base_len - 1; n < new_len; ++n) {
            if (new_str[n] == '_') {
                size_t rem_len = new_len - n - 1;
                memcpy(ext_str + ext_len, new_str + n + 1, rem_len);
                for (size_t e = 0; e < rem_len; ++e) {
                    if (ext_str[ext_len + e] == '_') {
                        ext_str[ext_len + e] = 0;
                    }
                }
                ext_len += rem_len;
                break;
            } else {
                ext_str[ext_len]  = new_str[n];
                ext_len          += 2;
            }
        }
        // Pass new strings to fdt
        fdt_node_add_prop(cpu, "riscv,isa", new_str, new_len + 1);
        fdt_node_add_prop(cpu, "riscv,isa-base", base, base_len + 1);
        fdt_node_add_prop(cpu, "riscv,isa-extensions", ext_str, ext_len + 1);
        safe_free(new_str);
        safe_free(ext_str);
    }
#else
    UNUSED(machine);
    UNUSED(str);
#endif
}

#if defined(USE_FDT)

// zkr (cryptographic seed CSR) is wired up as a virtual entropy source per
// Section 4.4.3 of the ratified Zkr 1.0.1 spec: riscv_csr_seed returns
// OPST=ES16 + 16 bits of rvvm_csprng_bytes-backed entropy on every poll,
// and mseccfg.SSEED / USEED are set at hart reset (see rvvm_create_machine's
// thread init) so S-mode / U-mode guests can access it without trapping.
//
// Historical note: this advertisement was dropped briefly (commit 52c70d1)
// while the stub returned raw 16 bits with OPST=00 (BIST), which on Linux
// 6.18+ makes arch_get_random_seed_longs a 100-retry no-op. Re-added once
// the stub started returning ES16.
static const char* riscv_exts
    = "c_zic64b_zicbom_zicbop_zicboz_ziccamoa_ziccif_zicclsm_ziccrse_zicntr_zicond_zicsr_zifencei_zihintntl_"
      "zihintpause_zimop_zmmul_za64rs_zaamo_zabha_zacas_zalrsc_zawrs_zfa_zca_zcb_zcd_zcmop_zba_zbb_zbc_zbkb_zbkx_zbs_zkr_"
      "smcsrind_ssccptr_sscounterenw_sscsrind_sstc_sstvala_sstvecd_ssstrict_ssu64xl_svpbmt_svvptc_svadu_svbare";

static char* rvvm_merge_strings_internal(const char* str1, const char* str2)
{
    size_t str1_len = str1 ? rvvm_strlen(str1) : 0;
    size_t str2_len = str2 ? rvvm_strlen(str2) : 0;
    char*  ret      = safe_new_arr(char, str1_len + str2_len + 1);
    if (str1) {
        memcpy(ret, str1, str1_len);
    }
    if (str2) {
        memcpy(ret + str1_len, str2, str2_len);
    }
    return ret;
}

static void rvvm_init_fdt(rvvm_machine_t* machine)
{
    machine->fdt = fdt_node_create(NULL);
    fdt_node_add_prop_u32(machine->fdt, "#address-cells", 2);
    fdt_node_add_prop_u32(machine->fdt, "#size-cells", 2);
    fdt_node_add_prop_str(machine->fdt, "model", "RVVM " RVVM_VERSION);
    fdt_node_add_prop(machine->fdt, "compatible", "lekkit,rvvm\0riscv-virtio\0", 25);

    // FDT /chosen node
    struct fdt_node* chosen         = fdt_node_create("chosen");
    uint8_t          rng_buffer[64] = {0};
    rvvm_randombytes(rng_buffer, sizeof(rng_buffer));
    fdt_node_add_prop(chosen, "rng-seed", rng_buffer, sizeof(rng_buffer));
    fdt_node_add_child(machine->fdt, chosen);

    // FDT /memory node
    struct fdt_node* memory = fdt_node_create_reg("memory", machine->mem.addr);
    fdt_node_add_prop_str(memory, "device_type", "memory");
    fdt_node_add_prop_reg(memory, "reg", machine->mem.addr, machine->mem.size);
    fdt_node_add_child(machine->fdt, memory);

    // FDT /cpus node
    struct fdt_node* cpus = fdt_node_create("cpus");
    fdt_node_add_prop_u32(cpus, "#address-cells", 1);
    fdt_node_add_prop_u32(cpus, "#size-cells", 0);
    fdt_node_add_prop_u32(cpus, "timebase-frequency", rvvm_get_opt(machine, RVVM_OPT_TIME_FREQ));
    fdt_node_add_child(machine->fdt, cpus);

    struct fdt_node* cpu_map = fdt_node_create("cpu-map");
    struct fdt_node* cluster = fdt_node_create("cluster0");
    fdt_node_add_child(cpu_map, cluster);

    vector_foreach (machine->harts, i) {
        struct fdt_node* cpu = fdt_node_create_reg("cpu", i);

        fdt_node_add_prop_str(cpu, "device_type", "cpu");
        fdt_node_add_prop_u32(cpu, "reg", i);
        fdt_node_add_prop(cpu, "compatible", "lekkit,rvvm\0riscv\0", 18);
        fdt_node_add_prop_u32(cpu, "clock-frequency", 3000000000);
        fdt_node_add_prop_u32(cpu, "riscv,cbop-block-size", 64);
        fdt_node_add_prop_u32(cpu, "riscv,cboz-block-size", 64);
        fdt_node_add_prop_u32(cpu, "riscv,cbom-block-size", 64);
        if (vector_at(machine->harts, i)->rv64) {
            fdt_node_add_prop_str(cpu, "riscv,isa", "rv64ima");
            fdt_node_add_prop_str(cpu, "riscv,isa-base", "rv64i");
            fdt_node_add_prop(cpu, "riscv,isa-extensions", NULL, 0);
            fdt_node_add_prop_str(cpu, "mmu-type", "riscv,sv39");
        } else {
            fdt_node_add_prop_str(cpu, "riscv,isa", "rv32ima");
            fdt_node_add_prop_str(cpu, "riscv,isa-base", "rv32i");
            fdt_node_add_prop(cpu, "riscv,isa-extensions", NULL, 0);
            fdt_node_add_prop_str(cpu, "mmu-type", "riscv,sv32");
        }

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

    fdt_node_add_child(cpus, cpu_map);

    // FDT /soc node
    struct fdt_node* soc = fdt_node_create("soc");
    fdt_node_add_prop_u32(soc, "#address-cells", 2);
    fdt_node_add_prop_u32(soc, "#size-cells", 2);
    fdt_node_add_prop_str(soc, "compatible", "simple-bus");
    fdt_node_add_prop(soc, "ranges", NULL, 0);
    fdt_node_add_child(machine->fdt, soc);
    machine->fdt_soc = soc;

    // ISA string
#if defined(USE_FPU)
    rvvm_append_isa_string(machine, "fd");
#endif
    rvvm_append_isa_string(machine, riscv_exts);
}

static void rvvm_prepare_fdt(rvvm_machine_t* machine)
{
    if (rvvm_get_opt(machine, RVVM_OPT_HW_IMITATE)) {
        fdt_node_add_prop_str(machine->fdt, "model", "Amelia Semico Vertin Mk.I");
        fdt_node_add_prop_str(machine->fdt, "compatible", "amelia,board-generic");

        vector_foreach (machine->harts, i) {
            struct fdt_node* cpus = fdt_node_find(machine->fdt, "cpus");
            struct fdt_node* cpu  = fdt_node_find_reg(cpus, "cpu", i);
            fdt_node_add_prop(cpu, "compatible", "lekkit,arc7xx\0riscv\0", 20);
        }
    }
}

#endif

static size_t rvvm_fdt_addr(rvvm_machine_t* machine, size_t fdt_size)
{
    return align_size_down(machine->mem.size > fdt_size ? machine->mem.size - fdt_size : 0, 8);
}

static rvvm_addr_t rvvm_pass_fdt(rvvm_machine_t* machine)
{
    if (rvvm_get_opt(machine, RVVM_OPT_FDT_ADDR)) {
        // API user manually passes FDT
        return rvvm_get_opt(machine, RVVM_OPT_FDT_ADDR);
    } else if (machine->fdt_file) {
        // Load FDT from file
        uint32_t size = rvfilesize(machine->fdt_file);
        size_t   off  = rvvm_fdt_addr(machine, size);
        if (size < machine->mem.size) {
            rvread(machine->fdt_file, ((uint8_t*)machine->mem.data) + off, machine->mem.size - off, 0);
            rvvm_info("Loaded FDT at %#.8llx, size %u", (unsigned long long)machine->mem.addr + off, size);
            return machine->mem.addr + off;
        }
    } else {
        // Generate FDT
#if defined(USE_FDT)
        rvvm_prepare_fdt(machine);
        uint32_t size = fdt_size(machine->fdt);
        size_t   off  = rvvm_fdt_addr(machine, size);
        if (fdt_serialize(machine->fdt, ((uint8_t*)machine->mem.data) + off, machine->mem.size - off, 0)) {
            rvvm_info("Generated FDT at %#.8llx, size %u", (unsigned long long)machine->mem.addr + off, size);
            return machine->mem.addr + off;
        }
#else
        rvvm_error("Support for FDT is disabled in this build");
        return 0;
#endif
    }

    rvvm_error("Flattened Device Tree does not fit in RAM");
    return 0;
}

static void rvvm_reset_machine_state(rvvm_machine_t* machine)
{
    // Reset devices
    vector_foreach (machine->mmio_devs, i) {
        rvvm_mmio_dev_t* dev = vector_at(machine->mmio_devs, i);
        if (dev->type && dev->type->reset) {
            dev->type->reset(dev);
        }
    }

    // Load firmware, kernel, FDT into RAM if needed
    bool elf = !rvvm_get_opt(machine, RVVM_OPT_HW_IMITATE);
    if (machine->fw_file) {
        bin_objcopy(machine->fw_file, machine->mem.data, machine->mem.size, elf);
    }
    if (machine->kernel_file) {
        size_t kernel_offset = machine->rv64 ? 0x200000U : 0x400000U;
        size_t kernel_size   = machine->mem.size > kernel_offset ? machine->mem.size - kernel_offset : 0;
        bin_objcopy(machine->kernel_file, ((uint8_t*)machine->mem.data) + kernel_offset, kernel_size, elf);
    }

    // Reset CPUs
    rvvm_addr_t fdt_addr = rvvm_pass_fdt(machine);
    rvtimer_init(&machine->timer, rvvm_get_opt(machine, RVVM_OPT_TIME_FREQ));
    vector_foreach (machine->harts, i) {
        rvvm_hart_t* vm = vector_at(machine->harts, i);
        // Zero MIE, MPRV, etc
        vm->csr.status = 0;
        // Set mcause to 0
        vm->csr.cause[RISCV_PRIV_MACHINE] = 0;
        // Invalidate reservation
        vm->lrsc = false;
        // Pass hartid in register a0 & hartid CSR
        vm->csr.hartid               = i;
        vm->registers[RISCV_REG_X10] = i;
        // Pass FDT address in register a1
        vm->registers[RISCV_REG_X11] = fdt_addr;
        // Jump to RESET_PC
        vm->registers[RISCV_REG_PC] = rvvm_get_opt(machine, RVVM_OPT_RESET_PC);
        // Switch into machine mode, flush icache
        riscv_switch_priv(vm, RISCV_PRIV_MACHINE);
        riscv_jit_flush_cache(vm);
    }

    atomic_store_uint32(&machine->power_state, RVVM_POWER_ON);
}

static bool rvvm_eventloop_tick(bool manual)
{
    bool ret = false;
    if (vector_size(global_machines) == 0 || global_manual == !manual) {
        // No running machines left or switched eventloop mode
        return true;
    }

    vector_foreach_back (global_machines, m) {
        rvvm_machine_t* machine     = vector_at(global_machines, m);
        uint32_t        power_state = atomic_load_uint32(&machine->power_state);

        if (power_state == RVVM_POWER_ON) {
            vector_foreach (machine->harts, i) {
                rvvm_hart_t* vm = vector_at(machine->harts, i);
#if defined(USE_THREAD_EMU)
                riscv_hart_run(vector_at(machine->harts, i));
#endif
                // Сheck hart timer interrupts
                riscv_hart_check_timer(vector_at(machine->harts, i));
                if (rvvm_get_opt(machine, RVVM_OPT_MAX_CPU_CENT) < 100) {
                    uint32_t preempt = 10 - ((10 * rvvm_get_opt(machine, RVVM_OPT_MAX_CPU_CENT) + 9) / 100);
                    riscv_hart_preempt(vm, preempt);
                }
            }

            vector_foreach (machine->mmio_devs, i) {
                rvvm_mmio_dev_t* dev = vector_at(machine->mmio_devs, i);
                if (dev->type && dev->type->update) {
                    // Update device
                    dev->type->update(dev);
                }
            }
        } else {
            // The machine was shut down or reset
            vector_foreach (machine->harts, i) {
                riscv_hart_pause(vector_at(machine->harts, i));
            }
            // Call reset/poweroff handler
            if (power_state == RVVM_POWER_RESET) {
                rvvm_info("Machine %p resetting", machine);
                rvvm_reset_machine_state(machine);
                vector_foreach (machine->harts, i) {
                    riscv_hart_spawn(vector_at(machine->harts, i));
                }
            } else {
                rvvm_info("Machine %p shutting down", machine);
                atomic_store_uint32(&machine->running, false);
                vector_erase(global_machines, m);
                if (manual) {
                    // Return from manual eventloop whenever a machine powers down
                    ret = true;
                }
            }
        }
    }
    return ret;
}

#if defined(HOST_TARGET_EMSCRIPTEN)

// Implement proper Emscripten eventloop instead of a built-in one
#include <emscripten.h>

static void rvvm_eventloop_tick_em(void)
{
    rvvm_eventloop_tick(true);
}

#endif

static void* rvvm_eventloop(void* manual)
{
    bool running = true;
#if defined(HOST_TARGET_EMSCRIPTEN)
    if (manual) {
        emscripten_set_main_loop(rvvm_eventloop_tick_em, 0, running);
    }
#else
    uint32_t    delay = 1000000000ULL / 60;
    rvtimer_t   timer = ZERO_INIT;
    rvtimecmp_t cmp   = ZERO_INIT;
    rvtimer_init(&timer, 1000000000ULL);
    rvtimecmp_init(&cmp, &timer);
    rvtimecmp_set(&cmp, delay);

    if (!manual && !rvvm_has_arg("noisolation")) {
        rvvm_restrict_this_thread();
    }

    while (running) {
        bool tick = rvtimecmp_pending(&cmp);
        if (!tick) {
            tick = condvar_wait_ns(eventloop_cond, rvtimecmp_delay_ns(&cmp));
        }
        if (tick) {
            uint64_t next = rvtimecmp_get(&cmp) + delay;
            uint64_t time = rvtimer_get(&timer);
            rvtimecmp_set(&cmp, EVAL_MAX(time, next));

            scoped_spin_lock (&global_lock) {
                running = !rvvm_eventloop_tick(!!manual);
            }
        }
    }
#endif
    return NULL;
}

static void rvvm_reconfigure_eventloop(void)
{
#if defined(HOST_TARGET_EMSCRIPTEN)
    scoped_spin_lock (&eventloop_lock) {
        eventloop_thread = NULL;
        emscripten_cancel_main_loop();
        if (!global_manual) {
            emscripten_set_main_loop(rvvm_eventloop_tick_em, 0, false);
        }
    }
#else
    bool needs_cond   = false;
    bool needs_thread = false;
    scoped_spin_lock (&global_lock) {
        needs_cond   = global_manual || vector_size(global_machines);
        needs_thread = !global_manual && vector_size(global_machines);
    }
    scoped_spin_lock (&eventloop_lock) {
        if (!needs_thread && eventloop_thread) {
            condvar_wake(eventloop_cond);
            thread_join(eventloop_thread);
            eventloop_thread = NULL;
        }
        if (!needs_cond && eventloop_cond) {
            condvar_free(eventloop_cond);
            eventloop_cond = NULL;
        }
        if (needs_cond && !eventloop_cond) {
            eventloop_cond = condvar_create();
        }
        if (needs_thread && !eventloop_thread) {
            eventloop_thread = thread_create(rvvm_eventloop, NULL);
        }
    }
#endif
}

static void rvvm_set_manual_eventloop(bool manual)
{
    scoped_spin_lock (&global_lock) {
        global_manual = manual;
    }
    rvvm_reconfigure_eventloop();
}

static void rvvm_wake_eventloop(void)
{
    scoped_spin_lock (&eventloop_lock) {
        condvar_wake(eventloop_cond);
    }
}

static rvfile_t* file_reopen_check_size(rvfile_t* orig, const char* path, size_t size)
{
    rvclose(orig);
    if (path) {
        rvfile_t* file = rvopen(path, 0);
        if (!file) {
            rvvm_error("Failed to open \"%s\"", path);
        }
        if (rvfilesize(file) > size) {
            rvvm_error("File \"%s\" doesn't fit in RAM", path);
            rvclose(file);
            file = NULL;
        }
        return file;
    }
    return NULL;
}

static void rvvm_mmio_free(rvvm_mmio_dev_t* dev)
{
    rvvm_cleanup_mmio_desc(dev);
    free(dev);
}

PUBLIC bool rvvm_check_abi(int abi)
{
    UNUSED(abi);
#if !defined(RVVM_ABI_VERSION) || RVVM_ABI_VERSION <= 0
    rvvm_warn("This is a staging librvvm version with unstable ABI/API");
    return true;
#else
    return abi == RVVM_ABI_VERSION;
#endif
}

/*
 * RVVM Machine Management
 */

PUBLIC rvvm_machine_t* rvvm_create_machine(size_t mem_size, size_t hart_count, const char* isa)
{
    // TODO: Proper full ISA string parsing
    bool rv64 = !isa || (rvvm_strfind(isa, "rv64") == isa);
    bool rv32 = isa && (rvvm_strfind(isa, "rv32") == isa);

    stacktrace_init();

    if (!rv64 && !rv32) {
        rvvm_error("Invalid ISA string: \"%s\"", isa);
        return NULL;
    }

#if !defined(USE_RV32)
    if (rv32) {
        rvvm_error("Support for riscv32 is disabled in this build");
        return NULL;
    }
#endif
#if !defined(USE_RV64)
    if (rv64) {
        rvvm_error("Support for riscv64 is disabled in this build");
        return NULL;
    }
#endif

    if (hart_count == 0 || hart_count > 1024) {
        rvvm_error("Invalid machine core count: %u", (uint32_t)hart_count);
        return NULL;
    }

    rvvm_machine_t* machine = safe_new_obj(rvvm_machine_t);
    machine->rv64           = rv64;
    if (!riscv_init_ram(&machine->mem, RVVM_DEFAULT_MEMBASE, mem_size)) {
        free(machine);
        return NULL;
    }

    // Default options
    rvvm_set_opt(machine, RVVM_OPT_MAX_CPU_CENT, 100);
    rvvm_set_opt(machine, RVVM_OPT_MEM_BASE, RVVM_DEFAULT_MEMBASE);
    rvvm_set_opt(machine, RVVM_OPT_RESET_PC, RVVM_DEFAULT_MEMBASE);
    rvvm_set_opt(machine, RVVM_OPT_TIME_FREQ, 10000000);

#if defined(USE_JIT)
    rvvm_set_opt(machine, RVVM_OPT_JIT, !rvvm_has_arg("nojit"));
    rvvm_set_opt(machine, RVVM_OPT_JIT_HARVARD, rvvm_has_arg("rvjit_harvard"));
    if (rvvm_getarg_size("jitcache")) {
        rvvm_set_opt(machine, RVVM_OPT_JIT_CACHE, rvvm_getarg_size("jitcache"));
    } else {
        // Default: 16M-64M JIT cache per hart (Depends on RAM amount)
        size_t jit_cache = 16U << 20;
        if (mem_size >= (512U << 20)) {
            jit_cache = 32U << 20;
        }
        if (mem_size >= (1U << 30)) {
            jit_cache = 64U << 20;
        }
        rvvm_set_opt(machine, RVVM_OPT_JIT_CACHE, jit_cache);
    }
#endif

    for (size_t i = 0; i < hart_count; ++i) {
        vector_push_back(machine->harts, riscv_hart_init(machine));
    }

#if defined(USE_FDT)
    rvvm_init_fdt(machine);
#endif

    return machine;
}

PUBLIC void rvvm_set_cmdline(rvvm_machine_t* machine, const char* str)
{
#if defined(USE_FDT)
    if (machine) {
        struct fdt_node* chosen = fdt_node_find(machine->fdt, "chosen");
        fdt_node_add_prop_str(chosen, "bootargs", str);
    }
#endif
    UNUSED(machine);
    UNUSED(str);
}

PUBLIC void rvvm_append_cmdline(rvvm_machine_t* machine, const char* str)
{
#if defined(USE_FDT)
    if (machine) {
        struct fdt_node* chosen = fdt_node_find(machine->fdt, "chosen");
        // Obtain /chosen/bootargs
        const char* cmdline = fdt_node_get_prop_data(chosen, "bootargs");
        // Append cmdline
        char* tmp = cmdline ? rvvm_merge_strings_internal(cmdline, " ") : NULL;
        char* new = rvvm_merge_strings_internal(tmp, str);
        fdt_node_add_prop_str(chosen, "bootargs", new);
        free(tmp);
        free(new);
    }
#endif
    UNUSED(machine);
    UNUSED(str);
}

PUBLIC bool rvvm_load_firmware(rvvm_machine_t* machine, const char* path)
{
    if (machine) {
        machine->fw_file = file_reopen_check_size(machine->fw_file, path, machine->mem.size);
        return !!machine->fw_file;
    }
    return false;
}

PUBLIC bool rvvm_load_kernel(rvvm_machine_t* machine, const char* path)
{
    if (machine) {
        size_t kernel_offset = machine->rv64 ? 0x200000U : 0x400000U;
        size_t kernel_size   = machine->mem.size > kernel_offset ? machine->mem.size - kernel_offset : 0;
        machine->kernel_file = file_reopen_check_size(machine->kernel_file, path, kernel_size);
        return !!machine->kernel_file;
    }
    return false;
}

PUBLIC bool rvvm_load_fdt(rvvm_machine_t* machine, const char* path)
{
    if (machine) {
        machine->fdt_file = file_reopen_check_size(machine->fdt_file, path, machine->mem.size >> 1);
        return !!machine->fdt_file;
    }
    return false;
}

PUBLIC bool rvvm_dump_fdt(rvvm_machine_t* machine, const char* path)
{
#if defined(USE_FDT)
    if (machine) {
        rvfile_t* file = rvopen(path, RVFILE_RW | RVFILE_CREAT | RVFILE_TRUNC);
        if (file) {
            size_t size   = fdt_size(rvvm_get_fdt_root(machine));
            void*  buffer = safe_calloc(size, 1);
            size          = fdt_serialize(rvvm_get_fdt_root(machine), buffer, size, 0);
            rvwrite(file, buffer, size, 0);
            rvclose(file);
            free(buffer);
            return true;
        }
    }
#else
    rvvm_error("Support for FDT is disabled in this build");
#endif
    UNUSED(machine);
    UNUSED(path);
    return false;
}

PUBLIC rvvm_addr_t rvvm_get_opt(rvvm_machine_t* machine, uint32_t opt)
{
    if (likely(machine)) {
        if (opt < RVVM_OPTS_ARR_SIZE) {
            return atomic_load_uint64_ex(&machine->opts[opt], ATOMIC_RELAXED);
        }
        switch (opt) {
            case RVVM_OPT_MEM_BASE:
                return machine->mem.addr;
            case RVVM_OPT_MEM_SIZE:
                return machine->mem.size;
            case RVVM_OPT_CPU_COUNT:
                return vector_size(machine->harts);
        }
    }
    return 0;
}

PUBLIC bool rvvm_set_opt(rvvm_machine_t* machine, uint32_t opt, rvvm_addr_t val)
{
    if (likely(machine)) {
        if (opt < RVVM_OPTS_ARR_SIZE) {
            atomic_store_uint64_ex(&machine->opts[opt], val, ATOMIC_RELAXED);
            return true;
        }
        switch (opt) {
            case RVVM_OPT_MEM_BASE:
                machine->mem.addr = val;
                return true;
        }
    }
    return false;
}

PUBLIC bool rvvm_start_machine(rvvm_machine_t* machine)
{
    if (machine && !atomic_swap_uint32(&machine->running, true)) {
        scoped_spin_lock (&global_lock) {
            if (!rvvm_machine_powered(machine)) {
                rvvm_reset_machine_state(machine);
            }
            vector_foreach (machine->harts, i) {
                riscv_hart_prepare(vector_at(machine->harts, i));
            }
            vector_foreach (machine->harts, i) {
                riscv_hart_spawn(vector_at(machine->harts, i));
            }
            // Register the machine as running
            vector_push_back(global_machines, machine);
        }
        rvvm_reconfigure_eventloop();
        return true;
    }
    return false;
}

PUBLIC bool rvvm_pause_machine(rvvm_machine_t* machine)
{
    if (machine && atomic_swap_uint32(&machine->running, false)) {
        scoped_spin_lock (&global_lock) {
            vector_foreach (machine->harts, i) {
                riscv_hart_pause(vector_at(machine->harts, i));
            }
            vector_foreach_back (global_machines, i) {
                if (vector_at(global_machines, i) == machine) {
                    vector_erase(global_machines, i);
                    break;
                }
            }
        }
        rvvm_reconfigure_eventloop();
        return true;
    }
    return false;
}

PUBLIC void rvvm_reset_machine(rvvm_machine_t* machine, bool reset)
{
    if (machine) {
        // Handled by eventloop
        atomic_store_uint32_relax(&machine->power_state, reset ? RVVM_POWER_RESET : RVVM_POWER_OFF);
        rvvm_wake_eventloop();
    }
}

PUBLIC bool rvvm_machine_running(rvvm_machine_t* machine)
{
    if (likely(machine)) {
        return atomic_load_uint32_relax(&machine->running);
    }
    return false;
}

PUBLIC bool rvvm_machine_powered(rvvm_machine_t* machine)
{
    if (likely(machine)) {
        return atomic_load_uint32_relax(&machine->power_state) != RVVM_POWER_OFF;
    }
    return false;
}

PUBLIC void rvvm_free_machine(rvvm_machine_t* machine)
{
    if (machine) {
        rvvm_pause_machine(machine);

        // Shut down the eventloop if needed
        rvvm_reconfigure_eventloop();

        // Clean up devices in LIFO order
        vector_foreach_back (machine->mmio_devs, i) {
            rvvm_mmio_free(vector_at(machine->mmio_devs, i));
        }

        vector_foreach_back (machine->harts, i) {
            riscv_hart_free(vector_at(machine->harts, i));
        }

        vector_free(machine->harts);
        vector_free(machine->mmio_devs);
        vector_free(machine->msi_targets);

        riscv_free_ram(&machine->mem);
        rvclose(machine->fw_file);
        rvclose(machine->kernel_file);
        rvclose(machine->fdt_file);

#if defined(USE_FDT)
        fdt_node_free(machine->fdt);
#endif

        free(machine);
    }
}

PUBLIC void rvvm_run_eventloop(void)
{
    rvvm_set_manual_eventloop(true);
    rvvm_eventloop((void*)(size_t)1);
    rvvm_set_manual_eventloop(false);
}

/*
 * RVVM Device API
 */

PUBLIC bool rvvm_mmio_none(rvvm_mmio_dev_t* dev, void* dest, size_t offset, uint8_t size)
{
    UNUSED(dev);
    UNUSED(offset);
    memset(dest, 0, size);
    return true;
}

PUBLIC bool rvvm_write_ram(rvvm_machine_t* machine, rvvm_addr_t dest, const void* src, size_t size)
{
    if (likely(machine && dest >= machine->mem.addr && dest + size <= machine->mem.addr + machine->mem.size)) {
        memcpy(((uint8_t*)machine->mem.data) + (dest - machine->mem.addr), src, size);
        riscv_jit_mark_dirty_mem(machine, dest, size);
        return true;
    }
    return false;
}

PUBLIC bool rvvm_read_ram(rvvm_machine_t* machine, void* dest, rvvm_addr_t src, size_t size)
{
    if (likely(machine && src >= machine->mem.addr && src + size <= machine->mem.addr + machine->mem.size)) {
        memcpy(dest, ((uint8_t*)machine->mem.data) + (src - machine->mem.addr), size);
        return true;
    }
    return false;
}

PUBLIC void* rvvm_get_dma_ptr(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size)
{
    if (likely(machine && addr >= machine->mem.addr && addr + size <= machine->mem.addr + machine->mem.size)) {
        riscv_jit_mark_dirty_mem(machine, addr, size);
        return ((uint8_t*)machine->mem.data) + (addr - machine->mem.addr);
    }
    return NULL;
}

static inline bool rvvm_mmio_overlap_check(rvvm_addr_t addr1, size_t size1, rvvm_addr_t addr2, size_t size2)
{
    return addr1 < (addr2 + size2) && addr2 < (addr1 + size1);
}

PUBLIC rvvm_addr_t rvvm_mmio_zone_auto(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size)
{
    // Regions of size 0 are ignored (Those are non-IO placeholders)
    if (machine && size) {
        bool free_zone = false;
        scoped_spin_lock (&global_lock) {
            while (!free_zone) {
                free_zone = true;
                if (rvvm_mmio_overlap_check(addr, size, machine->mem.addr, machine->mem.size)) {
                    addr      = (machine->mem.addr + machine->mem.size + 0xFFFULL) & ~0xFFFULL;
                    free_zone = false;
                } else {
                    vector_foreach (machine->mmio_devs, i) {
                        rvvm_mmio_dev_t* dev = vector_at(machine->mmio_devs, i);
                        if (rvvm_mmio_overlap_check(addr, size, dev->addr, dev->size)) {
                            addr      = (dev->addr + dev->size + 0xFFFULL) & ~0xFFFULL;
                            free_zone = false;
                            break;
                        }
                    }
                }
            }
        }
    }
    return addr;
}

PUBLIC rvvm_mmio_dev_t* rvvm_attach_mmio(rvvm_machine_t* machine, const rvvm_mmio_dev_t* mmio_desc)
{
    if (machine && mmio_desc) {
        rvvm_mmio_dev_t* dev = safe_new_obj(rvvm_mmio_dev_t);
        memcpy(dev, mmio_desc, sizeof(rvvm_mmio_dev_t));
        dev->machine = machine;

        // Normalize access properties: Power of two, default 1 - 8 bytes
        dev->min_op_size = dev->min_op_size ? bit_next_pow2(dev->min_op_size) : 1;
        dev->max_op_size = dev->max_op_size ? bit_next_pow2(dev->max_op_size) : 8;

        if (dev->min_op_size > dev->max_op_size || dev->min_op_size > 8) {
            rvvm_error("Invalid MMIO device \"%s\" properties: min_op %u, max_op %u", //
                       dev->type ? dev->type->name : NULL, dev->min_op_size, dev->max_op_size);
            rvvm_mmio_free(dev);
            return NULL;
        }
        if (rvvm_mmio_zone_auto(machine, dev->addr, dev->size) != dev->addr) {
            rvvm_error("Failed to attach MMIO device \"%s\" to occupied address %#.8llx", //
                       dev->type ? dev->type->name : NULL, (unsigned long long)dev->addr);
            rvvm_mmio_free(dev);
            return NULL;
        }
        if (dev->mapping && ((dev->addr & 0xFFFU) ^ (((size_t)dev->mapping) & 0xFFFU))) {
            // Misaligned mappings harm performance with KVM or shadow pagetable accel
            DO_ONCE(rvvm_warn("Misaligned MMIO mapping \"%s\" may affect performance", //
                              dev->type ? dev->type->name : NULL));
        }

        rvvm_info("Attached MMIO device at 0x%.8llx, type \"%s\"", //
                  (unsigned long long)dev->addr, dev->type ? dev->type->name : NULL);

        scoped_spin_lock (&global_lock) {
            bool was_running = rvvm_pause_machine(machine);
            vector_push_back(machine->mmio_devs, dev);
            if (was_running) {
                rvvm_start_machine(machine);
            }
        }
        return dev;
    }
    return NULL;
}

PUBLIC void rvvm_remove_mmio(rvvm_mmio_dev_t* mmio_dev)
{
    if (mmio_dev) {
        rvvm_machine_t* machine = mmio_dev->machine;
        if (machine) {
            scoped_spin_lock (&global_lock) {
                bool was_running = rvvm_pause_machine(machine);

                // Remove from machine device list
                vector_foreach_back (machine->mmio_devs, i) {
                    if (vector_at(machine->mmio_devs, i) == mmio_dev) {
                        vector_erase(machine->mmio_devs, i);
                    }
                }

                // It's a shared memory mapping, flush each hart TLB
                if (mmio_dev->mapping) {
                    vector_foreach (machine->harts, i) {
                        rvvm_hart_t* vm = vector_at(machine->harts, i);
                        riscv_tlb_flush(vm);
                    }
                }

                if (was_running) {
                    rvvm_start_machine(machine);
                }
            }
        }
        rvvm_mmio_free(mmio_dev);
    }
}

PUBLIC void rvvm_cleanup_mmio_desc(const rvvm_mmio_dev_t* mmio_desc)
{
    if (mmio_desc) {
        // Either device implements it's own cleanup routine, or we free it's data buffer
        rvvm_mmio_dev_t mmio = *mmio_desc;
        rvvm_info("Freeing MMIO device \"%s\"", mmio.type ? mmio.type->name : NULL);
        if (mmio.type && mmio.type->remove) {
            mmio.type->remove(&mmio);
        } else {
            safe_free(mmio.data);
        }
    }
}

PUBLIC rvvm_intc_t* rvvm_get_intc(rvvm_machine_t* machine)
{
    if (likely(machine)) {
        return machine->intc;
    }
    return NULL;
}

PUBLIC void rvvm_set_intc(rvvm_machine_t* machine, rvvm_intc_t* intc)
{
    if (machine && intc) {
        machine->intc = intc;
    }
}

PUBLIC pci_bus_t* rvvm_get_pci_bus(rvvm_machine_t* machine)
{
    if (likely(machine)) {
        return machine->pci_bus;
    }
    return NULL;
}

PUBLIC void rvvm_set_pci_bus(rvvm_machine_t* machine, pci_bus_t* pci_bus)
{
    if (machine && pci_bus) {
        machine->pci_bus = pci_bus;
    }
}

PUBLIC i2c_bus_t* rvvm_get_i2c_bus(rvvm_machine_t* machine)
{
    if (likely(machine)) {
        return machine->i2c_bus;
    }
    return NULL;
}

PUBLIC void rvvm_set_i2c_bus(rvvm_machine_t* machine, i2c_bus_t* i2c_bus)
{
    if (machine && i2c_bus) {
        machine->i2c_bus = i2c_bus;
    }
}

PUBLIC struct fdt_node* rvvm_get_fdt_root(rvvm_machine_t* machine)
{
#if defined(USE_FDT)
    if (likely(machine)) {
        return machine->fdt;
    }
#endif
    UNUSED(machine);
    return NULL;
}

PUBLIC struct fdt_node* rvvm_get_fdt_soc(rvvm_machine_t* machine)
{
#if defined(USE_FDT)
    if (likely(machine)) {
        return machine->fdt_soc;
    }
#endif
    UNUSED(machine);
    return NULL;
}

/*
 * RVVM Interrupt API
 */

PUBLIC bool rvvm_send_msi_irq(rvvm_machine_t* machine, rvvm_addr_t addr, uint32_t val)
{
    if (likely(machine)) {
        uint32_t le = 0;
        write_uint32_le(&le, val);
        // Address must be aligned
        if (likely(!(addr & 3))) {
            // TODO: Thread safety on machine->msi_targets
            vector_foreach (machine->msi_targets, i) {
                rvvm_mmio_dev_t* mmio = vector_at(machine->msi_targets, i);
                if (mmio->addr <= addr && addr < (mmio->addr + mmio->size)) {
                    return mmio->write(mmio, &le, addr - mmio->addr, sizeof(le));
                }
            }
        }
        rvvm_debug("Failed to send MSI IRQ %#.8x to %#.8llx", val, (unsigned long long)addr);
    }
    return false;
}

PUBLIC bool rvvm_attach_msi_target(rvvm_machine_t* machine, const rvvm_mmio_dev_t* mmio_desc)
{
    if (machine) {
        if (mmio_desc->min_op_size <= 4 && mmio_desc->max_op_size >= 4) {
            rvvm_mmio_dev_t* mmio = rvvm_attach_mmio(machine, mmio_desc);
            if (mmio) {
                vector_push_back(machine->msi_targets, mmio);
                return true;
            }
        } else {
            rvvm_error("Invalid MSI target \"%s\" properties", //
                       mmio_desc->type ? mmio_desc->type->name : NULL);
        }
    }
    return false;
}

PUBLIC rvvm_irq_t rvvm_alloc_irq(rvvm_intc_t* intc)
{
    if (intc) {
        if (intc->alloc_irq) {
            return intc->alloc_irq(intc);
        }
        return atomic_add_uint32(&intc->last_irq, 1) + 1;
    }
    return 0;
}

PUBLIC bool rvvm_send_irq(rvvm_intc_t* intc, rvvm_irq_t irq)
{
    if (likely(intc)) {
        return intc->send_irq(intc, irq);
    }
    return false;
}

PUBLIC bool rvvm_raise_irq(rvvm_intc_t* intc, rvvm_irq_t irq)
{
    if (likely(intc)) {
        if (likely(intc->raise_irq)) {
            return intc->raise_irq(intc, irq);
        }
    }
    return rvvm_send_irq(intc, irq);
}

PUBLIC bool rvvm_lower_irq(rvvm_intc_t* intc, rvvm_irq_t irq)
{
    if (likely(intc && intc->lower_irq)) {
        return intc->lower_irq(intc, irq);
    }
    return false;
}

PUBLIC bool rvvm_fdt_describe_irq(struct fdt_node* node, rvvm_intc_t* intc, rvvm_irq_t irq)
{
#if defined(USE_FDT)
    if (node && intc) {
        uint32_t cells[8] = {0};
        size_t   count    = rvvm_fdt_irq_cells(intc, irq, cells, STATIC_ARRAY_SIZE(cells));
        fdt_node_add_prop_u32(node, "interrupt-parent", rvvm_fdt_intc_phandle(intc));
        fdt_node_add_prop_cells(node, "interrupts", cells, count);
        return true;
    }
#endif
    UNUSED(node);
    UNUSED(intc);
    UNUSED(irq);
    return false;
}

PUBLIC uint32_t rvvm_fdt_intc_phandle(rvvm_intc_t* intc)
{
    if (intc && intc->fdt_phandle) {
        return intc->fdt_phandle(intc);
    }
    return 0;
}

PUBLIC size_t rvvm_fdt_irq_cells(rvvm_intc_t* intc, rvvm_irq_t irq, uint32_t* cells, size_t size)
{
    if (intc && intc->fdt_irq_cells) {
        return intc->fdt_irq_cells(intc, irq, cells, size);
    }
    return 0;
}

/*
 * Userland emulation API (WIP)
 */

PUBLIC rvvm_machine_t* rvvm_create_userland(const char* isa)
{
    // TODO: Proper ISA string parsing
    bool rv64 = !isa || (rvvm_strfind(isa, "rv64") == isa);
    bool rv32 = isa && (rvvm_strfind(isa, "rv32") == isa);

    stacktrace_init();

    if (!rv64 && !rv32) {
        rvvm_error("Invalid ISA string: %s", isa);
        return NULL;
    }

    rvvm_machine_t* machine = safe_new_obj(rvvm_machine_t);

    // Pass whole process address space except the NULL page
    // RVVM expects mem.data to be non-NULL, let's leave that for now
    machine->mem.addr = 0x1000;
    machine->mem.size = (size_t)-0x1000ULL;
    machine->mem.data = (void*)0x1000;
    machine->rv64     = rv64;

    // Set nanosecond machine timer precision by default
    rvvm_set_opt(machine, RVVM_OPT_TIME_FREQ, 1000000000ULL);

#if defined(USE_JIT)
    rvvm_set_opt(machine, RVVM_OPT_JIT, true);
    rvvm_set_opt(machine, RVVM_OPT_JIT_HARVARD, true);
    rvvm_set_opt(machine, RVVM_OPT_JIT_CACHE, 16U << 20);
#endif

    return machine;
}

PUBLIC void rvvm_flush_icache(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size)
{
    // WIP, issue a total cache flush on all harts
    // TODO: Needs improvements in RVJIT
    UNUSED(addr);
    UNUSED(size);
    if (likely(machine)) {
        scoped_spin_lock (&global_lock) {
            vector_foreach (machine->harts, i) {
                riscv_jit_flush_cache(vector_at(machine->harts, i));
            }
        }
    }
}

PUBLIC rvvm_hart_t* rvvm_create_user_thread(rvvm_machine_t* machine)
{
    if (likely(machine)) {
        rvvm_hart_t* thread = riscv_hart_init(machine);
        riscv_hart_prepare(thread);

        // Allow time CSR access from U-mode
        thread->csr.counteren[RISCV_PRIV_MACHINE]    = CSR_COUNTEREN_MASK;
        thread->csr.counteren[RISCV_PRIV_SUPERVISOR] = CSR_COUNTEREN_MASK;

        // Allow Zkr seed CSR access from U-mode
        thread->csr.mseccfg = CSR_MSECCFG_MASK;

        // Allow Zicboz/Zicbom access from U-mode
        thread->csr.envcfg[RISCV_PRIV_MACHINE]    = CSR_MENVCFG_MASK;
        thread->csr.envcfg[RISCV_PRIV_SUPERVISOR] = CSR_SENVCFG_MASK;

#if defined(USE_FPU)
        // Initialize FPU by writing to status CSR
        rvvm_uxlen_t mstatus = (FS_INITIAL << 13);
        riscv_csr_op(thread, CSR_MSTATUS, &mstatus, CSR_SETBITS);
#endif

#if defined(USE_JIT)
        // Enable pointer optimization
        rvjit_set_native_ptrs(&thread->jit, true);
#endif

        riscv_switch_priv(thread, RISCV_PRIV_USER);

        scoped_spin_lock (&global_lock) {
            if (!vector_size(machine->harts)) {
                // This is the main thread, initialize the timer
                rvtimer_init(&machine->timer, rvvm_get_opt(machine, RVVM_OPT_TIME_FREQ));
            }
        }
        return thread;
    }
    return NULL;
}

PUBLIC void rvvm_free_user_thread(rvvm_hart_t* thread)
{
    if (likely(thread)) {
        scoped_spin_lock (&global_lock) {
            vector_foreach (thread->machine->harts, i) {
                if (vector_at(thread->machine->harts, i) == thread) {
                    vector_erase(thread->machine->harts, i);
                    riscv_hart_free(thread);
                    break;
                }
            }
        }
    }
}

PUBLIC rvvm_addr_t rvvm_run_user_thread(rvvm_hart_t* thread)
{
    if (likely(thread)) {
        return riscv_hart_run_userland(thread);
    }
    return 0;
}

PUBLIC rvvm_addr_t rvvm_read_cpu_reg(rvvm_hart_t* thread, size_t reg_id)
{
    if (likely(thread)) {
        if (reg_id < (RVVM_REGID_X0 + 32)) {
            return thread->registers[reg_id - RVVM_REGID_X0];
#if defined(USE_FPU)
        } else if (reg_id < (RVVM_REGID_F0 + 32)) {
            rvvm_addr_t ret;
            memcpy(&ret, &thread->fpu_registers[reg_id - RVVM_REGID_F0], sizeof(ret));
            return ret;
#endif
        } else if (reg_id == RVVM_REGID_PC) {
            return thread->registers[RISCV_REG_PC];
        } else if (reg_id == RVVM_REGID_CAUSE) {
            return thread->csr.cause[RISCV_PRIV_USER];
        } else if (reg_id == RVVM_REGID_TVAL) {
            return thread->csr.tval[RISCV_PRIV_USER];
        } else {
            rvvm_error("rvvm_read_cpu_reg(%#x): Unknown register", (uint32_t)reg_id);
        }
    }
    return 0;
}

PUBLIC void rvvm_write_cpu_reg(rvvm_hart_t* thread, size_t reg_id, rvvm_addr_t reg)
{
    if (likely(thread)) {
        if (reg_id < (RVVM_REGID_X0 + 32)) {
            thread->registers[reg_id - RVVM_REGID_X0] = reg;
#if defined(USE_FPU)
        } else if (reg_id < (RVVM_REGID_F0 + 32)) {
            memcpy(&thread->fpu_registers[reg_id - RVVM_REGID_F0], &reg, sizeof(reg));
#endif
        } else if (reg_id == RVVM_REGID_PC) {
            thread->registers[RISCV_REG_PC] = reg;
        } else if (reg_id == RVVM_REGID_CAUSE) {
            thread->csr.cause[RISCV_PRIV_USER] = reg;
        } else if (reg_id == RVVM_REGID_TVAL) {
            thread->csr.tval[RISCV_PRIV_USER] = reg;
        } else {
            rvvm_error("rvvm_write_cpu_reg(%#x): Unknown register", (uint32_t)reg_id);
        }
    }
}

POP_OPTIMIZATION_SIZE
