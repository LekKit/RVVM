/*
rvvm_snapshot.c - RVVM Snapshot serialization
Copyright (C) 2020-2026 LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <rvvm/rvvm_blk.h>
#include <rvvm/rvvm_snapshot.h>

#include <cpu/riscv_cpu.h>
#include <cpu/riscv_hart.h>
#include <cpu/riscv_mmu.h>
#include <util/rvtimer.h>
#include <util/vector.h>

#include "mem_ops.h"
#include "rvvm.h"
#include "utils.h"

#define SNAP_MAGIC "\x7Frvvm-snapshot0\xFF"

PUSH_OPTIMIZATION_SIZE

struct rvvm_snapshot {
    rvvm_blk_dev_t* blk;

    uint64_t off;

    bool out;
    bool err;
};

RVVM_PUBLIC rvvm_snapshot_t* rvvm_snapshot_open(rvvm_blk_dev_t* blk, bool write)
{
    rvvm_snapshot_t* snap = safe_new_obj(rvvm_snapshot_t);

    snap->blk = blk;
    snap->out = write;

    return snap;
}

RVVM_PUBLIC bool rvvm_snapshot_close(rvvm_snapshot_t* snap)
{
    if (snap) {
        bool ret = !snap->err;
        rvvm_blk_close(snap->blk);
        free(snap);
        return ret;
    }
    return false;
}

static bool rvvm_snapshot_section_next(rvvm_snapshot_t* snap)
{
    uint8_t tmp[24] = {0};
    if (snap->out) {
        uint64_t prev = snap->off;
        snap->off     = rvvm_blk_tell_head(snap->blk);
        memcpy(tmp, SNAP_MAGIC, 16);
        write_uint64_le_m(tmp + 16, snap->off);
        if (rvvm_blk_write(snap->blk, tmp, sizeof(tmp), prev) == sizeof(tmp)) {
            write_uint64_le_m(tmp + 16, 0);
            if (rvvm_blk_write_head(snap->blk, tmp, sizeof(tmp)) == sizeof(tmp)) {
                return true;
            }
        }
    } else {
        if (rvvm_blk_read_head(snap->blk, tmp, sizeof(tmp)) == sizeof(tmp) && //
            !memcmp(tmp, SNAP_MAGIC, 16)) {
            snap->off = read_uint64_le(tmp + 16);
            return true;
        }
    }
    snap->err = true;
    return false;
}

RVVM_PUBLIC bool rvvm_snapshot_section(rvvm_snapshot_t* snap, const char* name)
{
    if (snap && name && !snap->err) {
        char   buf[256] = {0};
        size_t len      = rvvm_strlen(name);
        if (len > 255) {
            len = 255;
        }
        if (snap->out) {
            memcpy(buf, name, len);
        }
        do {
            if (!rvvm_snapshot_section_next(snap) || !rvvm_snapshot_data(snap, buf, len)) {
                return false;
            }
        } while (!snap->out && rvvm_strcmp(buf, name) == false);
        return true;
    }
    return false;
}

RVVM_PUBLIC bool rvvm_snapshot_writing(rvvm_snapshot_t* snap)
{
    if (snap) {
        return snap->out;
    }
    return false;
}

RVVM_PUBLIC bool rvvm_snapshot_data(rvvm_snapshot_t* snap, void* data, size_t size)
{
    if (snap && data && !snap->err) {
        if (snap->out) {
            if (rvvm_blk_write_head(snap->blk, data, size) != size) {
                return false;
            }
        } else {
            if (rvvm_blk_tell_head(snap->blk) + size > snap->off) {
                return false;
            }
            if (rvvm_blk_read_head(snap->blk, data, size) != size) {
                return false;
            }
        }
        return true;
    }
    return false;
}

RVVM_PUBLIC bool rvvm_snapshot_host(rvvm_snapshot_t* snap, void* data, size_t size)
{
    if (snap && data && !(((size_t)data) & (size - 1))) {
        switch (size) {
            case 1:
                return rvvm_snapshot_data(snap, data, size);
            case 2: {
                uint16_t tmp = atomic_load_uint16_relax(data);
                write_uint16_le(&tmp, tmp);
                if (!rvvm_snapshot_data(snap, &tmp, sizeof(tmp))) {
                    return false;
                } else if (!snap->out) {
                    atomic_store_uint16_relax(data, read_uint16_le(&tmp));
                }
                return true;
            }
            case 4: {
                uint32_t tmp = atomic_load_uint32_relax(data);
                write_uint32_le(&tmp, tmp);
                if (!rvvm_snapshot_data(snap, &tmp, sizeof(tmp))) {
                    return false;
                } else if (!snap->out) {
                    atomic_store_uint32_relax(data, read_uint32_le(&tmp));
                }
                return true;
            }
            case 8: {
                uint64_t tmp = atomic_load_uint64_relax(data);
                write_uint64_le(&tmp, tmp);
                if (!rvvm_snapshot_data(snap, &tmp, sizeof(tmp))) {
                    return false;
                } else if (!snap->out) {
                    atomic_store_uint64_relax(data, read_uint64_le(&tmp));
                }
                return true;
            }
        }
    }
    return false;
}

static bool rvvm_snapshot_hart(rvvm_snapshot_t* snap, rvvm_hart_t* vm)
{
    bool ok = true;
    for (size_t i = 0; i < RISCV_REGS_MAX; ++i) {
        ok &= rvvm_snapshot_field(snap, vm->registers[i]);
    }

#if defined(USE_FPU)
    for (size_t i = 0; i < RISCV_FPU_REGS_MAX; ++i) {
        ok &= rvvm_snapshot_host(snap, &vm->fpu_registers[i], sizeof(vm->fpu_registers[i]));
    }
#endif

#if defined(USE_RVV)
    ok &= rvvm_snapshot_data(snap, vm->rvv_state, sizeof(vm->rvv_state));
#endif

    ok &= rvvm_snapshot_field(snap, vm->root_page_table);
    ok &= rvvm_snapshot_field(snap, vm->mmu_mode);
    ok &= rvvm_snapshot_field(snap, vm->priv_mode);
    ok &= rvvm_snapshot_field(snap, vm->trap);
    ok &= rvvm_snapshot_field(snap, vm->trap_pc);
    ok &= rvvm_snapshot_field(snap, vm->lrsc);
    ok &= rvvm_snapshot_field(snap, vm->lrsc_addr);
    ok &= rvvm_snapshot_field(snap, vm->lrsc_cas);

    ok &= rvvm_snapshot_field(snap, vm->csr.status);
    ok &= rvvm_snapshot_field(snap, vm->csr.ie);
    ok &= rvvm_snapshot_field(snap, vm->csr.ip);
    ok &= rvvm_snapshot_field(snap, vm->csr.isa);
    for (size_t i = 0; i < RISCV_PRIVS_MAX; ++i) {
        ok &= rvvm_snapshot_field(snap, vm->csr.edeleg[i]);
        ok &= rvvm_snapshot_field(snap, vm->csr.ideleg[i]);
        ok &= rvvm_snapshot_field(snap, vm->csr.tvec[i]);
        ok &= rvvm_snapshot_field(snap, vm->csr.scratch[i]);
        ok &= rvvm_snapshot_field(snap, vm->csr.epc[i]);
        ok &= rvvm_snapshot_field(snap, vm->csr.cause[i]);
        ok &= rvvm_snapshot_field(snap, vm->csr.tval[i]);
        ok &= rvvm_snapshot_field(snap, vm->csr.iselect[i]);
        ok &= rvvm_snapshot_field(snap, vm->csr.counteren[i]);
        ok &= rvvm_snapshot_field(snap, vm->csr.envcfg[i]);
    }
    ok &= rvvm_snapshot_field(snap, vm->csr.mseccfg);
    ok &= rvvm_snapshot_field(snap, vm->csr.fcsr);
    ok &= rvvm_snapshot_field(snap, vm->csr.vcsr);
    ok &= rvvm_snapshot_field(snap, vm->csr.vtype);
    ok &= rvvm_snapshot_field(snap, vm->csr.hartid);

    // AIA register files are allocated in pairs for M/S modes
    uint8_t aia = !!vm->aia;
    ok &= rvvm_snapshot_field(snap, aia);
    if (aia) {
        if (!vm->aia) {
            riscv_hart_aia_init(vm);
        }
        for (size_t i = 0; i < 2; ++i) {
            ok &= rvvm_snapshot_field(snap, vm->aia[i].eidelivery);
            ok &= rvvm_snapshot_field(snap, vm->aia[i].eithreshold);
            for (size_t j = 0; j < RVVM_AIA_ARR_LEN; ++j) {
                ok &= rvvm_snapshot_field(snap, vm->aia[i].eip[j]);
                ok &= rvvm_snapshot_field(snap, vm->aia[i].eie[j]);
            }
        }
    }

    uint64_t mtimecmp = rvtimecmp_get(&vm->mtimecmp);
    uint64_t stimecmp = rvtimecmp_get(&vm->stimecmp);
    ok &= rvvm_snapshot_field(snap, mtimecmp);
    ok &= rvvm_snapshot_field(snap, stimecmp);
    ok &= rvvm_snapshot_field(snap, vm->pending_irqs);

    if (!rvvm_snapshot_writing(snap)) {
        rvtimecmp_set(&vm->mtimecmp, mtimecmp);
        rvtimecmp_set(&vm->stimecmp, stimecmp);

        // Address translation and compiled code are caches over guest memory,
        // which the loaded state has no relation to
        riscv_tlb_flush(vm);
#if defined(USE_JIT)
        if (vm->jit_enabled) {
            riscv_jit_flush_cache(vm);
        }
#endif
        riscv_hart_check_timer(vm);
    }
    return ok;
}

RVVM_PUBLIC bool rvvm_machine_snapshot(rvvm_machine_t* machine, rvvm_snapshot_t* snap)
{
    if (!machine || !snap) {
        return false;
    }
    if (atomic_load_uint32(&machine->running)) {
        rvvm_error("Snapshot of a running machine, pause it first");
        return false;
    }

    bool     resume     = !rvvm_snapshot_writing(snap);
    uint64_t mem_size   = machine->mem.size;
    uint64_t hart_count = vector_size(machine->harts);
    uint64_t time       = rvtimer_get(&machine->timer);
    uint8_t  rv64       = machine->rv64;

    bool ok = rvvm_snapshot_section(snap, "machine");
    ok &= rvvm_snapshot_field(snap, mem_size);
    ok &= rvvm_snapshot_field(snap, hart_count);
    ok &= rvvm_snapshot_field(snap, rv64);
    ok &= rvvm_snapshot_field(snap, time);
    ok &= rvvm_snapshot_field(snap, machine->power_state);
    if (!ok) {
        return false;
    }

    if (resume) {
        // A snapshot only fits the machine it was taken from
        if (mem_size != machine->mem.size || hart_count != vector_size(machine->harts) || rv64 != machine->rv64) {
            rvvm_error("Snapshot does not match this machine");
            return false;
        }
        rvtimer_rebase(&machine->timer, time);
    }

    ok &= rvvm_snapshot_section(snap, "ram");
    ok &= rvvm_snapshot_data(snap, machine->mem.data, machine->mem.size);

    vector_foreach (machine->harts, i) {
        ok &= rvvm_snapshot_section(snap, "hart");
        ok &= rvvm_snapshot_hart(snap, vector_at(machine->harts, i));
    }

    vector_foreach (machine->mmio_devs, i) {
        rvvm_mmio_dev_t* dev = vector_at(machine->mmio_devs, i);
        if (dev->type && dev->type->suspend) {
            dev->type->suspend(dev, snap, resume);
        }
    }

    return ok;
}

POP_OPTIMIZATION_SIZE
