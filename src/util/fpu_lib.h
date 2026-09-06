/*
fpu_lib.h - Floating-point handling library
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef LEKKIT_FPU_LIB_H
#define LEKKIT_FPU_LIB_H

#include "fpu_types.h"
#include "mem_ops.h"
#include "bit_ops.h"

/*
 * NOTE: Floating-point is actually subtly (or seriously) broken on many compilers and platforms
 *
 * For anyone who wants to understand this stuff in detail, the f16/f32/f64 FP types were named like
 * that for a reason. Look them up in ICD-10, you'll probably get like all of them by the end of it.
 *
 * Signaling NaNs are silently converted to quit NaNs on 8087 FPU (Legacy x86 FPU before SSE2):
 * - The conversion happens upon storing a 8087 register into memory, together with other transformations
 * - This is possibly the most convoluted issue to fix, and a major reason this library exists at all
 * Workaround: Introduce fpu_fp32_t/fpu_fp64_t typedefs, which are only unwrapped to native floating-point
 *   type during an actual FP operation, preventing involuntary changes to the floating-point representation.
 *   Signaling NaN checks are implemented in software.
 * USE_SOFT_FPU_WRAP is enabled on i386 and m68k, and allows proper IEEE 754 conformance.
 *
 * Signaling comparisons (<, <=) may be miscompiled into quiet comparisons:
 * - Older GCC <8.1, Clang <12.0 emit quiet <, <= comparisons on x86, arm64
 * - Clang 12.0+ requires -frounding-math or #pragma STDC FENV_ACCESS ON
 * - Quiet (unordered) compares are still emitted on arm32, powerpc, loongarch64...
 * - ChibiCC, Cproc compilers emit quiet <, <= comparisons (ucomiss instead of comiss)
 * - SlimCC compiler (ChibiCC fork) fixes this
 * Workaround: Manually raise FE_INVALID if neither a < b nor b <= a
 *
 * Quiet comparisons (__builtin_isless) may be miscompiled or inefficient:
 * - Signaling comparisons are emitted on RISC-V instead
 * Workaround: Manually check !fpu_is_nan() before <, <= comparison
 *
 * Converting floats into 64-bit integers may break FE_INEXACT semantics:
 * - For inexact fp->i64 conversions, FE_INEXACT is missed on riscv32, arm32, powerpc32, loongarch64
 * - Same issue with TCC compiler
 * - Cproc compiler erroneously raises FE_INEXACT for exact fp->i64 conversions
 * Workaround: Manually raise FE_INEXACT on inexact conversion
 *
 * Converting floats into unsigned integers may be broken:
 * - Cproc compiler uses cvtss2si for fp->u64 conversions, which is incorrect for >INT64_MAX inputs
 * - Same for XCC compiler, and fp->u32 conversions are also broken in same manner
 * Workaround: Implement fp->u32 conversion as fp->i64, fp->u64 conversion in software for >INT_MAX inputs
 *
 * Converting unsigned integers into floats may be broken:
 * - ChibiCC compiler uses cvtsi2ss for u64->fp conversions, which is incorrect for >INT64_MAX inputs
 * - Same for XCC compiler, and u32->fp conversions are also broken in same manner
 * - SlimCC compiler (ChibiCC fork) fixes this
 * Workaround: Implement u32->fp conversion as i64->fp, u64->fp conversion in software for >INT_MAX inputs
 *
 * Intel OneAPI compiler enables FTZ/DAZ by default (Among other things):
 * - Enabling "flush to zero" or "denormals are zero" breaks denormals and causes test failure
 * Workaround: Pass -fno-fast-math at build time, clear FTZ/DAZ in MXCSR handling code
 *
 * Calling into libm may break FE_INEXACT/FE_INVALID semantics:
 * - Windows fails to set FE_INVALID on sqrt(-1), even when using MinGW
 * - Musl has FENV_SUPPORT as optional, and may fail to set FE_INEXACT, etc
 * Workaround: Handle FE_INVALID manually, prefer native instructions
 *
 * Calling into libm may be completely broken:
 * - WinNT 3.x/4.0, Win95 lack SSE state handling, any libm function that uses SSE crashes
 * - Determinism is desired, yet various libm implementation may have non-bit-exact results
 * Workaround: Handle fenv in asembly, prefer native instructions
 *
 * Many platforms lack FPU environment handling (Exceptions / Rounding):
 * - WASM (Emscripten)
 * - MIPS, DEC Alpha, HPPA, M68K, SH4, Hexagon
 * - Windows CE (ARM32), or any other soft-float ARM target
 * Workaround: Thread-local exception flags, which are raised manually based on various checks
 *
 * NOTE: FPU exception emulation is very expensive, and does not affect like most of software
 * (outside some specific IEEE754 tests). It is disabled on WASM by default for now.
 *
 * NOTE: RISC-V THead CPUs also lack FPU exceptions, but I'm hesitant to enable such workaround
 * on RISC-V in general as this case is a single non-conforming hardware implementation.
 *
 * This list is not even considering various historical compilers, and MSVC. To be continued?...
 *
 * Because of this, and because most workarounds have little performance impact, consider limited
 * list of compilers/platforms a "Known Nice Platform" (C) and apply workarounds otherwise.
 */

#if CLANG_CHECK_VER(12, 0)
// Fix various floating-point misoptimizations. Why the fuck is Clang not IEEE 754 compliant by default???
#if !defined(__INTEL_LLVM_COMPILER) /**/                                                                               \
    && !defined(__i386__) && !defined(__x86_64__) && !defined(__aarch64__) && !defined(__riscv_d)
// This pragma fixes build on Intel OneAPI compiler, and Clang sqrt() behavior at least on powerpc
// Emabling this flag on x86/arm64/riscv needlessly pessimizes the codegen
#pragma float_control(precise)
#endif
#pragma STDC FENV_ACCESS ON
#elif defined(COMPILER_IS_MSVC)
#pragma fenv_access(on)
#endif

/*
 * Hide <math.h> definitions from generic code if possible - it should not be used
 */
#if GNU_BUILTIN(__builtin_sqrt) && GNU_BUILTIN(__builtin_sqrtf)
#define fpu_sqrt_internal(d)  __builtin_sqrt(d)
#define fpu_sqrtf_internal(f) __builtin_sqrtf(f)
#elif CHECK_INCLUDE(math.h, 1)
#include <math.h>
#define fpu_sqrt_internal(d)  sqrt(d)
#define fpu_sqrtf_internal(f) sqrtf(f)
#else
#define USE_SOFT_FPU_SQRT 1
#endif

// Check FPU environment support, enable emulation if needed
#if !defined(USE_FPU_WORKAROUNDS) && defined(GNU_EXTS) && defined(__i386__) /**/                                       \
    && !defined(__SSE__) && !defined(__SSE_MATH__) && !defined(__FXSR__)
#define FENV_8087_IMPL 1
#elif !defined(USE_FPU_WORKAROUNDS) && defined(GNU_EXTS) && defined(__x86_64__) /**/                                   \
    && defined(__SSE2__) && defined(__SSE2_MATH__)
#define FENV_SSE2_IMPL 1
#elif defined(__SOFTFP__)                                                                /**/                          \
    || defined(__m68k__) || defined(__sh__) || defined(__hppa__) || defined(__hexagon__) /**/                          \
    || defined(__alpha__) || defined(__alpha) || defined(_M_ALPHA)                       /**/                          \
    || defined(__mips__) || defined(__mips) || defined(__mips64__) || defined(__mips64)  /**/                          \
    || (defined(_WIN32) && defined(_M_ARM)) || !CHECK_INCLUDE(fenv.h, 1)
#undef USE_SOFT_FPU_FENV
#define USE_SOFT_FPU_FENV 1
#else
#include <fenv.h>
#if defined(FE_ALL_EXCEPT) && defined(FE_INEXACT) && defined(FE_INVALID)
#define FENV_EXCEPTIONS_IMPL 1
#else
#undef USE_SOFT_FPU_FENV
#define USE_SOFT_FPU_FENV 1
#endif
#if defined(FE_DOWNWARD) || defined(FE_UPWARD) || defined(FE_TOWARDZERO)
#define FENV_ROUNDING_IMPL 1
#endif
#endif

// GCC 8.1+, Clang 12.0+, SlimCC respect IEEE 754
#undef FPU_LIB_KNOWN_SANE_COMPILER
#if !defined(USE_FPU_WORKAROUNDS) /**/                                                                                 \
    && (GCC_CHECK_VER(8, 1) || CLANG_CHECK_VER(12, 0) || defined(__slimcc__))
#define FPU_LIB_KNOWN_SANE_COMPILER 1
#endif

// Conversion uint->fp is correct on modern GCC/Clang, SlimCC
#undef FPU_LIB_CORRECT_CONVERSION_UINT_FP
#if defined(FPU_LIB_KNOWN_SANE_COMPILER)
#define FPU_LIB_CORRECT_CONVERSION_UINT_FP 1
#endif

// Conversion fp->uint is correct on i386, x86_64, arm64, riscv64, powerpc64, mips64
#undef FPU_LIB_CORRECT_CONVERSION_FP_UINT
#if !defined(USE_SOFT_FPU_FENV) && defined(FPU_LIB_KNOWN_SANE_COMPILER)        /**/                                    \
    && (defined(__i386__) || defined(__x86_64__)                               /**/                                    \
        || defined(__aarch64__) || (defined(__riscv_d) && defined(HOST_64BIT)) /**/                                    \
        || defined(__powerpc64__) || defined(__mips64__))
#define FPU_LIB_CORRECT_CONVERSION_FP_UINT 1
#endif

// Comparison via <, <= is signaling on i386, x86_64, arm64, riscv
#undef FPU_LIB_CORRECT_SIGNALING_COMPARE
#if !defined(USE_SOFT_FPU_FENV) && defined(FPU_LIB_KNOWN_SANE_COMPILER) /**/                                           \
    && (defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) || defined(__riscv_d))
#define FPU_LIB_CORRECT_SIGNALING_COMPARE 1
#endif

// Comparison via __builtin_isless() is quiet on i386, x86_64, arm, arm64, powerpc
#undef FPU_LIB_CORRECT_BUILTIN_QUIET_COMPARE
#if !defined(USE_SOFT_FPU_FENV) && defined(FPU_LIB_KNOWN_SANE_COMPILER)    /**/                                        \
    && (defined(__i386__) || defined(__x86_64__)                           /**/                                        \
        || defined(__arm__) || defined(__aarch64__)                        /**/                                        \
        || defined(__powerpc__) || defined(__powerpc64__))                 /**/                                        \
    && GNU_BUILTIN(__builtin_isless) && GNU_BUILTIN(__builtin_islessequal) /**/                                        \
    && GNU_BUILTIN(__builtin_isgreater) && GNU_BUILTIN(__builtin_isgreaterequal)
#define FPU_LIB_CORRECT_BUILTIN_QUIET_COMPARE 1
#endif

// Min/Max via __builtin_fmin() conforms to IEEE 754-2008 on riscv
#undef FPU_LIB_CORRECT_BUILTIN_IEEE754_2008_FMINMAX
#if !defined(USE_SOFT_FPU_FENV) && defined(FPU_LIB_KNOWN_SANE_COMPILER) /**/                                           \
    && defined(__riscv_f) && defined(__riscv_d)                         /**/                                           \
    && GNU_BUILTIN(__builtin_fminf) && GNU_BUILTIN(__builtin_fmin)      /**/                                           \
    && GNU_BUILTIN(__builtin_fmaxf) && GNU_BUILTIN(__builtin_fmax)
#define FPU_LIB_CORRECT_BUILTIN_IEEE754_2008_FMINMAX 1
#endif

// Copying sign via __builtin_copysign() is observably more optimal on x86_64, arm64, riscv
#undef FPU_LIB_OPTIMAL_BUILTIN_COPYSIGN
#if !defined(USE_SOFT_FPU_WRAP) && defined(FPU_LIB_KNOWN_SANE_COMPILER)    /**/                                        \
    && (defined(__x86_64__) || defined(__aarch64__) || defined(__riscv_d)) /**/                                        \
    && GNU_BUILTIN(__builtin_copysign) && GNU_BUILTIN(__builtin_copysignf)
#define FPU_LIB_OPTIMAL_BUILTIN_COPYSIGN 1
#endif

// Fused-multiply-add via __builtin_fma() is optimal on arm64, riscv, powerpc
#undef FPU_LIB_OPTIMAL_BUILTIN_FMA
#if !defined(USE_SOFT_FPU_FENV)                                                                       /**/             \
    && (defined(__aarch64__) || defined(__riscv_d) || defined(__powerpc__) || defined(__powerpc64__)) /**/             \
    && GNU_BUILTIN(__builtin_fmaf) && GNU_BUILTIN(__builtin_fma)
#define FPU_LIB_OPTIMAL_BUILTIN_FMA 1
#endif

/*
 * FPU Environment handling
 *
 * NOTE: Those definitions match RISC-V bitfields :D
 */

// FPU Exception flags
#define FPU_LIB_FLAG_NX         0x01 // Inexact result
#define FPU_LIB_FLAG_UF         0x02 // Underflow
#define FPU_LIB_FLAG_OF         0x04 // Overflow
#define FPU_LIB_FLAG_DZ         0x08 // Division by zero
#define FPU_LIB_FLAG_NV         0x10 // Invalid operation

#define FPU_LIB_FLAGS_ALL       0x1F

// FPU Rounding modes
#define FPU_LIB_ROUND_NE        0x00 // Round to nearest
#define FPU_LIB_ROUND_TZ        0x01 // Round to zero
#define FPU_LIB_ROUND_DN        0x02 // Round down (Towards negative infinity)
#define FPU_LIB_ROUND_UP        0x03 // Round up (Towards positive infinity)
#define FPU_LIB_ROUND_MM        0x04 // Round to nearest, ties to Max Magnitude

// FPU Classification
#define FPU_CLASS_NEG_INF       0x00 // Negative infinity
#define FPU_CLASS_NEG_NORMAL    0x01 // Negative normal
#define FPU_CLASS_NEG_SUBNORMAL 0x02 // Negative subnormal
#define FPU_CLASS_NEG_ZERO      0x03 // Negative zero
#define FPU_CLASS_POS_ZERO      0x04 // Positive zero
#define FPU_CLASS_POS_SUBNORMAL 0x05 // Positive subnormal
#define FPU_CLASS_POS_NORMAL    0x06 // Positive normal
#define FPU_CLASS_POS_INF       0x07 // Positive infinity
#define FPU_CLASS_NAN_SIG       0x08 // Signaling NaN
#define FPU_CLASS_NAN_QUIET     0x09 // Quitet NaN

// Exception handling
uint32_t fpu_get_exceptions(void);
void     fpu_set_exceptions(uint32_t new_exceptions);
void     fpu_raise_exceptions(uint32_t set_exceptions);
void     fpu_clear_exceptions(uint32_t clr_exceptions);

// Rounding mode handling
uint32_t fpu_get_rounding_mode(void);
void     fpu_set_rounding_mode(uint32_t mode);

// Floating-point Classification
slow_path uint32_t fpu_fclass32(fpu_f32_t f);
slow_path uint32_t fpu_fclass64(fpu_f64_t d);

// Internal rounding helpers (Only to be coverted to an integer afterwards)
slow_path fpu_f32_t fpu_round_f32_internal(fpu_f32_t f, uint32_t mode);
slow_path fpu_f64_t fpu_round_f64_internal(fpu_f64_t d, uint32_t mode);

// Unintrusive soft exception handling for inlined paths
slow_path void fpu_raise_inexact(void);
slow_path void fpu_raise_invalid(void);
slow_path void fpu_raise_ovrflow(void);
slow_path void fpu_raise_divzero(void);

#if defined(USE_SOFT_FPU_FENV)
slow_path void fpu_soft_fenv_check_add32(fpu_f32_t sum, fpu_f32_t a, fpu_f32_t b);
slow_path void fpu_soft_fenv_check_add64(fpu_f64_t sum, fpu_f64_t a, fpu_f64_t b);
slow_path void fpu_soft_fenv_check_mul32(fpu_f32_t mul, fpu_f32_t a, fpu_f32_t b, bool is_div);
slow_path void fpu_soft_fenv_check_mul64(fpu_f64_t sum, fpu_f64_t a, fpu_f64_t b, bool is_div);
#endif

#if defined(USE_SOFT_FPU_SQRT)
// NOTE: Must be called with positive input
fpu_f32_t fpu_sqrt32_soft_internal(fpu_f32_t f);
fpu_f64_t fpu_sqrt64_soft_internal(fpu_f64_t d);
#endif

/*
 * Floating-point bitcasts
 */

#define FPU_LIB_FP32_SIGNEDFP_MASK 0x80000000U
#define FPU_LIB_FP32_NOSIGNED_MASK 0x7FFFFFFFU
#define FPU_LIB_FP32_EXPONENT_MASK 0x7F800000U
#define FPU_LIB_FP32_MANTISSA_MASK 0x007FFFFFU
#define FPU_LIB_FP32_QUIETNAN_MASK 0x00400000U

#define FPU_LIB_FP32_POSITIVE_INF  0x7F800000U
#define FPU_LIB_FP32_NEGATIVE_INF  0xFF800000U

#define FPU_LIB_FP64_SIGNEDFP_MASK 0x8000000000000000ULL
#define FPU_LIB_FP64_NOSIGNED_MASK 0x7FFFFFFFFFFFFFFFULL
#define FPU_LIB_FP64_EXPONENT_MASK 0x7FF0000000000000ULL
#define FPU_LIB_FP64_MANTISSA_MASK 0x000FFFFFFFFFFFFFULL
#define FPU_LIB_FP64_QUIETNAN_MASK 0x0008000000000000ULL

#define FPU_LIB_FP64_POSITIVE_INF  0x7FF0000000000000ULL
#define FPU_LIB_FP64_NEGATIVE_INF  0xFFF0000000000000ULL

// Override to assert a different canonical NaN in current translation unit
#ifndef FPU_LIB_FP32_CANONICAL_NAN
#define FPU_LIB_FP32_CANONICAL_NAN 0x7FC00000U
#endif
#ifndef FPU_LIB_FP64_CANONICAL_NAN
#define FPU_LIB_FP64_CANONICAL_NAN 0x7FF8000000000000ULL
#endif

#define FPU_LIB_FP32_NEGATIVE_ZERO 0x80000000U
#define FPU_LIB_FP64_NEGATIVE_ZERO 0x8000000000000000ULL

#define FPU_LIB_FP32_MINIMUM_NORM  0x00800000U
#define FPU_LIB_FP64_MINIMUM_NORM  0x0010000000000000ULL

static forceinline uint16_t fpu_bit_f16_to_u16(fpu_f16_t f)
{
#if defined(USE_SOFT_FPU_ENCAP)
    return f.scalar;
#else
    return f;
#endif
}

static forceinline fpu_f16_t fpu_bit_u16_to_f16(uint16_t u)
{
#if defined(USE_SOFT_FPU_ENCAP)
    fpu_f16_t f = {.scalar = u};
    return f;
#else
    return u;
#endif
}

static forceinline uint32_t fpu_bit_f32_to_u32(fpu_f32_t f)
{
#if defined(USE_SOFT_FPU_WRAP) && defined(USE_SOFT_FPU_ENCAP)
    volatile uint32_t u = f.scalar;
    return u;
#elif defined(USE_SOFT_FPU_WRAP)
    volatile uint32_t u = f;
    return u;
#else
    uint32_t u;
    memcpy(&u, &f, sizeof(f));
    return u;
#endif
}

static forceinline fpu_f32_t fpu_bit_u32_to_f32(uint32_t u)
{
#if defined(USE_SOFT_FPU_WRAP) && defined(USE_SOFT_FPU_ENCAP)
    fpu_f32_t f = {.scalar = u};
    return f;
#elif defined(USE_SOFT_FPU_WRAP)
    return u;
#else
    fpu_f32_t f;
    memcpy(&f, &u, sizeof(f));
    return f;
#endif
}

static forceinline uint64_t fpu_bit_f64_to_u64(fpu_f64_t d)
{
#if defined(USE_SOFT_FPU_WRAP) && defined(USE_SOFT_FPU_ENCAP)
    volatile uint64_t u = d.scalar;
    return u;
#elif defined(USE_SOFT_FPU_WRAP)
    volatile uint64_t u = d;
    return u;
#else
    uint64_t u;
    memcpy(&u, &d, sizeof(d));
    return u;
#endif
}

static forceinline fpu_f64_t fpu_bit_u64_to_f64(uint64_t u)
{
#if defined(USE_SOFT_FPU_WRAP) && defined(USE_SOFT_FPU_ENCAP)
    fpu_f64_t d = {.scalar = u};
    return d;
#elif defined(USE_SOFT_FPU_WRAP)
    return u;
#else
    fpu_f64_t d;
    memcpy(&d, &u, sizeof(d));
    return d;
#endif
}

static forceinline bool fpu_is_bit_equal16(fpu_f16_t a, fpu_f16_t b)
{
    return fpu_bit_f16_to_u16(a) == fpu_bit_f16_to_u16(b);
}

static forceinline bool fpu_is_bit_equal32(fpu_f32_t a, fpu_f32_t b)
{
    return fpu_bit_f32_to_u32(a) == fpu_bit_f32_to_u32(b);
}

static forceinline bool fpu_is_bit_equal64(fpu_f64_t a, fpu_f64_t b)
{
    return fpu_bit_f64_to_u64(a) == fpu_bit_f64_to_u64(b);
}

/*
 * Software wrapping of scalar floating-point
 */

static forceinline actual_float_t fpu_raw_f32(fpu_f32_t f)
{
#if defined(USE_SOFT_FPU_WRAP)
    actual_float_t ret;
    memcpy(&ret, &f, sizeof(f));
    return ret;
#elif defined(USE_SOFT_FPU_ENCAP)
    return f.scalar;
#else
    return f;
#endif
}

static forceinline actual_double_t fpu_raw_f64(fpu_f64_t d)
{
#if defined(USE_SOFT_FPU_WRAP)
    actual_double_t ret;
    memcpy(&ret, &d, sizeof(d));
    return ret;
#elif defined(USE_SOFT_FPU_ENCAP)
    return d.scalar;
#else
    return d;
#endif
}

static forceinline fpu_f32_t fpu_wrap_f32(actual_float_t f)
{
#if defined(USE_SOFT_FPU_WRAP)
    fpu_f32_t ret;
    memcpy(&ret, &f, sizeof(f));
    return ret;
#elif defined(USE_SOFT_FPU_ENCAP)
    fpu_f32_t ret = {.scalar = f};
    return ret;
#else
    return f;
#endif
}

static forceinline fpu_f64_t fpu_wrap_f64(actual_double_t d)
{
#if defined(USE_SOFT_FPU_WRAP)
    fpu_f64_t ret;
    memcpy(&ret, &d, sizeof(d));
    return ret;
#elif defined(USE_SOFT_FPU_ENCAP)
    fpu_f64_t ret = {.scalar = d};
    return ret;
#else
    return d;
#endif
}

/*
 * Fast software floating-point helpers
 */

// Checks for finite non-NaN, non-Inf input, never sets exceptions
static forceinline bool fpu_is_finite32(fpu_f32_t f)
{
    return (fpu_bit_f32_to_u32(f) << 1) < (FPU_LIB_FP32_EXPONENT_MASK << 1);
}

static forceinline bool fpu_is_finite64(fpu_f64_t d)
{
    return (fpu_bit_f64_to_u64(d) << 1) < (FPU_LIB_FP64_EXPONENT_MASK << 1);
}

// Checks for positive, possibly infinite non-NaN input, never sets exceptions
static forceinline bool fpu_is_positive32(fpu_f32_t f)
{
    return fpu_bit_f32_to_u32(f) <= FPU_LIB_FP32_POSITIVE_INF;
}

static forceinline bool fpu_is_positive64(fpu_f64_t d)
{
    return fpu_bit_f64_to_u64(d) <= FPU_LIB_FP64_POSITIVE_INF;
}

// Checks for negative, possibly infinite non-NaN input, never sets exceptions
static forceinline bool fpu_is_negative32(fpu_f32_t f)
{
    int32_t i = (int32_t)fpu_bit_f32_to_u32(f);
    return i <= (int32_t)FPU_LIB_FP32_NEGATIVE_INF;
}

static forceinline bool fpu_is_negative64(fpu_f64_t d)
{
    int64_t i = (int64_t)fpu_bit_f64_to_u64(d);
    return i <= (int64_t)FPU_LIB_FP64_NEGATIVE_INF;
}

// Checks for NaN input, never sets exceptions
static forceinline bool fpu_is_nan32_soft(fpu_f32_t f)
{
    return (fpu_bit_f32_to_u32(f) << 1) > (FPU_LIB_FP32_POSITIVE_INF << 1);
}

static forceinline bool fpu_is_nan64_soft(fpu_f64_t d)
{
    return (fpu_bit_f64_to_u64(d) << 1) > (FPU_LIB_FP64_POSITIVE_INF << 1);
}

// Checks for sNaN input, never sets exceptions
static forceinline bool fpu_is_snan32_soft(fpu_f32_t f)
{
    uint32_t u = fpu_bit_f32_to_u32(f) << 1;
    return (u - ((FPU_LIB_FP32_POSITIVE_INF << 1) + 1)) < ((FPU_LIB_FP32_QUIETNAN_MASK << 1) - 1);
}

static forceinline bool fpu_is_snan64_soft(fpu_f64_t d)
{
    uint64_t u = fpu_bit_f64_to_u64(d) << 1;
    return (u - ((FPU_LIB_FP64_POSITIVE_INF << 1) + 1)) < ((FPU_LIB_FP64_QUIETNAN_MASK << 1) - 1);
}

static forceinline bool fpu_is_zero32_soft(fpu_f32_t f)
{
    return !(fpu_bit_f32_to_u32(f) & FPU_LIB_FP32_NOSIGNED_MASK);
}

static forceinline bool fpu_is_zero64_soft(fpu_f64_t d)
{
    return !(fpu_bit_f64_to_u64(d) & FPU_LIB_FP64_NOSIGNED_MASK);
}

static forceinline bool fpu_is_inf32_soft(fpu_f32_t f)
{
    return (fpu_bit_f32_to_u32(f) & FPU_LIB_FP32_NOSIGNED_MASK) == FPU_LIB_FP32_POSITIVE_INF;
}

static forceinline bool fpu_is_inf64_soft(fpu_f64_t d)
{
    return (fpu_bit_f64_to_u64(d) & FPU_LIB_FP64_NOSIGNED_MASK) == FPU_LIB_FP64_POSITIVE_INF;
}

static forceinline bool fpu_fma32_invalid_soft(fpu_f32_t a, fpu_f32_t b, fpu_f32_t c)
{
    uint32_t ua = fpu_bit_f32_to_u32(a);
    uint32_t ub = fpu_bit_f32_to_u32(b);
    uint32_t uc = fpu_bit_f32_to_u32(c);

    bool a_nan = fpu_is_nan32_soft(a);
    bool b_nan = fpu_is_nan32_soft(b);
    bool a_snan = fpu_is_snan32_soft(a);
    bool b_snan = fpu_is_snan32_soft(b);
    bool c_snan = fpu_is_snan32_soft(c);
    bool a_zero = fpu_is_zero32_soft(a);
    bool b_zero = fpu_is_zero32_soft(b);
    bool a_inf = fpu_is_inf32_soft(a);
    bool b_inf = fpu_is_inf32_soft(b);
    bool c_inf = fpu_is_inf32_soft(c);

    bool inf_zero = (a_inf && b_zero) || (b_inf && a_zero);
    bool product_inf = (a_inf && !b_zero && !b_nan) || (b_inf && !a_zero && !a_nan);
    bool opposite_inf_add = product_inf && c_inf && ((ua ^ ub ^ uc) & FPU_LIB_FP32_SIGNEDFP_MASK);

    return a_snan || b_snan || c_snan || inf_zero || opposite_inf_add;
}

static forceinline bool fpu_fma64_invalid_soft(fpu_f64_t a, fpu_f64_t b, fpu_f64_t c)
{
    uint64_t ua = fpu_bit_f64_to_u64(a);
    uint64_t ub = fpu_bit_f64_to_u64(b);
    uint64_t uc = fpu_bit_f64_to_u64(c);

    bool a_nan = fpu_is_nan64_soft(a);
    bool b_nan = fpu_is_nan64_soft(b);
    bool a_snan = fpu_is_snan64_soft(a);
    bool b_snan = fpu_is_snan64_soft(b);
    bool c_snan = fpu_is_snan64_soft(c);
    bool a_zero = fpu_is_zero64_soft(a);
    bool b_zero = fpu_is_zero64_soft(b);
    bool a_inf = fpu_is_inf64_soft(a);
    bool b_inf = fpu_is_inf64_soft(b);
    bool c_inf = fpu_is_inf64_soft(c);

    bool inf_zero = (a_inf && b_zero) || (b_inf && a_zero);
    bool product_inf = (a_inf && !b_zero && !b_nan) || (b_inf && !a_zero && !a_nan);
    bool opposite_inf_add = product_inf && c_inf && ((ua ^ ub ^ uc) & FPU_LIB_FP64_SIGNEDFP_MASK);

    return a_snan || b_snan || c_snan || inf_zero || opposite_inf_add;
}

// Returns normalized exponent value
static forceinline int32_t fpu_exponent32(fpu_f32_t f)
{
    return ((int32_t)((fpu_bit_f32_to_u32(f) >> 23) & 0xFF)) - 0x7F;
}

static forceinline int32_t fpu_exponent64(fpu_f64_t f)
{
    return ((int32_t)((fpu_bit_f64_to_u64(f) >> 52) & 0x7FF)) - 0x3FF;
}

// Returns true on non-zero fractional part
static forceinline bool fpu_is_fractional32(fpu_f32_t f)
{
    uint32_t u = fpu_bit_f32_to_u32(f);
    if (u << 1) {
        int32_t e = fpu_exponent32(f);
        if (e < 0) {
            return true;
        } else if (e < 23) {
            return !!(u & (FPU_LIB_FP32_MANTISSA_MASK >> e));
        }
    }
    return false;
}

static forceinline bool fpu_is_fractional64(fpu_f64_t d)
{
    uint64_t u = fpu_bit_f64_to_u64(d);
    if (u << 1) {
        int32_t e = fpu_exponent64(d);
        if (e < 0) {
            return true;
        } else if (e < 52) {
            return !!(u & (FPU_LIB_FP64_MANTISSA_MASK >> e));
        }
    }
    return false;
}

/*
 * Floating-point precision error calculation
 *
 * See https://ir.cwi.nl/pub/9159/9159D.pdf
 */

static inline fpu_f32_t fpu_add_error32(fpu_f32_t sum, fpu_f32_t a, fpu_f32_t b)
{
    actual_float_t fs = fpu_raw_f32(sum);
    actual_float_t fa = fpu_raw_f32(a);
    actual_float_t fb = fpu_raw_f32(b);
    actual_float_t fi = fs - fa;
    return fpu_wrap_f32((fb - fi) + (fa - (fs - fi)));
}

static inline fpu_f64_t fpu_add_error64(fpu_f64_t sum, fpu_f64_t a, fpu_f64_t b)
{
    actual_double_t fs = fpu_raw_f64(sum);
    actual_double_t fa = fpu_raw_f64(a);
    actual_double_t fb = fpu_raw_f64(b);
    actual_double_t fi = fs - fa;
    return fpu_wrap_f64((fb - fi) + (fa - (fs - fi)));
}

static inline fpu_f32_t fpu_mul_error32(fpu_f32_t mul, fpu_f32_t a, fpu_f32_t b)
{
    actual_float_t fm = fpu_raw_f32(mul);
    actual_float_t ha = fpu_raw_f32(fpu_bit_u32_to_f32(fpu_bit_f32_to_u32(a) & ~0x7FFU));
    actual_float_t la = fpu_raw_f32(a) - ha;
    actual_float_t hb = fpu_raw_f32(fpu_bit_u32_to_f32(fpu_bit_f32_to_u32(b) & ~0x7FFU));
    actual_float_t lb = fpu_raw_f32(b) - hb;
    return fpu_wrap_f32((((ha * hb - fm) + ha * lb) + la * hb) + la * lb);
}

static inline fpu_f64_t fpu_mul_error64(fpu_f64_t mul, fpu_f64_t a, fpu_f64_t b)
{
    actual_double_t fm = fpu_raw_f64(mul);
    actual_double_t ha = fpu_raw_f64(fpu_bit_u64_to_f64(fpu_bit_f64_to_u64(a) & ~0x7FFFFFFULL));
    actual_double_t la = fpu_raw_f64(a) - ha;
    actual_double_t hb = fpu_raw_f64(fpu_bit_u64_to_f64(fpu_bit_f64_to_u64(b) & ~0x7FFFFFFULL));
    actual_double_t lb = fpu_raw_f64(b) - hb;
    return fpu_wrap_f64((((ha * hb - fm) + ha * lb) + la * hb) + la * lb);
}

/*
 * Floating-point odd rounding against error value for precision fixup
 */

static inline fpu_f32_t fpu_odd_round32(fpu_f32_t val, fpu_f32_t err)
{
    uint32_t uv = fpu_bit_f32_to_u32(val);
    uint32_t ue = fpu_bit_f32_to_u32(err);
    if (ue && !(uv & 1)) {
        if (ue >> 31) {
            return fpu_bit_u32_to_f32(uv - 1);
        } else {
            return fpu_bit_u32_to_f32(uv + 1);
        }
    }
    return val;
}

static inline fpu_f64_t fpu_odd_round64(fpu_f64_t val, fpu_f64_t err)
{
    uint64_t uv = fpu_bit_f64_to_u64(val);
    uint64_t ue = fpu_bit_f64_to_u64(err);
    if (ue && !(uv & 1)) {
        if (ue >> 63) {
            return fpu_bit_u64_to_f64(uv - 1);
        } else {
            return fpu_bit_u64_to_f64(uv + 1);
        }
    }
    return val;
}

/*
 * Floating-point NaN-boxing
 */

static forceinline fpu_f64_t fpu_nanbox_f32(fpu_f32_t f)
{
    return fpu_bit_u64_to_f64(fpu_bit_f32_to_u32(f) | 0xFFFFFFFF00000000ULL);
}

static forceinline fpu_f32_t fpu_unpack_f32_from_f64(fpu_f64_t d)
{
    return fpu_bit_u32_to_f32(fpu_bit_f64_to_u64(d));
}

static forceinline fpu_f32_t fpu_nan_unbox_f32(fpu_f64_t d)
{
    uint64_t u = fpu_bit_f64_to_u64(d);
    if (likely(u >= 0xFFFFFFFF00000000ULL)) {
        return fpu_bit_u32_to_f32(u);
    }
    return fpu_bit_u32_to_f32(FPU_LIB_FP32_CANONICAL_NAN);
}

/*
 * Floating-point load/store intrinsics
 */

TSAN_SUPPRESS static forceinline fpu_f32_t fpu_load_f32_le(const void* addr)
{
#if defined(HOST_LITTLE_ENDIAN)
    return *(const safe_aliasing fpu_f32_t*)addr;
#else
    return fpu_bit_u32_to_f32(read_uint32_le(addr));
#endif
}

TSAN_SUPPRESS static forceinline void fpu_store_f32_le(void* addr, fpu_f32_t f)
{
#if defined(HOST_LITTLE_ENDIAN)
    *(safe_aliasing fpu_f32_t*)addr = f;
#else
    write_uint32_le(addr, fpu_bit_f32_to_u32(f));
#endif
}

TSAN_SUPPRESS static forceinline fpu_f64_t fpu_load_f64_le(const void* addr)
{
#if defined(HOST_LITTLE_ENDIAN)
    return *(const safe_aliasing fpu_f64_t*)addr;
#else
    return fpu_bit_u64_to_f64(read_uint64_le(addr));
#endif
}

TSAN_SUPPRESS static forceinline void fpu_store_f64_le(void* addr, fpu_f64_t d)
{
#if defined(HOST_LITTLE_ENDIAN)
    *(safe_aliasing fpu_f64_t*)addr = d;
#else
    write_uint64_le(addr, fpu_bit_f64_to_u64(d));
#endif
}

/*
 * Floating-point NaN checking
 */

// Those functions raise FE_INVALID on sNaN, and that is commonly needed
static forceinline bool fpu_is_nan32(fpu_f32_t f)
{
#if defined(USE_SOFT_FPU_FENV)
    if (likely(!fpu_is_nan32_soft(f))) {
        return false;
    }
    if (fpu_is_snan32_soft(f)) {
        fpu_raise_invalid();
    }
    return true;
#else
    return fpu_raw_f32(f) != fpu_raw_f32(f);
#endif
}

static forceinline bool fpu_is_nan64(fpu_f64_t d)
{
#if defined(USE_SOFT_FPU_FENV)
    if (likely(!fpu_is_nan64_soft(d))) {
        return false;
    }
    if (fpu_is_snan64_soft(d)) {
        fpu_raise_invalid();
    }
    return true;
#else
    return fpu_raw_f64(d) != fpu_raw_f64(d);
#endif
}

// NOTE: Those functions assume FE_INVALID is already set
static forceinline bool fpu_is_nan32_fast_soft(fpu_f32_t f)
{
#if defined(USE_SOFT_FPU_FENV)
    return fpu_is_nan32_soft(f);
#else
    return fpu_raw_f32(f) != fpu_raw_f32(f);
#endif
}

static forceinline bool fpu_is_nan64_fast_soft(fpu_f64_t d)
{
#if defined(USE_SOFT_FPU_FENV)
    return fpu_is_nan64_soft(d);
#else
    return fpu_raw_f64(d) != fpu_raw_f64(d);
#endif
}

/*
 * Floating-point sign operations
 */

static forceinline bool fpu_signbit32(fpu_f32_t f)
{
    return !!(fpu_bit_f32_to_u32(f) >> 31);
}

static forceinline bool fpu_signbit64(fpu_f64_t d)
{
    return !!(fpu_bit_f64_to_u64(d) >> 63);
}

static forceinline fpu_f32_t fpu_neg32(fpu_f32_t f)
{
#if defined(USE_SOFT_FPU_WRAP)
    return fpu_bit_u32_to_f32(fpu_bit_f32_to_u32(f) ^ FPU_LIB_FP32_SIGNEDFP_MASK);
#else
    return fpu_wrap_f32(-fpu_raw_f32(f));
#endif
}

static forceinline fpu_f64_t fpu_neg64(fpu_f64_t d)
{
#if defined(USE_SOFT_FPU_WRAP)
    return fpu_bit_u64_to_f64(fpu_bit_f64_to_u64(d) ^ FPU_LIB_FP64_SIGNEDFP_MASK);
#else
    return fpu_wrap_f64(-fpu_raw_f64(d));
#endif
}

static forceinline fpu_f32_t fpu_fsgnj32(fpu_f32_t a, fpu_f32_t b)
{
#if defined(FPU_LIB_OPTIMAL_BUILTIN_COPYSIGN)
    return fpu_wrap_f32(__builtin_copysignf(fpu_raw_f32(a), fpu_raw_f32(b)));
#else
    uint32_t ua = fpu_bit_f32_to_u32(a);
    uint32_t ub = fpu_bit_f32_to_u32(b);
    return fpu_bit_u32_to_f32(ua ^ ((ua ^ ub) & FPU_LIB_FP32_SIGNEDFP_MASK));
#endif
}

static forceinline fpu_f64_t fpu_fsgnj64(fpu_f64_t a, fpu_f64_t b)
{
#if defined(FPU_LIB_OPTIMAL_BUILTIN_COPYSIGN)
    return fpu_wrap_f64(__builtin_copysign(fpu_raw_f64(a), fpu_raw_f64(b)));
#else
    uint64_t ua = fpu_bit_f64_to_u64(a);
    uint64_t ub = fpu_bit_f64_to_u64(b);
    return fpu_bit_u64_to_f64(ua ^ ((ua ^ ub) & FPU_LIB_FP64_SIGNEDFP_MASK));
#endif
}

static forceinline fpu_f32_t fpu_fsgnjn32(fpu_f32_t a, fpu_f32_t b)
{
#if defined(FPU_LIB_OPTIMAL_BUILTIN_COPYSIGN)
    return fpu_wrap_f32(__builtin_copysignf(fpu_raw_f32(a), -fpu_raw_f32(b)));
#else
    uint32_t ua = fpu_bit_f32_to_u32(a);
    uint32_t ub = fpu_bit_f32_to_u32(b);
    return fpu_bit_u32_to_f32(ua ^ (~(ua ^ ub) & FPU_LIB_FP32_SIGNEDFP_MASK));
#endif
}

static forceinline fpu_f64_t fpu_fsgnjn64(fpu_f64_t a, fpu_f64_t b)
{
#if defined(FPU_LIB_OPTIMAL_BUILTIN_COPYSIGN)
    return fpu_wrap_f64(__builtin_copysign(fpu_raw_f64(a), -fpu_raw_f64(b)));
#else
    uint64_t ua = fpu_bit_f64_to_u64(a);
    uint64_t ub = fpu_bit_f64_to_u64(b);
    return fpu_bit_u64_to_f64(ua ^ (~(ua ^ ub) & FPU_LIB_FP64_SIGNEDFP_MASK));
#endif
}

static forceinline fpu_f32_t fpu_fsgnjx32(fpu_f32_t a, fpu_f32_t b)
{
    uint32_t ua = fpu_bit_f32_to_u32(a);
    uint32_t ub = fpu_bit_f32_to_u32(b);
    return fpu_bit_u32_to_f32(ua ^ (ub & FPU_LIB_FP32_SIGNEDFP_MASK));
}

static forceinline fpu_f64_t fpu_fsgnjx64(fpu_f64_t a, fpu_f64_t b)
{
    uint64_t ua = fpu_bit_f64_to_u64(a);
    uint64_t ub = fpu_bit_f64_to_u64(b);
    return fpu_bit_u64_to_f64(ua ^ (ub & FPU_LIB_FP64_SIGNEDFP_MASK));
}

/*
 * Floating-point width conversions
 */

static forceinline fpu_f64_t fpu_fcvt_f32_to_f64(fpu_f32_t f)
{
    return fpu_wrap_f64((actual_double_t)fpu_raw_f32(f));
}

static forceinline fpu_f32_t fpu_fcvt_f64_to_f32(fpu_f64_t d)
{
    return fpu_wrap_f32((actual_float_t)fpu_raw_f64(d));
}

/*
 * Floating-point Arithmetic
 */

static forceinline fpu_f32_t fpu_add32(fpu_f32_t a, fpu_f32_t b)
{
    fpu_f32_t sum = fpu_wrap_f32(fpu_raw_f32(a) + fpu_raw_f32(b));
#if defined(USE_SOFT_FPU_FENV)
    fpu_soft_fenv_check_add32(sum, a, b);
#endif
    return sum;
}

static forceinline fpu_f64_t fpu_add64(fpu_f64_t a, fpu_f64_t b)
{
    fpu_f64_t sum = fpu_wrap_f64(fpu_raw_f64(a) + fpu_raw_f64(b));
#if defined(USE_SOFT_FPU_FENV)
    fpu_soft_fenv_check_add64(sum, a, b);
#endif
    return sum;
}

static forceinline fpu_f32_t fpu_sub32(fpu_f32_t a, fpu_f32_t b)
{
    fpu_f32_t sub = fpu_wrap_f32(fpu_raw_f32(a) - fpu_raw_f32(b));
#if defined(USE_SOFT_FPU_FENV)
    fpu_soft_fenv_check_add32(sub, a, fpu_neg32(b));
#endif
    return sub;
}

static forceinline fpu_f64_t fpu_sub64(fpu_f64_t a, fpu_f64_t b)
{
    fpu_f64_t sub = fpu_wrap_f64(fpu_raw_f64(a) - fpu_raw_f64(b));
#if defined(USE_SOFT_FPU_FENV)
    fpu_soft_fenv_check_add64(sub, a, fpu_neg64(b));
#endif
    return sub;
}

static forceinline fpu_f32_t fpu_mul32(fpu_f32_t a, fpu_f32_t b)
{
    fpu_f32_t mul = fpu_wrap_f32(fpu_raw_f32(a) * fpu_raw_f32(b));
#if defined(USE_SOFT_FPU_FENV)
    fpu_soft_fenv_check_mul32(mul, a, b, false);
#endif
    return mul;
}

static forceinline fpu_f64_t fpu_mul64(fpu_f64_t a, fpu_f64_t b)
{
    fpu_f64_t mul = fpu_wrap_f64(fpu_raw_f64(a) * fpu_raw_f64(b));
#if defined(USE_SOFT_FPU_FENV)
    fpu_soft_fenv_check_mul64(mul, a, b, false);
#endif
    return mul;
}

static forceinline fpu_f32_t fpu_div32(fpu_f32_t a, fpu_f32_t b)
{
    fpu_f32_t div = fpu_wrap_f32(fpu_raw_f32(a) / fpu_raw_f32(b));
#if defined(USE_SOFT_FPU_FENV)
    fpu_soft_fenv_check_mul32(a, b, div, true);
#endif
    return div;
}

static forceinline fpu_f64_t fpu_div64(fpu_f64_t a, fpu_f64_t b)
{
    fpu_f64_t div = fpu_wrap_f64(fpu_raw_f64(a) / fpu_raw_f64(b));
#if defined(USE_SOFT_FPU_FENV)
    fpu_soft_fenv_check_mul64(a, b, div, true);
#endif
    return div;
}

/*
 * IEEE (and RISC-V) underflow means tiny *after* rounding: the result, rounded to
 * full precision with an unbounded exponent range, lies below the minimum normal.
 * Some hosts (e.g. aarch64) detect tininess before rounding instead; the two
 * disagree exactly when the result is +/- the minimum normal, so on that boundary
 * the after-rounding verdict is recomputed and the UF flag forced to match.
 * old_exceptions preserves a UF that was already sticky before the op.
 */
static forceinline void fpu_fma32_fixup_uf(fpu_f32_t a, fpu_f32_t b, fpu_f32_t c, uint32_t old_exceptions)
{
    uint32_t exceptions = fpu_get_exceptions();
    // The product is exact in f64, so sum is the exact result rounded once at 53
    // bits; tininess is decided by comparing it in f64 against the mode-dependent
    // magnitude below which the unbounded 24-bit rounding falls under 2^-126:
    // - to-nearest: the midpoint 2^-126 - 2^-151, exclusive (both NE and MM
    //   resolve an exact-midpoint tie up to 2^-126). Only here can sum sit exactly
    //   on the threshold with the true result on either side; the exact TwoSum
    //   error breaks the tie (2Sum is exact in round-to-nearest).
    // - rounding away from zero: the largest 24-bit value below, 2^-126 - 2^-150,
    //   inclusive. sum is rounded away too, so sum <= T already implies the exact
    //   result is <= T.
    // - rounding toward zero: 2^-126 itself, exclusive; likewise sum < T implies
    //   the exact result is < T.
    fpu_f64_t mul  = fpu_mul64(fpu_fcvt_f32_to_f64(a), fpu_fcvt_f32_to_f64(b));
    fpu_f64_t add  = fpu_fcvt_f32_to_f64(c);
    fpu_f64_t sum  = fpu_add64(mul, add);
    uint64_t  bits = fpu_bit_f64_to_u64(sum);
    uint64_t  mag  = bits & FPU_LIB_FP64_NOSIGNED_MASK;
    uint32_t  mode = fpu_get_rounding_mode();
    bool      away = (mode == FPU_LIB_ROUND_UP) == !(bits >> 63);
    bool      tiny;
    if (mode == FPU_LIB_ROUND_NE || mode == FPU_LIB_ROUND_MM) {
        tiny = mag < 0x380FFFFFF0000000ULL; // 2^-126 - 2^-151
        if (mag == 0x380FFFFFF0000000ULL) {
            uint64_t err = fpu_bit_f64_to_u64(fpu_add_error64(sum, mul, add));
            tiny         = (err << 1) != 0 && ((err ^ bits) >> 63); // exact result below the midpoint
        }
    } else if ((mode == FPU_LIB_ROUND_UP || mode == FPU_LIB_ROUND_DN) && away) {
        tiny = mag <= 0x380FFFFFE0000000ULL; // 2^-126 - 2^-150
    } else {
        tiny = mag < 0x3810000000000000ULL; // 2^-126
    }
    if (tiny) {
        exceptions |= FPU_LIB_FLAG_UF;
    } else {
        exceptions = (exceptions & ~FPU_LIB_FLAG_UF) | (old_exceptions & FPU_LIB_FLAG_UF);
    }
    fpu_set_exceptions(exceptions);
}

static forceinline func_opt_size fpu_f32_t fpu_fma32(fpu_f32_t a, fpu_f32_t b, fpu_f32_t c)
{
    uint32_t old_exceptions = fpu_get_exceptions();
    bool invalid = fpu_fma32_invalid_soft(a, b, c);

#if defined(FPU_LIB_OPTIMAL_BUILTIN_FMA)
    fpu_f32_t ret = fpu_wrap_f32(__builtin_fmaf(fpu_raw_f32(a), fpu_raw_f32(b), fpu_raw_f32(c)));
#else
    fpu_f64_t mul = fpu_mul64(fpu_fcvt_f32_to_f64(a), fpu_fcvt_f32_to_f64(b));
    fpu_f64_t add = fpu_fcvt_f32_to_f64(c);
    fpu_f64_t sum = fpu_add64(mul, add);
    fpu_f64_t err = fpu_add_error64(sum, mul, add);
    fpu_f64_t res = fpu_odd_round64(sum, err);
    fpu_f32_t ret = fpu_fcvt_f64_to_f32(res);
#if defined(USE_SOFT_FPU_FENV)
    fpu_soft_fenv_check_add64(fpu_fcvt_f32_to_f64(ret), sum, err);
#endif
#endif

    if (unlikely((fpu_bit_f32_to_u32(ret) & FPU_LIB_FP32_NOSIGNED_MASK) == FPU_LIB_FP32_MINIMUM_NORM)) {
        fpu_fma32_fixup_uf(a, b, c, old_exceptions);
    }

    uint32_t exceptions = fpu_get_exceptions();
    if (invalid) {
        fpu_raise_invalid();
    } else if ((exceptions & ~old_exceptions) & FPU_LIB_FLAG_NV) {
        fpu_set_exceptions((exceptions & ~FPU_LIB_FLAG_NV) | (old_exceptions & FPU_LIB_FLAG_NV));
    }

    return ret;
}

// The bare fused op: no soft NV check, no flag fixups
static forceinline fpu_f64_t fpu_fma64_raw(fpu_f64_t a, fpu_f64_t b, fpu_f64_t c)
{
#if defined(FPU_LIB_OPTIMAL_BUILTIN_FMA)
    return fpu_wrap_f64(__builtin_fma(fpu_raw_f64(a), fpu_raw_f64(b), fpu_raw_f64(c)));
#else
    fpu_f64_t mul = fpu_mul64(a, b);
    fpu_f64_t e_m = fpu_mul_error64(mul, a, b);
    fpu_f64_t sum = fpu_add64(mul, c);
    fpu_f64_t e_s = fpu_add_error64(sum, mul, c);
    fpu_f64_t e_f = fpu_add64(e_m, e_s);
    fpu_f64_t err = fpu_odd_round64(e_f, fpu_add_error64(e_f, e_s, e_m));
    return fpu_add64(sum, err);
#endif
}

/*
 * f64 counterpart of fpu_fma32_fixup_uf: no wider type exists, so rescale the op
 * by 2^52 into the normal range, where the single fused rounding is already
 * unbounded-equivalent, and test the scaled result against 2^-970.
 *
 * The scaling is safe: the result rounds to +/-2^-1022, so at least one of a*b, c
 * has a set bit at 2^-1022 or below (else the sum is either 0 or larger). If it is
 * in c, |c| <= 2^-969 and by triangle inequality |a*b| <= 2^-968; if it is in the
 * exact product (up to 106 bits wide), |a*b| <= 2^-916 and |c| <= 2^-915. Either
 * way |c|*2^52 is tiny and, for b != 0, |b| >= 2^-1074 gives |a| <= 2^158, so
 * a*2^52 cannot overflow. If b == 0 then a is unbounded and a*2^52 may overflow to
 * inf, making rs NaN -- which correctly reads as "not tiny", since a result of
 * exactly +/-2^-1022 from b == 0 is the exact value of c, and exact never
 * underflows.
 */
static forceinline void fpu_fma64_fixup_uf(fpu_f64_t a, fpu_f64_t b, fpu_f64_t c, uint32_t old_exceptions)
{
    uint32_t  exceptions = fpu_get_exceptions();
    fpu_f64_t scale      = fpu_bit_u64_to_f64(0x4330000000000000ULL); // 2^52
    fpu_f64_t rs         = fpu_fma64_raw(fpu_mul64(a, scale), b, fpu_mul64(c, scale));
    bool      tiny       = (fpu_bit_f64_to_u64(rs) & FPU_LIB_FP64_NOSIGNED_MASK) < 0x0350000000000000ULL; // 2^-970
    if (tiny) {
        exceptions |= FPU_LIB_FLAG_UF;
    } else {
        exceptions = (exceptions & ~FPU_LIB_FLAG_UF) | (old_exceptions & FPU_LIB_FLAG_UF);
    }
    fpu_set_exceptions(exceptions);
}

static forceinline fpu_f64_t fpu_fma64(fpu_f64_t a, fpu_f64_t b, fpu_f64_t c)
{
    uint32_t old_exceptions = fpu_get_exceptions();
    bool     invalid        = fpu_fma64_invalid_soft(a, b, c);

    fpu_f64_t ret = fpu_fma64_raw(a, b, c);

    if (unlikely((fpu_bit_f64_to_u64(ret) & FPU_LIB_FP64_NOSIGNED_MASK) == FPU_LIB_FP64_MINIMUM_NORM)) {
        fpu_fma64_fixup_uf(a, b, c, old_exceptions);
    }

    uint32_t exceptions = fpu_get_exceptions();
    if (invalid) {
        fpu_raise_invalid();
    } else if ((exceptions & ~old_exceptions) & FPU_LIB_FLAG_NV) {
        fpu_set_exceptions((exceptions & ~FPU_LIB_FLAG_NV) | (old_exceptions & FPU_LIB_FLAG_NV));
    }

    return ret;
}

static forceinline fpu_f32_t fpu_sqrt32(fpu_f32_t f)
{
    if (likely(fpu_is_positive32(f))) {
#if defined(USE_SOFT_FPU_SQRT)
        fpu_f32_t ret = fpu_sqrt32_soft_internal(f);
#else
        fpu_f32_t ret = fpu_wrap_f32(fpu_sqrtf_internal(fpu_raw_f32(f)));
#endif
#if defined(USE_SOFT_FPU_FENV)
        fpu_soft_fenv_check_mul32(f, ret, ret, false);
#endif
        return ret;
    }
    if (fpu_is_negative32(f) && fpu_bit_f32_to_u32(f) != FPU_LIB_FP32_NEGATIVE_ZERO) {
        // Negative non-zero: raise invalid, return canonical NaN
        fpu_raise_invalid();
        return fpu_bit_u32_to_f32(FPU_LIB_FP32_CANONICAL_NAN);
    }
    // NaN propagation, and sqrt(-0.0) == -0.0 (no exception)
    return f;
}

static forceinline fpu_f64_t fpu_sqrt64(fpu_f64_t d)
{
    if (likely(fpu_is_positive64(d))) {
#if defined(USE_SOFT_FPU_SQRT)
        fpu_f64_t ret = fpu_sqrt64_soft_internal(d);
#else
        fpu_f64_t ret = fpu_wrap_f64(fpu_sqrt_internal(fpu_raw_f64(d)));
#endif
#if defined(USE_SOFT_FPU_FENV)
        fpu_soft_fenv_check_mul64(d, ret, ret, false);
#endif
        return ret;
    }
    if (fpu_is_negative64(d) && fpu_bit_f64_to_u64(d) != FPU_LIB_FP64_NEGATIVE_ZERO) {
        // Negative non-zero: raise invalid, return canonical NaN
        fpu_raise_invalid();
        return fpu_bit_u64_to_f64(FPU_LIB_FP64_CANONICAL_NAN);
    }
    // NaN propagation, and sqrt(-0.0) == -0.0 (no exception)
    return d;
}

/*
 * Integer to floating-point conversions
 */

static forceinline fpu_f32_t fpu_fcvt_u32_to_f32(uint32_t u)
{
#if !defined(FPU_LIB_CORRECT_CONVERSION_UINT_FP)
    // Use i64->f32 conversion for correct >0x80000000 values handling
    return fpu_wrap_f32((actual_float_t)(int64_t)u);
#else
    return fpu_wrap_f32((actual_float_t)u);
#endif
}

static forceinline fpu_f64_t fpu_fcvt_u32_to_f64(uint32_t u)
{
#if !defined(FPU_LIB_CORRECT_CONVERSION_UINT_FP)
    // Use i64->f64 conversion for correct >0x80000000 values handling
    return fpu_wrap_f64((actual_double_t)(int64_t)u);
#else
    return fpu_wrap_f64((actual_double_t)u);
#endif
}

static forceinline fpu_f32_t fpu_fcvt_i32_to_f32(int32_t i)
{
    return fpu_wrap_f32((actual_float_t)i);
}

static forceinline fpu_f64_t fpu_fcvt_i32_to_f64(int32_t i)
{
    return fpu_wrap_f64((actual_double_t)i);
}

static forceinline fpu_f32_t fpu_fcvt_u64_to_f32(uint64_t u)
{
#if !defined(FPU_LIB_CORRECT_CONVERSION_UINT_FP)
    if (likely(!(u >> 63))) {
        return fpu_wrap_f32((actual_float_t)(int64_t)u);
    }
    // Lower conversion from u64 as halved, evenly rounded i64, multiply back
    return fpu_wrap_f32(((actual_float_t)(int64_t)((u >> 1) | (u & 1))) * 2.f);
#else
    return fpu_wrap_f32((actual_float_t)u);
#endif
}

static forceinline fpu_f64_t fpu_fcvt_u64_to_f64(uint64_t u)
{
#if !defined(FPU_LIB_CORRECT_CONVERSION_UINT_FP)
    if (likely(!(u >> 63))) {
        return fpu_wrap_f64((actual_double_t)(int64_t)u);
    }
    // Lower conversion from u64 as halved, evenly rounded i64, multiply back
    return fpu_wrap_f64(((actual_double_t)(int64_t)((u >> 1) | (u & 1))) * 2.0);
#else
    return fpu_wrap_f64((actual_double_t)u);
#endif
}

static forceinline fpu_f32_t fpu_fcvt_i64_to_f32(int64_t i)
{
    return fpu_wrap_f32((actual_float_t)i);
}

static forceinline fpu_f64_t fpu_fcvt_i64_to_f64(int64_t i)
{
    return fpu_wrap_f64((actual_double_t)i);
}

static forceinline fpu_f64_t fpu_round_i64_to_f64(int64_t i, uint32_t rm)
{
    bool sign = i < 0;
    uint64_t mag = sign ? (0 - (uint64_t)i) : (uint64_t)i;

    if (unlikely(!mag)) {
        return fpu_bit_u64_to_f64(0);
    }

    if (unlikely(rm > FPU_LIB_ROUND_MM)) {
        rm = fpu_get_rounding_mode();
    }

    uint32_t exp = 0;
    uint64_t tmp = mag;
    while (tmp >>= 1) {
        ++exp;
    }

    uint64_t sig = 0;

    if (likely(exp <= 52)) {
        sig = mag << (52 - exp);
    } else {
        uint32_t shift = exp - 52;
        uint64_t mask = (1ULL << shift) - 1;
        uint64_t rem = mag & mask;
        uint64_t half = 1ULL << (shift - 1);

        sig = mag >> shift;

        bool increment = false;
        switch (rm) {
            case FPU_LIB_ROUND_NE:
                increment = (rem > half) || ((rem == half) && (sig & 1));
                break;
            case FPU_LIB_ROUND_TZ:
                increment = false;
                break;
            case FPU_LIB_ROUND_DN:
                increment = sign && rem;
                break;
            case FPU_LIB_ROUND_UP:
                increment = !sign && rem;
                break;
            case FPU_LIB_ROUND_MM:
                increment = rem >= half;
                break;
        }

        if (unlikely(rem)) {
            fpu_raise_inexact();
        }

        if (increment) {
            ++sig;
            if (unlikely(sig == (1ULL << 53))) {
                sig >>= 1;
                ++exp;
            }
        }
    }

    uint64_t bits = (sign ? FPU_LIB_FP64_SIGNEDFP_MASK : 0) | (((uint64_t)exp + 0x3FF) << 52) | (sig & FPU_LIB_FP64_MANTISSA_MASK);

    return fpu_bit_u64_to_f64(bits);
}

/*
 * Check whether floating-point value fits an integer type, never raises exceptions
 */

static forceinline bool fpu_f32_fits_u32(fpu_f32_t f)
{
    uint32_t u = fpu_bit_f32_to_u32(f);
    return u < 0x4F800000U || ((int32_t)u) < ((int32_t)0xBF800000U);
}

static forceinline bool fpu_f64_fits_u32(fpu_f64_t d)
{
    uint64_t u = fpu_bit_f64_to_u64(d);
    return u < 0x41F0000000000000ULL || ((int64_t)u) < ((int64_t)0xBFF0000000000000ULL);
}

static forceinline bool fpu_f32_fits_i32(fpu_f32_t f)
{
    uint32_t u = fpu_bit_f32_to_u32(f);
    return (bit_rotl32(u, 1) ^ 1) <= 0x9E000000U; // |f| < 2^31 or f == -2^31
}

static forceinline bool fpu_f64_fits_i32(fpu_f64_t d)
{
    uint64_t u = fpu_bit_f64_to_u64(d);
    return (bit_rotl64(u, 1) ^ 1) <= 0x83C0000000000000ULL; // |d| < 2^31 or d == -2^31
}

static forceinline bool fpu_f32_fits_u64(fpu_f32_t f)
{
    uint32_t u = fpu_bit_f32_to_u32(f);
    return u < 0x5F800000U || ((int32_t)u) < ((int32_t)0xBF800000U);
}

static forceinline bool fpu_f64_fits_u64(fpu_f64_t d)
{
    uint64_t u = fpu_bit_f64_to_u64(d);
    return u < 0x43F0000000000000ULL || ((int64_t)u) < ((int64_t)0xBFF0000000000000ULL);
}

static forceinline bool fpu_f32_fits_i64(fpu_f32_t f)
{
    uint32_t u = fpu_bit_f32_to_u32(f);
    return (bit_rotl32(u, 1) ^ 1) <= 0xBE000000U; // |f| < 2^63 or f == -2^63
}

static forceinline bool fpu_f64_fits_i64(fpu_f64_t d)
{
    uint64_t u = fpu_bit_f64_to_u64(d);
    return (bit_rotl64(u, 1) ^ 1) <= 0x87C0000000000000ULL; // |d| < 2^63 or d == -2^63
}

/*
 * Floating-point to integer conversions
 *
 * - Converting a value outside output range raises invalid flag & saturates the output
 * - Converting a fractional value raises inexact flag
 * - NaN is treated like positive infinity
 */

// Float to integer
static forceinline uint32_t fpu_fcvt_f32_to_u32(fpu_f32_t f)
{
    if (likely(fpu_f32_fits_u32(f))) {
#if defined(USE_SOFT_FPU_FENV) || !defined(FPU_LIB_CORRECT_CONVERSION_FP_UINT)
        uint32_t ret = (int64_t)fpu_raw_f32(f);
        if (unlikely(!fpu_is_zero32_soft(f) && !fpu_is_bit_equal32(f, fpu_fcvt_u32_to_f32(ret)))) {
            fpu_raise_inexact();
        }
        return ret;
#else
        return (uint32_t)fpu_raw_f32(f);
#endif
    }
    fpu_raise_invalid();
    if (fpu_is_negative32(f)) {
        return 0;
    }
    return -1;
}

static forceinline uint32_t fpu_fcvt_f64_to_u32(fpu_f64_t d)
{
    if (likely(fpu_f64_fits_u32(d))) {
#if defined(USE_SOFT_FPU_FENV) || !defined(FPU_LIB_CORRECT_CONVERSION_FP_UINT)
        uint32_t ret = (int64_t)fpu_raw_f64(d);
        if (unlikely(!fpu_is_zero64_soft(d) && !fpu_is_bit_equal64(d, fpu_fcvt_u32_to_f64(ret)))) {
            fpu_raise_inexact();
        }
        return ret;
#else
        return (uint32_t)fpu_raw_f64(d);
#endif
    }
    fpu_raise_invalid();
    if (fpu_is_negative64(d)) {
        return 0;
    }
    return -1;
}

static forceinline int32_t fpu_fcvt_f32_to_i32(fpu_f32_t f)
{
    if (likely(fpu_f32_fits_i32(f))) {
        int32_t ret = (int32_t)fpu_raw_f32(f);
#if defined(USE_SOFT_FPU_FENV)
        if (unlikely(!fpu_is_zero32_soft(f) && !fpu_is_bit_equal32(f, fpu_fcvt_i32_to_f32(ret)))) {
            fpu_raise_inexact();
        }
#endif
        return ret;
    }
    fpu_raise_invalid();
    if (fpu_is_negative32(f)) {
        return 0x80000000U;
    }
    return 0x7FFFFFFFU;
}

static forceinline int32_t fpu_fcvt_f64_to_i32(fpu_f64_t d)
{
    if (likely(fpu_f64_fits_i32(d))) {
        int32_t ret = (int32_t)fpu_raw_f64(d);
#if defined(USE_SOFT_FPU_FENV)
        if (unlikely(!fpu_is_zero64_soft(d) && !fpu_is_bit_equal64(d, fpu_fcvt_i32_to_f64(ret)))) {
            fpu_raise_inexact();
        }
#endif
        return ret;
    }
    fpu_raise_invalid();
    if (fpu_is_negative64(d)) {
        return 0x80000000U;
    }
    return 0x7FFFFFFFU;
}

static forceinline int32_t fpu_fcvtmod_f64_to_i32(fpu_f64_t d)
{
    /*
     * fcvtmod.w.d (Zfa): convert a double to int32 with round-towards-zero,
     * returning the low 32 bits of the rounded two's complement result
     * (modulo 2^32, no saturation). NaN and infinity convert to 0.
     * Exception flags match fcvt.w.d: NV on NaN/inf/overflow, NX on
     * inexact rounding.
     */
    const uint64_t u    = fpu_bit_f64_to_u64(d);
    const uint64_t sign = u >> 63;
    const uint64_t exp  = (u >> 52) & 0x7FF;
    const uint64_t frac = u & 0xFFFFFFFFFFFFFULL;

    if (unlikely(exp == 0x7FF)) {
        // NaN or infinity: result is 0, raise NV
        fpu_raise_invalid();
        return 0;
    }
    if (unlikely(exp == 0)) {
        // Zero or denormal: |d| < 2^-1022, RTZ gives 0
        if (unlikely(frac != 0)) {
            fpu_raise_inexact();
        }
        return 0;
    }

    const int64_t e = (int64_t)exp - 1023;      // unbiased exponent
    const uint64_t m = frac | (1ULL << 52);     // implicit leading bit
    uint64_t r;
    bool overflow = false;
    bool inexact  = false;

    if (e <= 52) {
        if (e >= 0) {
            const int sh = 52 - (int)e;
            r = m >> sh;
            if (unlikely(m & ((1ULL << sh) - 1))) {
                inexact = true;
            }
        } else {
            // |d| < 1.0: RTZ gives 0
            r = 0;
            inexact = true;
        }
        if (e < 31) {
            // Result fits in int32
        } else if (e == 31) {
            // Only INT32_MIN is exactly representable
            overflow = !(sign && r == (1ULL << 31));
        } else {
            overflow = true;
        }
    } else {
        // |d| >= 2^53: already an integer, may still have bits to return
        const int sh = (int)e - 52;
        r = (sh < 64) ? (m << sh) : 0;
        overflow = true;
    }

    if (sign) {
        r = 0 - r;
    }

    if (unlikely(overflow)) {
        fpu_raise_invalid();
    } else if (unlikely(inexact)) {
        fpu_raise_inexact();
    }
    return (int32_t)r;
}

static forceinline uint64_t fpu_fcvt_f32_to_u64(fpu_f32_t f)
{
    if (likely(fpu_f32_fits_u64(f))) {
#if defined(USE_SOFT_FPU_FENV) || !defined(FPU_LIB_CORRECT_CONVERSION_FP_UINT)
        uint64_t ret;
        if (likely(fpu_f32_fits_i64(f))) {
            ret = (int64_t)fpu_raw_f32(f);
        } else {
            // Emulate conversion to u64 in software
            ret = (((uint64_t)fpu_bit_f32_to_u32(f)) << 40) - 0x8000000000000000ULL;
        }
        if (unlikely(!fpu_is_zero32_soft(f) && !fpu_is_bit_equal32(f, fpu_fcvt_u64_to_f32(ret)))) {
            fpu_raise_inexact();
        }
        return ret;
#else
        return (uint64_t)fpu_raw_f32(f);
#endif
    }
    fpu_raise_invalid();
    if (fpu_is_negative32(f)) {
        return 0;
    }
    return -1;
}

static forceinline uint64_t fpu_fcvt_f64_to_u64(fpu_f64_t d)
{
    if (likely(fpu_f64_fits_u64(d))) {
#if defined(USE_SOFT_FPU_FENV) || !defined(FPU_LIB_CORRECT_CONVERSION_FP_UINT)
        uint64_t ret;
        if (likely(fpu_f64_fits_i64(d))) {
            ret = (int64_t)fpu_raw_f64(d);
        } else {
            // Emulate conversion to u64 in software
            ret = (fpu_bit_f64_to_u64(d) << 11) - 0x8000000000000000ULL;
        }
        if (unlikely(!fpu_is_zero64_soft(d) && !fpu_is_bit_equal64(d, fpu_fcvt_u64_to_f64(ret)))) {
            fpu_raise_inexact();
        }
        return ret;
#else
        return (uint64_t)fpu_raw_f64(d);
#endif
    }
    fpu_raise_invalid();
    if (fpu_is_negative64(d)) {
        return 0;
    }
    return -1;
}

static forceinline int64_t fpu_fcvt_f32_to_i64(fpu_f32_t f)
{
    if (likely(fpu_f32_fits_i64(f))) {
        int64_t ret = (int64_t)fpu_raw_f32(f);
#if defined(USE_SOFT_FPU_FENV) || !defined(FPU_LIB_CORRECT_CONVERSION_FP_LONG)
        if (unlikely(!fpu_is_zero32_soft(f) && !fpu_is_bit_equal32(f, fpu_fcvt_i64_to_f32(ret)))) {
            fpu_raise_inexact();
        }
#endif
        return ret;
    }
    fpu_raise_invalid();
    if (fpu_is_negative32(f)) {
        return 0x8000000000000000ULL;
    }
    return 0x7FFFFFFFFFFFFFFFULL;
}

static forceinline int64_t fpu_fcvt_f64_to_i64(fpu_f64_t d)
{
    if (likely(fpu_f64_fits_i64(d))) {
        int64_t ret = (int64_t)fpu_raw_f64(d);
#if defined(USE_SOFT_FPU_FENV) || !defined(FPU_LIB_CORRECT_CONVERSION_FP_LONG)
        if (unlikely(!fpu_is_zero64_soft(d) && !fpu_is_bit_equal64(d, fpu_fcvt_i64_to_f64(ret)))) {
            fpu_raise_inexact();
        }
#endif
        return ret;
    }
    fpu_raise_invalid();
    if (fpu_is_negative64(d)) {
        return 0x8000000000000000ULL;
    }
    return 0x7FFFFFFFFFFFFFFFULL;
}

/*
 * Floating-point to integer rounding
 */

static forceinline uint32_t fpu_round_f32_to_u32(fpu_f32_t f, uint32_t mode)
{
    if (unlikely(fpu_is_zero32_soft(f))) {
        return 0;
    }

    fpu_f32_t rounded = f;

    if (likely(mode != FPU_LIB_ROUND_TZ)) {
        rounded = fpu_round_f32_internal(f, mode);
    }

    uint32_t ret = fpu_fcvt_f32_to_u32(rounded);

    if (likely(!(fpu_get_exceptions() & FPU_LIB_FLAG_NV))) {
        if (unlikely(!fpu_is_bit_equal32(f, fpu_fcvt_u32_to_f32(ret)))) {
            fpu_raise_inexact();
        }
    }

    return ret;
}

static forceinline uint32_t fpu_round_f64_to_u32(fpu_f64_t d, uint32_t mode)
{
    if (unlikely(fpu_is_zero64_soft(d))) {
        return 0;
    }

    fpu_f64_t rounded = d;

    if (likely(mode != FPU_LIB_ROUND_TZ)) {
        rounded = fpu_round_f64_internal(d, mode);
    }

    uint32_t ret = fpu_fcvt_f64_to_u32(rounded);

    if (likely(!(fpu_get_exceptions() & FPU_LIB_FLAG_NV))) {
        if (unlikely(!fpu_is_bit_equal64(d, fpu_fcvt_u32_to_f64(ret)))) {
            fpu_raise_inexact();
        }
    }

    return ret;
}

static forceinline int32_t fpu_round_f32_to_i32(fpu_f32_t f, uint32_t mode)
{
    if (unlikely(fpu_is_zero32_soft(f))) {
        return 0;
    }

    fpu_f32_t rounded = f;

    if (likely(mode != FPU_LIB_ROUND_TZ)) {
        rounded = fpu_round_f32_internal(f, mode);
    }

    int32_t ret = fpu_fcvt_f32_to_i32(rounded);

    if (likely(!(fpu_get_exceptions() & FPU_LIB_FLAG_NV))) {
        if (unlikely(!fpu_is_bit_equal32(f, fpu_fcvt_i32_to_f32(ret)))) {
            fpu_raise_inexact();
        }
    }

    return ret;
}

static forceinline int32_t fpu_round_f64_to_i32(fpu_f64_t d, uint32_t mode)
{
    if (unlikely(fpu_is_zero64_soft(d))) {
        return 0;
    }

    fpu_f64_t rounded = d;

    if (likely(mode != FPU_LIB_ROUND_TZ)) {
        rounded = fpu_round_f64_internal(d, mode);
    }

    int32_t ret = fpu_fcvt_f64_to_i32(rounded);

    if (likely(!(fpu_get_exceptions() & FPU_LIB_FLAG_NV))) {
        if (unlikely(!fpu_is_bit_equal64(d, fpu_fcvt_i32_to_f64(ret)))) {
            fpu_raise_inexact();
        }
    }

    return ret;
}

static forceinline uint64_t fpu_round_f32_to_u64(fpu_f32_t f, uint32_t mode)
{
    if (unlikely(fpu_is_zero32_soft(f))) {
        return 0;
    }

    fpu_f32_t rounded = f;

    if (likely(mode != FPU_LIB_ROUND_TZ)) {
        rounded = fpu_round_f32_internal(f, mode);
    }

    uint64_t ret = fpu_fcvt_f32_to_u64(rounded);

    if (likely(!(fpu_get_exceptions() & FPU_LIB_FLAG_NV))) {
        if (unlikely(!fpu_is_bit_equal32(f, fpu_fcvt_u64_to_f32(ret)))) {
            fpu_raise_inexact();
        }
    }

    return ret;
}

static forceinline uint64_t fpu_round_f64_to_u64(fpu_f64_t d, uint32_t mode)
{
    if (unlikely(fpu_is_zero64_soft(d))) {
        return 0;
    }

    fpu_f64_t rounded = d;

    if (likely(mode != FPU_LIB_ROUND_TZ)) {
        rounded = fpu_round_f64_internal(d, mode);
    }

    uint64_t ret = fpu_fcvt_f64_to_u64(rounded);

    if (likely(!(fpu_get_exceptions() & FPU_LIB_FLAG_NV))) {
        if (unlikely(!fpu_is_bit_equal64(d, fpu_fcvt_u64_to_f64(ret)))) {
            fpu_raise_inexact();
        }
    }

    return ret;
}

static forceinline int64_t fpu_round_f32_to_i64(fpu_f32_t f, uint32_t mode)
{
    if (unlikely(fpu_is_zero32_soft(f))) {
        return 0;
    }

    fpu_f32_t rounded = f;

    if (likely(mode != FPU_LIB_ROUND_TZ)) {
        rounded = fpu_round_f32_internal(f, mode);
    }

    int64_t ret = fpu_fcvt_f32_to_i64(rounded);

    if (likely(!(fpu_get_exceptions() & FPU_LIB_FLAG_NV))) {
        if (unlikely(!fpu_is_bit_equal32(f, fpu_fcvt_i64_to_f32(ret)))) {
            fpu_raise_inexact();
        }
    }

    return ret;
}

static forceinline int64_t fpu_round_f64_to_i64(fpu_f64_t d, uint32_t mode)
{
    if (unlikely(fpu_is_zero64_soft(d))) {
        return 0;
    }

    fpu_f64_t rounded = d;

    if (likely(mode != FPU_LIB_ROUND_TZ)) {
        rounded = fpu_round_f64_internal(d, mode);
    }

    int64_t ret = fpu_fcvt_f64_to_i64(rounded);

    if (likely(!(fpu_get_exceptions() & FPU_LIB_FLAG_NV))) {
        if (unlikely(!fpu_is_bit_equal64(d, fpu_fcvt_i64_to_f64(ret)))) {
            fpu_raise_inexact();
        }
    }

    return ret;
}

/*
 * IEEE 754 Signaling Comparisons
 *
 * - If at least one operand is a NaN, the result is false, and invalid operation exception flag is set
 * - Negative zero -0.0 is not distinguished from 0.0
 */

// Comparison: Less than (<)
static forceinline bool fpu_is_flt32_sig(fpu_f32_t a, fpu_f32_t b)
{
#if defined(FPU_LIB_CORRECT_SIGNALING_COMPARE)
    return fpu_raw_f32(a) < fpu_raw_f32(b);
#else
    if (fpu_raw_f32(a) < fpu_raw_f32(b)) {
        return true;
    } else if (likely(fpu_raw_f32(b) <= fpu_raw_f32(a))) {
        return false;
    }
    fpu_raise_invalid();
    return false;
#endif
}

static forceinline bool fpu_is_flt64_sig(fpu_f64_t a, fpu_f64_t b)
{
#if defined(FPU_LIB_CORRECT_SIGNALING_COMPARE)
    return fpu_raw_f64(a) < fpu_raw_f64(b);
#else
    if (fpu_raw_f64(a) < fpu_raw_f64(b)) {
        return true;
    } else if (likely(fpu_raw_f64(b) <= fpu_raw_f64(a))) {
        return false;
    }
    fpu_raise_invalid();
    return false;
#endif
}

// Comparison: Less than or equal (<=)
static forceinline bool fpu_is_fle32_sig(fpu_f32_t a, fpu_f32_t b)
{
#if defined(FPU_LIB_CORRECT_SIGNALING_COMPARE)
    return fpu_raw_f32(a) <= fpu_raw_f32(b);
#else
    if (fpu_raw_f32(a) <= fpu_raw_f32(b)) {
        return true;
    } else if (likely(fpu_raw_f32(b) < fpu_raw_f32(a))) {
        return false;
    }
    fpu_raise_invalid();
    return false;
#endif
}

static forceinline bool fpu_is_fle64_sig(fpu_f64_t a, fpu_f64_t b)
{
#if defined(FPU_LIB_CORRECT_SIGNALING_COMPARE)
    return fpu_raw_f64(a) <= fpu_raw_f64(b);
#else
    if (fpu_raw_f64(a) <= fpu_raw_f64(b)) {
        return true;
    } else if (likely(fpu_raw_f64(b) < fpu_raw_f64(a))) {
        return false;
    }
    fpu_raise_invalid();
    return false;
#endif
}

/*
 * IEEE 754 Quiet Comparisons
 *
 * - If at least one operand is a NaN, the result is false
 * - If at least one operand is a signaling NaN, the invalid operation exception flag is set
 * - Infinity is handled as being larger than anything finite, -Inf < any non-Nan, etc
 * - Negative zero -0.0 is not distinguished from 0.0
 */

// Comparison: Equal (a == b)
static forceinline bool fpu_is_equal32_quiet(fpu_f32_t a, fpu_f32_t b)
{
#if defined(USE_SOFT_FPU_FENV)
    if (likely(fpu_raw_f32(a) == fpu_raw_f32(b))) {
        return true;
    }
    if (fpu_is_snan32_soft(a) || fpu_is_snan32_soft(b)) {
        fpu_raise_invalid();
    }
    return false;
#else
    return fpu_raw_f32(a) == fpu_raw_f32(b);
#endif
}

static forceinline bool fpu_is_equal64_quiet(fpu_f64_t a, fpu_f64_t b)
{
#if defined(USE_SOFT_FPU_FENV)
    if (likely(fpu_raw_f64(a) == fpu_raw_f64(b))) {
        return true;
    }
    if (fpu_is_snan64_soft(a) || fpu_is_snan64_soft(b)) {
        fpu_raise_invalid();
    }
    return false;
#else
    return fpu_raw_f64(a) == fpu_raw_f64(b);
#endif
}

// Comparison: Less than (a < b)
static forceinline bool fpu_is_flt32_quiet(fpu_f32_t a, fpu_f32_t b)
{
#if defined(FPU_LIB_CORRECT_BUILTIN_QUIET_COMPARE)
    return __builtin_isless(fpu_raw_f32(a), fpu_raw_f32(b));
#else
    if (likely(!fpu_is_nan32(a) && !fpu_is_nan32(b))) {
        return fpu_raw_f32(a) < fpu_raw_f32(b);
    }
    return false;
#endif
}

static forceinline bool fpu_is_flt64_quiet(fpu_f64_t a, fpu_f64_t b)
{
#if defined(FPU_LIB_CORRECT_BUILTIN_QUIET_COMPARE)
    return __builtin_isless(fpu_raw_f64(a), fpu_raw_f64(b));
#else
    if (likely(!fpu_is_nan64(a) && !fpu_is_nan64(b))) {
        return fpu_raw_f64(a) < fpu_raw_f64(b);
    }
    return false;
#endif
}

// Comparison: Less than or equal (a <= b)
static forceinline bool fpu_is_fle32_quiet(fpu_f32_t a, fpu_f32_t b)
{
#if defined(FPU_LIB_CORRECT_BUILTIN_QUIET_COMPARE)
    return __builtin_islessequal(fpu_raw_f32(a), fpu_raw_f32(b));
#else
    if (likely(!fpu_is_nan32(a) && !fpu_is_nan32(b))) {
        return fpu_raw_f32(a) <= fpu_raw_f32(b);
    }
    return false;
#endif
}

static forceinline bool fpu_is_fle64_quiet(fpu_f64_t a, fpu_f64_t b)
{
#if defined(FPU_LIB_CORRECT_BUILTIN_QUIET_COMPARE)
    return __builtin_islessequal(fpu_raw_f64(a), fpu_raw_f64(b));
#else
    if (likely(!fpu_is_nan64(a) && !fpu_is_nan64(b))) {
        return fpu_raw_f64(a) <= fpu_raw_f64(b);
    }
    return false;
#endif
}

/*
 * IEEE 754-2008 minNum and maxNum
 *
 * - If one operand is a NaN, the result is the non-NaN operand
 * - If at least one operand is a signaling NaN, the invalid operation exception flag is set
 * - Negative zero -0.0 is considered smaller than 0.0
 */

static forceinline fpu_f32_t fpu_min32(fpu_f32_t a, fpu_f32_t b)
{
#if defined(FPU_LIB_CORRECT_BUILTIN_IEEE754_2008_FMINMAX)
    // On RISC-V, __builtin_fminf() actually behaves the way we need
    return fpu_wrap_f32(__builtin_fminf(fpu_raw_f32(a), fpu_raw_f32(b)));
#else
    if (fpu_is_flt32_quiet(a, b)) {
        return a;
    } else if (fpu_is_flt32_quiet(b, a)) {
        return b;
    } else if (fpu_is_negative32(b)) {
        return b;
    } else if (!fpu_is_nan32_fast_soft(a)) {
        return a;
    } else if (!fpu_is_nan32_fast_soft(b)) {
        return b;
    }
    return fpu_bit_u32_to_f32(FPU_LIB_FP32_CANONICAL_NAN);
#endif
}

static forceinline fpu_f64_t fpu_min64(fpu_f64_t a, fpu_f64_t b)
{
#if defined(FPU_LIB_CORRECT_BUILTIN_IEEE754_2008_FMINMAX)
    return fpu_wrap_f64(__builtin_fmin(fpu_raw_f64(a), fpu_raw_f64(b)));
#else
    if (fpu_is_flt64_quiet(a, b)) {
        return a;
    } else if (fpu_is_flt64_quiet(b, a)) {
        return b;
    } else if (fpu_is_negative64(b)) {
        return b;
    } else if (!fpu_is_nan64_fast_soft(a)) {
        return a;
    } else if (!fpu_is_nan64_fast_soft(b)) {
        return b;
    }
    return fpu_bit_u64_to_f64(FPU_LIB_FP64_CANONICAL_NAN);
#endif
}

static forceinline fpu_f32_t fpu_max32(fpu_f32_t a, fpu_f32_t b)
{
#if defined(FPU_LIB_CORRECT_BUILTIN_IEEE754_2008_FMINMAX)
    return fpu_wrap_f32(__builtin_fmaxf(fpu_raw_f32(a), fpu_raw_f32(b)));
#else
    if (fpu_is_flt32_quiet(a, b)) {
        return b;
    } else if (fpu_is_flt32_quiet(b, a)) {
        return a;
    } else if (fpu_is_positive32(b)) {
        return b;
    } else if (!fpu_is_nan32_fast_soft(a)) {
        return a;
    } else if (!fpu_is_nan32_fast_soft(b)) {
        return b;
    }
    return fpu_bit_u32_to_f32(FPU_LIB_FP32_CANONICAL_NAN);
#endif
}

static forceinline fpu_f64_t fpu_max64(fpu_f64_t a, fpu_f64_t b)
{
#if defined(FPU_LIB_CORRECT_BUILTIN_IEEE754_2008_FMINMAX)
    return fpu_wrap_f64(__builtin_fmax(fpu_raw_f64(a), fpu_raw_f64(b)));
#else
    if (fpu_is_flt64_quiet(a, b)) {
        return b;
    } else if (fpu_is_flt64_quiet(b, a)) {
        return a;
    } else if (fpu_is_positive64(b)) {
        return b;
    } else if (!fpu_is_nan64_fast_soft(a)) {
        return a;
    } else if (!fpu_is_nan64_fast_soft(b)) {
        return b;
    }
    return fpu_bit_u64_to_f64(FPU_LIB_FP64_CANONICAL_NAN);
#endif
}

#endif
