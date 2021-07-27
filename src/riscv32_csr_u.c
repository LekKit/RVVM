/*
riscv32_csr_u.c - RISC-V User Level Control and Status Registers
Copyright (C) 2021  LekKit <github.com/LekKit>
                    Mr0maks <mr.maks0443@gmail.com>

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

#include "bit_ops.h"
#include "riscv32.h"
#include "riscv32_csr.h"

#ifdef USE_FPU
#include <fenv.h>
#endif

#define CSR_USTATUS_MASK 0x11

extern uint64_t clint_mtime;

#ifdef USE_FPU
#define FFLAG_NX (1 << 0) /* inexact */
#define FFLAG_UF (1 << 1) /* undeflow */
#define FFLAG_OF (1 << 2) /* overflow */
#define FFLAG_DZ (1 << 3) /* divide by zero */
#define FFLAG_NV (1 << 4) /* invalid operation */

static int fpu_get_exceptions()
{
    int ret = 0;
    int exc = fetestexcept(FE_ALL_EXCEPT);
    if (exc & FE_INEXACT)   ret |= FFLAG_NX;
    if (exc & FE_UNDERFLOW) ret |= FFLAG_UF;
    if (exc & FE_OVERFLOW)  ret |= FFLAG_OF;
    if (exc & FE_DIVBYZERO) ret |= FFLAG_DZ;
    if (exc & FE_INVALID)   ret |= FFLAG_NV;
    return ret;
}

static void fpu_set_exceptions(uint32_t flags)
{
    int exc = 0;
    feclearexcept(FE_ALL_EXCEPT);
    if (flags & FFLAG_NX) exc |= FE_INEXACT;
    if (flags & FFLAG_UF) exc |= FE_UNDERFLOW;
    if (flags & FFLAG_OF) exc |= FE_OVERFLOW;
    if (flags & FFLAG_DZ) exc |= FE_DIVBYZERO;
    if (flags & FFLAG_NV) exc |= FE_INVALID;
    if (exc != 0) feraiseexcept(exc);
}

static bool riscv32_csr_fflags(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    if (!fpu_is_enabled(vm)) {
        return false;
    }
    uint32_t val = fpu_get_exceptions();
    uint32_t oldval = val;
    csr_helper(&val, dest, op);
    if (val != oldval) fpu_set_fs(vm, S_DIRTY);
    fpu_set_exceptions(val);
    vm->csr.fcsr &= ~((1 << 5) - 1);
    vm->csr.fcsr |= val;
    vm->csr.fcsr &= 0xff;
    *dest &= 0x1f;
    return true;
}

static bool riscv32_csr_frm(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    if (!fpu_is_enabled(vm)) {
        return false;
    }
    uint32_t val = vm->csr.fcsr >> 5;
    uint32_t oldval = val;
    csr_helper(&val, dest, op);
    if (val != oldval) fpu_set_fs(vm, S_DIRTY);
    fpu_set_rm(vm, val & ((1 << 3) - 1));
    vm->csr.fcsr = (vm->csr.fcsr & ((1 << 5) - 1)) | (val << 5);
    vm->csr.fcsr &= 0xff;
    *dest &= 0x7;
    return true;
}

static bool riscv32_csr_fcsr(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    if (!fpu_is_enabled(vm)) {
        return false;
    }
    uint32_t val = vm->csr.fcsr | fpu_get_exceptions();
    uint32_t oldval = val;
    csr_helper(&val, dest, op);
    if (val != oldval) fpu_set_fs(vm, S_DIRTY);
    fpu_set_rm(vm, bit_cut(val, 5, 3));
    fpu_set_exceptions(val);
    vm->csr.fcsr = val;
    vm->csr.fcsr &= 0xff;
    *dest &= 0xff;
    return true;
}
#endif

static bool riscv32_csr_time(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    UNUSED(op);
    rvtimer_update(&vm->timer);
    *dest = vm->timer.time;
    return true;
}

static bool riscv32_csr_timeh(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    UNUSED(op);
    *dest = vm->timer.time >> 32;
    return true;
}

void riscv32_csr_u_init()
{
    // User Trap Setup
    riscv32_csr_init(0x000, "ustatus", riscv32_csr_unimp);
    riscv32_csr_init(0x004, "uie", riscv32_csr_unimp);
    riscv32_csr_init(0x005, "utvec", riscv32_csr_unimp);

    // User Trap Handling
    riscv32_csr_init(0x040, "uscratch", riscv32_csr_unimp);
    riscv32_csr_init(0x041, "uepc", riscv32_csr_unimp);
    riscv32_csr_init(0x042, "ucause", riscv32_csr_unimp);
    riscv32_csr_init(0x043, "utval", riscv32_csr_unimp);
    riscv32_csr_init(0x044, "uip", riscv32_csr_unimp);

    // User Floating-Point CSRs
#ifdef USE_FPU
    riscv32_csr_init(0x001, "fflags", riscv32_csr_fflags);
    riscv32_csr_init(0x002, "frm", riscv32_csr_frm);
    riscv32_csr_init(0x003, "fcsr", riscv32_csr_fcsr);
#endif

    // User Counter/Timers
    riscv32_csr_init(0xC00, "cycle", riscv32_csr_unimp);
    riscv32_csr_init(0xC01, "time", riscv32_csr_time);
    riscv32_csr_init(0xC02, "instret", riscv32_csr_unimp);
    riscv32_csr_init(0xC80, "cycleh", riscv32_csr_unimp);
    riscv32_csr_init(0xC81, "timeh", riscv32_csr_timeh);
    riscv32_csr_init(0xC82, "instreth", riscv32_csr_unimp);

    for (uint32_t i=3; i<32; ++i) {
        riscv32_csr_init(0xC00+i, "hpmcounter", riscv32_csr_unimp);
        riscv32_csr_init(0xC80+i, "hpmcounterh", riscv32_csr_unimp);
    }
}
