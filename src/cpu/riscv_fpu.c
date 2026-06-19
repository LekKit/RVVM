/*
riscv_fpu.h - RISC-V Floating-Point interpreter
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#if defined(USE_FPU)

#include "riscv_fpu.h"

#define RISCV_FPU_GEN_RM_CASES(insn)                                                                                   \
    insn:                                                                                                              \
    case insn | 0x1000U:                                                                                               \
    case insn | 0x2000U:                                                                                               \
    case insn | 0x3000U:                                                                                               \
    case insn | 0x4000U:                                                                                               \
    case insn | 0x7000U

static const uint32_t riscv_fli_table[32] = {
    0xBF800000UL, // -1.0
    0x00800000UL, // Minimum positive normal
    0x37800000UL, // 1.0 x 2^-16
    0x38000000UL, // 1.0 x 2^-15
    0x3B800000UL, // 1.0 x 2^-8
    0x3C000000UL, // 1.0 x 2^-7
    0x3D800000UL, // 0.0625
    0x3E000000UL, // 0.125
    0x3E800000UL, // 0.25
    0x3EA00000UL, // 0.3125
    0x3EC00000UL, // 0.375
    0x3EE00000UL, // 0.4375
    0x3F000000UL, // 0.5
    0x3F200000UL, // 0.625
    0x3F400000UL, // 0.75
    0x3F600000UL, // 0.825
    0x3F800000UL, // 1.0
    0x3FA00000UL, // 1.25
    0x3FC00000UL, // 1.5
    0x3FE00000UL, // 1.75
    0x40000000UL, // 2.0
    0x40200000UL, // 2.5
    0x40400000UL, // 3.0
    0x40800000UL, // 4.0
    0x41000000UL, // 8.0
    0x41800000UL, // 16.0
    0x43000000UL, // 128.0
    0x43800000UL, // 256.0
    0x47000000UL, // 2^15
    0x47800000UL, // 2^16
    0x7F800000UL, // Infinity
    0x7FC00000UL, // Canonical NaN
};

/*
 * RMM (round to nearest, ties to max magnitude) == IEEE 754 roundTiesToAway.
 *
 * The host FPU has no such mode, so when frm == RMM the host is left in
 * round-to-nearest-even (see fpu_set_rounding_mode()). RNE and roundTiesToAway
 * produce identical results EXCEPT on an exact halfway tie, where RNE rounds to
 * even and roundTiesToAway rounds to the larger-magnitude neighbour.
 *
 * So we compute the op in RNE, recover the EXACT rounding error via the library's
 * error-free transforms (TwoSum / TwoProduct), and only when that error is
 * exactly half a ULP away from zero do we step the result outward by one ULP.
 * (The previous implementation rounded toward +/-inf unconditionally, which is
 * correct on ties but wrong for every inexact non-tie. See issue #204.)
 *
 * fadd/fsub/fmul use riscv_rmm_apply (below). fsqrt never needs a fixup: a square
 * root is irrational unless exact, and its result is always normal (the square
 * root of even the smallest subnormal is ~2^-75), so RNE == roundTiesToAway. A
 * *normally-rounded* quotient is likewise never an exact halfway case — but a
 * *subnormal* quotient has reduced precision and CAN land exactly on a tie, so
 * fdiv gets a dedicated subnormal fixup (riscv_rmm_div_apply).
 *
 * The error-free transforms run only for a finite result, and the per-op wrappers
 * (riscv_rmm_add/mul/div) snapshot and restore the exception flags around them:
 * TwoSum/TwoProduct do raw host arithmetic whose intermediate steps can raise
 * spurious exceptions (inf-inf -> NV, near-FLT_MAX -> OF) that must not leak into
 * fflags. The genuine flags are already set by the base op.
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

// roundTiesToAway fixup for fadd/fsub (fsub passes b negated). Flag-isolated.
static forceinline fpu_f32_t riscv_rmm_add_f32(fpu_f32_t n, fpu_f32_t a, fpu_f32_t b)
{
    if (unlikely(!fpu_is_finite32(n))) {
        return n;
    }
    const uint32_t exc = fpu_get_exceptions();
    const fpu_f32_t r  = riscv_rmm_apply_f32(n, fpu_add_error32(n, a, b));
    fpu_set_exceptions(exc);
    return r;
}

static forceinline fpu_f64_t riscv_rmm_add_f64(fpu_f64_t n, fpu_f64_t a, fpu_f64_t b)
{
    if (unlikely(!fpu_is_finite64(n))) {
        return n;
    }
    const uint32_t exc = fpu_get_exceptions();
    const fpu_f64_t r  = riscv_rmm_apply_f64(n, fpu_add_error64(n, a, b));
    fpu_set_exceptions(exc);
    return r;
}

// roundTiesToAway fixup for fmul. Flag-isolated.
static forceinline fpu_f32_t riscv_rmm_mul_f32(fpu_f32_t n, fpu_f32_t a, fpu_f32_t b)
{
    if (unlikely(!fpu_is_finite32(n))) {
        return n;
    }
    const uint32_t exc = fpu_get_exceptions();
    const fpu_f32_t r  = riscv_rmm_apply_f32(n, fpu_mul_error32(n, a, b));
    fpu_set_exceptions(exc);
    return r;
}

static forceinline fpu_f64_t riscv_rmm_mul_f64(fpu_f64_t n, fpu_f64_t a, fpu_f64_t b)
{
    if (unlikely(!fpu_is_finite64(n))) {
        return n;
    }
    const uint32_t exc = fpu_get_exceptions();
    const fpu_f64_t r  = riscv_rmm_apply_f64(n, fpu_mul_error64(n, a, b));
    fpu_set_exceptions(exc);
    return r;
}

/*
 * roundTiesToAway fixup for fdiv. Only a subnormal (or zero) quotient can be an
 * exact tie; a normal quotient never is, so the common path returns immediately
 * (non-finite results also have a non-zero exponent field and return unchanged).
 *
 * For a subnormal result the exact residual rho = a - n*b is representable, so
 * fma(-n, b, a) recovers it exactly. The true quotient is n + rho/b, so it is the
 * exact midpoint (a tie, rounded away) iff 2*rho == gap*b, where gap = away - n
 * (= +/- the subnormal step). |rho| <= |b|*2^-1075, so every value below stays in
 * range and exact. Flag-isolated, as the residual machinery can raise spurious
 * exceptions.
 */
static forceinline fpu_f32_t riscv_rmm_div_apply_f32(fpu_f32_t n, fpu_f32_t a, fpu_f32_t b)
{
    if (likely(fpu_bit_f32_to_u32(n) & FPU_LIB_FP32_EXPONENT_MASK)) {
        return n;  // normal / inf / nan: RNE already == roundTiesToAway
    }
    const uint32_t  exc  = fpu_get_exceptions();
    const fpu_f32_t rho  = fpu_fma32(fpu_neg32(n), b, a);  // exact residual a - n*b
    const fpu_f32_t away = fpu_bit_u32_to_f32(fpu_bit_f32_to_u32(n) + 1);
    const fpu_f32_t gap  = fpu_sub32(away, n);             // +/- 2^-149, exact
    // Tie iff 2*rho == gap*b, evaluated in fp64 where both sides are exact
    // (operands widen losslessly and gap*b ~ 2^-22 stays well inside the range).
    const fpu_f64_t two_rho = fpu_add64(fpu_fcvt_f32_to_f64(rho), fpu_fcvt_f32_to_f64(rho));
    const fpu_f64_t gap_b   = fpu_mul64(fpu_fcvt_f32_to_f64(gap), fpu_fcvt_f32_to_f64(b));
    const fpu_f32_t r       = fpu_is_bit_equal64(two_rho, gap_b) ? away : n;
    fpu_set_exceptions(exc);
    return r;
}

static forceinline fpu_f64_t riscv_rmm_div_apply_f64(fpu_f64_t n, fpu_f64_t a, fpu_f64_t b)
{
    if (likely(fpu_bit_f64_to_u64(n) & FPU_LIB_FP64_EXPONENT_MASK)) {
        return n;
    }
    const uint32_t  exc  = fpu_get_exceptions();
    const fpu_f64_t rho  = fpu_fma64(fpu_neg64(n), b, a);
    const fpu_f64_t away = fpu_bit_u64_to_f64(fpu_bit_f64_to_u64(n) + 1);
    const fpu_f64_t gap  = fpu_sub64(away, n);             // +/- 2^-1074, exact
    // fp64 has no wider type; gap*b underflows, so rescale 2*rho == gap*b by the
    // gap magnitude (2^1074) as two exact power-of-two steps: rho*2^1075 == +/-b.
    const fpu_f64_t scaled = fpu_mul64(fpu_mul64(rho, fpu_bit_u64_to_f64(0x7FE0000000000000ULL)),  // 2^1023
                                       fpu_bit_u64_to_f64(0x4330000000000000ULL));                 // 2^52
    const fpu_f64_t target = (fpu_bit_f64_to_u64(gap) >> 63) ? fpu_neg64(b) : b;
    const fpu_f64_t r      = fpu_is_bit_equal64(scaled, target) ? away : n;
    fpu_set_exceptions(exc);
    return r;
}

static slow_path void riscv_emulate_f_opc_op_impl(rvvm_hart_t* vm, const uint32_t insn, const bool rmm)
{
    const size_t   rds = bit_ext_u32(insn, 7, 5);
    const uint32_t rm  = bit_ext_u32(insn, 12, 3);
    const size_t   rs1 = bit_ext_u32(insn, 15, 5);
    const size_t   rs2 = bit_ext_u32(insn, 20, 5);

    if (likely(riscv_fpu_is_enabled(vm))) {

        switch (insn & 0xFE007000UL) {
            /*
             * FPU computations
             */
            case RISCV_FPU_GEN_RM_CASES(0x00000000UL): { // fadd.s
                const fpu_f32_t a = riscv_view_s(vm, rs1), b = riscv_view_s(vm, rs2);
                const fpu_f32_t n = fpu_add32(a, b);
                riscv_emit_s(vm, rds, rmm ? riscv_rmm_add_f32(n, a, b) : n);
                return;
            }
            case RISCV_FPU_GEN_RM_CASES(0x02000000UL): { // fadd.d
                const fpu_f64_t a = riscv_view_d(vm, rs1), b = riscv_view_d(vm, rs2);
                const fpu_f64_t n = fpu_add64(a, b);
                riscv_emit_d(vm, rds, rmm ? riscv_rmm_add_f64(n, a, b) : n);
                return;
            }
            case RISCV_FPU_GEN_RM_CASES(0x08000000UL): { // fsub.s
                const fpu_f32_t a = riscv_view_s(vm, rs1), b = riscv_view_s(vm, rs2);
                const fpu_f32_t n = fpu_sub32(a, b);
                riscv_write_s(vm, rds, rmm ? riscv_rmm_add_f32(n, a, fpu_neg32(b)) : n);
                return;
            }
            case RISCV_FPU_GEN_RM_CASES(0x0A000000UL): { // fsub.d
                const fpu_f64_t a = riscv_view_d(vm, rs1), b = riscv_view_d(vm, rs2);
                const fpu_f64_t n = fpu_sub64(a, b);
                riscv_write_d(vm, rds, rmm ? riscv_rmm_add_f64(n, a, fpu_neg64(b)) : n);
                return;
            }
            case RISCV_FPU_GEN_RM_CASES(0x10000000UL): { // fmul.s
                const fpu_f32_t a = riscv_view_s(vm, rs1), b = riscv_view_s(vm, rs2);
                const fpu_f32_t n = fpu_mul32(a, b);
                riscv_emit_s(vm, rds, rmm ? riscv_rmm_mul_f32(n, a, b) : n);
                return;
            }
            case RISCV_FPU_GEN_RM_CASES(0x12000000UL): { // fmul.d
                const fpu_f64_t a = riscv_view_d(vm, rs1), b = riscv_view_d(vm, rs2);
                const fpu_f64_t n = fpu_mul64(a, b);
                riscv_emit_d(vm, rds, rmm ? riscv_rmm_mul_f64(n, a, b) : n);
                return;
            }
            case RISCV_FPU_GEN_RM_CASES(0x18000000UL): { // fdiv.s
                const fpu_f32_t a = riscv_view_s(vm, rs1), b = riscv_view_s(vm, rs2);
                const fpu_f32_t n = fpu_div32(a, b);
                riscv_emit_s(vm, rds, rmm ? riscv_rmm_div_apply_f32(n, a, b) : n);
                return;
            }
            case RISCV_FPU_GEN_RM_CASES(0x1A000000UL): { // fdiv.d
                const fpu_f64_t a = riscv_view_d(vm, rs1), b = riscv_view_d(vm, rs2);
                const fpu_f64_t n = fpu_div64(a, b);
                riscv_emit_d(vm, rds, rmm ? riscv_rmm_div_apply_f64(n, a, b) : n);
                return;
            }
            case RISCV_FPU_GEN_RM_CASES(0x58000000UL): // fsqrt.s
                if (likely(!rs2)) {
                    riscv_emit_s(vm, rds, fpu_sqrt32(riscv_view_s(vm, rs1)));
                    return;
                }
                break;
            case RISCV_FPU_GEN_RM_CASES(0x5A000000UL): // fsqrt.s
                if (likely(!rs2)) {
                    riscv_emit_d(vm, rds, fpu_sqrt64(riscv_view_d(vm, rs1)));
                    return;
                }
                break;
            /*
             * FPU conversions
             */
            case RISCV_FPU_GEN_RM_CASES(0x40000000UL):
                switch (rs2) {
                    case 0x01: // fcvt.s.d
                        riscv_write_s(vm, rds, fpu_fcvt_f64_to_f32(riscv_view_d(vm, rs1)));
                        return;
                    case 0x04: // fround.s (Zfa)
                    case 0x05: // TODO: froundx.s (Zfa)
                        riscv_emit_s(vm, rds, fpu_fcvt_i64_to_f32(fpu_round_f32_to_i64(riscv_view_s(vm, rs1), rm)));
                        return;
                }
                break;
            case RISCV_FPU_GEN_RM_CASES(0x42000000UL):
                switch (rs2) {
                    case 0x00: // fcvt.d.s
                        riscv_write_d(vm, rds, fpu_fcvt_f32_to_f64(riscv_view_s(vm, rs1)));
                        return;
                    case 0x04: // fround.s (Zfa)
                    case 0x05: // TODO: froundx.s (Zfa)
                        riscv_emit_d(vm, rds, fpu_fcvt_i64_to_f64(fpu_round_f64_to_i64(riscv_view_d(vm, rs1), rm)));
                        return;
                }
                break;
            case RISCV_FPU_GEN_RM_CASES(0xC0000000UL):
                switch (rs2) {
                    case 0x00: // fcvt.w.s
                        riscv_write_reg(vm, rds, (int32_t)fpu_round_f32_to_i32(riscv_view_s(vm, rs1), rm));
                        return;
                    case 0x01: // fcvt.wu.s
                        riscv_write_reg(vm, rds, (int32_t)fpu_round_f32_to_u32(riscv_view_s(vm, rs1), rm));
                        return;
                    case 0x02: // fcvt.l.s
                        if (likely(vm->rv64)) {
                            riscv_write_reg(vm, rds, (int64_t)fpu_round_f32_to_i64(riscv_view_s(vm, rs1), rm));
                            return;
                        }
                        break;
                    case 0x03: // fcvt.lu.s
                        if (likely(vm->rv64)) {
                            riscv_write_reg(vm, rds, (int64_t)fpu_round_f32_to_u64(riscv_view_s(vm, rs1), rm));
                            return;
                        }
                        break;
                }
                break;
            case RISCV_FPU_GEN_RM_CASES(0xC2000000UL):
                switch (rs2) {
                    case 0x00: // fcvt.w.d
                        riscv_write_reg(vm, rds, (int32_t)fpu_round_f64_to_i32(riscv_view_d(vm, rs1), rm));
                        return;
                    case 0x01: // fcvt.wu.d
                        riscv_write_reg(vm, rds, (int32_t)fpu_round_f64_to_u32(riscv_view_d(vm, rs1), rm));
                        return;
                    case 0x02: // fcvt.l.d
                        if (likely(vm->rv64)) {
                            riscv_write_reg(vm, rds, (int64_t)fpu_round_f64_to_i64(riscv_view_d(vm, rs1), rm));
                            return;
                        }
                        break;
                    case 0x03: // fcvt.lu.d
                        if (likely(vm->rv64)) {
                            riscv_write_reg(vm, rds, (int64_t)fpu_round_f64_to_u64(riscv_view_d(vm, rs1), rm));
                            return;
                        }
                        break;
                    case 0x08: // fcvtmod.w.d (Zfa)
                        if (likely(rm == 0x01)) {
                            riscv_write_reg(vm, rds, (int32_t)fpu_fcvt_f64_to_i32(riscv_view_d(vm, rs1)));
                        }
                        break;
                }
                break;
            case RISCV_FPU_GEN_RM_CASES(0xD0000000UL):
                switch (rs2) {
                    case 0x00: // fcvt.s.w
                        riscv_emit_s(vm, rds, fpu_fcvt_i32_to_f32(riscv_read_reg(vm, rs1)));
                        return;
                    case 0x01: // fcvt.s.wu
                        riscv_emit_s(vm, rds, fpu_fcvt_u32_to_f32(riscv_read_reg(vm, rs1)));
                        return;
                    case 0x02: // fcvt.s.l
                        if (likely(vm->rv64)) {
                            riscv_emit_s(vm, rds, fpu_fcvt_i64_to_f32(riscv_read_reg(vm, rs1)));
                            return;
                        }
                        break;
                    case 0x03: // fcvt.s.lu
                        if (likely(vm->rv64)) {
                            riscv_emit_s(vm, rds, fpu_fcvt_u64_to_f32(riscv_read_reg(vm, rs1)));
                            return;
                        }
                        break;
                }
                break;
            case RISCV_FPU_GEN_RM_CASES(0xD2000000UL):
                switch (rs2) {
                    case 0x00: // fcvt.d.w
                        riscv_emit_d(vm, rds, fpu_fcvt_i32_to_f64(riscv_read_reg(vm, rs1)));
                        return;
                    case 0x01: // fcvt.d.wu
                        riscv_emit_d(vm, rds, fpu_fcvt_u32_to_f64(riscv_read_reg(vm, rs1)));
                        return;
                    case 0x02: // fcvt.d.l
                        if (likely(vm->rv64)) {
                            riscv_emit_d(vm, rds, fpu_fcvt_i64_to_f64(riscv_read_reg(vm, rs1)));
                            return;
                        }
                        break;
                    case 0x03: // fcvt.d.lu
                        if (likely(vm->rv64)) {
                            riscv_emit_d(vm, rds, fpu_fcvt_u64_to_f64(riscv_read_reg(vm, rs1)));
                            return;
                        }
                        break;
                }
                break;
            /*
             * FPU sign manipulations
             */
            case 0x20000000UL: // fsgnj.s
                riscv_emit_s(vm, rds, fpu_fsgnj32(riscv_read_s(vm, rs1), riscv_read_s(vm, rs2)));
                return;
            case 0x20001000UL: // fsgnjn.s
                riscv_emit_s(vm, rds, fpu_fsgnjn32(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case 0x20002000UL: // fsgnjx.s
                riscv_emit_s(vm, rds, fpu_fsgnjx32(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case 0x22000000UL: // fsgnj.d
                riscv_emit_d(vm, rds, fpu_fsgnj64(riscv_read_d(vm, rs1), riscv_read_d(vm, rs2)));
                return;
            case 0x22001000UL: // fsgnjn.d
                riscv_emit_d(vm, rds, fpu_fsgnjn64(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            case 0x22002000UL: // fsgnjx.d
                riscv_emit_d(vm, rds, fpu_fsgnjx64(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            /*
             * FPU comparisons
             */
            case 0x28000000UL: // fmin.s
                riscv_emit_s(vm, rds, fpu_min32(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case 0x28001000UL: // fmax.s
                riscv_emit_s(vm, rds, fpu_max32(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case 0x28002000UL: // fminm.s (Zfa)
                riscv_write_s(vm, rds, fpu_min32(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case 0x28003000UL: // fmaxm.s (Zfa)
                riscv_write_s(vm, rds, fpu_max32(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case 0x2A000000UL: // fmin.d
                riscv_emit_d(vm, rds, fpu_min64(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            case 0x2A001000UL: // fmax.d
                riscv_emit_d(vm, rds, fpu_max64(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            case 0x2A002000UL: // fminm.d (Zfa)
                riscv_write_d(vm, rds, fpu_min64(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            case 0x2A003000UL: // fmaxm.d (Zfa)
                riscv_write_d(vm, rds, fpu_max64(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            case 0xA0000000UL: // fle.s
                riscv_write_reg(vm, rds, fpu_is_fle32_sig(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case 0xA0001000UL: // flt.s
                riscv_write_reg(vm, rds, fpu_is_flt32_sig(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case 0xA0002000UL: // feq.s
                riscv_write_reg(vm, rds, fpu_is_equal32_quiet(riscv_read_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case 0xA0004000UL: // fleq.s (Zfa)
                riscv_write_reg(vm, rds, fpu_is_fle32_quiet(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case 0xA0005000UL: // fltq.s (Zfa)
                riscv_write_reg(vm, rds, fpu_is_flt32_quiet(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case 0xA2000000UL: // fle.d
                riscv_write_reg(vm, rds, fpu_is_fle64_sig(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            case 0xA2001000UL: // flt.d
                riscv_write_reg(vm, rds, fpu_is_flt64_sig(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            case 0xA2002000UL: // feq.d
                riscv_write_reg(vm, rds, fpu_is_equal64_quiet(riscv_read_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            case 0xA2004000UL: // fleq.d (Zfa)
                riscv_write_reg(vm, rds, fpu_is_fle64_quiet(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            case 0xA2005000UL: // fltq.d (Zfa)
                riscv_write_reg(vm, rds, fpu_is_flt64_quiet(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            /*
             * FPU bitcasts
             */
            case 0xE0000000UL: // fmv.x.w
                if (likely(!rs2)) {
                    riscv_write_reg(vm, rds, (int32_t)fpu_bit_f32_to_u32(riscv_view_s(vm, rs1)));
                    return;
                }
                break;
            case 0xE0001000UL: // fclass.s
                if (likely(!rs2)) {
                    riscv_write_reg(vm, rds, 1U << fpu_fclass32(riscv_view_s(vm, rs1)));
                    return;
                }
                break;
            case 0xE2000000UL:
                if (likely(vm->rv64 && !rs2)) { // fmv.x.d (RV64)
                    riscv_write_reg(vm, rds, (int64_t)fpu_bit_f64_to_u64(riscv_view_d(vm, rs1)));
                    return;
                } else if (rs2 == 1) { // fmvh.x.d (Zfa, RV32)
                    riscv_write_reg(vm, rds, (int32_t)(fpu_bit_f64_to_u64(riscv_view_d(vm, rs1)) >> 32));
                    return;
                }
                break;
            case 0xE2001000UL:
                if (likely(!rs2)) { // fclass.d
                    riscv_write_reg(vm, rds, 1U << fpu_fclass64(riscv_view_d(vm, rs1)));
                    return;
                }
                break;
            case 0xF0000000UL:
                switch (rs2) {
                    case 0x00: // fmv.w.x
                        riscv_emit_s(vm, rds, fpu_bit_u32_to_f32(riscv_read_reg(vm, rs1)));
                        return;
                    case 0x01: // fli.s (Zfa)
                        riscv_emit_s(vm, rds, fpu_bit_u32_to_f32(riscv_fli_table[rs1]));
                        return;
                }
                break;
            case 0xF2000000UL:
                if (likely(vm->rv64)) {
                    switch (rs2) {
                        case 0x00: // fmv.d.x
                            riscv_emit_d(vm, rds, fpu_bit_u64_to_f64(riscv_read_reg(vm, rs1)));
                            return;
                        case 0x01: // fli.d (Zfa)
                            if (rs1 == 1) {
                                // Minimum normal exponent differs for fp64
                                riscv_emit_d(vm, rds, fpu_bit_u64_to_f64(0x0010000000000000ULL));
                            } else {
                                riscv_emit_d(vm, rds, fpu_fcvt_f32_to_f64(fpu_bit_u32_to_f32(riscv_fli_table[rs1])));
                            }
                            return;
                    }
                }
                break;
        }
    }

    riscv_illegal_insn(vm, insn);
}

/*
 * RISC-V selects the rounding mode either dynamically (the frm CSR, when the
 * instruction's rm field is DYN) or statically (the rm field itself). The host
 * FPU tracks frm, so a static rm field that differs from frm must be applied
 * around the op. RMM has no host mode at all and is synthesized in RNE by the
 * fixups above (riscv_rmm_apply / riscv_rmm_div_apply), so its sub-ops must run
 * in RNE. funct3 == rm only carries a rounding mode on rounding-capable ops, so
 * this never misfires on fsgnj/fcmp/fclass/fmv.
 */
slow_path void riscv_emulate_f_opc_op(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t rm  = bit_ext_u32(insn, 12, 3);
    const uint32_t frm = vm->csr.fcsr >> 5;
    // Effective rounding mode: a static rm field overrides the dynamic frm CSR.
    const uint32_t eff = (rm == 0x07) ? frm : rm;
    const bool     rmm = (eff == 0x04);
    // Override the host mode when synthesizing RMM (run sub-ops in RNE) or when a
    // static rm field selects a host-native mode other than the one frm left set.
    if (unlikely(rmm || (rm != 0x07 && eff != frm))) {
        const uint32_t host = fpu_get_rounding_mode();
        fpu_set_rounding_mode(rmm ? FPU_LIB_ROUND_NE : eff);
        riscv_emulate_f_opc_op_impl(vm, insn, rmm);
        fpu_set_rounding_mode(host);
    } else {
        riscv_emulate_f_opc_op_impl(vm, insn, false);
    }
}

#endif
