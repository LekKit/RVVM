/*
riscv_fpu.h - RISC-V Floating-Point ISA interpreter template
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef RVVM_RISCV_FPU_H
#define RVVM_RISCV_FPU_H

#include "fpu_lib.h"      // IWYU pragma: keep
#include "riscv_common.h" // IWYU pragma: keep

#if defined(USE_RVV)
#include "riscv_vector.h" // IWYU pragma: keep
#endif

#if defined(USE_FPU)

// Rounding mode values of 5 & 6 are illegal
static forceinline bool riscv_fpu_rm_is_valid(uint32_t rm)
{
    rm = 6 - rm;
    return rm > 1;
}

// Bit-precise register read (raw low 32 bits, no NaN-box check) — for fmv.x.w
static forceinline fpu_f32_t riscv_view_s(rvvm_hart_t* vm, size_t reg)
{
    return fpu_unpack_f32_from_f64(vm->fpu_registers[reg]);
}

// Value register read: a mal-boxed narrow operand reads as the canonical NaN
static forceinline fpu_f32_t riscv_read_s(rvvm_hart_t* vm, size_t reg)
{
    return fpu_nan_unbox_f32(vm->fpu_registers[reg]);
}

// For bit-precise float register writes
static forceinline void riscv_emit_s(rvvm_hart_t* vm, size_t reg, fpu_f32_t val)
{
    vm->fpu_registers[reg] = fpu_nanbox_f32(val);
    riscv_fpu_set_dirty(vm);
}

// Canonizes the written result
static forceinline void riscv_write_s(rvvm_hart_t* vm, size_t reg, fpu_f32_t val)
{
    if (likely(!fpu_is_nan32(val))) {
        riscv_emit_s(vm, reg, val);
    } else {
        riscv_emit_s(vm, reg, fpu_bit_u32_to_f32(FPU_LIB_FP32_CANONICAL_NAN));
    }
}

static forceinline fpu_f64_t riscv_view_d(rvvm_hart_t* vm, size_t reg)
{
    return vm->fpu_registers[reg];
}

static forceinline fpu_f64_t riscv_read_d(rvvm_hart_t* vm, size_t reg)
{
    return vm->fpu_registers[reg];
}

static forceinline void riscv_emit_d(rvvm_hart_t* vm, size_t reg, fpu_f64_t val)
{
    vm->fpu_registers[reg] = val;
    riscv_fpu_set_dirty(vm);
}

static forceinline void riscv_write_d(rvvm_hart_t* vm, size_t reg, fpu_f64_t val)
{
    if (likely(!fpu_is_nan64(val))) {
        riscv_emit_d(vm, reg, val);
    } else {
        riscv_emit_d(vm, reg, fpu_bit_u64_to_f64(FPU_LIB_FP64_CANONICAL_NAN));
    }
}

slow_path void riscv_emulate_f_opc_op(rvvm_hart_t* vm, const uint32_t insn);

#if defined(RISCV32) || defined(RISCV64)

static forceinline void riscv_emulate_f_opc_load(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t  rds  = bit_ext_u32(insn, 7, 5);
    const size_t  rs1  = bit_ext_u32(insn, 15, 5);
    const sxlen_t off  = bit_ext_i32(insn, 20, 12);
    const xlen_t  addr = riscv_read_reg(vm, rs1) + off;

    if (likely(riscv_fpu_is_enabled(vm))) {
        switch (bit_ext_u32(insn, 12, 3)) {
            case 0x02: // flw
                riscv_load_float(vm, addr, rds);
                return;
            case 0x03: // fld
                riscv_load_double(vm, addr, rds);
                return;
        }
    }

    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_f_opc_store(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t  rs1  = bit_ext_u32(insn, 15, 5);
    const size_t  rs2  = bit_ext_u32(insn, 20, 5);
    const sxlen_t off  = (int32_t)((((uint32_t)(((int32_t)insn) >> 25)) << 5) | bit_ext_u32(insn, 7, 5));
    const xlen_t  addr = riscv_read_reg(vm, rs1) + off;

    if (likely(riscv_fpu_is_enabled(vm))) {
        switch (bit_ext_u32(insn, 12, 3)) {
            case 0x02: // fsw
                riscv_store_float(vm, addr, rs2);
                return;
            case 0x03: // fsd
                riscv_store_double(vm, addr, rs2);
                return;
        }
    }

    riscv_illegal_insn(vm, insn);
}

/*
 * FMA honors the rounding mode like any other rounding op: dynamically via frm
 * (the host already tracks it) or statically via the instruction's rm field.
 * fpu_fma rounds in the host mode, so a static rm that differs from frm must be
 * applied around the op; RMM has no host mode and is run in RNE. (RMM differs
 * from RNE only on an exact halfway tie, and an exact FMA tie is vanishingly rare
 * — the remaining ties-away gap is shared with the rest of the FP path.)
 */
static forceinline fpu_f32_t riscv_fma_round_f32(rvvm_hart_t* vm, uint32_t rm, fpu_f32_t a, fpu_f32_t b, fpu_f32_t c)
{
    const uint32_t frm = vm->csr.fcsr >> 5;
    const uint32_t eff = (rm == 0x07) ? frm : rm;
    if (unlikely(eff == 0x04 || (rm != 0x07 && eff != frm))) {
        const uint32_t host = fpu_get_rounding_mode();
        fpu_set_rounding_mode(eff == 0x04 ? FPU_LIB_ROUND_NE : eff);
        const fpu_f32_t r = fpu_fma32(a, b, c);
        fpu_set_rounding_mode(host);
        return r;
    }
    return fpu_fma32(a, b, c);
}

static forceinline fpu_f64_t riscv_fma_round_f64(rvvm_hart_t* vm, uint32_t rm, fpu_f64_t a, fpu_f64_t b, fpu_f64_t c)
{
    const uint32_t frm = vm->csr.fcsr >> 5;
    const uint32_t eff = (rm == 0x07) ? frm : rm;
    if (unlikely(eff == 0x04 || (rm != 0x07 && eff != frm))) {
        const uint32_t host = fpu_get_rounding_mode();
        fpu_set_rounding_mode(eff == 0x04 ? FPU_LIB_ROUND_NE : eff);
        const fpu_f64_t r = fpu_fma64(a, b, c);
        fpu_set_rounding_mode(host);
        return r;
    }
    return fpu_fma64(a, b, c);
}

static forceinline void riscv_emulate_f_fmadd(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t   rds = bit_ext_u32(insn, 7, 5);
    const uint32_t rm  = bit_ext_u32(insn, 12, 3);
    const size_t   rs1 = bit_ext_u32(insn, 15, 5);
    const size_t   rs2 = bit_ext_u32(insn, 20, 5);
    const size_t   rs3 = insn >> 27;

    if (likely(riscv_fpu_is_enabled(vm) && riscv_fpu_rm_is_valid(rm))) {
        switch (bit_ext_u32(insn, 25, 2)) {
            case 0x0: // fmadd.s
                riscv_write_s(vm, rds,
                             riscv_fma_round_f32(vm, rm, riscv_read_s(vm, rs1), //
                                       riscv_read_s(vm, rs2), //
                                       riscv_read_s(vm, rs3)));
                return;
            case 0x1: // fmadd.d
                riscv_write_d(vm, rds,
                             riscv_fma_round_f64(vm, rm, riscv_view_d(vm, rs1), //
                                       riscv_view_d(vm, rs2), //
                                       riscv_view_d(vm, rs3)));
                return;
        }
    }

    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_f_fmsub(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t   rds = bit_ext_u32(insn, 7, 5);
    const uint32_t rm  = bit_ext_u32(insn, 12, 3);
    const size_t   rs1 = bit_ext_u32(insn, 15, 5);
    const size_t   rs2 = bit_ext_u32(insn, 20, 5);
    const size_t   rs3 = insn >> 27;

    if (likely(riscv_fpu_is_enabled(vm) && riscv_fpu_rm_is_valid(rm))) {
        switch (bit_ext_u32(insn, 25, 2)) {
            case 0x0: // fmsub.s
                riscv_write_s(vm, rds,
                             riscv_fma_round_f32(vm, rm, riscv_read_s(vm, rs1), //
                                       riscv_read_s(vm, rs2), //
                                       fpu_neg32(riscv_read_s(vm, rs3))));
                return;
            case 0x1: // fmsub.d
                riscv_write_d(vm, rds,
                             riscv_fma_round_f64(vm, rm, riscv_view_d(vm, rs1), //
                                       riscv_view_d(vm, rs2), //
                                       fpu_neg64(riscv_view_d(vm, rs3))));
                return;
        }
    }

    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_f_fnmsub(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t   rds = bit_ext_u32(insn, 7, 5);
    const uint32_t rm  = bit_ext_u32(insn, 12, 3);
    const size_t   rs1 = bit_ext_u32(insn, 15, 5);
    const size_t   rs2 = bit_ext_u32(insn, 20, 5);
    const size_t   rs3 = insn >> 27;

    if (likely(riscv_fpu_is_enabled(vm) && riscv_fpu_rm_is_valid(rm))) {
        switch (bit_ext_u32(insn, 25, 2)) {
            case 0x0: // fnmsub.s
                riscv_write_s(vm, rds,
                             riscv_fma_round_f32(vm, rm, fpu_neg32(riscv_read_s(vm, rs1)), //
                                       riscv_read_s(vm, rs2),            //
                                       riscv_read_s(vm, rs3)));
                return;
            case 0x1: // fnmsub.d
                riscv_write_d(vm, rds,
                             riscv_fma_round_f64(vm, rm, fpu_neg64(riscv_view_d(vm, rs1)), //
                                       riscv_view_d(vm, rs2),            //
                                       riscv_view_d(vm, rs3)));
                return;
        }
    }

    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_f_fnmadd(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t   rds = bit_ext_u32(insn, 7, 5);
    const uint32_t rm  = bit_ext_u32(insn, 12, 3);
    const size_t   rs1 = bit_ext_u32(insn, 15, 5);
    const size_t   rs2 = bit_ext_u32(insn, 20, 5);
    const size_t   rs3 = insn >> 27;

    if (likely(riscv_fpu_is_enabled(vm) && riscv_fpu_rm_is_valid(rm))) {
        switch (bit_ext_u32(insn, 25, 2)) {
            case 0x0: // fnmadd.s = -(rs1*rs2) - rs3; negate operands so the single
                       // rounding sees the correctly-signed result (directed modes)
                riscv_write_s(vm, rds,
                             riscv_fma_round_f32(vm, rm, fpu_neg32(riscv_read_s(vm, rs1)), //
                                                 riscv_read_s(vm, rs2), //
                                                 fpu_neg32(riscv_read_s(vm, rs3))));
                return;
            case 0x1: // fnmadd.d
                riscv_write_d(vm, rds,
                             riscv_fma_round_f64(vm, rm, fpu_neg64(riscv_view_d(vm, rs1)), //
                                                 riscv_view_d(vm, rs2), //
                                                 fpu_neg64(riscv_view_d(vm, rs3))));
                return;
        }
    }

    riscv_illegal_insn(vm, insn);
}

#endif

#endif

#endif
