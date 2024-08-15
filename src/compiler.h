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

#if defined(__GNUC__) && !defined(__llvm__) && !defined(__INTEL_COMPILER)
#define GCC_CHECK_VER(major, minor) (__GNUC__ > major || \
        (__GNUC__ == major && __GNUC_MINOR__ >= minor))
#else
#define GCC_CHECK_VER(major, minor) 0
#endif

#ifdef __clang__
#define CLANG_CHECK_VER(major, minor) (__clang_major__ > major || \
          (__clang_major__ == major && __clang_minor__ >= minor))
#else
#define CLANG_CHECK_VER(major, minor) 0
#endif

// Check GNU attribute presence
#if defined(GNU_EXTS) && defined(__has_attribute)
#define GNU_ATTRIBUTE(attr) __has_attribute(attr)
#else
#define GNU_ATTRIBUTE(attr) 0
#endif

// Check GNU builtin presence
#if defined(GNU_EXTS) && defined(__has_builtin)
#define GNU_BUILTIN(builtin) __has_builtin(builtin)
#else
#define GNU_BUILTIN(builtin) 0
#endif

// Check header presence
#ifdef __has_include
#define CHECK_INCLUDE(include) __has_include(#include)
#else
#define CHECK_INCLUDE(include) 1
#endif

// Branch optimization hints
#if GNU_BUILTIN(__builtin_expect)
#define likely(x)     __builtin_expect(!!(x),1)
#define unlikely(x)   __builtin_expect(!!(x),0)
#else
#define likely(x)     (x)
#define unlikely(x)   (x)
#endif

#if GNU_BUILTIN(__builtin_prefetch) && !defined(NO_PREFETCH)
#define mem_prefetch(addr, rw, loc) __builtin_prefetch(addr, !!(rw), loc)
#else
#define mem_prefetch(addr, rw, loc)
#endif

#if GNU_ATTRIBUTE(__noinline__)
#define NOINLINE      __attribute__((__noinline__))
#elif defined(_MSC_VER)
#define NOINLINE      __declspec(noinline)
#else
#define NOINLINE
#endif

#if GNU_ATTRIBUTE(__destructor__)
#define GNU_DESTRUCTOR __attribute__((__destructor__))
#else
#define GNU_DESTRUCTOR
#endif

#if GNU_ATTRIBUTE(__constructor__)
#define GNU_CONSTRUCTOR __attribute__((__constructor__))
#else
#define GNU_CONSTRUCTOR
#endif

/*
 * This is used to remove unnecessary register spills from algorithm fast path
 * when a slow path call is present. Hopefully one day similar thing will appear in GCC.
 *
 * This attribute is BROKEN before Clang 17 and generates broken binaries if used <17!!!
 */
#if CLANG_CHECK_VER(17, 0) && GNU_ATTRIBUTE(__preserve_most__)
#define slow_path __attribute__((__preserve_most__,__noinline__))
#else
#define slow_path NOINLINE
#endif

#if GNU_ATTRIBUTE(__always_inline__)
#define forceinline inline __attribute__((__always_inline__))
#elif defined(_MSC_VER)
#define forceinline __forceinline
#else
#define forceinline inline
#endif

#if GNU_ATTRIBUTE(__flatten__)
#define flatten_calls __attribute__((__flatten__))
#else
#define flatten_calls
#endif

// Match GCC macro __SANITIZE_THREAD__ on Clang, provide __SANITIZE_MEMORY__
#if defined(__clang__) && defined(__has_feature)
#if __has_feature(thread_sanitizer) && !defined(__SANITIZE_THREAD__)
#define __SANITIZE_THREAD__
#endif
#if __has_feature(memory_sanitizer) && !defined(__SANITIZE_MEMORY__)
#define __SANITIZE_MEMORY__
#endif
#endif

// Suppress ThreadSanitizer in places with false alarms (emulated load/stores or RCU)
// Guest dataraces hinder normal code instrumentation, so this is handy
#if defined(__SANITIZE_THREAD__) && !defined(USE_SANITIZE_FULL) && GNU_ATTRIBUTE(__no_sanitize__)
#define TSAN_SUPPRESS __attribute__((__no_sanitize__("thread")))
#else
#define TSAN_SUPPRESS
#endif

// Suppress MemorySanitizer in places with false alarms (non-instrumented syscalls, X11 libs, etc)
#if defined(__SANITIZE_MEMORY__) && !defined(USE_SANITIZE_FULL) && GNU_ATTRIBUTE(__no_sanitize__)
#define MSAN_SUPPRESS __attribute__((__no_sanitize__("memory")))
#else
#define MSAN_SUPPRESS
#endif

// Optimization pragmas (Clang doesn't support this)
#if GCC_CHECK_VER(4, 4)
#define SOURCE_OPTIMIZATION_NONE _Pragma("GCC optimize(\"O0\")")
#define SOURCE_OPTIMIZATION_O2 _Pragma("GCC optimize(\"O2\")")
#define SOURCE_OPTIMIZATION_O3 _Pragma("GCC optimize(\"O3\")")
#else
#define SOURCE_OPTIMIZATION_NONE
#define SOURCE_OPTIMIZATION_O2
#define SOURCE_OPTIMIZATION_O3
#endif
#if GCC_CHECK_VER(12, 1)
#define SOURCE_OPTIMIZATION_SIZE _Pragma("GCC optimize(\"Oz\")")
#elif GCC_CHECK_VER(4, 4)
#define SOURCE_OPTIMIZATION_SIZE _Pragma("GCC optimize(\"Os\")")
#else
#define SOURCE_OPTIMIZATION_SIZE
#endif

// Pushable size optimization attribute, Clang supports this to some degree
#if CLANG_CHECK_VER(8, 0) && GNU_ATTRIBUTE(__minsize__)
#define PUSH_OPTIMIZATION_SIZE _Pragma("clang attribute push (__attribute__((__minsize__)), apply_to=function)")
#define POP_OPTIMIZATION_SIZE _Pragma("clang attribute pop")
#elif GCC_CHECK_VER(4, 4)
#define PUSH_OPTIMIZATION_SIZE _Pragma("GCC push_options") SOURCE_OPTIMIZATION_SIZE
#define POP_OPTIMIZATION_SIZE _Pragma("GCC pop_options")
#else
#define PUSH_OPTIMIZATION_SIZE
#define POP_OPTIMIZATION_SIZE
#endif

// Guess endianness based on arch/common macros
// Able to detect big-endian MIPS, ARM, PowerPC, PA-RISC, s390
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || defined(__MIPSEB__) || \
    defined(__ARMEB__) || defined(__hppa__) || defined(__hppa64__) || defined(__s390__)
#define HOST_BIG_ENDIAN 1
#elif !defined(GNU_EXTS) || (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define HOST_LITTLE_ENDIAN 1
#endif

// Determine whether host has fast misaligned access (Hint)
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_AMD64) || defined(__aarch64__)
#define HOST_FAST_MISALIGN 1
#else
// Not sure about other arches, misaligns may be very slow or crash
#define HOST_NO_MISALIGN 1
#endif

#define UNUSED(x) (void)x

// Determine host bitness (Hint)
#if UINTPTR_MAX == UINT64_MAX
#define HOST_64BIT 1
#elif UINTPTR_MAX == UINT32_MAX
#define HOST_32BIT 1
#endif

// Unwrap a token or a token value into a string literal
#define MACRO_MKSTRING(x) #x
#define MACRO_TOSTRING(x) MACRO_MKSTRING(x)

// GNU extension that omits file path, use if available
#ifndef __FILE_NAME__
#define __FILE_NAME__ __FILE__
#endif

// Unwraps to example.c@128
#define SOURCE_LINE __FILE_NAME__ "@" MACRO_TOSTRING(__LINE__)

#define MACRO_ASSERT_NAMED(cond, name) typedef char static_assert_at_line_##name[(cond) ? 1 : -1]
#define MACRO_ASSERT_UNWRAP(cond, tok) MACRO_ASSERT_NAMED(cond, tok)

// Static build-time assertions
#ifdef IGNORE_BUILD_ASSERTS
#define BUILD_ASSERT(cond)
#elif __STDC_VERSION__ >= 201112LL && !defined(__chibicc__)
#define BUILD_ASSERT(cond) _Static_assert(cond, MACRO_TOSTRING(cond))
#else
#define BUILD_ASSERT(cond) MACRO_ASSERT_UNWRAP(cond, __LINE__)
#endif

// Same as BUILD_ASSERT, but produces an expression with value 0
#ifdef IGNORE_BUILD_ASSERTS
#define BUILD_ASSERT_EXPR(cond) 0
#else
#define BUILD_ASSERT_EXPR(cond) (sizeof(char[(cond) ? 1 : -1]) - 1)
#endif

// Weak symbol linkage (Runtime library probing)
#if defined(GNU_EXTS)
#define WEAK_LINKAGE(symbol) _Pragma(MACRO_TOSTRING(weak symbol))
#else
#define WEAK_LINKAGE(symbol)
#endif

#endif
