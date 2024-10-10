/*
atomics.h - Atomic operations
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

#ifndef ATOMICS_H
#define ATOMICS_H

#include <stdint.h>
#include <stdbool.h>
#include "compiler.h"

#if __STDC_VERSION__ >= 201112LL && !defined(__STDC_NO_ATOMICS__) && !defined(__chibicc__)
// Use C11 atomics on modern compilers
// Those are broken on chibicc compiler, fallback if we detect it
#include <stdatomic.h>
#define C11_ATOMICS_IMPL 1

#define ATOMIC_RELAXED memory_order_relaxed
#define ATOMIC_CONSUME memory_order_consume
#define ATOMIC_ACQUIRE memory_order_acquire
#define ATOMIC_RELEASE memory_order_release
#define ATOMIC_ACQ_REL memory_order_acq_rel
#define ATOMIC_SEQ_CST memory_order_seq_cst

#elif GCC_CHECK_VER(4, 7) || CLANG_CHECK_VER(3, 1)
// Use libatomic-compatible compiler intrinsics on GCC 4.7+ and Clang 3.1+
#define GNU_ATOMICS_IMPL 1
#define GNU_ATOMICS_INTRINS 1

#define ATOMIC_RELAXED __ATOMIC_RELAXED
#define ATOMIC_CONSUME __ATOMIC_CONSUME
#define ATOMIC_ACQUIRE __ATOMIC_ACQUIRE
#define ATOMIC_RELEASE __ATOMIC_RELEASE
#define ATOMIC_ACQ_REL __ATOMIC_ACQ_REL
#define ATOMIC_SEQ_CST __ATOMIC_SEQ_CST

#define __atomic_load_4             __atomic_load_n
#define __atomic_store_4            __atomic_store_n
#define __atomic_exchange_4         __atomic_exchange_n
#define __atomic_compare_exchange_4 __atomic_compare_exchange_n
#define __atomic_fetch_add_4        __atomic_fetch_add
#define __atomic_fetch_sub_4        __atomic_fetch_sub
#define __atomic_fetch_and_4        __atomic_fetch_and
#define __atomic_fetch_xor_4        __atomic_fetch_xor
#define __atomic_fetch_or_4         __atomic_fetch_or

#define __atomic_load_8             __atomic_load_n
#define __atomic_store_8            __atomic_store_n
#define __atomic_exchange_8         __atomic_exchange_n
#define __atomic_compare_exchange_8 __atomic_compare_exchange_n
#define __atomic_fetch_add_8        __atomic_fetch_add
#define __atomic_fetch_sub_8        __atomic_fetch_sub
#define __atomic_fetch_and_8        __atomic_fetch_and
#define __atomic_fetch_xor_8        __atomic_fetch_xor
#define __atomic_fetch_or_8         __atomic_fetch_or

#define atomic_thread_fence         __atomic_thread_fence

#else

#define ATOMIC_RELAXED 0
#define ATOMIC_CONSUME 1
#define ATOMIC_ACQUIRE 2
#define ATOMIC_RELEASE 3
#define ATOMIC_ACQ_REL 4
#define ATOMIC_SEQ_CST 5

#ifdef _WIN32
// Use Interlocked Win32 functions
#include <windows.h>

#elif GCC_CHECK_VER(4, 1)
// Use legacy GNU __sync atomics
#define SYNC_ATOMICS_IMPL 1

#elif !defined(NO_LIBATOMIC)
// Directly call libatomic functions
#define GNU_ATOMICS_IMPL 1

uint32_t __atomic_load_4(const volatile void* ptr, int memorder);
void     __atomic_store_4(volatile void* ptr, uint32_t val, int memorder);
uint32_t __atomic_exchange_4(volatile void* ptr, uint32_t val, int memorder);
bool     __atomic_compare_exchange_4(volatile void* ptr, void* expected, uint32_t desired,
                                     bool weak, int success_memorder, int failure_memorder);
uint32_t __atomic_fetch_add_4(volatile void* ptr, uint32_t val, int memorder);
uint32_t __atomic_fetch_sub_4(volatile void* ptr, uint32_t val, int memorder);
uint32_t __atomic_fetch_and_4(volatile void* ptr, uint32_t val, int memorder);
uint32_t __atomic_fetch_xor_4(volatile void* ptr, uint32_t val, int memorder);
uint32_t __atomic_fetch_or_4(volatile void* ptr, uint32_t val, int memorder);

uint64_t __atomic_load_8(const volatile void* ptr, int memorder);
void     __atomic_store_8(volatile void* ptr, uint64_t val, int memorder);
uint64_t __atomic_exchange_8(volatile void* ptr, uint64_t val, int memorder);
bool     __atomic_compare_exchange_8(volatile void* ptr, void* expected, uint64_t desired,
                                     bool weak, int success_memorder, int failure_memorder);
uint64_t __atomic_fetch_add_8(volatile void* ptr, uint64_t val, int memorder);
uint64_t __atomic_fetch_sub_8(volatile void* ptr, uint64_t val, int memorder);
uint64_t __atomic_fetch_and_8(volatile void* ptr, uint64_t val, int memorder);
uint64_t __atomic_fetch_xor_8(volatile void* ptr, uint64_t val, int memorder);
uint64_t __atomic_fetch_or_8(volatile void* ptr, uint64_t val, int memorder);

void     atomic_thread_fence(int memorder);

#else

// Damned slow atomics emulation using a global lock
#warning No native atomics support, using slow emulation via pthread mutex!
#include <pthread.h>
static pthread_mutex_t global_atomic_lock = PTHREAD_MUTEX_INITIALIZER;

#endif

#endif

#ifndef HOST_LITTLE_ENDIAN
// Use portable conversions instead of host atomics for explicit little endian
#include "mem_ops.h"
#endif

static forceinline void atomic_fence_ex(int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL) || defined(GNU_ATOMICS_IMPL)
    atomic_thread_fence(memorder);
#elif defined(SYNC_ATOMICS_IMPL)
    __sync_synchronize();
#elif _WIN32
    MemoryBarrier();
#endif
}

static forceinline void atomic_fence(void)
{
    atomic_fence_ex(ATOMIC_ACQ_REL);
}

/*
 * Host-endian 32-bit operations
 */

static forceinline uint32_t atomic_load_uint32_ex(const void* addr, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_load_explicit((_Atomic uint32_t*)addr, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_load_4((const uint32_t*)addr, memorder);
#elif defined(_WIN32)
    return InterlockedOr((LONG*)addr, 0);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_add((uint32_t*)addr, 0);
#else
    pthread_mutex_lock(&global_atomic_lock);
    uint32_t ret = *(const uint32_t*)addr;
    pthread_mutex_unlock(&global_atomic_lock);
    return ret;
#endif
}

static forceinline bool atomic_cas_uint32_ex(void* addr, uint32_t exp, uint32_t val, bool weak, int succ, int fail)
{
    UNUSED(weak); UNUSED(succ); UNUSED(fail);
#if defined(__riscv_a) && defined(GNU_EXTS)
    // Workaround RISC-V atomic CAS miscompilation on GCC <14.1
    uint32_t ret = 1, tmp = 0;
    __asm__ __volatile__ (
        "lrsc_cas_loop%=: \n\t"
        "lr.w.aq %1, (%4) \n\t"
        "bne %1, %2, lrsc_cas_exit%= \n\t"
        "sc.w.rl %0, %3, (%4) \n\t"
        "bnez %0, lrsc_cas_loop%= \n\t"
        "lrsc_cas_exit%=: "
        : "=&r" (ret), "=&r" (tmp) : "r" (exp), "r" (val), "p" (addr) : "memory");
    return tmp == exp;
#elif defined(C11_ATOMICS_IMPL)
    if (weak) {
        return atomic_compare_exchange_weak_explicit((_Atomic uint32_t*)addr, &exp, val, succ, fail);
    } else {
        return atomic_compare_exchange_strong_explicit((_Atomic uint32_t*)addr, &exp, val, succ, fail);
    }
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_compare_exchange_4((uint32_t*)addr, &exp, val, weak, succ, fail);
#elif defined(_WIN32)
    return InterlockedCompareExchange((LONG*)addr, val, exp) == (LONG)exp;
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_bool_compare_and_swap((uint32_t*)addr, exp, val);
#else
    bool ret = false;
    pthread_mutex_lock(&global_atomic_lock);
    if (*(uint32_t*)addr == exp) {
        *(uint32_t*)addr = val;
        ret = true;
    }
    pthread_mutex_unlock(&global_atomic_lock);
    return ret;
#endif
}

static forceinline uint32_t atomic_swap_uint32_ex(void* addr, uint32_t val, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_exchange_explicit((_Atomic uint32_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_exchange_4((uint32_t*)addr, val, memorder);
#elif defined(_WIN32)
    return InterlockedExchange((LONG*)addr, val);
#else
    uint32_t tmp = 0;
    do {
        tmp = atomic_load_uint32_ex(addr, ATOMIC_ACQUIRE);
    } while (!atomic_cas_uint32_ex(addr, tmp, val, false, ATOMIC_ACQ_REL, ATOMIC_ACQ_REL));
    return tmp;
#endif
}

static forceinline void atomic_store_uint32_ex(void* addr, uint32_t val, int memorder)
{
#if defined(C11_ATOMICS_IMPL)
    atomic_store_explicit((_Atomic uint32_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    __atomic_store_4((uint32_t*)addr, val, memorder);
#else
    atomic_swap_uint32_ex(addr, val, memorder);
#endif
}

static forceinline uint32_t atomic_add_uint32_ex(void* addr, uint32_t val, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_fetch_add_explicit((_Atomic uint32_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_add_4((uint32_t*)addr, val, memorder);
#elif defined(_WIN32)
    return InterlockedExchangeAdd((LONG*)addr, val);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_add((uint32_t*)addr, val);
#else
    uint32_t tmp = 0;
    do {
        tmp = atomic_load_uint32_ex(addr, ATOMIC_ACQUIRE);
    } while (!atomic_cas_uint32_ex(addr, tmp, tmp + val, false, ATOMIC_ACQ_REL, ATOMIC_ACQ_REL));
    return tmp;
#endif
}

static forceinline uint32_t atomic_sub_uint32_ex(void* addr, uint32_t val, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_fetch_sub_explicit((_Atomic uint32_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_sub_4((uint32_t*)addr, val, memorder);
#elif defined(_WIN32)
    return InterlockedExchangeAdd((LONG*)addr, -val);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_sub((uint32_t*)addr, val);
#else
    uint32_t tmp = 0;
    do {
        tmp = atomic_load_uint32_ex(addr, ATOMIC_ACQUIRE);
    } while (!atomic_cas_uint32_ex(addr, tmp, tmp - val, false, ATOMIC_ACQ_REL, ATOMIC_ACQ_REL));
    return tmp;
#endif
}

static forceinline uint32_t atomic_and_uint32_ex(void* addr, uint32_t val, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_fetch_and_explicit((_Atomic uint32_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_and_4((uint32_t*)addr, val, memorder);
#elif defined(_WIN32)
    return InterlockedAnd((LONG*)addr, val);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_and((uint32_t*)addr, val);
#else
    uint32_t tmp = 0;
    do {
        tmp = atomic_load_uint32_ex(addr, ATOMIC_ACQUIRE);
    } while (!atomic_cas_uint32_ex(addr, tmp, tmp & val, false, ATOMIC_ACQ_REL, ATOMIC_ACQ_REL));
    return tmp;
#endif
}

static forceinline uint32_t atomic_xor_uint32_ex(void* addr, uint32_t val, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_fetch_xor_explicit((_Atomic uint32_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_xor_4((uint32_t*)addr, val, memorder);
#elif defined(_WIN32)
    return InterlockedXor((LONG*)addr, val);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_xor((uint32_t*)addr, val);
#else
    uint32_t tmp = 0;
    do {
        tmp = atomic_load_uint32_ex(addr, ATOMIC_ACQUIRE);
    } while (!atomic_cas_uint32_ex(addr, tmp, tmp ^ val, false, ATOMIC_ACQ_REL, ATOMIC_ACQ_REL));
    return tmp;
#endif
}

static forceinline uint32_t atomic_or_uint32_ex(void* addr, uint32_t val, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_fetch_or_explicit((_Atomic uint32_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_or_4((uint32_t*)addr, val, memorder);
#elif defined(_WIN32)
    return InterlockedOr((LONG*)addr, val);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_or((uint32_t*)addr, val);
#else
    uint32_t tmp = 0;
    do {
        tmp = atomic_load_uint32_ex(addr, ATOMIC_ACQUIRE);
    } while (!atomic_cas_uint32_ex(addr, tmp, tmp | val, false, ATOMIC_ACQ_REL, ATOMIC_ACQ_REL));
    return tmp;
#endif
}

static forceinline uint32_t atomic_load_uint32(const void* addr)
{
    return atomic_load_uint32_ex(addr, ATOMIC_ACQUIRE);
}

static forceinline void atomic_store_uint32(void* addr, uint32_t val)
{
    atomic_store_uint32_ex(addr, val, ATOMIC_RELEASE);
}

static forceinline bool atomic_cas_uint32(void* addr, uint32_t exp, uint32_t val)
{
    return atomic_cas_uint32_ex(addr, exp, val, false, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
}

static forceinline uint32_t atomic_swap_uint32(void* addr, uint32_t val)
{
    return atomic_swap_uint32_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline bool atomic_cas_uint32_weak(void* addr, uint32_t exp, uint32_t val)
{
    return atomic_cas_uint32_ex(addr, exp, val, true, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
}

static forceinline uint32_t atomic_add_uint32(void* addr, uint32_t val)
{
    return atomic_add_uint32_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint32_t atomic_sub_uint32(void* addr, uint32_t val)
{
    return atomic_sub_uint32_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint32_t atomic_and_uint32(void* addr, uint32_t val)
{
    return atomic_and_uint32_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint32_t atomic_xor_uint32(void* addr, uint32_t val)
{
    return atomic_xor_uint32_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint32_t atomic_or_uint32(void* addr, uint32_t val)
{
    return atomic_or_uint32_ex(addr, val, ATOMIC_ACQ_REL);
}

/*
 * Host-endian 64-bit operations
 */

static forceinline uint64_t atomic_load_uint64_ex(const void* addr, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_load_explicit((_Atomic uint64_t*)addr, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_load_8((const uint64_t*)addr, memorder);
#elif defined(_WIN32)
    return InterlockedOr64((LONG64*)addr, 0);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_add((uint64_t*)addr, 0);
#else
    pthread_mutex_lock(&global_atomic_lock);
    uint64_t ret = *(const uint64_t*)addr;
    pthread_mutex_unlock(&global_atomic_lock);
    return ret;
#endif
}

static forceinline bool atomic_cas_uint64_ex(void* addr, uint64_t exp, uint64_t val, bool weak, int succ, int fail)
{
    UNUSED(weak); UNUSED(succ); UNUSED(fail);
#if defined(__riscv_a) && __riscv_xlen >= 64 && defined(GNU_EXTS)
    // Workaround RISC-V atomic CAS miscompilation on GCC <14.1
    uint64_t ret = 1, tmp = 0;
    __asm__ __volatile__ (
        "lrsc_cas_loop%=: \n\t"
        "lr.d.aq %1, (%4) \n\t"
        "bne %1, %2, lrsc_cas_exit%= \n\t"
        "sc.d.rl %0, %3, (%4) \n\t"
        "bnez %0, lrsc_cas_loop%= \n\t"
        "lrsc_cas_exit%=: "
        : "=&r" (ret), "=&r" (tmp) : "r" (exp), "r" (val), "p" (addr) : "memory");
    return tmp == exp;
#elif defined(C11_ATOMICS_IMPL)
    if (weak) {
        return atomic_compare_exchange_weak_explicit((_Atomic uint64_t*)addr, &exp, val, succ, fail);
    } else {
        return atomic_compare_exchange_strong_explicit((_Atomic uint64_t*)addr, &exp, val, succ, fail);
    }
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_compare_exchange_8((uint64_t*)addr, &exp, val, weak, succ, fail);
#elif defined(_WIN32)
    return InterlockedCompareExchange64((LONG64*)addr, val, exp) == (LONG64)exp;
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_bool_compare_and_swap((uint64_t*)addr, exp, val);
#else
    bool ret = false;
    pthread_mutex_lock(&global_atomic_lock);
    if (*(uint64_t*)addr == exp) {
        *(uint64_t*)addr = val;
        ret = true;
    }
    pthread_mutex_unlock(&global_atomic_lock);
    return ret;
#endif
}

static forceinline uint64_t atomic_swap_uint64_ex(void* addr, uint64_t val, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_exchange_explicit((_Atomic uint64_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_exchange_8((uint64_t*)addr, val, memorder);
#elif defined(_WIN32)
    return InterlockedExchange64((LONG64*)addr, val);
#else
    uint64_t tmp = 0;
    do {
        tmp = atomic_load_uint64_ex(addr, ATOMIC_ACQUIRE);
    } while (!atomic_cas_uint64_ex(addr, tmp, val, false, ATOMIC_ACQ_REL, ATOMIC_ACQ_REL));
    return tmp;
#endif
}

static forceinline void atomic_store_uint64_ex(void* addr, uint64_t val, int memorder)
{
#if defined(C11_ATOMICS_IMPL)
    atomic_store_explicit((_Atomic uint64_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    __atomic_store_8((uint64_t*)addr, val, memorder);
#else
    atomic_swap_uint64_ex(addr, val, memorder);
#endif
}

static forceinline uint64_t atomic_add_uint64_ex(void* addr, uint64_t val, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_fetch_add_explicit((_Atomic uint64_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_add_8((uint64_t*)addr, val, memorder);
#elif defined(_WIN32)
    return InterlockedExchangeAdd64((LONG64*)addr, val);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_add((uint64_t*)addr, val);
#else
    uint64_t tmp = 0;
    do {
        tmp = atomic_load_uint64_ex(addr, ATOMIC_ACQUIRE);
    } while (!atomic_cas_uint64_ex(addr, tmp, tmp + val, false, ATOMIC_ACQ_REL, ATOMIC_ACQ_REL));
    return tmp;
#endif
}

static forceinline uint64_t atomic_sub_uint64_ex(void* addr, uint64_t val, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_fetch_sub_explicit((_Atomic uint64_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_sub_8((uint64_t*)addr, val, memorder);
#elif defined(_WIN32)
    return InterlockedExchangeAdd64((LONG64*)addr, -val);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_sub((uint64_t*)addr, val);
#else
    uint64_t tmp = 0;
    do {
        tmp = atomic_load_uint64_ex(addr, ATOMIC_ACQUIRE);
    } while (!atomic_cas_uint64_ex(addr, tmp, tmp - val, false, ATOMIC_ACQ_REL, ATOMIC_ACQ_REL));
    return tmp;
#endif
}

static forceinline uint64_t atomic_and_uint64_ex(void* addr, uint64_t val, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_fetch_and_explicit((_Atomic uint64_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_and_8((uint64_t*)addr, val, memorder);
#elif defined(_WIN32)
    return InterlockedAnd64((LONG64*)addr, val);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_and((uint64_t*)addr, val);
#else
    uint64_t tmp = 0;
    do {
        tmp = atomic_load_uint64_ex(addr, ATOMIC_ACQUIRE);
    } while (!atomic_cas_uint64_ex(addr, tmp, tmp & val, false, ATOMIC_ACQ_REL, ATOMIC_ACQ_REL));
    return tmp;
#endif
}

static forceinline uint64_t atomic_xor_uint64_ex(void* addr, uint64_t val, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_fetch_xor_explicit((_Atomic uint64_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_xor_8((uint64_t*)addr, val, memorder);
#elif defined(_WIN32)
    return InterlockedXor64((LONG64*)addr, val);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_xor((uint64_t*)addr, val);
#else
    uint64_t tmp = 0;
    do {
        tmp = atomic_load_uint64_ex(addr, ATOMIC_ACQUIRE);
    } while (!atomic_cas_uint64_ex(addr, tmp, tmp ^ val, false, ATOMIC_ACQ_REL, ATOMIC_ACQ_REL));
    return tmp;
#endif
}

static forceinline uint64_t atomic_or_uint64_ex(void* addr, uint64_t val, int memorder)
{
    UNUSED(memorder);
#if defined(C11_ATOMICS_IMPL)
    return atomic_fetch_or_explicit((_Atomic uint64_t*)addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_or_8((uint64_t*)addr, val, memorder);
#elif defined(_WIN32)
    return InterlockedOr64((LONG64*)addr, val);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_or((uint64_t*)addr, val);
#else
    uint64_t tmp = 0;
    do {
        tmp = atomic_load_uint64_ex(addr, ATOMIC_ACQUIRE);
    } while (!atomic_cas_uint64_ex(addr, tmp, tmp | val, false, ATOMIC_ACQ_REL, ATOMIC_ACQ_REL));
    return tmp;
#endif
}

static forceinline uint64_t atomic_load_uint64(const void* addr)
{
    return atomic_load_uint64_ex(addr, ATOMIC_ACQUIRE);
}

static forceinline void atomic_store_uint64(void* addr, uint64_t val)
{
    atomic_store_uint64_ex(addr, val, ATOMIC_RELEASE);
}

static forceinline uint64_t atomic_swap_uint64(void* addr, uint64_t val)
{
    return atomic_swap_uint64_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline bool atomic_cas_uint64(void* addr, uint64_t exp, uint64_t val)
{
    return atomic_cas_uint64_ex(addr, exp, val, false, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
}

static forceinline bool atomic_cas_uint64_weak(void* addr, uint64_t exp, uint64_t val)
{
    return atomic_cas_uint64_ex(addr, exp, val, true, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
}

static forceinline uint64_t atomic_add_uint64(void* addr, uint64_t val)
{
    return atomic_add_uint64_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint64_t atomic_sub_uint64(void* addr, uint64_t val)
{
    return atomic_sub_uint64_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint64_t atomic_and_uint64(void* addr, uint64_t val)
{
    return atomic_and_uint64_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint64_t atomic_xor_uint64(void* addr, uint64_t val)
{
    return atomic_xor_uint64_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint64_t atomic_or_uint64(void* addr, uint64_t val)
{
    return atomic_or_uint64_ex(addr, val, ATOMIC_ACQ_REL);
}

/*
 * Emulated little-endian atomics for big-endian hosts
 */

static inline void atomic_store_uint32_le(void* addr, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    atomic_store_uint32(addr, val);
#else
    write_uint32_le(&val, val);
    atomic_store_uint32(addr, val);
#endif
}

static inline uint32_t atomic_load_uint32_le(const void* addr)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_load_uint32(addr);
#else
    uint32_t val = atomic_load_uint32(addr);
    return read_uint32_le(&val);
#endif
}

static inline uint32_t atomic_swap_uint32_le(void* addr, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_swap_uint32(addr, val);
#else
    write_uint32_le(&val, val);
    val = atomic_swap_uint32(addr, val);
    return read_uint32_le(&val);
#endif
}

static inline bool atomic_cas_uint32_le(void* addr, uint32_t exp, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_cas_uint32_ex(addr, exp, val, true, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
#else
    write_uint32_le(&exp, exp);
    write_uint32_le(&val, val);
    return atomic_cas_uint32_ex(addr, exp, val, true, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
#endif
}

static inline uint32_t atomic_or_uint32_le(void* addr, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_or_uint32(addr, val);
#else
    write_uint32_le(&val, val);
    val = atomic_or_uint32(addr, val);
    return read_uint32_le(&val);
#endif
}

static inline uint32_t atomic_xor_uint32_le(void* addr, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_xor_uint32(addr, val);
#else
    write_uint32_le(&val, val);
    val = atomic_xor_uint32(addr, val);
    return read_uint32_le(&val);
#endif
}

static inline uint32_t atomic_and_uint32_le(void* addr, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_and_uint32(addr, val);
#else
    write_uint32_le(&val, val);
    val = atomic_and_uint32(addr, val);
    return read_uint32_le(&val);
#endif
}

static inline void atomic_store_uint64_le(void* addr, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    atomic_store_uint64(addr, val);
#else
    write_uint64_le(&val, val);
    atomic_store_uint64(addr, val);
#endif
}

static inline uint64_t atomic_load_uint64_le(const void* addr)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_load_uint64(addr);
#else
    uint64_t val = atomic_load_uint64(addr);
    return read_uint64_le(&val);
#endif
}

static inline uint64_t atomic_swap_uint64_le(void* addr, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_swap_uint64(addr, val);
#else
    write_uint64_le(&val, val);
    val = atomic_swap_uint64(addr, val);
    return read_uint64_le(&val);
#endif
}

static inline bool atomic_cas_uint64_le(void* addr, uint64_t exp, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_cas_uint64_ex(addr, exp, val, true, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
#else
    write_uint64_le(&exp, exp);
    write_uint64_le(&val, val);
    return atomic_cas_uint64_ex(addr, exp, val, true, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
#endif
}

static inline uint64_t atomic_or_uint64_le(void* addr, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_or_uint64(addr, val);
#else
    write_uint64_le(&val, val);
    val = atomic_or_uint64(addr, val);
    return read_uint64_le(&val);
#endif
}

static inline uint64_t atomic_xor_uint64_le(void* addr, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_xor_uint64(addr, val);
#else
    write_uint64_le(&val, val);
    val = atomic_xor_uint64(addr, val);
    return read_uint64_le(&val);
#endif
}

static inline uint64_t atomic_and_uint64_le(void* addr, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_and_uint64(addr, val);
#else
    write_uint64_le(&val, val);
    val = atomic_and_uint64(addr, val);
    return read_uint64_le(&val);
#endif
}

/*
 * CAS-based arithmetic operations
 * Store operation result if the value is unchanged
 */

static inline uint32_t atomic_add_uint32_le(void* addr, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_add_uint32(addr, val);
#else
    uint32_t tmp;
    do {
        tmp = atomic_load_uint32_le(addr);
    } while (!atomic_cas_uint32_le(addr, tmp, tmp + val));
    return tmp;
#endif
}

static inline uint32_t atomic_sub_uint32_le(void* addr, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_sub_uint32(addr, val);
#else
    uint32_t tmp;
    do {
        tmp = atomic_load_uint32_le(addr);
    } while (!atomic_cas_uint32_le(addr, tmp, tmp - val));
    return tmp;
#endif
}

static inline int32_t atomic_max_int32_le(void* addr, int32_t val)
{
    int32_t tmp;
    do {
        tmp = atomic_load_uint32_le(addr);
    } while (!atomic_cas_uint32_le(addr, tmp, tmp > val ? tmp : val));
    return tmp;
}

static inline int32_t atomic_min_int32_le(void* addr, int32_t val)
{
    int32_t tmp;
    do {
        tmp = atomic_load_uint32_le(addr);
    } while (!atomic_cas_uint32_le(addr, tmp, tmp < val ? tmp : val));
    return tmp;
}

static inline uint32_t atomic_maxu_uint32_le(void* addr, uint32_t val)
{
    uint32_t tmp;
    do {
        tmp = atomic_load_uint32_le(addr);
    } while (!atomic_cas_uint32_le(addr, tmp, tmp > val ? tmp : val));
    return tmp;
}

static inline uint32_t atomic_minu_uint32_le(void* addr, uint32_t val)
{
    uint32_t tmp;
    do {
        tmp = atomic_load_uint32_le(addr);
    } while (!atomic_cas_uint32_le(addr, tmp, tmp < val ? tmp : val));
    return tmp;
}

static inline uint64_t atomic_add_uint64_le(void* addr, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_add_uint64(addr, val);
#else
    uint64_t tmp;
    do {
        tmp = atomic_load_uint64_le(addr);
    } while (!atomic_cas_uint64_le(addr, tmp, tmp + val));
    return tmp;
#endif
}

static inline uint64_t atomic_sub_uint64_le(void* addr, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_sub_uint64(addr, val);
#else
    uint64_t tmp;
    do {
        tmp = atomic_load_uint64_le(addr);
    } while (!atomic_cas_uint64_le(addr, tmp, tmp - val));
    return tmp;
#endif
}

static inline int64_t atomic_max_int64_le(void* addr, int64_t val)
{
    int64_t tmp;
    do {
        tmp = atomic_load_uint64_le(addr);
    } while (!atomic_cas_uint64_le(addr, tmp, tmp > val ? tmp : val));
    return tmp;
}

static inline int64_t atomic_min_int64_le(void* addr, int64_t val)
{
    int64_t tmp;
    do {
        tmp = atomic_load_uint64_le(addr);
    } while (!atomic_cas_uint64_le(addr, tmp, tmp < val ? tmp : val));
    return tmp;
}

static inline uint64_t atomic_maxu_uint64_le(void* addr, uint64_t val)
{
    uint64_t tmp;
    do {
        tmp = atomic_load_uint64_le(addr);
    } while (!atomic_cas_uint64_le(addr, tmp, tmp > val ? tmp : val));
    return tmp;
}

static inline uint64_t atomic_minu_uint64_le(void* addr, uint64_t val)
{
    uint64_t tmp;
    do {
        tmp = atomic_load_uint64_le(addr);
    } while (!atomic_cas_uint64_le(addr, tmp, tmp < val ? tmp : val));
    return tmp;
}

#endif
