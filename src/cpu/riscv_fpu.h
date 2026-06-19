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

// Bit-precise register reads
static forceinline fpu_f32_t riscv_view_s(rvvm_hart_t* vm, size_t reg)
{
    return fpu_unpack_f32_from_f64(vm->fpu_registers[reg]);
}

// Normalized register reads
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

/*
 * roundTiesToAway core fixup, shared by the OP-FP dispatch (riscv_fpu.c) and the
 * FMA family (riscv_rmm_fma_apply, below). Given a round-to-nearest-even result n
 * and the EXACT rounding error err = (true result) - n, step n one ULP outward
 * only on an exact halfway tie (err exactly half a ULP, pointing away from zero);
 * otherwise RNE already equals roundTiesToAway. See the strategy comment in
 * riscv_fpu.c.
 */
static forceinline fpu_f32_t riscv_rmm_apply_f32(fpu_f32_t n, fpu_f32_t err)
{
    const uint32_t un = fpu_bit_f32_to_u32(n);
    const uint32_t ue = fpu_bit_f32_to_u32(err);
    // Skip exact results (err == +/-0) and errors pointing toward zero: in both
    // cases RNE already delivers the roundTiesToAway value.
    if ((ue << 1) == 0 || (un >> 31) != (ue >> 31)) {
        return n;
    }
    // n + 1 ULP toward larger magnitude, and the exact gap to it.
    const fpu_f32_t away    = fpu_bit_u32_to_f32(un + 1);
    const fpu_f32_t spacing = fpu_sub32(away, n);  // exact: adjacent floats
    // Tie iff the exact result is the midpoint, i.e. 2*err == spacing.
    if (fpu_is_bit_equal32(fpu_add32(err, err), spacing)) {
        return away;
    }
    return n;
}

static forceinline fpu_f64_t riscv_rmm_apply_f64(fpu_f64_t n, fpu_f64_t err)
{
    const uint64_t un = fpu_bit_f64_to_u64(n);
    const uint64_t ue = fpu_bit_f64_to_u64(err);
    if ((ue << 1) == 0 || (un >> 63) != (ue >> 63)) {
        return n;
    }
    const fpu_f64_t away    = fpu_bit_u64_to_f64(un + 1);
    const fpu_f64_t spacing = fpu_sub64(away, n);
    if (fpu_is_bit_equal64(fpu_add64(err, err), spacing)) {
        return away;
    }
    return n;
}

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
// Underflow flag, RISC-V semantics: UF iff the result is tiny *after rounding* and
// inexact. A subnormal result is unambiguously tiny; a normal result above the
// smallest normal is not. The only hard case is a result of exactly the smallest
// normal: some hosts (e.g. aarch64) detect tininess *before* rounding and raise a
// spurious UF, but RISC-V follows the IEEE "after rounding" rule -- tiny iff the
// exact result, rounded to full precision with an *unbounded* exponent (the finer
// subnormal grid would not have crossed into the normal range), is still below the
// smallest normal. So at the boundary we recompute that: widen the exact a*b+c into
// f64 (lossless for a smallest-normal f32 result), scale into [0.5, 1), and round
// back to f32 in the effective mode -- a magnitude >= 1.0 means not tiny. The gate
// keeps the common FMA path at a single compare.
static forceinline void riscv_fma_fixup_uf32(fpu_f32_t r, fpu_f32_t a, fpu_f32_t b, fpu_f32_t c, uint32_t eff)
{
    if (likely((fpu_bit_f32_to_u32(r) & 0x7FFFFFFFU) != 0x00800000U)) {
        return;  // subnormal UF is genuine; a larger normal result has none
    }
    const uint32_t exc = fpu_get_exceptions();
    if (!(exc & FPU_LIB_FLAG_NX)) {
        return;  // an exact smallest-normal result never underflows
    }
    const fpu_f64_t e = fpu_add64(fpu_mul64(fpu_fcvt_f32_to_f64(a), fpu_fcvt_f32_to_f64(b)),
                                  fpu_fcvt_f32_to_f64(c));                          // exact
    const fpu_f64_t scaled = fpu_mul64(e, fpu_bit_u64_to_f64(0x47D0000000000000ULL));  // * 2^126 -> ~[0.5,1)
    const uint32_t host = fpu_get_rounding_mode();
    fpu_set_rounding_mode(eff == 0x04 ? FPU_LIB_ROUND_NE : eff);
    const fpu_f32_t rn = fpu_fcvt_f64_to_f32(scaled);
    fpu_set_rounding_mode(host);
    const bool tiny = (fpu_bit_f32_to_u32(rn) & 0x7FFFFFFFU) < 0x3F800000U;  // |rn| < 1.0
    fpu_set_exceptions(tiny ? (exc | FPU_LIB_FLAG_UF) : (exc & ~FPU_LIB_FLAG_UF));
}

// f64 has no wider type, so recompute the after-rounding tininess by scaling: a
// smallest-normal f64 result bounds |a|,|b| (a tiny product forces small operands),
// so a*2^200 cannot overflow, and fma(a*2^200, b, c*2^200) = (a*b+c)*2^200 lands in
// the normal range where the fused rounding is unbounded-exponent. The result is
// tiny iff that scaled magnitude is below the scaled smallest normal (2^-822).
static forceinline void riscv_fma_fixup_uf64(fpu_f64_t r, fpu_f64_t a, fpu_f64_t b, fpu_f64_t c, uint32_t eff)
{
    if (likely((fpu_bit_f64_to_u64(r) & 0x7FFFFFFFFFFFFFFFULL) != 0x0010000000000000ULL)) {
        return;  // subnormal UF is genuine; a larger normal result has none
    }
    const uint32_t exc = fpu_get_exceptions();
    if (!(exc & FPU_LIB_FLAG_NX)) {
        return;  // an exact smallest-normal result never underflows
    }
    const fpu_f64_t S = fpu_bit_u64_to_f64(0x4C70000000000000ULL);  // 2^200
    const uint32_t host = fpu_get_rounding_mode();
    fpu_set_rounding_mode(eff == 0x04 ? FPU_LIB_ROUND_NE : eff);
    const fpu_f64_t rs = fpu_fma64(fpu_mul64(a, S), b, fpu_mul64(c, S));  // (a*b+c) * 2^200
    fpu_set_rounding_mode(host);
    const bool tiny = (fpu_bit_f64_to_u64(rs) & 0x7FFFFFFFFFFFFFFFULL) < 0x0C90000000000000ULL;  // |rs| < 2^-822
    fpu_set_exceptions(tiny ? (exc | FPU_LIB_FLAG_UF) : (exc & ~FPU_LIB_FLAG_UF));
}

/*
 * roundTiesToAway fixup for the FMA family. As with the OP-FP ops, the host runs
 * in RNE under frm == RMM, so only an exact halfway tie needs correcting. Both are
 * flag-isolated: the error-free arithmetic does raw host ops whose intermediate
 * steps can raise spurious exceptions, while the genuine flags are already set by
 * the base fma.
 *
 * f32: widen to f64, where a*b is exact (48 <= 53 bits) and the away-side midpoint
 * m is exact (<= 25 significant bits, including the subnormal range). The op is an
 * exact tie iff a*b + c == m, tested as ((a*b + c) - m) == 0 with every term
 * exact. A subnormal f32 result is covered too: it widens to a normal f64.
 */
static forceinline fpu_f32_t riscv_rmm_fma_apply_f32(fpu_f32_t n, fpu_f32_t a, fpu_f32_t b, fpu_f32_t c)
{
    if (unlikely(!fpu_is_finite32(n))) {
        return n;
    }
    const uint32_t  exc   = fpu_get_exceptions();
    const fpu_f32_t away  = fpu_bit_u32_to_f32(fpu_bit_f32_to_u32(n) + 1);  // toward larger magnitude
    const fpu_f64_t dab   = fpu_mul64(fpu_fcvt_f32_to_f64(a), fpu_fcvt_f32_to_f64(b));  // exact
    const fpu_f64_t dc    = fpu_fcvt_f32_to_f64(c);
    const fpu_f64_t s1    = fpu_add64(dab, dc);
    const fpu_f64_t s1e   = fpu_add_error64(s1, dab, dc);  // exact: dab + dc == s1 + s1e
    const fpu_f64_t dn    = fpu_fcvt_f32_to_f64(n);
    const fpu_f64_t half  = fpu_bit_u64_to_f64(0x3FE0000000000000ULL);  // 0.5
    const fpu_f64_t m     = fpu_add64(dn, fpu_mul64(fpu_sub64(fpu_fcvt_f32_to_f64(away), dn), half));
    const fpu_f64_t resid = fpu_add64(fpu_sub64(s1, m), s1e);  // (a*b + c) - m, exactly 0 only on a tie
    const fpu_f32_t r     = ((fpu_bit_f64_to_u64(resid) << 1) == 0) ? away : n;
    fpu_set_exceptions(exc);
    return r;
}

/*
 * f64: there is no wider host type, so recover the exact residual a*b + c - n via
 * the Boldo-Muller error-free FMA (TwoProduct + two TwoSums + a final FastTwoSum),
 * which splits it into a non-overlapping pair (r2, r3). An exact tie has the
 * residual equal to +/- half a ULP -- a single float -- so r3 == 0 and r2 is that
 * half-ULP, exactly the condition riscv_rmm_apply_f64 already tests.
 */
static forceinline fpu_f64_t riscv_rmm_fma_apply_f64(fpu_f64_t n, fpu_f64_t a, fpu_f64_t b, fpu_f64_t c)
{
    if (unlikely(!fpu_is_finite64(n))) {
        return n;
    }
    const uint32_t  exc = fpu_get_exceptions();
    const fpu_f64_t p   = fpu_mul64(a, b);
    const fpu_f64_t pe  = fpu_fma64(a, b, fpu_neg64(p));  // TwoProduct tail: a*b == p + pe
    const fpu_f64_t a1  = fpu_add64(c, pe);
    const fpu_f64_t a2  = fpu_add_error64(a1, c, pe);     // TwoSum(c, pe)
    const fpu_f64_t b1  = fpu_add64(p, a1);
    const fpu_f64_t b2  = fpu_add_error64(b1, p, a1);     // TwoSum(p, a1)
    const fpu_f64_t g   = fpu_add64(fpu_sub64(b1, n), b2);
    const fpu_f64_t r2  = fpu_add64(g, a2);               // FastTwoSum(g, a2): residual == r2 + r3
    const fpu_f64_t r3  = fpu_sub64(a2, fpu_sub64(r2, g));
    const fpu_f64_t r   = ((fpu_bit_f64_to_u64(r3) << 1) == 0) ? riscv_rmm_apply_f64(n, r2) : n;
    fpu_set_exceptions(exc);
    return r;
}

static forceinline fpu_f32_t riscv_fma_round_f32(rvvm_hart_t* vm, uint32_t rm, fpu_f32_t a, fpu_f32_t b, fpu_f32_t c)
{
    const uint32_t frm = vm->csr.fcsr >> 5;
    const uint32_t eff = (rm == 0x07) ? frm : rm;
    fpu_f32_t r;
    if (unlikely(eff == 0x04)) {
        // RMM: compute in RNE, then apply the roundTiesToAway fixup.
        const uint32_t host = fpu_get_rounding_mode();
        fpu_set_rounding_mode(FPU_LIB_ROUND_NE);
        r = riscv_rmm_fma_apply_f32(fpu_fma32(a, b, c), a, b, c);
        fpu_set_rounding_mode(host);
    } else if (rm != 0x07 && eff != frm) {
        // Static host-native mode that differs from frm.
        const uint32_t host = fpu_get_rounding_mode();
        fpu_set_rounding_mode(eff);
        r = fpu_fma32(a, b, c);
        fpu_set_rounding_mode(host);
    } else {
        r = fpu_fma32(a, b, c);
    }
    riscv_fma_fixup_uf32(r, a, b, c, eff);
    return r;
}

static forceinline fpu_f64_t riscv_fma_round_f64(rvvm_hart_t* vm, uint32_t rm, fpu_f64_t a, fpu_f64_t b, fpu_f64_t c)
{
    const uint32_t frm = vm->csr.fcsr >> 5;
    const uint32_t eff = (rm == 0x07) ? frm : rm;
    fpu_f64_t r;
    if (unlikely(eff == 0x04)) {
        // RMM: compute in RNE, then apply the roundTiesToAway fixup.
        const uint32_t host = fpu_get_rounding_mode();
        fpu_set_rounding_mode(FPU_LIB_ROUND_NE);
        r = riscv_rmm_fma_apply_f64(fpu_fma64(a, b, c), a, b, c);
        fpu_set_rounding_mode(host);
    } else if (rm != 0x07 && eff != frm) {
        // Static host-native mode that differs from frm.
        const uint32_t host = fpu_get_rounding_mode();
        fpu_set_rounding_mode(eff);
        r = fpu_fma64(a, b, c);
        fpu_set_rounding_mode(host);
    } else {
        r = fpu_fma64(a, b, c);
    }
    riscv_fma_fixup_uf64(r, a, b, c, eff);
    return r;
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
                riscv_emit_s(vm, rds,
                             riscv_fma_round_f32(vm, rm, riscv_view_s(vm, rs1), //
                                       riscv_view_s(vm, rs2), //
                                       riscv_view_s(vm, rs3)));
                return;
            case 0x1: // fmadd.d
                riscv_emit_d(vm, rds,
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
                riscv_emit_s(vm, rds,
                             riscv_fma_round_f32(vm, rm, riscv_view_s(vm, rs1), //
                                       riscv_view_s(vm, rs2), //
                                       fpu_neg32(riscv_view_s(vm, rs3))));
                return;
            case 0x1: // fmsub.d
                riscv_emit_d(vm, rds,
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
                riscv_emit_s(vm, rds,
                             riscv_fma_round_f32(vm, rm, fpu_neg32(riscv_view_s(vm, rs1)), //
                                       riscv_view_s(vm, rs2),            //
                                       riscv_view_s(vm, rs3)));
                return;
            case 0x1: // fnmsub.d
                riscv_emit_d(vm, rds,
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
                riscv_emit_s(vm, rds,
                             riscv_fma_round_f32(vm, rm, fpu_neg32(riscv_view_s(vm, rs1)), //
                                                 riscv_view_s(vm, rs2), //
                                                 fpu_neg32(riscv_view_s(vm, rs3))));
                return;
            case 0x1: // fnmadd.d
                riscv_emit_d(vm, rds,
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
