/*
fpu_lib.h - Floating-point handling library
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

#ifndef RVVM_FPU_LIB_H
#define RVVM_FPU_LIB_H

#include "compiler.h"
#include "fpu_ops.h"
#include "mem_ops.h"
#include "bit_ops.h"

/* well... */
#if defined(_WIN32) && defined(__clang__)
#define _C_COMPLEX_T
typedef _Complex double _C_double_complex;
typedef _Complex float _C_float_complex;
typedef _Complex long double _C_ldouble_complex;
#endif
#include <math.h>

#define fpu_isnan(x) isnan(x)

static forceinline bool fpu_is_snanf(float f)
{
    uint32_t i;
    memcpy(&i, &f, sizeof(f));
    return fpu_isnan(f) && !bit_check(i, 22);
}

static forceinline bool fpu_is_snand(double d)
{
    uint64_t i;
    memcpy(&i, &d, sizeof(d));
    return fpu_isnan(d) && !bit_check(i, 51);
}

static forceinline bool fpu_signbitf(float f)
{
    uint32_t i;
    memcpy(&i, &f, sizeof(f));
    return i >> 31;
}

static forceinline bool fpu_signbitd(double d)
{
    uint64_t i;
    memcpy(&i, &d, sizeof(d));
    return i >> 63;
}

static forceinline float fpu_sqrtf(float val)
{
    float ret = sqrtf(val);
    if (unlikely(val < 0 && !fetestexcept(FE_INVALID))) feraiseexcept(FE_INVALID);
    return ret;
}

static forceinline double fpu_sqrtd(double val)
{
    double ret = sqrt(val);
    if (unlikely(val < 0 && !fetestexcept(FE_INVALID))) feraiseexcept(FE_INVALID);
    return ret;
}

static forceinline float fpu_copysignf(float a, float b)
{
    return copysignf(a, b);
}

static forceinline double fpu_copysignd(double a, double b)
{
    return copysign(a, b);
}

static forceinline float fpu_copysignxf(float a, float b)
{
    uint32_t ia, ib;
    memcpy(&ia, &a, sizeof(a));
    memcpy(&ib, &b, sizeof(b));
    ib ^= ia;
    memcpy(&b, &ib, sizeof(b));
    return fpu_copysignf(a, b);
}

static forceinline double fpu_copysignxd(double a, double b)
{
    uint64_t ia, ib;
    memcpy(&ia, &a, sizeof(a));
    memcpy(&ib, &b, sizeof(b));
    ib ^= ia;
    memcpy(&b, &ib, sizeof(b));
    return fpu_copysignd(a, b);
}

static forceinline float fpu_fmaf(float a, float b, float c)
{
#ifdef UNDER_CE // WinCE libc doesn't have fma()
    return a * b + c;
#else
    return fmaf(a, b, c);
#endif
}

static forceinline double fpu_fmad(double a, double b, double c)
{
#ifdef UNDER_CE // WinCE libc doesn't have fma()
    return a * b + c;
#else
    return fma(a, b, c);
#endif
}

static forceinline float fpu_minf(float x, float y)
{
#if defined(__riscv_f)
    // On real RISC-V, fmin/fmax actually behave the way we need
    return fminf(x, y);
#else
#if defined(GNU_EXTS)
    // isless/isgreater are non-signalling comparisons
    if (isless(x, y)) {
        return x;
    } else if (likely(isless(y, x))) {
        return y;
    }
#endif
    if (unlikely(fpu_isnan(x))) {
        // If one of operands is NaN, return a different operand
        if (fpu_is_snanf(x) || fpu_is_snanf(y)) {
            feraiseexcept(FE_INVALID);
        }
        return y;
    } else if (unlikely(fpu_isnan(y))) {
        if (fpu_is_snanf(x) || fpu_is_snanf(y)) {
            feraiseexcept(FE_INVALID);
        }
        return x;
#if !defined(GNU_EXTS)
    } else if (x < y) {
        return x;
    } else if (y < x) {
        return y;
#endif
    } else {
        // -0.0 is less than 0.0, but not handled by isless/isgreater
        return fpu_signbitf(x) ? x : y;
    }
#endif
}

static forceinline float fpu_maxf(float x, float y)
{
#if defined(__riscv_f)
    return fmaxf(x, y);
#else
#if defined(GNU_EXTS)
    if (isgreater(x, y)) {
        return x;
    } else if (likely(isgreater(y, x))) {
        return y;
    }
#endif
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
#if !defined(GNU_EXTS)
    } else if (x > y) {
        return x;
    } else if (y > x) {
        return y;
#endif
    } else {
        return fpu_signbitf(x) ? y : x;
    }
#endif
}

static forceinline double fpu_mind(double x, double y)
{
#if defined(__riscv_d)
    return fmin(x, y);
#else
#if defined(GNU_EXTS)
    if (isless(x, y)) {
        return x;
    } else if (likely(isless(y, x))) {
        return y;
    }
#endif
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
#if !defined(GNU_EXTS)
    } else if (x < y) {
        return x;
    } else if (y < x) {
        return y;
#endif
    } else {
        return fpu_signbitd(x) ? x : y;
    }
#endif
}

static forceinline double fpu_maxd(double x, double y)
{
#if defined(__riscv_d)
    return fmax(x, y);
#else
#if defined(GNU_EXTS)
    if (isgreater(x, y)) {
        return x;
    } else if (likely(isgreater(y, x))) {
        return y;
    }
#endif
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
#if !defined(GNU_EXTS)
    } else if (x > y) {
        return x;
    } else if (y > x) {
        return y;
#endif
    } else {
        return fpu_signbitd(x) ? y : x;
    }
#endif
}

#endif
