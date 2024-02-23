/*
riscv_f.c - RISC-V F/D extension decoder and interpreter
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>
                    LekKit <github.com/LekKit>

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

/*
 * NOTE: This file is compiled twice.
 * RVD macro is defined for D extension, uses F otherwise
 */

#define RISCV_CPU_SOURCE

#include "riscv_cpu.h"

#ifdef USE_FPU
#include "compiler.h"
#include "bit_ops.h"
#include "riscv_mmu.h"
#include "riscv_csr.h"

#include "fpu_ops.h"
#include <float.h>
/* well... */
#if defined(_WIN32) && defined(__clang__)
#define _C_COMPLEX_T
typedef _Complex double _C_double_complex;
typedef _Complex float _C_float_complex;
typedef _Complex long double _C_ldouble_complex;
#endif
#include <math.h>

#ifdef RVD

#define SIGNIFICAND_SIZE 53 // Used for sNaN classification
#define SIGNBIT_OFF      63
typedef double   fnative_t;
typedef uint64_t fint_bits_t;
// Bit 26 of opcode instruction
#define BIT26 0

#define fpu_read_register fpu_read_register64
#define fpu_write_register fpu_write_register64
#define riscv_store_fnative riscv_store_double
#define riscv_load_fnative riscv_load_double

#define fpu_copysign copysign
#define fpu_fma fma
#define fpu_sqrt sqrt
#define fpu_modf modf
#define fpu_trunc trunc
#define fpu_floor floor
#define fpu_ceil ceil
#define fpu_round round
#define fpu_rint rint

#else
#define SIGNIFICAND_SIZE 24
#define SIGNBIT_OFF      31
typedef float    fnative_t;
typedef uint32_t fint_bits_t;
#define BIT26 0

#define fpu_read_register fpu_read_register32
#define fpu_write_register fpu_write_register32
#define riscv_store_fnative riscv_store_float
#define riscv_load_fnative riscv_load_float

#define fpu_copysign copysignf
#define fpu_fma fmaf
#define fpu_sqrt sqrtf
#define fpu_modf modff
#define fpu_trunc truncf
#define fpu_floor floorf
#define fpu_ceil ceilf
#define fpu_round roundf
#define fpu_rint rintf

#endif

#define fpu_isnan isnan

static inline fnative_t canonize_nan(fnative_t x)
{
    if (unlikely(fpu_isnan(x))) {
#ifndef RVD
        uint32_t canonic_nan = 0x7fc00000;
#else
        uint64_t canonic_nan = 0x7ff8000000000000;
#endif
        memcpy(&x, &canonic_nan, sizeof(fnative_t));
    }
    return x;
}

static inline bool fpu_signbit(fnative_t f)
{
    fint_bits_t i;
    memcpy(&i, &f, sizeof(f));
    return i >> SIGNBIT_OFF;
}

static inline fnative_t fpu_copysign_xor(fnative_t a, fnative_t b)
{
    fint_bits_t ia, ib;
    memcpy(&ia, &a, sizeof(a));
    memcpy(&ib, &b, sizeof(b));
    ib ^= ia;
    memcpy(&b, &ib, sizeof(b));

    return fpu_copysign(a, b);
}

// Resulting NaN is always canonical
#define fpu_add(a, b)       canonize_nan((fnative_t)(a) + (fnative_t)(b))
#define fpu_sub(a, b)       canonize_nan((fnative_t)(a) - (fnative_t)(b))
#define fpu_mul(a, b)       canonize_nan((fnative_t)(a) * (fnative_t)(b))
#define fpu_div(a, b)       canonize_nan((fnative_t)(a) / (fnative_t)(b))
#define fpu_fmadd(a, b, c)  canonize_nan(fpu_fma(a, b, c))
#define fpu_fmsub(a, b, c)  canonize_nan(fpu_fma(a, b, -(fnative_t)c))
#define fpu_fnmadd(a, b, c) canonize_nan(-fpu_fma(a, b, c))
#define fpu_fnmsub(a, b, c) canonize_nan(-fpu_fma(a, b, -(fnative_t)c))
#define fpu_int2fp(t, i)    canonize_nan((t)(i))
#define fpu_fp2fp(t, x)     canonize_nan((t)(x))

#define fpu_eq(a, b)        ((fnative_t)(a) == (fnative_t)(b))
#define fpu_lt(a, b)        ((fnative_t)(a) <  (fnative_t)(b))
#define fpu_le(a, b)        ((fnative_t)(a) <= (fnative_t)(b))

enum {
    FCL_NEG_INF,
    FCL_NEG_NORMAL,
    FCL_NEG_SUBNORMAL,
    FCL_NEG_ZERO,
    FCL_POS_ZERO,
    FCL_POS_SUBNORMAL,
    FCL_POS_NORMAL,
    FCL_POS_INF,
    FCL_NAN_SIG,
    FCL_NAN_QUIET
};

/* Bitwise fuckery to check for sNaN/qNaN because C API is so awesome */
static inline bool fpu_is_snan(fnative_t f)
{
    fint_bits_t i;
    memcpy(&i, &f, sizeof(f));
    return fpu_isnan(f) && !bit_check(i, SIGNIFICAND_SIZE - 2);
}

static inline uint8_t fpu_fclass(fnative_t x)
{
    switch (fpclassify(x)) {
        case FP_INFINITE:  return fpu_signbit(x) ? FCL_NEG_INF : FCL_POS_INF;
        case FP_NORMAL:    return fpu_signbit(x) ? FCL_NEG_NORMAL : FCL_POS_NORMAL;
        case FP_SUBNORMAL: return fpu_signbit(x) ? FCL_NEG_SUBNORMAL : FCL_POS_SUBNORMAL;
        case FP_ZERO:      return fpu_signbit(x) ? FCL_NEG_ZERO : FCL_POS_ZERO;
        case FP_NAN:       return fpu_is_snan(x) ? FCL_NAN_SIG : FCL_NAN_QUIET;
    }

    /* never reached */
    return FCL_NAN_SIG;
}

static inline fnative_t fpu_round_even(fnative_t val) {
    fnative_t even;
    fnative_t frac = fpu_modf(val, &even);
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
static inline fnative_t fpu_round_to_rm(fnative_t x, uint8_t rm)
{
    fnative_t ret;
    switch (rm) {
        case RM_RNE: ret = fpu_round_even(x); break;
        case RM_RTZ: ret = fpu_trunc(x);      break;
        case RM_RDN: ret = fpu_floor(x);      break;
        case RM_RUP: ret = fpu_ceil(x);       break;
        case RM_RMM: ret = fpu_round(x);      break;
        default:     ret = fpu_rint(x);       break;
    }

    /*
     * Some libm implementations omit implementing FE_INEXACT flag
     * We check if we need to fix this at all first, since writing an exception
     * stalls the host pipeline, and is generally expensive.
     *
     * Another option could be "exception overlays" in hart context,
     * combined with host exceptions in fcsr.
     */
    if (unlikely(ret != x && !fetestexcept(FE_INEXACT))) {
        feraiseexcept(FE_INEXACT);
    }

    return ret;
}

static inline int32_t fpu_fp2int_uint32_t(fnative_t x, uint8_t rm)
{
    fnative_t ret = fpu_round_to_rm(x, rm);
    bool isinvalid = fpu_isnan(ret) || ret < 0.0 || ret >= 4294967296.0;
    if (unlikely(isinvalid)) {
        feraiseexcept(FE_INVALID);
        if (fpu_isnan(x) || !fpu_signbit(x)) {
            return ~(uint32_t)0;
        } else {
            return 0;
        }
    }

    return (uint32_t)ret;
}

static inline int32_t fpu_fp2int_int32_t(fnative_t x, uint8_t rm)
{
    fnative_t ret = fpu_round_to_rm(x, rm);
    bool isinvalid = fpu_isnan(ret)
                  || (ret < -2147483648.0)
                  || (ret >= 2147483648.0);
    if (unlikely(isinvalid)) {
        feraiseexcept(FE_INVALID);
        if (fpu_isnan(x) || !fpu_signbit(x)) {
            return ~(uint32_t)0 >> 1;
        } else {
            return ~(~(uint32_t)0 >> 1);
        }
    }
    return (int32_t)ret;
}

#ifdef RV64
static inline int64_t fpu_fp2int_uint64_t(fnative_t x, uint8_t rm)
{
    fnative_t ret = fpu_round_to_rm(x, rm);
    bool isinvalid = fpu_isnan(ret) || ret < 0.0 || ret >= 18446744073709551616.0;
    if (unlikely(isinvalid)) {
        feraiseexcept(FE_INVALID);
        if (fpu_isnan(x) || !fpu_signbit(x)) {
            return ~(uint64_t)0;
        } else {
            return 0;
        }
    }

    return (uint64_t)ret;
}

static inline int64_t fpu_fp2int_int64_t(fnative_t x, uint8_t rm)
{
    fnative_t ret = fpu_round_to_rm(x, rm);
    bool isinvalid = fpu_isnan(ret)
                  || (ret < -9223372036854775808.0)
                  || (ret >= 9223372036854775808.0);
    if (unlikely(isinvalid)) {
        feraiseexcept(FE_INVALID);
        if (fpu_isnan(x) || !fpu_signbit(x)) {
            return ~(uint64_t)0 >> 1;
        } else {
            return ~(~(uint64_t)0 >> 1);
        }
    }

    return (int64_t)ret;
}
#endif

static inline fnative_t fpu_fsqrt(fnative_t val) {
    fnative_t ret = canonize_nan(fpu_sqrt(val));

    if (unlikely(val < 0 && !fetestexcept(FE_INVALID))) {
        feraiseexcept(FE_INVALID);
    }
    return ret;
}

static inline fnative_t fpu_min(fnative_t x, fnative_t y)
{
    if (unlikely(fpu_isnan(x))) {
        // If any inputs is sNaN, raise FE_INVALID
        if (fpu_is_snan(x) || fpu_is_snan(y)) {
            feraiseexcept(FE_INVALID);
        }
        return canonize_nan(y);
    } else if (unlikely(fpu_isnan(y))) {
        if (fpu_is_snan(x) || fpu_is_snan(y)) {
            feraiseexcept(FE_INVALID);
        }
        return canonize_nan(x);
    } else if (x < y) {
        return x;
    } else if (y < x) {
        return y;
    } else {
        // -0.0 is less than 0.0
        return fpu_signbit(x) ? x : y;
    }
}

static inline fnative_t fpu_max(fnative_t x, fnative_t y)
{
    if (unlikely(fpu_isnan(x))) {
        if (fpu_is_snan(x) || fpu_is_snan(y)) {
            feraiseexcept(FE_INVALID);
        }
        return canonize_nan(y);
    } else if (unlikely(fpu_isnan(y))) {
        if (fpu_is_snan(x) || fpu_is_snan(y)) {
            feraiseexcept(FE_INVALID);
        }
        return canonize_nan(x);
    } else if (x > y) {
        return x;
    } else if (y > x) {
        return y;
    } else {
        return fpu_signbit(x) ? y : x;
    }
}

/* type for rounding mode */
typedef uint8_t rm_t;

/* funct7 opcodes, shifted right by 2 bits */
#define FT7_FADD 0x0
#define FT7_FSUB 0x1
#define FT7_FMUL 0x2
#define FT7_FDIV 0x3
#define FT7_FSQRT 0xb /* rs2 == 0 */
#define FT7_FSGN 0x4 /* multiple variants, selected by funct3 */
#define FT7_FMINMAX 0x5 /* distinguished by funct3 */
#define FT7_FCMP 0x14 /* distinguished by funct3 */
#define FT7_FCVT_W_S 0x18 /* rs2 == 0 (signed) or 1 (unsigned) */
#define FT7_FCVT_S_W 0x1a /* rs2 == 0 (signed) or 1 (unsigned) */
#ifndef RVD
#define FT7_FMV_X_W 0x1c /* rs2 == 0, funct3 == 0 (fmv) or 1 (fclass) */
#define FT7_FMV_W_X 0x1e /* rs2 == 0, funct3 == 0 */

/* this is encoded as F instruction */
#define FT7_FCVT_S_D 0x8 /* rs2 == 1 */
#else
#define FT7_FCVT_D_S 0x8 /* rs2 == 0 */
#define FT7_FCLASS 0x1c /* rs2 == 0, funct3 == 0 (fmv) or 1 (fclass) */
#ifdef RV64
#define FT7_FMV_X_D 0x1c
#define FT7_FMV_D_X 0x1e
#endif
#endif

static void riscv_f_flw(rvvm_hart_t *vm, const uint32_t insn)
{
    regid_t rds = bit_cut(insn, 7, 5);
    regid_t rs1 = bit_cut(insn, 15, 5);
    sxlen_t offset = sign_extend(bit_cut(insn, 20, 12), 12);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;

    riscv_load_fnative(vm, addr, rds);
}

static void riscv_f_fsw(rvvm_hart_t *vm, const uint32_t insn)
{
    regid_t rs1 = bit_cut(insn, 15, 5);
    regid_t rs2 = bit_cut(insn, 20, 5);
    sxlen_t offset = sign_extend(bit_cut(insn, 7, 5) |
                               (bit_cut(insn, 25, 7) << 5), 12);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;

    riscv_store_fnative(vm, addr, rs2);
}

static void riscv_f_fmadd(rvvm_hart_t *vm, const uint32_t insn)
{
    if (unlikely(bit_check(insn, 26) != BIT26)) {
        riscv_illegal_insn(vm, insn);
        return;
    }

    regid_t rs1 = bit_cut(insn, 15, 5);
    regid_t rs2 = bit_cut(insn, 20, 5);
    rm_t rm = fpu_set_rm(vm, bit_cut(insn, 12, 3));
    if (unlikely(rm == RM_INVALID)) {
        riscv_illegal_insn(vm, insn);
        return;
    }
    regid_t rd = bit_cut(insn, 7, 5);
    regid_t rs3 = bit_cut(insn, 27, 5);

    fnative_t res = fpu_fmadd(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2), fpu_read_register(vm, rs3));
    fpu_write_register(vm, rd, res);
    fpu_set_rm(vm, rm);
}

static void riscv_f_fmsub(rvvm_hart_t *vm, const uint32_t insn)
{
    if (unlikely(bit_check(insn, 26) != BIT26)) {
        riscv_illegal_insn(vm, insn);
        return;
    }

    regid_t rs1 = bit_cut(insn, 15, 5);
    regid_t rs2 = bit_cut(insn, 20, 5);
    rm_t rm = fpu_set_rm(vm, bit_cut(insn, 12, 3));
    if (unlikely(rm == RM_INVALID)) {
        riscv_illegal_insn(vm, insn);
        return;
    }
    regid_t rd = bit_cut(insn, 7, 5);
    regid_t rs3 = bit_cut(insn, 27, 5);

    fnative_t res = fpu_fmsub(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2), fpu_read_register(vm, rs3));
    fpu_write_register(vm, rd, res);
    fpu_set_rm(vm, rm);
}

static void riscv_f_fnmadd(rvvm_hart_t *vm, const uint32_t insn)
{
    if (unlikely(bit_check(insn, 26) != BIT26)) {
        riscv_illegal_insn(vm, insn);
        return;
    }

    regid_t rs1 = bit_cut(insn, 15, 5);
    regid_t rs2 = bit_cut(insn, 20, 5);
    rm_t rm = fpu_set_rm(vm, bit_cut(insn, 12, 3));
    if (unlikely(rm == RM_INVALID)) {
        riscv_illegal_insn(vm, insn);
        return;
    }
    regid_t rd = bit_cut(insn, 7, 5);
    regid_t rs3 = bit_cut(insn, 27, 5);

    fnative_t res = fpu_fnmadd(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2), fpu_read_register(vm, rs3));
    fpu_write_register(vm, rd, res);
    fpu_set_rm(vm, rm);
}

static void riscv_f_fnmsub(rvvm_hart_t *vm, const uint32_t insn)
{
    if (unlikely(bit_check(insn, 26) != BIT26)) {
        riscv_illegal_insn(vm, insn);
        return;
    }

    regid_t rs1 = bit_cut(insn, 15, 5);
    regid_t rs2 = bit_cut(insn, 20, 5);
    rm_t rm = fpu_set_rm(vm, bit_cut(insn, 12, 3));
    if (unlikely(rm == RM_INVALID)) {
        riscv_illegal_insn(vm, insn);
        return;
    }
    regid_t rd = bit_cut(insn, 7, 5);
    regid_t rs3 = bit_cut(insn, 27, 5);

    fnative_t res = fpu_fnmsub(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2), fpu_read_register(vm, rs3));
    fpu_write_register(vm, rd, res);
    fpu_set_rm(vm, rm);
}

static inline void riscv_f_fadd(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_add(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fsub(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_sub(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fmul(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_mul(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fdiv(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_div(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fsqrt(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_fsqrt(fpu_read_register(vm, rs1)));
}

static inline void riscv_f_fsgnj(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_copysign(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fsgnjn(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_copysign(fpu_read_register(vm, rs1), -fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fsgnjx(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_copysign_xor(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fmin(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_min(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fmax(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_max(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fcvt_s_w(rvvm_hart_t *vm, regid_t rs1, regid_t rd, bool is_unsigned)
{
    if (is_unsigned) {
        fpu_write_register(vm, rd, fpu_int2fp(uint32_t, riscv_read_register(vm, rs1)));
    } else {
        fpu_write_register(vm, rd, fpu_int2fp(int32_t, riscv_read_register_s(vm, rs1)));
    }
}

#ifdef RV64
static inline void riscv_f_fcvt_s_l(rvvm_hart_t *vm, regid_t rs1, regid_t rd, bool is_unsigned)
{
    if (is_unsigned) {
        fpu_write_register(vm, rd, fpu_int2fp(uint64_t, riscv_read_register(vm, rs1)));
    } else {
        fpu_write_register(vm, rd, fpu_int2fp(int64_t, riscv_read_register_s(vm, rs1)));
    }
}
#endif

#ifndef RVD
static inline void riscv_f_fmv_x_w(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    int32_t ival;
    float fval = read_float_nanbox(&vm->fpu_registers[rs1]);
    memcpy(&ival, &fval, sizeof(ival));
    riscv_write_register(vm, rd, ival);
}

static inline void riscv_f_fmv_w_x(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    float fval;
    uint32_t ival = riscv_read_register(vm, rs1);
    memcpy(&fval, &ival, sizeof(fval));
    fpu_write_register(vm, rd, fval);
}

static inline void riscv_f_fcvt_s_d(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_fp2fp(fnative_t, vm->fpu_registers[rs1]));
}
#else
static inline void riscv_f_fcvt_d_s(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    float val = read_float_normalize(&vm->fpu_registers[rs1]);
    fpu_write_register(vm, rd, fpu_fp2fp(fnative_t, val));
}

#ifdef RV64
static inline void riscv_f_fmv_x_d(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    int64_t ival;
    memcpy(&ival, &vm->fpu_registers[rs1], sizeof(ival));
    riscv_write_register(vm, rd, ival);
}

static inline void riscv_f_fmv_d_x(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    double fval;
    uint64_t ival = riscv_read_register(vm, rs1);
    memcpy(&fval, &ival, sizeof(fval));
    fpu_write_register(vm, rd, fval);
}

#endif
#endif

static inline void riscv_f_fclass(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    riscv_write_register(vm, rd, 1 << fpu_fclass(fpu_read_register(vm, rs1)));
}

static inline void riscv_f_feq(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    riscv_write_register(vm, rd, !!fpu_eq(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_flt(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    riscv_write_register(vm, rd, !!fpu_lt(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fle(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    riscv_write_register(vm, rd, !!fpu_le(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static void riscv_f_other(rvvm_hart_t *vm, const uint32_t insn)
{
    if (unlikely(bit_check(insn, 26) != BIT26)) {
        riscv_illegal_insn(vm, insn);
        return;
    }

    regid_t rs1 = bit_cut(insn, 15, 5);
    regid_t rs2 = bit_cut(insn, 20, 5);
    rm_t rm = bit_cut(insn, 12, 3);
    regid_t rd = bit_cut(insn, 7, 5);
    regid_t rs3 = bit_cut(insn, 27, 5);

    switch (rs3) {
        case FT7_FADD:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fadd(vm, rs1, rs2, rd);
            fpu_set_rm(vm, rm);
            break;
        case FT7_FSUB:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fsub(vm, rs1, rs2, rd);
            fpu_set_rm(vm, rm);
            break;
        case FT7_FMUL:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fmul(vm, rs1, rs2, rd);
            fpu_set_rm(vm, rm);
            break;
        case FT7_FDIV:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fdiv(vm, rs1, rs2, rd);
            fpu_set_rm(vm, rm);
            break;
        case FT7_FSQRT:
            if (unlikely(rs2 != 0)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fsqrt(vm, rs1, rd);
            fpu_set_rm(vm, rm);
            break;
        case FT7_FSGN:
            switch (rm) {
                case 0:
                    riscv_f_fsgnj(vm, rs1, rs2, rd);
                    break;
                case 1:
                    riscv_f_fsgnjn(vm, rs1, rs2, rd);
                    break;
                case 2:
                    riscv_f_fsgnjx(vm, rs1, rs2, rd);
                    break;
                default:
                    riscv_illegal_insn(vm, insn);
                    return;
            }
            break;
        case FT7_FMINMAX:
            switch (rm) {
                case 0:
                    riscv_f_fmin(vm, rs1, rs2, rd);
                    break;
                case 1:
                    riscv_f_fmax(vm, rs1, rs2, rd);
                    break;
                default:
                    riscv_illegal_insn(vm, insn);
                    return;
            }
            break;
        case FT7_FCVT_W_S:
#ifdef RV64
            if (rs2 < 2) {
#else
            if (likely(rs2 < 2)) {
#endif
                if (rs2 == 1) {
                    riscv_write_register(vm, rd, fpu_fp2int_uint32_t(fpu_read_register(vm, rs1), rm));
                } else {
                    riscv_write_register(vm, rd, fpu_fp2int_int32_t(fpu_read_register(vm, rs1), rm));
                }
#ifdef RV64
            } else if (likely(rs2 < 4)) {
                if (rs2 == 3) {
                    riscv_write_register(vm, rd, fpu_fp2int_uint64_t(fpu_read_register(vm, rs1), rm));
                } else {
                    riscv_write_register(vm, rd, fpu_fp2int_int64_t(fpu_read_register(vm, rs1), rm));
                }
#endif
            } else {
                riscv_illegal_insn(vm, insn);
            }
            break;
#ifndef RVD
        case FT7_FMV_X_W:
#else
        case FT7_FCLASS:
#endif
            if (unlikely(rs2 != 0)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            if (rm == 0) {
#ifndef RVD
                riscv_f_fmv_x_w(vm, rs1, rd);
#elif defined(RV64)
                riscv_f_fmv_x_d(vm, rs1, rd);
#else
                riscv_illegal_insn(vm, insn);
                return;
#endif
            } else if (likely(rm == 1)) {
                riscv_f_fclass(vm, rs1, rd);
            } else {
                riscv_illegal_insn(vm, insn);
                return;
            }
            break;
        case FT7_FCMP:
            switch (rm) {
                case 0:
                    riscv_f_fle(vm, rs1, rs2, rd);
                    break;
                case 1:
                    riscv_f_flt(vm, rs1, rs2, rd);
                    break;
                case 2:
                    riscv_f_feq(vm, rs1, rs2, rd);
                    break;
                default:
                    riscv_illegal_insn(vm, insn);
                    return;
            }
            break;
        case FT7_FCVT_S_W:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

#ifdef RV64
            if (rs2 < 2) {
#else
            if (likely(rs2 < 2)) {
#endif
                riscv_f_fcvt_s_w(vm, rs1, rd, rs2 == 1);
#ifdef RV64
            } else if (likely(rs2 < 4)) {
                riscv_f_fcvt_s_l(vm, rs1, rd, rs2 == 3);
#endif
            } else {
                riscv_illegal_insn(vm, insn);
            }
            fpu_set_rm(vm, rm);
            break;
#ifndef RVD
        case FT7_FMV_W_X:
            if (unlikely(rs2 != 0 || rm != 0)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            riscv_f_fmv_w_x(vm, rs1, rd);
            break;
        case FT7_FCVT_S_D:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            if (unlikely(rs2 != 1)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fcvt_s_d(vm, rs1, rd);
            break;
#else
#ifdef RV64
        case FT7_FMV_D_X:
            if (unlikely(rs2 != 0 || rm != 0)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            riscv_f_fmv_d_x(vm, rs1, rd);
            break;
#endif
        case FT7_FCVT_D_S:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            if (unlikely(rs2 != 0)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fcvt_d_s(vm, rs1, rd);
            fpu_set_rm(vm, rm);
            break;
#endif
        default:
            riscv_illegal_insn(vm, insn);
            return;
    }
}

#ifdef RVD
static void riscv_c_fld(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read double-precision floating point value from address rs1+offset to rds
    regid_t rds = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 5, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;

    riscv_load_double(vm, addr, rds);
}

static void riscv_c_fsd(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write double-precision floating point value rs2 to address rs1+offset
    regid_t rs2 = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 5, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;

    riscv_store_double(vm, addr, rs2);
}

static void riscv_c_fldsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read double-precision floating point value from address sp+offset to rds
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t offset = (bit_cut(instruction, 5, 2)  << 3)
                    | (bit_cut(instruction, 12, 1) << 5)
                    | (bit_cut(instruction, 2, 3)  << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;

    riscv_load_double(vm, addr, rds);
}

static void riscv_c_fsdsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write double-precision floating point value rs2 to address sp+offset
    regid_t rs2 = bit_cut(instruction, 2, 5);
    uint32_t offset = (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 7, 3) << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;

    riscv_store_double(vm, addr, rs2);
}

static void riscv_d_init(rvvm_hart_t* vm)
{
    riscv_install_opcode_ISB(vm, RVD_FLW, riscv_f_flw);
    riscv_install_opcode_ISB(vm, RVD_FSW, riscv_f_fsw);
    for (uint8_t i = 0; i < 8; ++i) {
        riscv_install_opcode_R(vm, RVD_FMADD | (i << 5), riscv_f_fmadd);
        riscv_install_opcode_R(vm, RVD_FMSUB | (i << 5), riscv_f_fmsub);
        riscv_install_opcode_R(vm, RVD_FNMSUB | (i << 5), riscv_f_fnmsub);
        riscv_install_opcode_R(vm, RVD_FNMADD | (i << 5), riscv_f_fnmadd);
        riscv_install_opcode_R(vm, RVD_OTHER | (i << 5), riscv_f_other);
    }

    riscv_install_opcode_C(vm, RVC_FLD, riscv_c_fld);
    riscv_install_opcode_C(vm, RVC_FSD, riscv_c_fsd);
    riscv_install_opcode_C(vm, RVC_FLDSP, riscv_c_fldsp);
    riscv_install_opcode_C(vm, RVC_FSDSP, riscv_c_fsdsp);
}

void riscv_d_enable(rvvm_hart_t* vm, bool enable)
{
    if (enable) {
        riscv_d_init(vm);
        return;
    }

    riscv_install_opcode_ISB(vm, RVD_FLW, riscv_illegal_insn);
    riscv_install_opcode_ISB(vm, RVD_FSW, riscv_illegal_insn);
    for (uint8_t i = 0; i < 8; ++i) {
        riscv_install_opcode_R(vm, RVD_FMADD | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(vm, RVD_FMSUB | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(vm, RVD_FNMSUB | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(vm, RVD_FNMADD | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(vm, RVD_OTHER | (i << 5), riscv_illegal_insn);
    }

    riscv_install_opcode_C(vm, RVC_FLD, riscv_c_illegal_insn);
    riscv_install_opcode_C(vm, RVC_FSD, riscv_c_illegal_insn);
    riscv_install_opcode_C(vm, RVC_FLDSP, riscv_c_illegal_insn);
    riscv_install_opcode_C(vm, RVC_FSDSP, riscv_c_illegal_insn);
}
#else
#ifndef RV64
static void riscv_c_flw(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read single-precision floating point value from address rs1+offset to rds
    regid_t rds = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 6, 1)  << 2)
                    | (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 5, 1)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;

    riscv_load_float(vm, addr, rds);
}

static void riscv_c_fsw(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write single-precision floating point value rs2 to address rs1+offset
    regid_t rs2 = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 6, 1)  << 2)
                    | (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 5, 1)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;

    riscv_store_float(vm, addr, rs2);
}

static void riscv_c_flwsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read single-precision floating point value from address sp+offset to rds
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t offset = (bit_cut(instruction, 4, 3)  << 2)
                    | (bit_cut(instruction, 12, 1) << 5)
                    | (bit_cut(instruction, 2, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;

    riscv_load_float(vm, addr, rds);
}

static void riscv_c_fswsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write single-precision floating point value rs2 to address sp+offset
    regid_t rs2 = bit_cut(instruction, 2, 5);
    uint32_t offset = (bit_cut(instruction, 9, 4) << 2)
                    | (bit_cut(instruction, 7, 2) << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;

    riscv_store_float(vm, addr, rs2);
}
#endif

static void riscv_f_init(rvvm_hart_t* vm)
{
    riscv_install_opcode_ISB(vm, RVF_FLW, riscv_f_flw);
    riscv_install_opcode_ISB(vm, RVF_FSW, riscv_f_fsw);
    for (uint8_t i = 0; i < 8; ++i) {
        riscv_install_opcode_R(vm, RVF_FMADD | (i << 5), riscv_f_fmadd);
        riscv_install_opcode_R(vm, RVF_FMSUB | (i << 5), riscv_f_fmsub);
        riscv_install_opcode_R(vm, RVF_FNMSUB | (i << 5), riscv_f_fnmsub);
        riscv_install_opcode_R(vm, RVF_FNMADD | (i << 5), riscv_f_fnmadd);
        riscv_install_opcode_R(vm, RVF_OTHER | (i << 5), riscv_f_other);
    }

#ifndef RV64
    riscv_install_opcode_C(vm, RVC_FLW, riscv_c_flw);
    riscv_install_opcode_C(vm, RVC_FSW, riscv_c_fsw);
    riscv_install_opcode_C(vm, RVC_FLWSP, riscv_c_flwsp);
    riscv_install_opcode_C(vm, RVC_FSWSP, riscv_c_fswsp);
#endif
}

void riscv_f_enable(rvvm_hart_t* vm, bool enable)
{
    if (enable) {
        riscv_f_init(vm);
        return;
    }

    riscv_install_opcode_ISB(vm, RVF_FLW, riscv_illegal_insn);
    riscv_install_opcode_ISB(vm, RVF_FSW, riscv_illegal_insn);
    for (uint8_t i = 0; i < 8; ++i) {
        riscv_install_opcode_R(vm, RVF_FMADD | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(vm, RVF_FMSUB | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(vm, RVF_FNMSUB | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(vm, RVF_FNMADD | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(vm, RVF_OTHER | (i << 5), riscv_illegal_insn);
    }

#ifndef RV64
    riscv_install_opcode_C(vm, RVC_FLW, riscv_c_illegal_insn);
    riscv_install_opcode_C(vm, RVC_FSW, riscv_c_illegal_insn);
    riscv_install_opcode_C(vm, RVC_FLWSP, riscv_c_illegal_insn);
    riscv_install_opcode_C(vm, RVC_FSWSP, riscv_c_illegal_insn);
#endif
}
#endif

#else

#ifdef RVD
void riscv_d_enable(rvvm_hart_t* vm, bool enable) { UNUSED(vm); UNUSED(enable); }
#else
void riscv_f_enable(rvvm_hart_t* vm, bool enable) { UNUSED(vm); UNUSED(enable); }
#endif
#endif
