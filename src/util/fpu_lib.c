/*
fpu_lib.c - Floating-point handling library
Copyright (C) 2025  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "fpu_lib.h"

PUSH_OPTIMIZATION_SIZE

#ifndef THREAD_LOCAL
#define THREAD_LOCAL
#endif

#if defined(FENV_8087_IMPL) || defined(FENV_SSE2_IMPL)

static inline uint32_t fpu_x86_sw_to_exceptions(uint32_t sw)
{
    uint32_t ret = 0;
    if (sw & 0x01U) {
        ret |= FPU_LIB_FLAG_NV;
    }
    if (sw & 0x04U) {
        ret |= FPU_LIB_FLAG_DZ;
    }
    if (sw & 0x08U) {
        ret |= FPU_LIB_FLAG_OF;
    }
    if (sw & 0x10U) {
        ret |= FPU_LIB_FLAG_UF;
    }
    if (sw & 0x20U) {
        ret |= FPU_LIB_FLAG_NX;
    }
    return ret;
}

static inline uint32_t fpu_exceptions_to_x86_sw(uint32_t exceptions)
{
    uint32_t ret = 0;
    if (exceptions & FPU_LIB_FLAG_NV) {
        ret |= 0x01U;
    }
    if (exceptions & FPU_LIB_FLAG_DZ) {
        ret |= 0x04U;
    }
    if (exceptions & FPU_LIB_FLAG_OF) {
        ret |= 0x08U;
    }
    if (exceptions & FPU_LIB_FLAG_UF) {
        ret |= 0x10U;
    }
    if (exceptions & FPU_LIB_FLAG_NX) {
        ret |= 0x20U;
    }
    return ret;
}

#elif defined(FENV_EXCEPTIONS_IMPL)

static inline uint32_t fpu_fenv_to_exceptions(uint32_t fenv)
{
    uint32_t ret = 0;
#if defined(FE_INEXACT)
    if (fenv & FE_INEXACT) {
        ret |= FPU_LIB_FLAG_NX;
    }
#endif
#if defined(FE_UNDERFLOW)
    if (fenv & FE_UNDERFLOW) {
        ret |= FPU_LIB_FLAG_UF;
    }
#endif
#if defined(FE_OVERFLOW)
    if (fenv & FE_OVERFLOW) {
        ret |= FPU_LIB_FLAG_OF;
    }
#endif
#if defined(FE_DIVBYZERO)
    if (fenv & FE_DIVBYZERO) {
        ret |= FPU_LIB_FLAG_DZ;
    }
#endif
#if defined(FE_INVALID)
    if (fenv & FE_INVALID) {
        ret |= FPU_LIB_FLAG_NV;
    }
#endif
    return ret;
}

static inline uint32_t fpu_exceptions_to_fenv(uint32_t exceptions)
{
    uint32_t ret = 0;
#if defined(FE_INEXACT)
    if (exceptions & FPU_LIB_FLAG_NX) {
        ret |= FE_INEXACT;
    }
#endif
#if defined(FE_UNDERFLOW)
    if (exceptions & FPU_LIB_FLAG_UF) {
        ret |= FE_UNDERFLOW;
    }
#endif
#if defined(FE_OVERFLOW)
    if (exceptions & FPU_LIB_FLAG_OF) {
        ret |= FE_OVERFLOW;
    }
#endif
#if defined(FE_DIVBYZERO)
    if (exceptions & FPU_LIB_FLAG_DZ) {
        ret |= FE_DIVBYZERO;
    }
#endif
#if defined(FE_INVALID)
    if (exceptions & FPU_LIB_FLAG_NV) {
        ret |= FE_INVALID;
    }
#endif
    return ret;
}

#else

static THREAD_LOCAL uint32_t fpu_exceptions = 0;
static THREAD_LOCAL uint32_t fpu_round_mode = FPU_LIB_ROUND_NE;

#endif

static uint32_t fpu_pump_exceptions_internal(uint32_t set_exceptions, uint32_t clr_exceptions)
{
#if defined(FENV_SSE2_IMPL)
    uint32_t sw = 0, nsw = 0;
    __asm__ __volatile__("stmxcsr %0" : "=m"(*&sw) : : "memory");
    // Mask all exceptions, disable DAZ/FTZ
    nsw = (sw & ~0x8040U) | 0x1F80U;
    if (set_exceptions) {
        nsw |= fpu_exceptions_to_x86_sw(set_exceptions);
    }
    if (clr_exceptions) {
        nsw &= ~fpu_exceptions_to_x86_sw(clr_exceptions);
    }
    if (unlikely(nsw != sw)) {
        __asm__ __volatile__("ldmxcsr %0" : : "m"(*&nsw) : "memory");
    }
    return fpu_x86_sw_to_exceptions(sw);
#elif defined(FENV_8087_IMPL)
    uint16_t sw = 0;
    if (set_exceptions || clr_exceptions) {
        uint16_t fenv[32] = ZERO_INIT;
        __asm__ __volatile__("fnstenv %0" : "=m"(*&fenv) : : "memory");
        sw = fenv[2];
        if (set_exceptions) {
            fenv[2] |= fpu_exceptions_to_x86_sw(set_exceptions);
        }
        if (clr_exceptions) {
            fenv[2] &= ~fpu_exceptions_to_x86_sw(clr_exceptions);
        }
        if (unlikely(fenv[2] != sw)) {
            // Mask all exceptions
            fenv[0] &= 0xFCFFU;
            fenv[0] |= 0x033FU;
            __asm__ __volatile__("fldenv %0" : : "m"(*&fenv) : "memory");
        }
    } else {
        // Simply read status word
        __asm__ __volatile__("fnstsw %0" : "=a"(sw) : : "memory");
    }
    return fpu_x86_sw_to_exceptions(sw);
#elif defined(FENV_EXCEPTIONS_IMPL)
    uint32_t ret    = fpu_fenv_to_exceptions(fetestexcept(FE_ALL_EXCEPT));
    set_exceptions &= ~ret;
    clr_exceptions &= ret;
    if (set_exceptions) {
        feraiseexcept(fpu_exceptions_to_fenv(set_exceptions));
    }
    if (clr_exceptions) {
        feclearexcept(fpu_exceptions_to_fenv(clr_exceptions));
    }
    return ret;
#else
    uint32_t ret    = fpu_exceptions;
    fpu_exceptions |= set_exceptions & FPU_LIB_FLAGS_ALL;
    fpu_exceptions &= ~clr_exceptions;
    return ret;
#endif
}

static uint32_t fpu_pump_rounding_mode_internal(uint32_t mode)
{
#if defined(FENV_SSE2_IMPL)
    uint32_t cw = 0, ncw = 0;
    __asm__ __volatile__("stmxcsr %0" : "=m"(*&cw) : : "memory");
    // Mask all exceptions, disable DAZ/FTZ
    ncw = (cw & ~0x8040U) | 0x1F80U;
    if (mode <= FPU_LIB_ROUND_MM) {
        ncw &= ~0x6000U;
        switch (mode) {
            case FPU_LIB_ROUND_DN:
                ncw |= 0x2000U;
                break;
            case FPU_LIB_ROUND_UP:
                ncw |= 0x4000U;
                break;
            case FPU_LIB_ROUND_TZ:
                ncw |= 0x6000U;
                break;
        }
        if (ncw != cw) {
            __asm__ __volatile__("ldmxcsr %0" : : "m"(*&ncw) : "memory");
        }
    }
    switch (cw & 0x6000U) {
        case 0x2000:
            return FPU_LIB_ROUND_DN;
        case 0x4000U:
            return FPU_LIB_ROUND_UP;
        case 0x6000U:
            return FPU_LIB_ROUND_TZ;
    }
    return FPU_LIB_ROUND_NE;
#elif defined(FENV_8087_IMPL)
    uint16_t cw = 0, ncw = 0;
    __asm__ __volatile__("fnstcw %0" : "=m"(*&cw) : : "memory");
    if (mode <= FPU_LIB_ROUND_MM) {
        // Mask all exceptions
        ncw = (cw & 0xF0FFU) | 0x033FU;
        switch (mode) {
            case FPU_LIB_ROUND_DN:
                ncw |= 0x0400U;
                break;
            case FPU_LIB_ROUND_UP:
                ncw |= 0x0800U;
                break;
            case FPU_LIB_ROUND_TZ:
                ncw |= 0x0C00U;
                break;
        }
        if (ncw != cw) {
            __asm__ __volatile__("fldcw %0" : : "m"(*&ncw) : "memory");
        }
    }
    switch (cw & 0x0C00U) {
        case 0x0400U:
            return FPU_LIB_ROUND_DN;
        case 0x0800U:
            return FPU_LIB_ROUND_UP;
        case 0x0C00U:
            return FPU_LIB_ROUND_TZ;
    }
    return FPU_LIB_ROUND_NE;
#elif defined(FENV_ROUNDING_IMPL)
    uint32_t ret = FPU_LIB_ROUND_NE;
    switch (fegetround()) {
#if defined(FE_DOWNWARD)
        case FE_DOWNWARD:
            ret = FPU_LIB_ROUND_DN;
            break;
#endif
#if defined(FE_UPWARD)
        case FE_UPWARD:
            ret = FPU_LIB_ROUND_UP;
            break;
#endif
#if defined(FE_TOWARDZERO)
        case FE_TOWARDZERO:
            ret = FPU_LIB_ROUND_TZ;
            break;
#endif
    }
    if (mode <= FPU_LIB_ROUND_MM && mode != ret) {
        switch (mode) {
#if defined(FE_DOWNWARD)
            case FPU_LIB_ROUND_DN:
                fesetround(FE_DOWNWARD);
                break;
#endif
#if defined(FE_UPWARD)
            case FPU_LIB_ROUND_UP:
                fesetround(FE_UPWARD);
                break;
#endif
#if defined(FE_TOWARDZERO)
            case FPU_LIB_ROUND_TZ:
                fesetround(FE_TOWARDZERO);
                break;
#endif
#if defined(FE_TONEAREST)
            default:
                fesetround(FE_TONEAREST);
                break;
#endif
        }
    }
    return ret;
#else
    uint32_t ret = fpu_round_mode;
    if (mode <= FPU_LIB_ROUND_MM) {
        fpu_round_mode = mode;
    }
    return ret;
#endif
}

uint32_t fpu_get_exceptions(void)
{
    return fpu_pump_exceptions_internal(0, 0);
}

void fpu_set_exceptions(uint32_t new_exceptions)
{
    fpu_pump_exceptions_internal(new_exceptions, ~new_exceptions);
}

void fpu_raise_exceptions(uint32_t set_exceptions)
{
    fpu_pump_exceptions_internal(set_exceptions, 0);
}

void fpu_clear_exceptions(uint32_t clr_exceptions)
{
    fpu_pump_exceptions_internal(0, clr_exceptions);
}

uint32_t fpu_get_rounding_mode(void)
{
    return fpu_pump_rounding_mode_internal(-1);
}

void fpu_set_rounding_mode(uint32_t mode)
{
    fpu_pump_rounding_mode_internal(mode);
}

slow_path void fpu_raise_invalid(void)
{
    fpu_raise_exceptions(FPU_LIB_FLAG_NV);
}

slow_path void fpu_raise_inexact(void)
{
    fpu_raise_exceptions(FPU_LIB_FLAG_NX);
}

slow_path void fpu_raise_ovrflow(void)
{
    fpu_raise_exceptions(FPU_LIB_FLAG_OF);
}

slow_path void fpu_raise_divzero(void)
{
    fpu_raise_exceptions(FPU_LIB_FLAG_DZ);
}

slow_path uint32_t fpu_fclass32(fpu_f32_t f)
{
    uint32_t s = fpu_bit_f32_to_u32(f);
    uint32_t u = s << 1;
    uint32_t ret;
    if (!u) {
        ret = FPU_CLASS_POS_ZERO;
    } else {
        uint32_t exp = u >> 24;
        if (!exp) {
            ret = FPU_CLASS_POS_SUBNORMAL;
        } else if (exp < 0xFFU) {
            ret = FPU_CLASS_POS_NORMAL;
        } else if (!(s << 9)) {
            ret = FPU_CLASS_POS_INF;
        } else {
            return (s & FPU_LIB_FP32_QUIETNAN_MASK) ? FPU_CLASS_NAN_QUIET : FPU_CLASS_NAN_SIG;
        }
    }
    if (s >> 31) {
        ret ^= FPU_CLASS_POS_INF;
    }
    return ret;
}

slow_path uint32_t fpu_fclass64(fpu_f64_t d)
{
    uint64_t s = fpu_bit_f64_to_u64(d);
    uint64_t u = s << 1;
    uint32_t ret;
    if (!u) {
        ret = FPU_CLASS_POS_ZERO;
    } else {
        uint32_t exp = u >> 53;
        if (!exp) {
            ret = FPU_CLASS_POS_SUBNORMAL;
        } else if (exp < 0x7FFU) {
            ret = FPU_CLASS_POS_NORMAL;
        } else if (!(s << 12)) {
            ret = FPU_CLASS_POS_INF;
        } else {
            return (s & FPU_LIB_FP64_QUIETNAN_MASK) ? FPU_CLASS_NAN_QUIET : FPU_CLASS_NAN_SIG;
        }
    }
    if (s >> 63) {
        ret ^= FPU_CLASS_POS_INF;
    }
    return ret;
}

/*
 * Round to an integral value directly on the encoding: no host FP arithmetic, so
 * no spurious exception flags and no double rounding. Only DYN falls back to the
 * tracked mode; an explicit RMM request is honored as-is.
 */
slow_path fpu_f32_t fpu_round_f32_internal(fpu_f32_t f, uint32_t mode)
{
    const uint32_t u = fpu_bit_f32_to_u32(f);
    const uint32_t s = u & FPU_LIB_FP32_SIGNEDFP_MASK;
    const int32_t  e = fpu_exponent32(f);
    bool away = false;
    if (unlikely(mode > FPU_LIB_ROUND_MM)) {
        mode = fpu_get_rounding_mode();
    }
    if (e >= 23 || !(u << 1)) {
        return f; // Already integral: |f| >= 2^23, +/-0, inf, NaN
    }
    if (e < 0) {
        // |f| in (0, 1) rounds to +/-0 or +/-1; the 0.5 tie goes to even == 0
        switch (mode) {
            case FPU_LIB_ROUND_NE:
                away = (e == -1) && (u & FPU_LIB_FP32_MANTISSA_MASK);
                break;
            case FPU_LIB_ROUND_MM:
                away = (e == -1);
                break;
            case FPU_LIB_ROUND_DN:
                away = !!s;
                break;
            case FPU_LIB_ROUND_UP:
                away = !s;
                break;
        }
        return fpu_bit_u32_to_f32(s | (away ? 0x3F800000U : 0));
    }
    // |f| in [1, 2^23): split the mantissa into integer part and fraction bits
    const uint32_t frac = u & (FPU_LIB_FP32_MANTISSA_MASK >> e);
    const uint32_t half = 0x00400000U >> e;
    const uint32_t step = 0x00800000U >> e; // 1.0 at this exponent; carries into a new binade
    switch (mode) {
        case FPU_LIB_ROUND_NE:
            away = frac > half || (frac == half && (u & step));
            break;
        case FPU_LIB_ROUND_MM:
            away = frac >= half;
            break;
        case FPU_LIB_ROUND_DN:
            away = s && frac;
            break;
        case FPU_LIB_ROUND_UP:
            away = !s && frac;
            break;
    }
    return fpu_bit_u32_to_f32((u - frac) + (away ? step : 0));
}

slow_path fpu_f64_t fpu_round_f64_internal(fpu_f64_t d, uint32_t mode)
{
    const uint64_t u = fpu_bit_f64_to_u64(d);
    const uint64_t s = u & FPU_LIB_FP64_SIGNEDFP_MASK;
    const int32_t  e = fpu_exponent64(d);
    bool away = false;
    if (unlikely(mode > FPU_LIB_ROUND_MM)) {
        mode = fpu_get_rounding_mode();
    }
    if (e >= 52 || !(u << 1)) {
        return d; // Already integral: |d| >= 2^52, +/-0, inf, NaN
    }
    if (e < 0) {
        switch (mode) {
            case FPU_LIB_ROUND_NE:
                away = (e == -1) && (u & FPU_LIB_FP64_MANTISSA_MASK);
                break;
            case FPU_LIB_ROUND_MM:
                away = (e == -1);
                break;
            case FPU_LIB_ROUND_DN:
                away = !!s;
                break;
            case FPU_LIB_ROUND_UP:
                away = !s;
                break;
        }
        return fpu_bit_u64_to_f64(s | (away ? 0x3FF0000000000000ULL : 0));
    }
    const uint64_t frac = u & (FPU_LIB_FP64_MANTISSA_MASK >> e);
    const uint64_t half = 0x0008000000000000ULL >> e;
    const uint64_t step = 0x0010000000000000ULL >> e;
    switch (mode) {
        case FPU_LIB_ROUND_NE:
            away = frac > half || (frac == half && (u & step));
            break;
        case FPU_LIB_ROUND_MM:
            away = frac >= half;
            break;
        case FPU_LIB_ROUND_DN:
            away = s && frac;
            break;
        case FPU_LIB_ROUND_UP:
            away = !s && frac;
            break;
    }
    return fpu_bit_u64_to_f64((u - frac) + (away ? step : 0));
}

#if defined(USE_SOFT_FPU_SQRT)

fpu_f32_t fpu_sqrt32_soft_internal(fpu_f32_t f)
{
    // Evil floating point bit level hacking
    // Approximates sqrtf() to 8 significant bits
    fpu_f32_t x = fpu_bit_u32_to_f32(0x1FBD1DF5U + (fpu_bit_f32_to_u32(f) >> 1));
#if !defined(USE_SOFT_FPU_FENV)
    uint32_t e = fpu_get_exceptions();
#endif
    // Newton algorithm iteration: Almost 17 significant bits
    x = fpu_wrap_f32((fpu_raw_f32(f) / fpu_raw_f32(x) + fpu_raw_f32(x)) * 0.5f);
    // Newton algorithm iteration: Almost 35 significant bits
    x = fpu_wrap_f32((fpu_raw_f32(f) / fpu_raw_f32(x) + fpu_raw_f32(x)) * 0.5f);
#if !defined(USE_SOFT_FPU_FENV)
    // Revert NX flag set by previous Newton iterations
    if (!(e & FPU_LIB_FLAG_NX)) {
        fpu_set_exceptions(e);
    }
#endif
    // This either produces an exact result, or within 1 ulp and sets NX flag
    x = fpu_wrap_f32((fpu_raw_f32(f) / fpu_raw_f32(x) + fpu_raw_f32(x)) * 0.5f);
    return x;
}

fpu_f64_t fpu_sqrt64_soft_internal(fpu_f64_t d)
{
    fpu_f64_t x = fpu_bit_u64_to_f64(0x1FF7A3BEA91D9B1BULL + (fpu_bit_f64_to_u64(d) >> 1));
#if !defined(USE_SOFT_FPU_FENV)
    uint32_t e = fpu_get_exceptions();
#endif
    // Newton algorithm iterations
    x = fpu_wrap_f64((fpu_raw_f64(d) / fpu_raw_f64(x) + fpu_raw_f64(x)) * 0.5);
    x = fpu_wrap_f64((fpu_raw_f64(d) / fpu_raw_f64(x) + fpu_raw_f64(x)) * 0.5);
    x = fpu_wrap_f64((fpu_raw_f64(d) / fpu_raw_f64(x) + fpu_raw_f64(x)) * 0.5);
#if !defined(USE_SOFT_FPU_FENV)
    // Revert NX flag set by previous Newton iterations
    if (!(e & FPU_LIB_FLAG_NX)) {
        fpu_set_exceptions(e);
    }
#endif
    x = fpu_wrap_f64((fpu_raw_f64(d) / fpu_raw_f64(x) + fpu_raw_f64(x)) * 0.5);
    if (!fpu_is_bit_equal64(d, fpu_wrap_f64(fpu_raw_f64(x) * fpu_raw_f64(x)))) {
        // Fixup lowest mantissa bit
        // I am not 100% sure why this is needed - please enlighten me someone
        x = fpu_bit_u64_to_f64(fpu_bit_f64_to_u64(x) | 1);
    }
    return x;
}

#endif

#if defined(USE_SOFT_FPU_FENV)

slow_path void fpu_soft_fenv_check_add32(fpu_f32_t sum, fpu_f32_t a, fpu_f32_t b)
{
    if (unlikely(!fpu_is_finite32(sum))) {
        if (fpu_raw_f32(sum) != fpu_raw_f32(sum)) {
            fpu_raise_invalid();
        } else {
            fpu_raise_ovrflow();
        }
    } else if (unlikely(fpu_bit_f32_to_u32(fpu_add_error32(sum, a, b)))) {
        fpu_raise_inexact();
    }
}

slow_path void fpu_soft_fenv_check_add64(fpu_f64_t sum, fpu_f64_t a, fpu_f64_t b)
{
    if (unlikely(!fpu_is_finite64(sum))) {
        if (fpu_raw_f64(sum) != fpu_raw_f64(sum)) {
            fpu_raise_invalid();
        } else {
            fpu_raise_ovrflow();
        }
    } else if (unlikely(fpu_bit_f64_to_u64(fpu_add_error64(sum, a, b)))) {
        fpu_raise_inexact();
    }
}

slow_path void fpu_soft_fenv_check_mul32(fpu_f32_t mul, fpu_f32_t a, fpu_f32_t b, bool is_div)
{
    if (unlikely(!fpu_is_finite32(mul))) {
        if (fpu_raw_f32(mul) != fpu_raw_f32(mul)) {
            fpu_raise_invalid();
        } else if (is_div) {
            fpu_raise_divzero();
        } else {
            fpu_raise_ovrflow();
        }
    } else if (unlikely(fpu_bit_f32_to_u32(fpu_mul_error32(mul, a, b)))) {
        fpu_raise_inexact();
    }
}

slow_path void fpu_soft_fenv_check_mul64(fpu_f64_t sum, fpu_f64_t a, fpu_f64_t b, bool is_div)
{
    if (unlikely(!fpu_is_finite64(sum))) {
        if (fpu_raw_f64(sum) != fpu_raw_f64(sum)) {
            fpu_raise_invalid();
        } else if (is_div) {
            fpu_raise_divzero();
        } else {
            fpu_raise_ovrflow();
        }
    } else if (unlikely(fpu_bit_f64_to_u64(fpu_mul_error64(sum, a, b)))) {
        fpu_raise_inexact();
    }
}

#endif

POP_OPTIMIZATION_SIZE
