/*
compiler.h - Compilers tricks and features
Copyright (C) 2021  LekKit <github.com/LekKit>

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

#ifndef COMPILER_H
#define COMPILER_H

#include <stdint.h>

#if defined(__GNUC__) || defined(__llvm__) || defined(__INTEL_COMPILER)
#define GNU_EXTS 1
#endif

#ifdef GNU_EXTS
#define likely(x)     __builtin_expect(!!(x),1)
#define unlikely(x)   __builtin_expect(!!(x),0)
#else
#define likely(x)     (x)
#define unlikely(x)   (x)
#endif

#ifdef GNU_EXTS
#define PUBLIC        __attribute__((visibility("default")))
#define HIDDEN        __attribute__((visibility("hidden")))
#define NOINLINE      __attribute__((noinline))
#elif defined(_WIN32)
#define PUBLIC        __declspec(dllexport)
#define HIDDEN
#define NOINLINE      __declspec(noinline)
#else
#define PUBLIC
#define HIDDEN
#define NOINLINE
#endif

#ifdef GNU_EXTS

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define HOST_LITTLE_ENDIAN 1
#else
#define HOST_BIG_ENDIAN 1
#endif

#else

// Guess endianness based on arch/common macros
// Able to detect big-endian MIPS, ARM, PowerPC, PA-RISC, s390
#if defined(__MIPSEB__) || defined(__ARMEB__) || \
    defined(__hppa__) || defined(__hppa64__) || defined(__s390__) || \
    (defined(__ppc__) || defined(__ppc64__)) && defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define HOST_BIG_ENDIAN 1
#else
#define HOST_LITTLE_ENDIAN 1
#endif

#endif

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_AMD64)
#define HOST_MISALIGN_SUPPORT 1
#else
// Not sure about other arches, misaligns may be very slow
// Safer this way
#define HOST_STRICT_ALIGNMENT 1
#endif

#define UNUSED(x) (void)x

#if UINTPTR_MAX == UINT64_MAX
#define HOST_64BIT 1
#endif

#define MACRO_MKSTRING(x) #x
#define MACRO_TOSTRING(x) MACRO_MKSTRING(x)

// Unwraps to src/example.c@128
#define SOURCE_LINE __FILE__"@"MACRO_TOSTRING(__LINE__)

#endif
