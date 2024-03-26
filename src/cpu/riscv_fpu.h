/*
riscv_fpu.h - RISC-V Floating-Point ISA interpreter template
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.

Alternatively, the contents of this file may be used under the terms
of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or any later version.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef RISCV_FPU_H
#define RISCV_FPU_H

#include "fpu_lib.h"

#define RISCV_FADD_S   0x0
#define RISCV_FSUB_S   0x4
#define RISCV_FMUL_S   0x8
#define RISCV_FDIV_S   0xC
#define RISCV_FSQRT_S  0x2C // rs2 field is zero
#define RISCV_FSGNJ_S  0x10 // rm field encodes funct3
#define RISCV_FCLAMP_S 0x14 // rm field encodes funct3, fmin/fmax
#define RISCV_FCVT_W_S 0x60 // rs2 field encodes conversion type
#define RISCV_FMVCLS_S 0x70 // rs2 field is zero, rm encodes fmv.x.w or fclass
#define RISCV_FCMP_S   0x50 // rm field encodes funct3
#define RISCV_FCVT_S_W 0x68 // rs2 field encodes conversion type
#define RISCV_FMV_W_X  0x78 // rs2, rm fields are zero

#define RISCV_FADD_D   0x1
#define RISCV_FSUB_D   0x5
#define RISCV_FMUL_D   0x9
#define RISCV_FDIV_D   0xD
#define RISCV_FSQRT_D  0x2D // rs2 field is zero
#define RISCV_FSGNJ_D  0x11 // rm field encodes funct3
#define RISCV_FCLAMP_D 0x15 // rm field encodes funct3, fmin/fmax
#define RISCV_FCVT_S_D 0x20 // rs2 is 1
#define RISCV_FCVT_D_S 0x21 // rs2 is 0
#define RISCV_FCVT_W_D 0x61 // rs2 field encodes conversion type
#define RISCV_FMVCLS_D 0x71 // rs2 field is zero, rm encodes fmv.x.w or fclass
#define RISCV_FCMP_D   0x51 // rm field encodes funct3
#define RISCV_FCVT_D_W 0x69 // rs2 field encodes conversion type
#define RISCV_FMV_D_X  0x79 // rs2, rm fields are zero

// FPU fclass instruction results
#define FCL_NEG_INF       0x0
#define FCL_NEG_NORMAL    0x1
#define FCL_NEG_SUBNORMAL 0x2
#define FCL_NEG_ZERO      0x3
#define FCL_POS_ZERO      0x4
#define FCL_POS_SUBNORMAL 0x5
#define FCL_POS_NORMAL    0x6
#define FCL_POS_INF       0x7
#define FCL_NAN_SIG       0x8
#define FCL_NAN_QUIET     0x9

// Bit-precise register reads
static forceinline float fpu_view_s(rvvm_hart_t* vm, regid_t reg)
{
    return read_float_nanbox(&vm->fpu_registers[reg]);
}

// Normalized register reads
static forceinline float fpu_read_s(rvvm_hart_t* vm, regid_t reg)
{
    return read_float_normalize(&vm->fpu_registers[reg]);
}

// For bit-precise float register writes
static forceinline void fpu_emit_s(rvvm_hart_t* vm, regid_t reg, float val)
{
    fpu_set_fs(vm, FS_DIRTY);
    write_float_nanbox(&vm->fpu_registers[reg], val);
}

// Canonizes the written result
static forceinline void fpu_write_s(rvvm_hart_t* vm, regid_t reg, float val)
{
    if (unlikely(fpu_isnan(val))) {
        uint32_t canonic_nan = 0x7fc00000;
        memcpy(&val, &canonic_nan, sizeof(val));
    }
    fpu_emit_s(vm, reg, val);
}

static forceinline double fpu_read_d(rvvm_hart_t* vm, regid_t reg)
{
    return vm->fpu_registers[reg];
}

static forceinline void fpu_emit_d(rvvm_hart_t* vm, regid_t reg, double val)
{
    fpu_set_fs(vm, FS_DIRTY);
    vm->fpu_registers[reg] = val;
}

static forceinline void fpu_write_d(rvvm_hart_t* vm, regid_t reg, double val)
{
    if (unlikely(fpu_isnan(val))) {
        uint64_t canonic_nan = 0x7ff8000000000000;
        memcpy(&val, &canonic_nan, sizeof(val));
    }
    fpu_emit_d(vm, reg, val);
}

// FPU operations lowering

static forceinline uint8_t fpu_fclassf(float x)
{
    switch (fpclassify(x)) {
        case FP_INFINITE:  return fpu_signbitf(x) ? FCL_NEG_INF : FCL_POS_INF;
        case FP_NORMAL:    return fpu_signbitf(x) ? FCL_NEG_NORMAL : FCL_POS_NORMAL;
        case FP_SUBNORMAL: return fpu_signbitf(x) ? FCL_NEG_SUBNORMAL : FCL_POS_SUBNORMAL;
        case FP_ZERO:      return fpu_signbitf(x) ? FCL_NEG_ZERO : FCL_POS_ZERO;
        default:           return fpu_is_snanf(x) ? FCL_NAN_SIG : FCL_NAN_QUIET;
    }
}

static forceinline uint8_t fpu_fclassd(double x)
{
    switch (fpclassify(x)) {
        case FP_INFINITE:  return fpu_signbitd(x) ? FCL_NEG_INF : FCL_POS_INF;
        case FP_NORMAL:    return fpu_signbitd(x) ? FCL_NEG_NORMAL : FCL_POS_NORMAL;
        case FP_SUBNORMAL: return fpu_signbitd(x) ? FCL_NEG_SUBNORMAL : FCL_POS_SUBNORMAL;
        case FP_ZERO:      return fpu_signbitd(x) ? FCL_NEG_ZERO : FCL_POS_ZERO;
        default:           return fpu_is_snand(x) ? FCL_NAN_SIG : FCL_NAN_QUIET;
    }
}

static forceinline float fpu_minf(float x, float y)
{
    if (unlikely(fpu_isnan(x))) {
        // If any inputs is sNaN, raise FE_INVALID
        if (fpu_is_snanf(x) || fpu_is_snanf(y)) {
            feraiseexcept(FE_INVALID);
        }
        return y;
    } else if (unlikely(fpu_isnan(y))) {
        if (fpu_is_snanf(x) || fpu_is_snanf(y)) {
            feraiseexcept(FE_INVALID);
        }
        return x;
    } else if (x < y) {
        return x;
    } else if (y < x) {
        return y;
    } else {
        // -0.0 is less than 0.0
        return fpu_signbitf(x) ? x : y;
    }
}

static forceinline float fpu_maxf(float x, float y)
{
    if (unlikely(fpu_isnan(x))) {
        if (fpu_is_snanf(x) || fpu_is_snanf(y)) {
            feraiseexcept(FE_INVALID);
        }
        return y;
    } else if (unlikely(fpu_isnan(y))) {
        if (fpu_is_snanf(x) || fpu_is_snanf(y)) {
            feraiseexcept(FE_INVALID);
        }
        return x;
    } else if (x > y) {
        return x;
    } else if (y > x) {
        return y;
    } else {
        return fpu_signbitf(x) ? y : x;
    }
}

static forceinline double fpu_mind(double x, double y)
{
    if (unlikely(fpu_isnan(x))) {
        // If any inputs is sNaN, raise FE_INVALID
        if (fpu_is_snand(x) || fpu_is_snand(y)) {
            feraiseexcept(FE_INVALID);
        }
        return y;
    } else if (unlikely(fpu_isnan(y))) {
        if (fpu_is_snand(x) || fpu_is_snand(y)) {
            feraiseexcept(FE_INVALID);
        }
        return x;
    } else if (x < y) {
        return x;
    } else if (y < x) {
        return y;
    } else {
        // -0.0 is less than 0.0
        return fpu_signbitd(x) ? x : y;
    }
}

static forceinline double fpu_maxd(double x, double y)
{
    if (unlikely(fpu_isnan(x))) {
        if (fpu_is_snand(x) || fpu_is_snand(y)) {
            feraiseexcept(FE_INVALID);
        }
        return y;
    } else if (unlikely(fpu_isnan(y))) {
        if (fpu_is_snand(x) || fpu_is_snand(y)) {
            feraiseexcept(FE_INVALID);
        }
        return x;
    } else if (x > y) {
        return x;
    } else if (y > x) {
        return y;
    } else {
        return fpu_signbitd(x) ? y : x;
    }
}

static forceinline float fpu_round_evenf(float val) {
    float even;
    float frac = modff(val, &even);
    if (frac < 0.5 && frac > -0.5) {
        return even;
    } else {
        return even + (even > 0.0 ? 1.0 : -1.0);
    }
}

static forceinline double fpu_round_evend(double val) {
    double even;
    double frac = modf(val, &even);
    if (frac < 0.5 && frac > -0.5) {
        return even;
    } else {
        return even + (even > 0.0 ? 1.0 : -1.0);
    }
}

/*
 * This probably should be done using softfp, since dynamic RM
 * could mess with libm internals
 */
static float fpu_round_to_rmf(float x, uint8_t rm)
{
    float ret;
    switch (rm) {
        case RM_RNE: ret = fpu_round_evend(x); break;
        case RM_RTZ: ret = truncf(x);      break;
        case RM_RDN: ret = floorf(x);      break;
        case RM_RUP: ret = ceilf(x);       break;
        case RM_RMM: ret = roundf(x);      break;
        default:     ret = rintf(x);       break;
    }
    /*
     * Some libm implementations omit implementing FE_INEXACT flag
     * We check if we need to fix this at all first, since writing an exception
     * stalls the host pipeline, and is generally expensive.
     *
     * Another option could be "exception overlays" in hart context,
     * combined with host exceptions in fcsr.
     */
    if (unlikely(ret != x && !fetestexcept(FE_INEXACT))) feraiseexcept(FE_INEXACT);
    return ret;
}

static double fpu_round_to_rmd(double x, uint8_t rm)
{
    double ret;
    switch (rm) {
        case RM_RNE: ret = fpu_round_evend(x); break;
        case RM_RTZ: ret = trunc(x);      break;
        case RM_RDN: ret = floor(x);      break;
        case RM_RUP: ret = ceil(x);       break;
        case RM_RMM: ret = round(x);      break;
        default:     ret = rint(x);       break;
    }
    if (unlikely(ret != x && !fetestexcept(FE_INEXACT))) feraiseexcept(FE_INEXACT);
    return ret;
}

static forceinline int32_t fpu_f2int_u32(float x, float rm)
{
    if (likely(rm == RM_RTZ && x > 0.5f && x < 4294967296.5f)) return (uint32_t)x;
    float ret = fpu_round_to_rmf(x, rm);
    if (unlikely(fpu_isnan(ret) || ret < 0.f || ret >= 4294967296.f)) {
        feraiseexcept(FE_INVALID);
        if (fpu_isnan(x) || !fpu_signbitf(x)) return ~0;
        return 0;
    }
    return (uint32_t)ret;
}

static forceinline int32_t fpu_d2int_u32(double x, float rm)
{
    if (likely(rm == RM_RTZ && x > 0.5 && x < 4294967296.5)) return (uint32_t)x;
    double ret = fpu_round_to_rmd(x, rm);
    if (unlikely(fpu_isnan(ret) || ret < 0.0 || ret >= 4294967296.0)) {
        feraiseexcept(FE_INVALID);
        if (fpu_isnan(x) || !fpu_signbitf(x)) return ~0;
        return 0;
    }
    return (uint32_t)ret;
}

static forceinline int32_t fpu_f2int_i32(float x, uint8_t rm)
{
    if (likely(rm == RM_RTZ && x > -2147483648.5f && x < 2147483648.5f)) return (int32_t)x;
    float ret = fpu_round_to_rmf(x, rm);
    if (unlikely(fpu_isnan(ret) || (ret < -2147483648.f) || (ret >= 2147483648.f))) {
        feraiseexcept(FE_INVALID);
        if (fpu_isnan(x) || !fpu_signbitf(x)) return ~(uint32_t)0 >> 1;
        return ~(~(uint32_t)0 >> 1);
    }
    return (int32_t)ret;
}

static forceinline int32_t fpu_d2int_i32(double x, uint8_t rm)
{
    if (likely(rm == RM_RTZ && x > -2147483648.5 && x < 2147483648.5)) return (int32_t)x;
    double ret = fpu_round_to_rmd(x, rm);
    if (unlikely(fpu_isnan(ret) || (ret < -2147483648.0) || (ret >= 2147483648.0))) {
        feraiseexcept(FE_INVALID);
        if (fpu_isnan(x) || !fpu_signbitd(x)) return ~(uint32_t)0 >> 1;
        return ~(~(uint32_t)0 >> 1);
    }
    return (int32_t)ret;
}

static forceinline int64_t fpu_f2int_u64(float x, uint8_t rm)
{
    if (likely(rm == RM_RTZ && x > -0.5f && x < 18446744073709551616.5f)) return (uint64_t)x;
    float ret = fpu_round_to_rmf(x, rm);
    if (unlikely(fpu_isnan(ret) || ret < 0.f || ret >= 18446744073709551616.f)) {
        feraiseexcept(FE_INVALID);
        if (fpu_isnan(x) || !fpu_signbitf(x)) return ~(uint64_t)0;
        return 0;
    }
    return (uint64_t)ret;
}

static forceinline int64_t fpu_d2int_u64(double x, uint8_t rm)
{
    if (likely(rm == RM_RTZ && x > -0.5 && x < 18446744073709551616.5)) return (uint64_t)x;
    double ret = fpu_round_to_rmd(x, rm);
    if (unlikely(fpu_isnan(ret) || ret < 0.0 || ret >= 18446744073709551616.0)) {
        feraiseexcept(FE_INVALID);
        if (fpu_isnan(x) || !fpu_signbitd(x)) return ~(uint64_t)0;
        return 0;
    }
    return (uint64_t)ret;
}

static forceinline int64_t fpu_f2int_i64(float x, uint8_t rm)
{
    if (likely(rm == RM_RTZ && x > -9223372036854775808.5f && x < 9223372036854775808.5f)) return (int64_t)x;
    float ret = fpu_round_to_rmf(x, rm);
    if (unlikely(fpu_isnan(ret) || (ret < -9223372036854775808.f) || (ret >= 9223372036854775808.f))) {
        feraiseexcept(FE_INVALID);
        if (fpu_isnan(x) || !fpu_signbitf(x)) return ~(uint64_t)0 >> 1;
        return ~(~(uint64_t)0 >> 1);
    }
    return (int64_t)ret;
}

static forceinline int64_t fpu_d2int_i64(double x, uint8_t rm)
{
    if (likely(rm == RM_RTZ && x > -9223372036854775808.5 && x < 9223372036854775808.5)) return (int64_t)x;
    double ret = fpu_round_to_rmd(x, rm);
    if (unlikely(fpu_isnan(ret) || (ret < -9223372036854775808.0) || (ret >= 9223372036854775808.0))) {
        feraiseexcept(FE_INVALID);
        if (fpu_isnan(x) || !fpu_signbitd(x)) return ~(uint64_t)0 >> 1;
        return ~(~(uint64_t)0 >> 1);
    }
    return (int64_t)ret;
}

static forceinline int32_t fpu_bitcast_fp2int_32(float f)
{
    int32_t i;
    memcpy(&i, &f, sizeof(i));
    return i;
}

static forceinline float fpu_bitcast_int2fp_32(uint32_t i)
{
    float f;
    memcpy(&f, &i, sizeof(f));
    return f;
}

static forceinline int64_t fpu_bitcast_fp2int_64(double f)
{
    int64_t i;
    memcpy(&i, &f, sizeof(i));
    return i;
}

static forceinline double fpu_bitcast_int2fp_64(uint64_t i)
{
    double f;
    memcpy(&f, &i, sizeof(f));
    return f;
}

static forceinline void riscv_emulate_f_opc_load(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t funct3 = bit_cut(insn, 12, 3);
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const sxlen_t offset = sign_extend(bit_cut(insn, 20, 12), 12);
    const xlen_t  addr = riscv_read_reg(vm, rs1) + offset;
    if (likely(fpu_is_enabled(vm))) switch (funct3) {
        case 0x2: // flw
            riscv_load_float(vm, addr, rds);
            return;
        case 0x3: // fld
            riscv_load_double(vm, addr, rds);
            return;
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_f_opc_store(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t funct3 = bit_cut(insn, 12, 3);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const regid_t rs2 = bit_cut(insn, 20, 5);
    const sxlen_t offset = sign_extend(bit_cut(insn, 7, 5) | (bit_cut(insn, 25, 7) << 5), 12);
    const xlen_t addr = riscv_read_reg(vm, rs1) + offset;
    if (likely(fpu_is_enabled(vm))) switch (funct3) {
        case 0x2: // fsw
            riscv_store_float(vm, addr, rs2);
            return;
        case 0x3: // fsd
            riscv_store_double(vm, addr, rs2);
            return;
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_f_fmadd(rvvm_hart_t* vm, const uint32_t insn)
{
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const regid_t rs2 = bit_cut(insn, 20, 5);
    const uint32_t funct2 = bit_cut(insn, 25, 2);
    const regid_t rs3 = insn >> 27;
    if (likely(fpu_is_enabled(vm))) switch (funct2) {
        case 0x0: // fmadd.s
            fpu_write_s(vm, rds, fpu_fmaf(fpu_read_s(vm, rs1), fpu_read_s(vm, rs2), fpu_read_s(vm, rs3)));
            return;
        case 0x1: // fmadd.d
            fpu_write_d(vm, rds, fpu_fmad(fpu_read_d(vm, rs1), fpu_read_d(vm, rs2), fpu_read_d(vm, rs3)));
            return;
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_f_fmsub(rvvm_hart_t* vm, const uint32_t insn)
{
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const regid_t rs2 = bit_cut(insn, 20, 5);
    const uint32_t funct2 = bit_cut(insn, 25, 2);
    const regid_t rs3 = insn >> 27;
    if (likely(fpu_is_enabled(vm))) switch (funct2) {
        case 0x0: // fmsub.s
            fpu_write_s(vm, rds, fpu_fmaf(fpu_read_s(vm, rs1), fpu_read_s(vm, rs2), -fpu_read_s(vm, rs3)));
            return;
        case 0x1: // fmsub.d
            fpu_write_d(vm, rds, fpu_fmad(fpu_read_d(vm, rs1), fpu_read_d(vm, rs2), -fpu_read_d(vm, rs3)));
            return;
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_f_fnmsub(rvvm_hart_t* vm, const uint32_t insn)
{
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const regid_t rs2 = bit_cut(insn, 20, 5);
    const uint32_t funct2 = bit_cut(insn, 25, 2);
    const regid_t rs3 = insn >> 27;
    if (likely(fpu_is_enabled(vm))) switch (funct2) {
        case 0x0: // fnmsub.s
            fpu_write_s(vm, rds, -fpu_fmaf(fpu_read_s(vm, rs1), fpu_read_s(vm, rs2), -fpu_read_s(vm, rs3)));
            return;
        case 0x1: // fnmsub.d
            fpu_write_d(vm, rds, -fpu_fmad(fpu_read_d(vm, rs1), fpu_read_d(vm, rs2), -fpu_read_d(vm, rs3)));
            return;
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_f_fnmadd(rvvm_hart_t* vm, const uint32_t insn)
{
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const regid_t rs2 = bit_cut(insn, 20, 5);
    const uint32_t funct2 = bit_cut(insn, 25, 2);
    const regid_t rs3 = insn >> 27;
    if (likely(fpu_is_enabled(vm))) switch (funct2) {
        case 0x0: // fnmadd.s
            fpu_write_s(vm, rds, -fpu_fmaf(fpu_read_s(vm, rs1), fpu_read_s(vm, rs2), fpu_read_s(vm, rs3)));
            return;
        case 0x1: // fnmadd.d
            fpu_write_d(vm, rds, -fpu_fmad(fpu_read_d(vm, rs1), fpu_read_d(vm, rs2), fpu_read_d(vm, rs3)));
            return;
    }
    riscv_illegal_insn(vm, insn);
}

static forceinline void riscv_emulate_f_opc_op(rvvm_hart_t* vm, const uint32_t insn)
{
    const regid_t rds = bit_cut(insn, 7, 5);
    const uint8_t rm  = bit_cut(insn, 12, 3);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const regid_t rs2 = bit_cut(insn, 20, 5);
    const uint32_t funct7 = insn >> 25;
    if (likely(fpu_is_enabled(vm))) switch (funct7) {
        case RISCV_FADD_S:
            fpu_write_s(vm, rds, fpu_read_s(vm, rs1) + fpu_read_s(vm, rs2));
            return;
        case RISCV_FADD_D:
            fpu_write_d(vm, rds, fpu_read_d(vm, rs1) + fpu_read_d(vm, rs2));
            return;
        case RISCV_FSUB_S:
            fpu_write_s(vm, rds, fpu_read_s(vm, rs1) - fpu_read_s(vm, rs2));
            return;
        case RISCV_FSUB_D:
            fpu_write_d(vm, rds, fpu_read_d(vm, rs1) - fpu_read_d(vm, rs2));
            return;
        case RISCV_FMUL_S:
            fpu_write_s(vm, rds, fpu_read_s(vm, rs1) * fpu_read_s(vm, rs2));
            return;
        case RISCV_FMUL_D:
            fpu_write_d(vm, rds, fpu_read_d(vm, rs1) * fpu_read_d(vm, rs2));
            return;
        case RISCV_FDIV_S:
            fpu_write_s(vm, rds, fpu_read_s(vm, rs1) / fpu_read_s(vm, rs2));
            return;
        case RISCV_FDIV_D:
            fpu_write_d(vm, rds, fpu_read_d(vm, rs1) / fpu_read_d(vm, rs2));
            return;
        case RISCV_FSQRT_S:
            if (likely(rs2 == 0)) {
                fpu_write_s(vm, rds, fpu_sqrtf(fpu_read_s(vm, rs1)));
                return;
            }
            break;
        case RISCV_FSQRT_D:
            if (likely(rs2 == 0)) {
                fpu_write_d(vm, rds, fpu_sqrtd(fpu_read_d(vm, rs1)));
                return;
            }
            break;
        case RISCV_FSGNJ_S:
            switch (rm) {
                case 0x0: // fsgnj.s
                    fpu_emit_s(vm, rds, fpu_copysignf(fpu_read_s(vm, rs1), fpu_read_s(vm, rs2)));
                    return;
                case 0x1: // fsgnjn.s
                    fpu_emit_s(vm, rds, fpu_copysignf(fpu_read_s(vm, rs1), -fpu_read_s(vm, rs2)));
                    return;
                case 0x2: // fsgnjx.s
                    fpu_emit_s(vm, rds, fpu_copysignxf(fpu_read_s(vm, rs1), fpu_read_s(vm, rs2)));
                    return;
            }
            break;
        case RISCV_FSGNJ_D:
            switch (rm) {
                case 0x0: // fsgnj.d
                    fpu_emit_d(vm, rds, fpu_copysignd(fpu_read_d(vm, rs1), fpu_read_d(vm, rs2)));
                    return;
                case 0x1: // fsgnjn.d
                    fpu_emit_d(vm, rds, fpu_copysignd(fpu_read_d(vm, rs1), -fpu_read_d(vm, rs2)));
                    return;
                case 0x2: // fsgnjx.d
                    fpu_emit_d(vm, rds, fpu_copysignxd(fpu_read_d(vm, rs1), fpu_read_d(vm, rs2)));
                    return;
            }
            break;
        case RISCV_FCLAMP_S:
            switch (rm) {
                case 0x0: // fmin.s
                    fpu_write_s(vm, rds, fpu_minf(fpu_read_s(vm, rs1), fpu_read_s(vm, rs2)));
                    return;
                case 0x1: // fmax.s
                    fpu_write_s(vm, rds, fpu_maxf(fpu_read_s(vm, rs1), fpu_read_s(vm, rs2)));
                    return;
            }
            break;
        case RISCV_FCLAMP_D:
            switch (rm) {
                case 0x0: // fmin.d
                    fpu_write_d(vm, rds, fpu_mind(fpu_read_d(vm, rs1), fpu_read_d(vm, rs2)));
                    return;
                case 0x1: // fmax.d
                    fpu_write_d(vm, rds, fpu_maxd(fpu_read_d(vm, rs1), fpu_read_d(vm, rs2)));
                    return;
            }
            break;
        case RISCV_FCVT_S_D:
            if (likely(rs2 == 1)) {
                fpu_write_s(vm, rds, fpu_read_d(vm, rs1));
                return;
            }
            break;
        case RISCV_FCVT_D_S:
            if (likely(rs2 == 0)) {
                fpu_write_d(vm, rds, fpu_read_s(vm, rs1));
                return;
            }
            break;
        case RISCV_FCVT_W_S:
            switch (rs2) {
                case 0x0: // fcvt.w.s
                    riscv_write_reg(vm, rds, fpu_f2int_i32(fpu_read_s(vm, rs1), rm));
                    return;
                case 0x1: // fcvt.wu.s
                    riscv_write_reg(vm, rds, fpu_f2int_u32(fpu_read_s(vm, rs1), rm));
                    return;
#ifdef RV64
                case 0x2: // fcvt.l.s
                    riscv_write_reg(vm, rds, fpu_f2int_i64(fpu_read_s(vm, rs1), rm));
                    return;
                case 0x3: // fcvt.lu.s
                    riscv_write_reg(vm, rds, fpu_f2int_u64(fpu_read_s(vm, rs1), rm));
                    return;
#endif
            }
            break;
        case RISCV_FCVT_W_D:
            switch (rs2) {
                case 0x0: // fcvt.w.d
                    riscv_write_reg(vm, rds, fpu_d2int_i32(fpu_read_d(vm, rs1), rm));
                    return;
                case 0x1: // fcvt.wu.d
                    riscv_write_reg(vm, rds, fpu_d2int_u32(fpu_read_d(vm, rs1), rm));
                    return;
#ifdef RV64
                case 0x2: // fcvt.l.d
                    riscv_write_reg(vm, rds, fpu_d2int_i64(fpu_read_d(vm, rs1), rm));
                    return;
                case 0x3: // fcvt.lu.d
                    riscv_write_reg(vm, rds, fpu_d2int_u64(fpu_read_d(vm, rs1), rm));
                    return;
#endif
            }
            break;
        case RISCV_FMVCLS_S:
            if (likely(rs2 == 0)) {
                switch (rm) {
                    case 0x0: // fmv.x.w
                        riscv_write_reg(vm, rds, fpu_bitcast_fp2int_32(fpu_view_s(vm, rs1)));
                        return;
                    case 0x1: // fclass.s
                        riscv_write_reg(vm, rds, 1U << fpu_fclassf(fpu_view_s(vm, rs1)));
                        return;
                }
            }
            break;
        case RISCV_FMVCLS_D:
            if (likely(rs2 == 0)) {
                switch (rm) {
#ifdef RV64
                    case 0x0: // fmv.x.d
                        riscv_write_reg(vm, rds, fpu_bitcast_fp2int_64(fpu_read_d(vm, rs1)));
                        return;
#endif
                    case 0x1: // fclass.d
                        riscv_write_reg(vm, rds, 1U << fpu_fclassd(fpu_read_d(vm, rs1)));
                        return;
                }
            }
            break;
        case RISCV_FCMP_S:
            switch (rm) {
                case 0x0: // fle.s
                    riscv_write_reg(vm, rds, fpu_read_s(vm, rs1) <= fpu_read_s(vm, rs2));
                    return;
                case 0x1: // flt.s
                    riscv_write_reg(vm, rds, fpu_read_s(vm, rs1) < fpu_read_s(vm, rs2));
                    return;
                case 0x2: // feq.s
                    riscv_write_reg(vm, rds, fpu_read_s(vm, rs1) == fpu_read_s(vm, rs2));
                    return;
            }
            break;
        case RISCV_FCMP_D:
            switch (rm) {
                case 0x0: // fle.d
                    riscv_write_reg(vm, rds, fpu_read_d(vm, rs1) <= fpu_read_d(vm, rs2));
                    return;
                case 0x1: // flt.d
                    riscv_write_reg(vm, rds, fpu_read_d(vm, rs1) < fpu_read_d(vm, rs2));
                    return;
                case 0x2: // feq.d
                    riscv_write_reg(vm, rds, fpu_read_d(vm, rs1) == fpu_read_d(vm, rs2));
                    return;
            }
            break;
        case RISCV_FCVT_S_W:
            switch (rs2) {
                case 0x0: // fcvt.s.w
                    fpu_write_s(vm, rds, (float)(int32_t)riscv_read_reg(vm, rs1));
                    return;
                case 0x1: // fcvt.s.wu
                    fpu_write_s(vm, rds, (float)(uint32_t)riscv_read_reg(vm, rs1));
                    return;
#ifdef RV64
                case 0x2: // fcvt.s.l
                    fpu_write_s(vm, rds, (float)(int64_t)riscv_read_reg(vm, rs1));
                    return;
                case 0x3: // fcvt.s.lu
                    fpu_write_s(vm, rds, (float)(uint64_t)riscv_read_reg(vm, rs1));
                    return;
#endif
            }
            break;
        case RISCV_FCVT_D_W:
            switch (rs2) {
                case 0x0: // fcvt.d.w
                    fpu_write_d(vm, rds, (double)(int32_t)riscv_read_reg(vm, rs1));
                    return;
                case 0x1: // fcvt.d.wu
                    fpu_write_d(vm, rds, (double)(uint32_t)riscv_read_reg(vm, rs1));
                    return;
#ifdef RV64
                case 0x2: // fcvt.d.l
                    fpu_write_d(vm, rds, (double)(int64_t)riscv_read_reg(vm, rs1));
                    return;
                case 0x3: // fcvt.d.lu
                    fpu_write_d(vm, rds, (double)(uint64_t)riscv_read_reg(vm, rs1));
                    return;
#endif
            }
            break;
        case RISCV_FMV_W_X:
            if (likely(rs2 == 0 && rm == 0)) {
                fpu_emit_s(vm, rds, fpu_bitcast_int2fp_32(riscv_read_reg(vm, rs1)));
                return;
            }
            break;
#ifdef RV64
        case RISCV_FMV_D_X:
            if (likely(rs2 == 0 && rm == 0)) {
                fpu_emit_d(vm, rds, fpu_bitcast_int2fp_64(riscv_read_reg(vm, rs1)));
                return;
            }
            break;
#endif
    }
    riscv_illegal_insn(vm, insn);
}

#endif
