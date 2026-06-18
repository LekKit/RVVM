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

static void riscv_prepare_rmm(rvvm_hart_t* vm, const uint32_t insn, const size_t rs1, const size_t rs2)
{
    bool neg = false;

    // Decide the sign of the output
    switch (insn & 0xFE000000UL) {
        case 0x00000000UL: // fadd.s
            neg = fpu_signbit32(fpu_add32(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
            break;;
        case 0x02000000UL: // fadd.d
            neg = fpu_signbit64(fpu_add64(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
            break;
        case 0x08000000UL: // fsub.s
            neg = fpu_signbit32(fpu_sub32(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
            break;
        case 0x0A000000UL: // fsub.d
            neg = fpu_signbit64(fpu_sub64(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
            break;
        case 0x10000000UL: // fmul.s
        case 0x18000000UL: // fdiv.s
            neg = fpu_signbit32(riscv_view_s(vm, rs1)) != fpu_signbit32(riscv_view_s(vm, rs2));
            break;
        case 0x12000000UL: // fmul.d
        case 0x1A000000UL: // fdiv.d
            neg = fpu_signbit64(riscv_view_d(vm, rs1)) != fpu_signbit64(riscv_view_d(vm, rs2));
            break;
        default:
            neg = fpu_signbit64(riscv_view_d(vm, rs1));
            break;
    }

    // Round to positive/negative infinity based on the result sign
    if (neg) {
        fpu_set_rounding_mode(FPU_LIB_ROUND_DN);
    } else {
        fpu_set_rounding_mode(FPU_LIB_ROUND_UP);
    }
}

slow_path void riscv_emulate_f_opc_op(rvvm_hart_t* vm, const uint32_t insn)
{
    const size_t   rds = bit_ext_u32(insn, 7, 5);
    const uint32_t rm  = bit_ext_u32(insn, 12, 3);
    const size_t   rs1 = bit_ext_u32(insn, 15, 5);
    const size_t   rs2 = bit_ext_u32(insn, 20, 5);

    if (likely(riscv_fpu_is_enabled(vm))) {

        if (unlikely(vm->csr.fcsr >> 5 == 0x04)) {
            // Handle RMM rounding
            riscv_prepare_rmm(vm, insn, rs1, rs2);
        }

        switch (insn & 0xFE007000UL) {
            /*
             * FPU computations
             */
            case RISCV_FPU_GEN_RM_CASES(0x00000000UL): // fadd.s
                riscv_emit_s(vm, rds, fpu_add32(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case RISCV_FPU_GEN_RM_CASES(0x02000000UL): // fadd.d
                riscv_emit_d(vm, rds, fpu_add64(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            case RISCV_FPU_GEN_RM_CASES(0x08000000UL): // fsub.s
                riscv_write_s(vm, rds, fpu_sub32(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case RISCV_FPU_GEN_RM_CASES(0x0A000000UL): // fsub.d
                riscv_write_d(vm, rds, fpu_sub64(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            case RISCV_FPU_GEN_RM_CASES(0x10000000UL): // fmul.s
                riscv_emit_s(vm, rds, fpu_mul32(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case RISCV_FPU_GEN_RM_CASES(0x12000000UL): // fmul.d
                riscv_emit_d(vm, rds, fpu_mul64(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
            case RISCV_FPU_GEN_RM_CASES(0x18000000UL): // fdiv.s
                riscv_emit_s(vm, rds, fpu_div32(riscv_view_s(vm, rs1), riscv_view_s(vm, rs2)));
                return;
            case RISCV_FPU_GEN_RM_CASES(0x1A000000UL): // fdiv.d
                riscv_emit_d(vm, rds, fpu_div64(riscv_view_d(vm, rs1), riscv_view_d(vm, rs2)));
                return;
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

#endif
