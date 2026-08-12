/*
atomics.h - Atomic operations
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef RVVM_UTIL_ATOMICS_H
#define RVVM_UTIL_ATOMICS_H

/*
 * Cheat sheet on C11 atomics and common CPU memory models (LekKit, 2025):
 *
 * Atomic operations are classified as following:
 * - Load:  An indivisible read from variable in memory
 * - Store: An indivisible write to variable in memory.
 * - RMW:   An indivisible read-modify-write on variable in memory.
 *          This includes operations like cas, swap, add, xor.
 *
 * An atomic load  will never observe a half-written (torn) value.
 * An atomic store will never disturb an in-progress atomic read-modify-write.
 * Read-modify-write operations are usually way more expensive than loads or stores.
 *
 * Memory accesses to different objects are subject to global reordering for other observers,
 * despite appearing completely sequential from local execution viewpoint.
 * Relaxed atomic access to the same variable is guaranteed to participate in a single modification order,
 * but without any other synchronisation mechanism it is possible to observe not the most recent value.
 *
 * The following types of reordering are done by compilers and CPUs:
 * - LoadLoad:   Later loads  may be retired before earlier loads
 * - LoadStore:  Later stores may be retired before earlier loads
 * - StoreStore: Later stores may be retired before earlier stores
 * - StoreLoad:  Later loads  may be retired before earlier stores
 * The StoreLoad reordering is allowed even under strong memory model, e.g. x86 TSO.
 *
 * The different models on how an issued store to memory will become visible to observers:
 * - Multi-copy atomic:     Store becomes locally visible, then _simultaneously_ becomes visible to all
 *                          other observers. This a common memory model for many hardware implementations.
 *
 * - Non-multi-copy-atomic: Store becomes visible to different observers at arbitrary points in time.
 *                          This happens on pre-POWER9 CPUs, and is possible under C11 memory model.
 *
 * To ensure correct order of operations on complex data structures, memory ordering is used.
 * Stronger memory ordering provides more guarantees, but is more expensive.
 *
 * The following memory ordering may be applied on fences/atomics in C11:
 * - RELAXED ordering implies no global ordering of the operation on it's own.
 *   Yet, relaxed operations on the same atomic variable are always ordered correctly.
 *
 * - ACQUIRE ordering prevents LoadLoad and LoadStore reordering.
 *   An acquire fence orders preceding loads before subsequent loads and stores.
 *   An acquire load completes before subsequent loads and stores.
 *
 * - RELEASE ordering prevents StoreStore and LoadStore reordering.
 *   A release fence orders subsequent stores after preceding loads and stores.
 *   A release store completes after previous loads and stores.
 *
 * - ACQ_REL ordering prevents LoadLoad, LoadStore and StoreStore reordering.
 *   An acq_rel fence orders subsequent loads and stores after preceding loads.
 *   An acq_rel fence orders subsequent stores after preceding loads and stores.
 *   An acq_rel RMW atomic acts as if release ordering applies to operations that
 *   precede it, and acquire ordering applies to operations that follow it.
 *   On non-multi-copy-atomic implementations, other threads may only observe the final store.
 *
 * - CONSUME ordering is a special one in regard to atomic pointer loads and data dependencies.
 *   A consume load of a pointer is ordered before accesses to data via that pointer.
 *   This matters on DEC Alpha, but contemporary compilers implement it inefficiently.
 *   Prefer to explicitly use RCU instead.
 *
 * - SEQ_CST ordering prevents any reordering, providing full sequential consistency.
 *   All seq_cst atomics/fences are interleaved in a global, sequentially consistent order with each other.
 *   This usually implies a store buffer drain or equivalent global ordering HW mechanism, which is expensive.
 *   Sequential consistency is usually seldom needed, but is desirable for some very specific algorithms.
 *
 * Under assumption of non-multi-copy-atomic model, only observing a release store
 * via an acquire load will establish any order for it with subsequent operations.
 * This is why the C11 standard semantically words acquire/release as "pairs-with" ordering.
 * Naturally, seq_cst ordering provides multi-copy atomicity regardless.
 *
 * This library sensibly defaults to the following:
 * - ACQUIRE semantics on loads,  which is "for free" on x86 and not very expensive otherwise
 * - RELEASE semantics on stores, which is "for free" on x86 and not very expensive otherwise
 * - ACQ_REL semantics on read-modify-write operations, which are heavyweight either way
 *
 * CAS (Compare and Swap) operation is semantically a load upon failure, and its failure
 * memory ordering should usually be relaxed, unless the original fetched value is used
 * in a way that should be globally ordered. This is rarely the case.
 */

#include <util/compiler.h>

#if !defined(HOST_LITTLE_ENDIAN)
// Use portable endian conversions for explicit little endian atomics
#include <util/mem_ops.h>
#endif

#undef ATOMIC_RELAXED
#undef ATOMIC_CONSUME
#undef ATOMIC_ACQUIRE
#undef ATOMIC_RELEASE
#undef ATOMIC_ACQ_REL
#undef ATOMIC_SEQ_CST

#if defined(USE_THREAD_EMU)
#define USE_ATOMIC_EMU 1
#endif

#if (GCC_CHECK_VER(4, 7) || CLANG_CHECK_VER(3, 1)) && !defined(USE_ATOMIC_EMU)

/*
 * Use GNU atomic compiler builtins on GCC 4.7+ and Clang 3.1+
 */

#define GNU_ATOMICS_IMPL 1

#define ATOMIC_RELAXED   __ATOMIC_RELAXED
#define ATOMIC_CONSUME   __ATOMIC_CONSUME
#define ATOMIC_ACQUIRE   __ATOMIC_ACQUIRE
#define ATOMIC_RELEASE   __ATOMIC_RELEASE
#define ATOMIC_ACQ_REL   __ATOMIC_ACQ_REL
#define ATOMIC_SEQ_CST   __ATOMIC_SEQ_CST

#elif !defined(__chibicc__) && !defined(USE_ATOMIC_EMU)          /**/                                                  \
    && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112LL /**/                                                  \
    && !defined(__STDC_NO_ATOMICS__) && CHECK_INCLUDE(stdatomic.h, 1)

/*
 * Use C11 atomics
 */

#include <stdatomic.h>

#define C11_ATOMICS_IMPL 1

#define ATOMIC_RELAXED   memory_order_relaxed
#define ATOMIC_CONSUME   memory_order_consume
#define ATOMIC_ACQUIRE   memory_order_acquire
#define ATOMIC_RELEASE   memory_order_release
#define ATOMIC_ACQ_REL   memory_order_acq_rel
#define ATOMIC_SEQ_CST   memory_order_seq_cst

#elif GCC_CHECK_VER(4, 1) && !defined(USE_ATOMIC_EMU)

/*
 * Use legacy GCC __sync atomics
 */

#define SYNC_ATOMICS_IMPL 1

#elif defined(_WIN32) && !defined(USE_ATOMIC_EMU) /**/                                                                 \
    && defined(HOST_32BIT) && (defined(UNDER_CE) || defined(_WIN32_WCE))

/*
 * Use WinCE InterlockedCompareExchange()
 */

#undef STRICT
#define STRICT 1
#undef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1

#include <windows.h>

#define WINCE_ATOMICS_IMPL 1
#define ATOMIC_EMU64_IMPL  1

#elif defined(_WIN32) && !defined(USE_ATOMIC_EMU)

/*
 * Use Win32 Interlocked atomics
 */

#undef STRICT
#define STRICT 1
#undef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1

#include <windows.h>

#define WIN32_ATOMICS_IMPL 1

#elif !defined(USE_NO_LIBATOMIC) && !defined(USE_ATOMIC_EMU)

/*
 * Use libatomic library
 */

#define LIBATOMIC_IMPL 1

uint8_t __atomic_load_1(const volatile void* ptr, int memorder);
void    __atomic_store_1(volatile void* ptr, uint8_t val, int memorder);
uint8_t __atomic_exchange_1(volatile void* ptr, uint8_t val, int memorder);
bool    __atomic_compare_exchange_1(volatile void* ptr, void* expected, uint8_t desired, //
                                    bool weak, int success_memorder, int failure_memorder);
uint8_t __atomic_fetch_add_1(volatile void* ptr, uint8_t val, int memorder);
uint8_t __atomic_fetch_sub_1(volatile void* ptr, uint8_t val, int memorder);
uint8_t __atomic_fetch_and_1(volatile void* ptr, uint8_t val, int memorder);
uint8_t __atomic_fetch_xor_1(volatile void* ptr, uint8_t val, int memorder);
uint8_t __atomic_fetch_or_1(volatile void* ptr, uint8_t val, int memorder);

uint16_t __atomic_load_2(const volatile void* ptr, int memorder);
void     __atomic_store_2(volatile void* ptr, uint16_t val, int memorder);
uint16_t __atomic_exchange_2(volatile void* ptr, uint16_t val, int memorder);
bool     __atomic_compare_exchange_2(volatile void* ptr, void* expected, uint16_t desired, //
                                     bool weak, int success_memorder, int failure_memorder);
uint16_t __atomic_fetch_add_2(volatile void* ptr, uint16_t val, int memorder);
uint16_t __atomic_fetch_sub_2(volatile void* ptr, uint16_t val, int memorder);
uint16_t __atomic_fetch_and_2(volatile void* ptr, uint16_t val, int memorder);
uint16_t __atomic_fetch_xor_2(volatile void* ptr, uint16_t val, int memorder);
uint16_t __atomic_fetch_or_2(volatile void* ptr, uint16_t val, int memorder);

uint32_t __atomic_load_4(const volatile void* ptr, int memorder);
void     __atomic_store_4(volatile void* ptr, uint32_t val, int memorder);
uint32_t __atomic_exchange_4(volatile void* ptr, uint32_t val, int memorder);
bool     __atomic_compare_exchange_4(volatile void* ptr, void* expected, uint32_t desired, //
                                     bool weak, int success_memorder, int failure_memorder);
uint32_t __atomic_fetch_add_4(volatile void* ptr, uint32_t val, int memorder);
uint32_t __atomic_fetch_sub_4(volatile void* ptr, uint32_t val, int memorder);
uint32_t __atomic_fetch_and_4(volatile void* ptr, uint32_t val, int memorder);
uint32_t __atomic_fetch_xor_4(volatile void* ptr, uint32_t val, int memorder);
uint32_t __atomic_fetch_or_4(volatile void* ptr, uint32_t val, int memorder);

uint64_t __atomic_load_8(const volatile void* ptr, int memorder);
void     __atomic_store_8(volatile void* ptr, uint64_t val, int memorder);
uint64_t __atomic_exchange_8(volatile void* ptr, uint64_t val, int memorder);
bool     __atomic_compare_exchange_8(volatile void* ptr, void* expected, uint64_t desired, //
                                     bool weak, int success_memorder, int failure_memorder);
uint64_t __atomic_fetch_add_8(volatile void* ptr, uint64_t val, int memorder);
uint64_t __atomic_fetch_sub_8(volatile void* ptr, uint64_t val, int memorder);
uint64_t __atomic_fetch_and_8(volatile void* ptr, uint64_t val, int memorder);
uint64_t __atomic_fetch_xor_8(volatile void* ptr, uint64_t val, int memorder);
uint64_t __atomic_fetch_or_8(volatile void* ptr, uint64_t val, int memorder);

#if defined(__SIZEOF_INT128__)
bool __atomic_compare_exchange_16(volatile void* ptr, void* expected, unsigned __int128 desired, //
                                  bool weak, int success_memorder, int failure_memorder);
#endif

void atomic_thread_fence(int memorder);

#else

/*
 * Use built-in atomics emulation
 */

#define ATOMIC_EMU32_IMPL  1
#define ATOMIC_EMU64_IMPL  1
#define ATOMIC_EMU128_IMPL 1

#endif

#ifndef ATOMIC_RELAXED
#define ATOMIC_RELAXED 0
#endif
#ifndef ATOMIC_CONSUME
#define ATOMIC_CONSUME 1
#endif
#ifndef ATOMIC_ACQUIRE
#define ATOMIC_ACQUIRE 2
#endif
#ifndef ATOMIC_RELEASE
#define ATOMIC_RELEASE 3
#endif
#ifndef ATOMIC_ACQ_REL
#define ATOMIC_ACQ_REL 4
#endif
#ifndef ATOMIC_SEQ_CST
#define ATOMIC_SEQ_CST 5
#endif

#if !defined(__SIZEOF_INT128__) || !defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16) /**/                                  \
    || !defined(HOST_LITTLE_ENDIAN) || defined(USE_NO_LIBATOMIC)
/*
 * Emulate 128-bit atomics
 */
#define ATOMIC_EMU128_IMPL 1
#endif

#if defined(__i386__) && !defined(__i586__) && !defined(__x86_64__) && defined(GNU_EXTS)
/*
 * Emulate 64-bit atomics on pre-586
 */
#define ATOMIC_EMU64_IMPL 1
#endif

#if !defined(__i386__) && !defined(__x86_64__) && defined(USE_NO_LIBATOMIC)
/*
 * Emulate 8-bit/16-bit atomics on non-x86 without libatomic
 */
#define ATOMIC_EMU8_IMPL  1
#define ATOMIC_EMU16_IMPL 1
#endif

#if defined(__riscv) && GCC_CHECK_VER(4, 1) && !GCC_CHECK_VER(14, 1)
/*
 * Workaround atomic CAS miscompilation on RISC-V GCC <14.1
 */
#define RISCV_CAS_WORKAROUND 1
#endif

#if defined(ATOMIC_EMU32_IMPL) || defined(ATOMIC_EMU64_IMPL) || defined(ATOMIC_EMU128_IMPL)
#if defined(USE_THREAD_EMU)
#define atomic_emu_lock(ptr)                                                                                           \
    do {                                                                                                               \
    } while (0)
#define atomic_emu_unlock(ptr) atomic_emu_lock(ptr)
#else
slow_path void atomic_emu_lock(const void* ptr);
slow_path void atomic_emu_unlock(const void* ptr);
#endif
#endif

static forceinline bool atomic_ordering_is_natural(int memorder)
{
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_AMD64)
    return memorder == ATOMIC_RELAXED //
        || memorder == ATOMIC_ACQUIRE //
        || memorder == ATOMIC_RELEASE //
        || memorder == ATOMIC_ACQ_REL;
#elif defined(__alpha__) || defined(__alpha) || defined(_M_ALPHA)
    return memorder == ATOMIC_RELAXED;
#else
    return memorder == ATOMIC_RELAXED || memorder == ATOMIC_CONSUME;
#endif
}

// Prevents compiler instruction reordering, special case use only!
static forceinline void atomic_compiler_barrier(void)
{
#if defined(GNU_EXTS)
    __asm__ __volatile__("" : : : "memory");
#elif defined(C11_ATOMICS_IMPL)
    atomic_signal_fence(memory_order_seq_cst);
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_AMD64))
    _ReadWriteBarrier();
#endif
}

/*
 * Atomic 32-bit operations
 */

static forceinline bool atomic_cas_uint32_ex(void* addr, uint32_t* exp, uint32_t val, //
                                             bool weak, int succ, int fail)
{
    UNUSED(weak && succ && fail);
#if defined(ATOMIC_EMU32_IMPL)
    uint32_t chck = *exp;
    atomic_emu_lock(addr);
    *exp = *(uint32_t*)addr;
    if (*exp == chck) {
        *(uint32_t*)addr = val;
    }
    atomic_emu_unlock(addr);
    return *exp == chck;
#elif defined(GNU_ATOMICS_IMPL) && !defined(RISCV_CAS_WORKAROUND)
    return __atomic_compare_exchange_n((uint32_t*)addr, exp, val, weak, succ, fail);
#elif defined(C11_ATOMICS_IMPL) && !defined(RISCV_CAS_WORKAROUND)
    if (weak) {
        return atomic_compare_exchange_weak_explicit((_Atomic uint32_t*)addr, exp, val, succ, fail);
    } else {
        return atomic_compare_exchange_strong_explicit((_Atomic uint32_t*)addr, exp, val, succ, fail);
    }
#elif defined(SYNC_ATOMICS_IMPL) || defined(RISCV_CAS_WORKAROUND)
    uint32_t chck = *exp;
    uint32_t orig = __sync_val_compare_and_swap((uint32_t*)addr, chck, val);
    *exp          = orig;
    return orig == chck;
#elif defined(WIN32_ATOMICS_IMPL) || defined(WINCE_ATOMICS_IMPL)
    uint32_t chck = *exp;
    uint32_t orig = InterlockedCompareExchange((LONG*)addr, val, chck);
    *exp          = orig;
    return orig == chck;
#elif defined(LIBATOMIC_IMPL)
    return __atomic_compare_exchange_4((uint32_t*)addr, exp, val, weak, succ, fail);
#endif
}

// Useful for RMW CAS loops
static forceinline bool atomic_cas_uint32_loop(void* addr, uint32_t* exp, uint32_t val)
{
    return atomic_cas_uint32_ex(addr, exp, val, true, ATOMIC_ACQ_REL, ATOMIC_RELAXED);
}

// Useful for performing A->B transition on known values
static forceinline bool atomic_cas_uint32_try(void* addr, uint32_t exp, uint32_t val, bool weak, int memorder)
{
    return atomic_cas_uint32_ex(addr, &exp, val, weak, memorder, ATOMIC_RELAXED);
}

static forceinline bool atomic_cas_uint32(void* addr, uint32_t exp, uint32_t val)
{
    return atomic_cas_uint32_try(addr, exp, val, false, ATOMIC_ACQ_REL);
}

static forceinline void atomic_fence_ex(int memorder)
{
    atomic_compiler_barrier();
    if (!atomic_ordering_is_natural(memorder)) {
#if defined(GNU_EXTS) && defined(__x86_64__)
        // Clang and older GCC use a sub-optimal `mfence` instruction for SEQ_CST fences
        __asm__ __volatile__("lock orq $0, (%%rsp)" : : : "memory");
#elif defined(GNU_ATOMICS_IMPL)
        __atomic_thread_fence(memorder);
#elif defined(C11_ATOMICS_IMPL) || defined(LIBATOMIC_IMPL)
        atomic_thread_fence(memorder);
#elif defined(SYNC_ATOMICS_IMPL)
        __sync_synchronize();
#elif defined(WIN32_ATOMICS_IMPL)
        MemoryBarrier();
#else
        uint32_t tmp = 0;
        atomic_cas_uint32(&tmp, 0, 0);
#endif
    }
}

static forceinline void atomic_fence(void)
{
    atomic_fence_ex(ATOMIC_ACQ_REL);
}

static forceinline uint32_t atomic_load_uint32_ex(const void* addr, int memorder)
{
    UNUSED(memorder);
#if !defined(ATOMIC_EMU32_IMPL) && defined(GNU_ATOMICS_IMPL)
    return __atomic_load_n(NONCONST_CAST(uint32_t*, addr), memorder);
#elif !defined(ATOMIC_EMU32_IMPL) && defined(C11_ATOMICS_IMPL)
    return atomic_load_explicit(NONCONST_CAST(_Atomic uint32_t*, addr), memorder);
#else
    if (likely(atomic_ordering_is_natural(memorder) && sizeof(void*) >= sizeof(uint32_t))) {
        return *(const safe_aliasing uint32_t*)addr;
    }
#if defined(LIBATOMIC_IMPL)
    return __atomic_load_4(NONCONST_CAST(uint32_t*, addr), memorder);
#else
    uint32_t tmp = 0;
    atomic_cas_uint32_ex(NONCONST_CAST(void*, addr), &tmp, tmp, true, memorder, memorder);
    return tmp;
#endif
#endif
}

static forceinline uint32_t atomic_load_uint32_relax(const void* addr)
{
    return atomic_load_uint32_ex(addr, ATOMIC_RELAXED);
}

static forceinline uint32_t atomic_load_uint32(const void* addr)
{
    return atomic_load_uint32_ex(addr, ATOMIC_ACQUIRE);
}

static forceinline uint32_t atomic_swap_uint32_ex(void* addr, uint32_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint32_t ret     = *(uint32_t*)addr;
    *(uint32_t*)addr = val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_exchange_n((uint32_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_exchange_explicit((_Atomic uint32_t*)addr, val, memorder);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedExchange((LONG*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_exchange_4((uint32_t*)addr, val, memorder);
#else
    uint32_t tmp = atomic_load_uint32_relax(addr);
    while (!atomic_cas_uint32_loop(addr, &tmp, val)) {
    }
    return tmp;
#endif
}

static forceinline uint32_t atomic_swap_uint32(void* addr, uint32_t val)
{
    return atomic_swap_uint32_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline void atomic_store_uint32_ex(void* addr, uint32_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_swap_uint32_ex(addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    __atomic_store_n((uint32_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    atomic_store_explicit((_Atomic uint32_t*)addr, val, memorder);
#else
    if (likely(atomic_ordering_is_natural(memorder) && sizeof(void*) >= sizeof(uint32_t))) {
        *(safe_aliasing uint32_t*)addr = val;
        return;
    }
#if defined(LIBATOMIC_IMPL)
    __atomic_store_4((uint32_t*)addr, val, memorder);
#else
    atomic_swap_uint32_ex(addr, val, memorder);
#endif
#endif
}

static forceinline void atomic_store_uint32_relax(void* addr, uint32_t val)
{
    atomic_store_uint32_ex(addr, val, ATOMIC_RELAXED);
}

static forceinline void atomic_store_uint32(void* addr, uint32_t val)
{
    atomic_store_uint32_ex(addr, val, ATOMIC_RELEASE);
}

static forceinline uint32_t atomic_add_uint32_ex(void* addr, uint32_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint32_t ret      = *(uint32_t*)addr;
    *(uint32_t*)addr += val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_add((uint32_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_fetch_add_explicit((_Atomic uint32_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_add((uint32_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedExchangeAdd((LONG*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_add_4((uint32_t*)addr, val, memorder);
#else
    uint32_t tmp = atomic_load_uint32_relax(addr);
    while (!atomic_cas_uint32_loop(addr, &tmp, tmp + val)) {
    }
    return tmp;
#endif
}

static forceinline uint32_t atomic_add_uint32(void* addr, uint32_t val)
{
    return atomic_add_uint32_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint32_t atomic_sub_uint32_ex(void* addr, uint32_t val, int memorder)
{
#if !defined(ATOMIC_EMU32_IMPL) && defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_sub((uint32_t*)addr, val, memorder);
#elif !defined(ATOMIC_EMU32_IMPL) && defined(C11_ATOMICS_IMPL)
    return atomic_fetch_sub_explicit((_Atomic uint32_t*)addr, val, memorder);
#else
    return atomic_add_uint32_ex(addr, -val, memorder);
#endif
}

static forceinline uint32_t atomic_sub_uint32(void* addr, uint32_t val)
{
    return atomic_sub_uint32_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint32_t atomic_and_uint32_ex(void* addr, uint32_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint32_t ret      = *(uint32_t*)addr;
    *(uint32_t*)addr &= val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_and((uint32_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_fetch_and_explicit((_Atomic uint32_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_and((uint32_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedAnd((LONG*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_and_4((uint32_t*)addr, val, memorder);
#else
    uint32_t tmp = atomic_load_uint32_relax(addr);
    while (!atomic_cas_uint32_loop(addr, &tmp, tmp & val)) {
    }
    return tmp;
#endif
}

static forceinline uint32_t atomic_and_uint32(void* addr, uint32_t val)
{
    return atomic_and_uint32_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint32_t atomic_xor_uint32_ex(void* addr, uint32_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint32_t ret      = *(uint32_t*)addr;
    *(uint32_t*)addr ^= val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_xor((uint32_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_fetch_xor_explicit((_Atomic uint32_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_xor((uint32_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedXor((LONG*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_xor_4((uint32_t*)addr, val, memorder);
#else
    uint32_t tmp = atomic_load_uint32_relax(addr);
    while (!atomic_cas_uint32_loop(addr, &tmp, tmp ^ val)) {
    }
    return tmp;
#endif
}

static forceinline uint32_t atomic_xor_uint32(void* addr, uint32_t val)
{
    return atomic_xor_uint32_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint32_t atomic_or_uint32_ex(void* addr, uint32_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint32_t ret      = *(uint32_t*)addr;
    *(uint32_t*)addr |= val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_or((uint32_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_fetch_or_explicit((_Atomic uint32_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_or((uint32_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedOr((LONG*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_or_4((uint32_t*)addr, val, memorder);
#else
    uint32_t tmp = atomic_load_uint32_relax(addr);
    while (!atomic_cas_uint32_loop(addr, &tmp, tmp | val)) {
    }
    return tmp;
#endif
}

static forceinline uint32_t atomic_or_uint32(void* addr, uint32_t val)
{
    return atomic_or_uint32_ex(addr, val, ATOMIC_ACQ_REL);
}

/*
 * Atomic 64-bit operations
 */

static forceinline bool atomic_cas_uint64_ex(void* addr, uint64_t* exp, uint64_t val, //
                                             bool weak, int succ, int fail)
{
    UNUSED(weak && succ && fail);
#if defined(ATOMIC_EMU64_IMPL)
    uint64_t chck = *exp;
    atomic_emu_lock(addr);
    *exp = *(uint64_t*)addr;
    if (*exp == chck) {
        *(uint64_t*)addr = val;
    }
    atomic_emu_unlock(addr);
    return *exp == chck;
#elif defined(GNU_ATOMICS_IMPL) && !defined(RISCV_CAS_WORKAROUND)
    return __atomic_compare_exchange_n((uint64_t*)addr, exp, val, weak, succ, fail);
#elif defined(C11_ATOMICS_IMPL) && !defined(RISCV_CAS_WORKAROUND)
    if (weak) {
        return atomic_compare_exchange_weak_explicit((_Atomic uint64_t*)addr, exp, val, succ, fail);
    } else {
        return atomic_compare_exchange_strong_explicit((_Atomic uint64_t*)addr, exp, val, succ, fail);
    }
#elif defined(SYNC_ATOMICS_IMPL) || defined(RISCV_CAS_WORKAROUND)
    uint64_t chck = *exp;
    uint64_t orig = __sync_val_compare_and_swap((uint64_t*)addr, chck, val);
    *exp          = orig;
    return orig == chck;
#elif defined(WIN32_ATOMICS_IMPL)
    uint64_t chck = *exp;
    uint64_t orig = InterlockedCompareExchange64((LONG64*)addr, val, chck);
    *exp          = orig;
    return orig == chck;
#elif defined(LIBATOMIC_IMPL)
    return __atomic_compare_exchange_8((uint64_t*)addr, exp, val, weak, succ, fail);
#endif
}

static forceinline bool atomic_cas_uint64_loop(void* addr, uint64_t* exp, uint64_t val)
{
    return atomic_cas_uint64_ex(addr, exp, val, true, ATOMIC_ACQ_REL, ATOMIC_RELAXED);
}

static forceinline bool atomic_cas_uint64_try(void* addr, uint64_t exp, uint64_t val, bool weak, int memorder)
{
    return atomic_cas_uint64_ex(addr, &exp, val, weak, memorder, ATOMIC_RELAXED);
}

static forceinline bool atomic_cas_uint64(void* addr, uint64_t exp, uint64_t val)
{
    return atomic_cas_uint64_try(addr, exp, val, false, ATOMIC_ACQ_REL);
}

static forceinline uint64_t atomic_load_uint64_ex(const void* addr, int memorder)
{
    UNUSED(memorder);
#if !defined(ATOMIC_EMU64_IMPL) && defined(GNU_ATOMICS_IMPL)
    return __atomic_load_n(NONCONST_CAST(uint64_t*, addr), memorder);
#elif !defined(ATOMIC_EMU64_IMPL) && defined(C11_ATOMICS_IMPL)
    return atomic_load_explicit(NONCONST_CAST(_Atomic uint64_t*, addr), memorder);
#else
    if (likely(atomic_ordering_is_natural(memorder) && sizeof(void*) >= sizeof(uint64_t))) {
        return *(const safe_aliasing uint64_t*)addr;
    }
#if defined(LIBATOMIC_IMPL)
    return __atomic_load_8(NONCONST_CAST(uint64_t*, addr), memorder);
#else
    uint64_t tmp = 0;
    atomic_cas_uint64_ex(NONCONST_CAST(void*, addr), &tmp, tmp, true, memorder, memorder);
    return tmp;
#endif
#endif
}

static forceinline uint64_t atomic_load_uint64_relax(const void* addr)
{
    return atomic_load_uint64_ex(addr, ATOMIC_RELAXED);
}

static forceinline uint64_t atomic_load_uint64(const void* addr)
{
    return atomic_load_uint64_ex(addr, ATOMIC_ACQUIRE);
}

static forceinline uint64_t atomic_swap_uint64_ex(void* addr, uint64_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU64_IMPL)
    atomic_emu_lock(addr);
    uint64_t ret     = *(uint64_t*)addr;
    *(uint64_t*)addr = val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_exchange_n((uint64_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_exchange_explicit((_Atomic uint64_t*)addr, val, memorder);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedExchange64((LONG64*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_exchange_8((uint32_t*)addr, val, memorder);
#else
    uint64_t tmp = atomic_load_uint64_relax(addr);
    while (!atomic_cas_uint64_loop(addr, &tmp, val)) {
    }
    return tmp;
#endif
}

static forceinline uint64_t atomic_swap_uint64(void* addr, uint64_t val)
{
    return atomic_swap_uint64_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline void atomic_store_uint64_ex(void* addr, uint64_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU64_IMPL)
    atomic_swap_uint64_ex(addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL)
    __atomic_store_n((uint64_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    atomic_store_explicit((_Atomic uint64_t*)addr, val, memorder);
#else
    if (likely(atomic_ordering_is_natural(memorder) && sizeof(void*) >= sizeof(uint64_t))) {
        *(safe_aliasing uint64_t*)addr = val;
        return;
    }
#if defined(LIBATOMIC_IMPL)
    __atomic_store_8((uint32_t*)addr, val, memorder);
#else
    atomic_swap_uint64_ex(addr, val, memorder);
#endif
#endif
}

static forceinline void atomic_store_uint64_relax(void* addr, uint64_t val)
{
    atomic_store_uint64_ex(addr, val, ATOMIC_RELAXED);
}

static forceinline void atomic_store_uint64(void* addr, uint64_t val)
{
    atomic_store_uint64_ex(addr, val, ATOMIC_RELEASE);
}

static forceinline uint64_t atomic_add_uint64_ex(void* addr, uint64_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU64_IMPL)
    atomic_emu_lock(addr);
    uint64_t ret      = *(uint64_t*)addr;
    *(uint64_t*)addr += val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_add((uint64_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_fetch_add_explicit((_Atomic uint64_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_add((uint64_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedExchangeAdd64((LONG64*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_add_8((uint64_t*)addr, val, memorder);
#else
    uint64_t tmp = atomic_load_uint64_relax(addr);
    while (!atomic_cas_uint64_loop(addr, &tmp, tmp + val)) {
    }
    return tmp;
#endif
}

static forceinline uint64_t atomic_add_uint64(void* addr, uint64_t val)
{
    return atomic_add_uint64_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint64_t atomic_sub_uint64_ex(void* addr, uint64_t val, int memorder)
{
#if defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_sub((uint64_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_fetch_sub_explicit((_Atomic uint64_t*)addr, val, memorder);
#else
    return atomic_add_uint64_ex(addr, -val, memorder);
#endif
}

static forceinline uint64_t atomic_sub_uint64(void* addr, uint64_t val)
{
    return atomic_sub_uint64_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint64_t atomic_and_uint64_ex(void* addr, uint64_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU64_IMPL)
    atomic_emu_lock(addr);
    uint64_t ret      = *(uint64_t*)addr;
    *(uint64_t*)addr &= val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_and((uint64_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_fetch_and_explicit((_Atomic uint64_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_and((uint64_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedAnd64((LONG64*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_and_8((uint64_t*)addr, val, memorder);
#else
    uint64_t tmp = atomic_load_uint64_relax(addr);
    while (!atomic_cas_uint64_loop(addr, &tmp, tmp & val)) {
    }
    return tmp;
#endif
}

static forceinline uint64_t atomic_and_uint64(void* addr, uint64_t val)
{
    return atomic_and_uint64_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint64_t atomic_xor_uint64_ex(void* addr, uint64_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU64_IMPL)
    atomic_emu_lock(addr);
    uint64_t ret      = *(uint64_t*)addr;
    *(uint64_t*)addr ^= val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_xor((uint64_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_fetch_xor_explicit((_Atomic uint64_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_xor((uint64_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedXor64((LONG64*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_xor_8((uint64_t*)addr, val, memorder);
#else
    uint64_t tmp = atomic_load_uint64_relax(addr);
    while (!atomic_cas_uint64_loop(addr, &tmp, tmp ^ val)) {
    }
    return tmp;
#endif
}

static forceinline uint64_t atomic_xor_uint64(void* addr, uint64_t val)
{
    return atomic_xor_uint64_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint64_t atomic_or_uint64_ex(void* addr, uint64_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU64_IMPL)
    atomic_emu_lock(addr);
    uint64_t ret      = *(uint64_t*)addr;
    *(uint64_t*)addr |= val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL)
    return __atomic_fetch_or((uint64_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_fetch_or_explicit((_Atomic uint64_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL)
    return __sync_fetch_and_or((uint64_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedOr64((LONG64*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_or_8((uint64_t*)addr, val, memorder);
#else
    uint64_t tmp = atomic_load_uint64_relax(addr);
    while (!atomic_cas_uint64_loop(addr, &tmp, tmp | val)) {
    }
    return tmp;
#endif
}

static forceinline uint64_t atomic_or_uint64(void* addr, uint64_t val)
{
    return atomic_or_uint64_ex(addr, val, ATOMIC_ACQ_REL);
}

/*
 * Atomic 128-bit operations
 */

static forceinline bool atomic_cas_uint128_ex(void* addr, uint64_t* exp, const uint64_t* val, //
                                              bool weak, int succ, int fail)
{
    UNUSED(weak && succ && fail);
#if defined(ATOMIC_EMU128_IMPL)
    uint64_t chck[2] = {exp[0], exp[1]};
    atomic_emu_lock(addr);
    exp[0] = ((uint64_t*)addr)[0];
    exp[1] = ((uint64_t*)addr)[1];
    if (exp[0] == chck[0] && exp[1] == chck[1]) {
        ((uint64_t*)addr)[0] = val[0];
        ((uint64_t*)addr)[1] = val[1];
    }
    atomic_emu_unlock(addr);
    return exp[0] == chck[0] && exp[1] == chck[1];
#elif defined(GNU_ATOMICS_IMPL) || defined(SYNC_ATOMICS_IMPL)
    __int128 chck = exp[0] | (((__int128)exp[1]) << 64);
    __int128 v128 = val[0] | (((__int128)val[1]) << 64);
    __int128 orig = __sync_val_compare_and_swap((__int128*)addr, chck, v128);
    exp[0]        = orig;
    exp[1]        = orig >> 64;
    return orig == chck;
#elif defined(LIBATOMIC_IMPL)
    return __atomic_compare_exchange_16((uint64_t*)addr, exp, val, weak, succ, fail);
#endif
}

/*
 * Atomic 8-bit operations
 */

static forceinline bool atomic_cas_uint8_ex(void* addr, uint8_t* exp, uint8_t val, //
                                            bool weak, int succ, int fail)
{
    UNUSED(weak && succ && fail);
#if defined(ATOMIC_EMU32_IMPL)
    uint8_t chck = *exp;
    atomic_emu_lock(addr);
    *exp = *(uint8_t*)addr;
    if (*exp == chck) {
        *(uint8_t*)addr = val;
    }
    atomic_emu_unlock(addr);
    return *exp == chck;
#elif defined(GNU_ATOMICS_IMPL) && !defined(RISCV_CAS_WORKAROUND) && !defined(ATOMIC_EMU8_IMPL)
    return __atomic_compare_exchange_n((uint8_t*)addr, exp, val, weak, succ, fail);
#elif defined(C11_ATOMICS_IMPL) && !defined(RISCV_CAS_WORKAROUND) && !defined(ATOMIC_EMU8_IMPL)
    if (weak) {
        return atomic_compare_exchange_weak_explicit((_Atomic uint8_t*)addr, exp, val, succ, fail);
    } else {
        return atomic_compare_exchange_strong_explicit((_Atomic uint8_t*)addr, exp, val, succ, fail);
    }
#elif (defined(SYNC_ATOMICS_IMPL) || defined(RISCV_CAS_WORKAROUND)) && !defined(ATOMIC_EMU8_IMPL)
    uint8_t chck = *exp;
    uint8_t orig = __sync_val_compare_and_swap((uint8_t*)addr, chck, val);
    *exp         = orig;
    return orig == chck;
#elif defined(LIBATOMIC_IMPL)
    return __atomic_compare_exchange_1((uint32_t*)addr, exp, val, weak, succ, fail);
#else
    size_t   byte_u32 = ((size_t)addr) & 3;
    void*    addr_u32 = (void*)(((uint8_t*)addr) - byte_u32);
    uint32_t shft_u32 = byte_u32 << 3;
    uint32_t mask_u32 = 0xFFU << shft_u32;
    uint32_t exp_u32  = ((uint32_t)*exp) << shft_u32;
    uint32_t val_u32  = ((uint32_t)val) << shft_u32;
    uint32_t tmp_u32  = atomic_load_uint32_relax(addr_u32);
    do {
        if ((tmp_u32 & mask_u32) != exp_u32) {
            return false;
        }
    } while (!atomic_cas_uint32_loop(addr_u32, &tmp_u32, (tmp_u32 & ~mask_u32) | val_u32));
    *exp = tmp_u32 >> shft_u32;
    return true;
#endif
}

static forceinline bool atomic_cas_uint8_loop(void* addr, uint8_t* exp, uint8_t val)
{
    return atomic_cas_uint8_ex(addr, exp, val, true, ATOMIC_ACQ_REL, ATOMIC_RELAXED);
}

static forceinline bool atomic_cas_uint8_try(void* addr, uint8_t exp, uint8_t val, bool weak, int memorder)
{
    return atomic_cas_uint8_ex(addr, &exp, val, weak, memorder, ATOMIC_RELAXED);
}

static forceinline bool atomic_cas_uint8(void* addr, uint8_t exp, uint8_t val)
{
    return atomic_cas_uint8_try(addr, exp, val, false, ATOMIC_ACQ_REL);
}

static forceinline uint8_t atomic_load_uint8_ex(const void* addr, int memorder)
{
    UNUSED(memorder);
#if !defined(ATOMIC_EMU32_IMPL) && defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return __atomic_load_n(NONCONST_CAST(uint8_t*, addr), memorder);
#elif !defined(ATOMIC_EMU32_IMPL) && defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return atomic_load_explicit(NONCONST_CAST(_Atomic uint8_t*, addr), memorder);
#else
    if (likely(atomic_ordering_is_natural(memorder))) {
        return *(const safe_aliasing uint8_t*)addr;
    }
#if defined(LIBATOMIC_IMPL)
    return __atomic_load_1(NONCONST_CAST(uint8_t*, addr), memorder);
#else
    uint8_t tmp = 0;
    atomic_cas_uint8_ex(NONCONST_CAST(void*, addr), &tmp, tmp, true, memorder, memorder);
    return tmp;
#endif
#endif
}

static forceinline uint8_t atomic_load_uint8_relax(const void* addr)
{
    return atomic_load_uint8_ex(addr, ATOMIC_RELAXED);
}

static forceinline uint8_t atomic_load_uint8(const void* addr)
{
    return atomic_load_uint8_ex(addr, ATOMIC_ACQUIRE);
}

static forceinline uint8_t atomic_swap_uint8_ex(void* addr, uint8_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint8_t ret     = *(uint8_t*)addr;
    *(uint8_t*)addr = val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return __atomic_exchange_n((uint8_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return atomic_exchange_explicit((_Atomic uint8_t*)addr, val, memorder);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_exchange_1((uint8_t*)addr, val, memorder);
#else
    uint8_t tmp = atomic_load_uint8_relax(addr);
    while (!atomic_cas_uint8_loop(addr, &tmp, val)) {
    }
    return tmp;
#endif
}

static forceinline uint8_t atomic_swap_uint8(void* addr, uint8_t val)
{
    return atomic_swap_uint8_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline void atomic_store_uint8_ex(void* addr, uint8_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_swap_uint8_ex(addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    __atomic_store_n((uint8_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    atomic_store_explicit((_Atomic uint8_t*)addr, val, memorder);
#else
    if (likely(atomic_ordering_is_natural(memorder))) {
        *(safe_aliasing uint8_t*)addr = val;
        return;
    }
#if defined(LIBATOMIC_IMPL)
    __atomic_store_1((uint8_t*)addr, val, memorder);
#else
    atomic_swap_uint8_ex(addr, val, memorder);
#endif
#endif
}

static forceinline void atomic_store_uint8_relax(void* addr, uint8_t val)
{
    atomic_store_uint8_ex(addr, val, ATOMIC_RELAXED);
}

static forceinline void atomic_store_uint8(void* addr, uint8_t val)
{
    atomic_store_uint8_ex(addr, val, ATOMIC_RELEASE);
}

static forceinline uint8_t atomic_add_uint8_ex(void* addr, uint8_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint8_t ret      = *(uint8_t*)addr;
    *(uint8_t*)addr += val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return __atomic_fetch_add((uint8_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return atomic_fetch_add_explicit((_Atomic uint8_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return __sync_fetch_and_add((uint8_t*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_add_1((uint8_t*)addr, val, memorder);
#else
    uint8_t tmp = atomic_load_uint8_relax(addr);
    while (!atomic_cas_uint8_loop(addr, &tmp, tmp + val)) {
    }
    return tmp;
#endif
}

static forceinline uint8_t atomic_add_uint8(void* addr, uint8_t val)
{
    return atomic_add_uint8_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint8_t atomic_sub_uint8_ex(void* addr, uint8_t val, int memorder)
{
#if !defined(ATOMIC_EMU32_IMPL) && defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return __atomic_fetch_sub((uint8_t*)addr, val, memorder);
#elif !defined(ATOMIC_EMU32_IMPL) && defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return atomic_fetch_sub_explicit((_Atomic uint8_t*)addr, val, memorder);
#else
    return atomic_add_uint8_ex(addr, -val, memorder);
#endif
}

static forceinline uint8_t atomic_sub_uint8(void* addr, uint8_t val)
{
    return atomic_sub_uint8_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint8_t atomic_and_uint8_ex(void* addr, uint8_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint8_t ret      = *(uint8_t*)addr;
    *(uint8_t*)addr &= val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return __atomic_fetch_and((uint8_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return atomic_fetch_and_explicit((_Atomic uint8_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return __sync_fetch_and_and((uint8_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedAnd8((CHAR*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_and_1((uint8_t*)addr, val, memorder);
#else
    size_t   byte_u32 = ((size_t)addr) & 3;
    void*    addr_u32 = (void*)(((uint8_t*)addr) - byte_u32);
    uint32_t shft_u32 = byte_u32 << 3;
    uint32_t val_u32  = (val << shft_u32) | ~(0xFFU << shft_u32);
    return atomic_and_uint32_ex(addr_u32, val_u32, memorder) >> shft_u32;
#endif
}

static forceinline uint8_t atomic_and_uint8(void* addr, uint8_t val)
{
    return atomic_and_uint8_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint8_t atomic_xor_uint8_ex(void* addr, uint8_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint8_t ret      = *(uint8_t*)addr;
    *(uint8_t*)addr ^= val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return __atomic_fetch_xor((uint8_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return atomic_fetch_xor_explicit((_Atomic uint8_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return __sync_fetch_and_xor((uint8_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedXor8((CHAR*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_xor_1((uint8_t*)addr, val, memorder);
#else
    size_t   byte_u32 = ((size_t)addr) & 3;
    void*    addr_u32 = (void*)(((uint8_t*)addr) - byte_u32);
    uint32_t shft_u32 = byte_u32 << 3;
    return atomic_xor_uint32_ex(addr_u32, val << shft_u32, memorder) >> shft_u32;
#endif
}

static forceinline uint8_t atomic_xor_uint8(void* addr, uint8_t val)
{
    return atomic_xor_uint8_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint8_t atomic_or_uint8_ex(void* addr, uint8_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint8_t ret      = *(uint8_t*)addr;
    *(uint8_t*)addr |= val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return __atomic_fetch_or((uint8_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return atomic_fetch_or_explicit((_Atomic uint8_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL) && !defined(ATOMIC_EMU8_IMPL)
    return __sync_fetch_and_or((uint8_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedOr8((CHAR*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_or_1((uint8_t*)addr, val, memorder);
#else
    size_t   byte_u32 = ((size_t)addr) & 3;
    void*    addr_u32 = (void*)(((uint8_t*)addr) - byte_u32);
    uint32_t shft_u32 = byte_u32 << 3;
    return atomic_or_uint32_ex(addr_u32, val << shft_u32, memorder) >> shft_u32;
#endif
}

static forceinline uint8_t atomic_or_uint8(void* addr, uint8_t val)
{
    return atomic_or_uint8_ex(addr, val, ATOMIC_ACQ_REL);
}

/*
 * Atomic 16-bit operations
 */

static forceinline bool atomic_cas_uint16_ex(void* addr, uint16_t* exp, uint16_t val, //
                                             bool weak, int succ, int fail)
{
    UNUSED(weak && succ && fail);
#if defined(ATOMIC_EMU32_IMPL)
    uint16_t chck = *exp;
    atomic_emu_lock(addr);
    *exp = *(uint16_t*)addr;
    if (*exp == chck) {
        *(uint16_t*)addr = val;
    }
    atomic_emu_unlock(addr);
    return *exp == chck;
#elif defined(GNU_ATOMICS_IMPL) && !defined(RISCV_CAS_WORKAROUND) && !defined(ATOMIC_EMU16_IMPL)
    return __atomic_compare_exchange_n((uint16_t*)addr, exp, val, weak, succ, fail);
#elif defined(C11_ATOMICS_IMPL) && !defined(RISCV_CAS_WORKAROUND) && !defined(ATOMIC_EMU16_IMPL)
    if (weak) {
        return atomic_compare_exchange_weak_explicit((_Atomic uint16_t*)addr, exp, val, succ, fail);
    } else {
        return atomic_compare_exchange_strong_explicit((_Atomic uint16_t*)addr, exp, val, succ, fail);
    }
#elif (defined(SYNC_ATOMICS_IMPL) || defined(RISCV_CAS_WORKAROUND)) && !defined(ATOMIC_EMU16_IMPL)
    uint16_t chck = *exp;
    uint16_t orig = __sync_val_compare_and_swap((uint16_t*)addr, chck, val);
    *exp          = orig;
    return orig == chck;
#elif defined(WIN32_ATOMICS_IMPL)
    uint16_t chck = *exp;
    uint16_t orig = InterlockedCompareExchange16((SHORT*)addr, val, chck);
    *exp          = orig;
    return orig == chck;
#elif defined(LIBATOMIC_IMPL)
    return __atomic_compare_exchange_2((uint16_t*)addr, exp, val, weak, succ, fail);
#else
    size_t   byte_u32 = ((size_t)addr) & 2;
    void*    addr_u32 = (void*)(((uint8_t*)addr) - byte_u32);
    uint32_t shft_u32 = byte_u32 << 3;
    uint32_t mask_u32 = 0xFFFFU << shft_u32;
    uint32_t exp_u32  = ((uint32_t)*exp) << shft_u32;
    uint32_t val_u32  = ((uint32_t)val) << shft_u32;
    uint32_t tmp_u32  = atomic_load_uint32_relax(addr_u32);
    do {
        if ((tmp_u32 & mask_u32) != exp_u32) {
            return false;
        }
    } while (!atomic_cas_uint32_loop(addr_u32, &tmp_u32, (tmp_u32 & ~mask_u32) | val_u32));
    *exp = tmp_u32 >> shft_u32;
    return true;
#endif
}

static forceinline bool atomic_cas_uint16_loop(void* addr, uint16_t* exp, uint16_t val)
{
    return atomic_cas_uint16_ex(addr, exp, val, true, ATOMIC_ACQ_REL, ATOMIC_RELAXED);
}

static forceinline bool atomic_cas_uint16_try(void* addr, uint16_t exp, uint16_t val, bool weak, int memorder)
{
    return atomic_cas_uint16_ex(addr, &exp, val, weak, memorder, ATOMIC_RELAXED);
}

static forceinline bool atomic_cas_uint16(void* addr, uint16_t exp, uint16_t val)
{
    return atomic_cas_uint16_try(addr, exp, val, false, ATOMIC_ACQ_REL);
}

static forceinline uint16_t atomic_load_uint16_ex(const void* addr, int memorder)
{
    UNUSED(memorder);
#if !defined(ATOMIC_EMU32_IMPL) && defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return __atomic_load_n(NONCONST_CAST(uint16_t*, addr), memorder);
#elif !defined(ATOMIC_EMU32_IMPL) && defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return atomic_load_explicit(NONCONST_CAST(_Atomic uint16_t*, addr), memorder);
#else
    if (likely(atomic_ordering_is_natural(memorder))) {
        return *(const safe_aliasing uint16_t*)addr;
    }
#if defined(LIBATOMIC_IMPL)
    return __atomic_load_2(NONCONST_CAST(uint16_t*, addr), memorder);
#else
    uint16_t tmp = 0;
    atomic_cas_uint16_ex(NONCONST_CAST(void*, addr), &tmp, tmp, true, memorder, memorder);
    return tmp;
#endif
#endif
}

static forceinline uint16_t atomic_load_uint16_relax(const void* addr)
{
    return atomic_load_uint16_ex(addr, ATOMIC_RELAXED);
}

static forceinline uint16_t atomic_load_uint16(const void* addr)
{
    return atomic_load_uint16_ex(addr, ATOMIC_ACQUIRE);
}

static forceinline uint16_t atomic_swap_uint16_ex(void* addr, uint16_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint16_t ret     = *(uint16_t*)addr;
    *(uint16_t*)addr = val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return __atomic_exchange_n((uint16_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return atomic_exchange_explicit((_Atomic uint16_t*)addr, val, memorder);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_exchange_2((uint16_t*)addr, val, memorder);
#else
    uint16_t tmp = atomic_load_uint16_relax(addr);
    while (!atomic_cas_uint16_loop(addr, &tmp, val)) {
    }
    return tmp;
#endif
}

static forceinline uint16_t atomic_swap_uint16(void* addr, uint16_t val)
{
    return atomic_swap_uint16_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline void atomic_store_uint16_ex(void* addr, uint16_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_swap_uint16_ex(addr, val, memorder);
#elif defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    __atomic_store_n((uint16_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    atomic_store_explicit((_Atomic uint16_t*)addr, val, memorder);
#else
    if (likely(atomic_ordering_is_natural(memorder))) {
        *(safe_aliasing uint16_t*)addr = val;
        return;
    }
#if defined(LIBATOMIC_IMPL)
    __atomic_store_2((uint16_t*)addr, val, memorder);
#else
    atomic_swap_uint16_ex(addr, val, memorder);
#endif
#endif
}

static forceinline void atomic_store_uint16_relax(void* addr, uint16_t val)
{
    atomic_store_uint16_ex(addr, val, ATOMIC_RELAXED);
}

static forceinline void atomic_store_uint16(void* addr, uint16_t val)
{
    atomic_store_uint16_ex(addr, val, ATOMIC_RELEASE);
}

static forceinline uint16_t atomic_add_uint16_ex(void* addr, uint16_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint16_t ret      = *(uint16_t*)addr;
    *(uint16_t*)addr += val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return __atomic_fetch_add((uint16_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return atomic_fetch_add_explicit((_Atomic uint16_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return __sync_fetch_and_add((uint16_t*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_add_2((uint16_t*)addr, val, memorder);
#else
    uint16_t tmp = atomic_load_uint16_relax(addr);
    while (!atomic_cas_uint16_loop(addr, &tmp, tmp + val)) {
    }
    return tmp;
#endif
}

static forceinline uint16_t atomic_add_uint16(void* addr, uint16_t val)
{
    return atomic_add_uint16_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint16_t atomic_sub_uint16_ex(void* addr, uint16_t val, int memorder)
{
#if !defined(ATOMIC_EMU32_IMPL) && defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return __atomic_fetch_sub((uint16_t*)addr, val, memorder);
#elif !defined(ATOMIC_EMU32_IMPL) && defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return atomic_fetch_sub_explicit((_Atomic uint16_t*)addr, val, memorder);
#else
    return atomic_add_uint16_ex(addr, -val, memorder);
#endif
}

static forceinline uint16_t atomic_sub_uint16(void* addr, uint16_t val)
{
    return atomic_sub_uint16_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint16_t atomic_and_uint16_ex(void* addr, uint16_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint16_t ret      = *(uint16_t*)addr;
    *(uint16_t*)addr &= val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return __atomic_fetch_and((uint16_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return atomic_fetch_and_explicit((_Atomic uint16_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return __sync_fetch_and_and((uint16_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedAnd16((SHORT*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_and_2((uint16_t*)addr, val, memorder);
#else
    size_t   byte_u32 = ((size_t)addr) & 2;
    void*    addr_u32 = (void*)(((uint8_t*)addr) - byte_u32);
    uint32_t shft_u32 = byte_u32 << 3;
    uint32_t val_u32  = (val << shft_u32) | ~(0xFFFFU << shft_u32);
    return atomic_and_uint32_ex(addr_u32, val_u32, memorder) >> shft_u32;
#endif
}

static forceinline uint16_t atomic_and_uint16(void* addr, uint16_t val)
{
    return atomic_and_uint16_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint16_t atomic_xor_uint16_ex(void* addr, uint16_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint16_t ret      = *(uint16_t*)addr;
    *(uint16_t*)addr ^= val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return __atomic_fetch_xor((uint16_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return atomic_fetch_xor_explicit((_Atomic uint16_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return __sync_fetch_and_xor((uint16_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedXor16((SHORT*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_xor_2((uint16_t*)addr, val, memorder);
#else
    size_t   byte_u32 = ((size_t)addr) & 2;
    void*    addr_u32 = (void*)(((uint8_t*)addr) - byte_u32);
    uint32_t shft_u32 = byte_u32 << 3;
    return atomic_xor_uint32_ex(addr_u32, val << shft_u32, memorder) >> shft_u32;
#endif
}

static forceinline uint16_t atomic_xor_uint16(void* addr, uint16_t val)
{
    return atomic_xor_uint16_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline uint16_t atomic_or_uint16_ex(void* addr, uint16_t val, int memorder)
{
    UNUSED(memorder);
#if defined(ATOMIC_EMU32_IMPL)
    atomic_emu_lock(addr);
    uint16_t ret      = *(uint16_t*)addr;
    *(uint16_t*)addr |= val;
    atomic_emu_unlock(addr);
    return ret;
#elif defined(GNU_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return __atomic_fetch_or((uint16_t*)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return atomic_fetch_or_explicit((_Atomic uint16_t*)addr, val, memorder);
#elif defined(SYNC_ATOMICS_IMPL) && !defined(ATOMIC_EMU16_IMPL)
    return __sync_fetch_and_or((uint16_t*)addr, val);
#elif defined(WIN32_ATOMICS_IMPL)
    return InterlockedOr16((SHORT*)addr, val);
#elif defined(LIBATOMIC_IMPL)
    return __atomic_fetch_or_2((uint16_t*)addr, val, memorder);
#else
    size_t   byte_u32 = ((size_t)addr) & 2;
    void*    addr_u32 = (void*)(((uint8_t*)addr) - byte_u32);
    uint32_t shft_u32 = byte_u32 << 3;
    return atomic_or_uint32_ex(addr_u32, val << shft_u32, memorder) >> shft_u32;
#endif
}

static forceinline uint16_t atomic_or_uint16(void* addr, uint16_t val)
{
    return atomic_or_uint16_ex(addr, val, ATOMIC_ACQ_REL);
}

/*
 * Atomic pointer operations
 */

static forceinline void* atomic_load_pointer_ex(const void* addr, int memorder)
{
#if defined(GNU_ATOMICS_IMPL)
    return __atomic_load_n(NONCONST_CAST(void**, addr), memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_load_explicit(NONCONST_CAST(void* _Atomic*, addr), memorder);
#elif defined(HOST_64BIT)
    return (void*)(size_t)atomic_load_uint64_ex(addr, memorder);
#elif defined(HOST_32BIT)
    return (void*)(size_t)atomic_load_uint32_ex(addr, memorder);
#elif defined(HOST_16BIT)
    return (void*)(size_t)atomic_load_uint16_ex(addr, memorder);
#else
#error Unknown CPU bitness and no C11/GNU atomics!
#endif
}

static forceinline void* atomic_load_pointer(const void* addr)
{
#if defined(__SANITIZE_THREAD__) || defined(__alpha__) || defined(__alpha) || defined(_M_ALPHA)
    // Consume ordering only matters for DEC Alpha, yet it carries redundant pessimizations
    // on most compilers for other CPUs. Also, make ThreadSanitizer happy.
    return atomic_load_pointer_ex(addr, ATOMIC_CONSUME);
#else
    // A compiler barrier is enough here for a standard-compliant consume ordering.
    void* ret = atomic_load_pointer_ex(addr, ATOMIC_RELAXED);
    atomic_compiler_barrier();
    return ret;
#endif
}

static forceinline bool atomic_cas_pointer_ex(void* addr, void** exp, void* val, bool weak, int succ, int fail)
{
#if defined(GNU_ATOMICS_IMPL) && !defined(RISCV_CAS_WORKAROUND)
    return __atomic_compare_exchange_n((void**)addr, exp, val, weak, succ, fail);
#elif defined(C11_ATOMICS_IMPL) && !defined(RISCV_CAS_WORKAROUND)
    if (weak) {
        return atomic_compare_exchange_weak_explicit((void* _Atomic*)addr, exp, val, succ, fail);
    } else {
        return atomic_compare_exchange_strong_explicit((void* _Atomic*)addr, exp, val, succ, fail);
    }
#elif defined(HOST_64BIT)
    return atomic_cas_uint64_ex(addr, (uint64_t*)exp, (size_t)val, weak, succ, fail);
#elif defined(HOST_32BIT)
    return atomic_cas_uint32_ex(addr, (uint32_t*)exp, (size_t)val, weak, succ, fail);
#elif defined(HOST_16BIT)
    return atomic_cas_uint16_ex(addr, (uint16_t*)exp, (size_t)val, weak, succ, fail);
#else
#error Unknown CPU bitness and no C11/GNU atomics!
#endif
}

static forceinline bool atomic_cas_pointer(void* addr, void* exp, void* val)
{
    return atomic_cas_pointer_ex(addr, &exp, val, false, ATOMIC_ACQ_REL, ATOMIC_RELAXED);
}

static forceinline void* atomic_swap_pointer_ex(void* addr, void* val, int memorder)
{
#if defined(GNU_ATOMICS_IMPL)
    return __atomic_exchange_n((void**)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    return atomic_exchange_explicit((void* _Atomic*)addr, val, memorder);
#elif defined(HOST_64BIT)
    return (void*)(size_t)atomic_swap_uint64_ex(addr, (size_t)val, memorder);
#elif defined(HOST_32BIT)
    return (void*)(size_t)atomic_swap_uint32_ex(addr, (size_t)val, memorder);
#elif defined(HOST_16BIT)
    return (void*)(size_t)atomic_swap_uint16_ex(addr, (size_t)val, memorder);
#else
#error Unknown CPU bitness and no C11/GNU atomics!
#endif
}

static forceinline void* atomic_swap_pointer(void* addr, void* val)
{
    return atomic_swap_pointer_ex(addr, val, ATOMIC_ACQ_REL);
}

static forceinline void atomic_store_pointer_ex(void* addr, void* val, int memorder)
{
#if defined(GNU_ATOMICS_IMPL)
    __atomic_store_n((void**)addr, val, memorder);
#elif defined(C11_ATOMICS_IMPL)
    atomic_store_explicit((void* _Atomic*)addr, val, memorder);
#elif defined(HOST_64BIT)
    atomic_store_uint64_ex(addr, (size_t)val, memorder);
#elif defined(HOST_32BIT)
    atomic_store_uint32_ex(addr, (size_t)val, memorder);
#elif defined(HOST_16BIT)
    atomic_store_uint16_ex(addr, (size_t)val, memorder);
#else
    atomic_swap_pointer_ex(addr, val, memorder);
#endif
}

static forceinline void atomic_store_pointer(void* addr, void* val)
{
    atomic_store_pointer_ex(addr, val, ATOMIC_RELEASE);
}

/*
 * Little-endian atomics for shared memory / emulation
 */

static inline uint32_t atomic_load_uint32_le(const void* addr)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_load_uint32(addr);
#else
    uint32_t val = atomic_load_uint32(addr);
    return read_uint32_le(&val);
#endif
}

static inline void atomic_store_uint32_le(void* addr, uint32_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    atomic_store_uint32(addr, val);
#else
    write_uint32_le(&val, val);
    atomic_store_uint32(addr, val);
#endif
}

static inline bool atomic_cas_uint32_le(void* addr, uint32_t exp, uint32_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_cas_uint32(addr, exp, val);
#else
    write_uint32_le(&exp, exp);
    write_uint32_le(&val, val);
    return atomic_cas_uint32(addr, exp, val);
#endif
}

static inline uint32_t atomic_swap_uint32_le(void* addr, uint32_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_swap_uint32(addr, val);
#else
    write_uint32_le(&val, val);
    val = atomic_swap_uint32(addr, val);
    return read_uint32_le(&val);
#endif
}

static inline uint32_t atomic_add_uint32_le(void* addr, uint32_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_add_uint32(addr, val);
#else
    uint32_t tmp = atomic_load_uint32_relax(addr);
    uint32_t res;
    do {
        write_uint32_le(&res, read_uint32_le(&tmp) + val);
    } while (!atomic_cas_uint32_loop(addr, &tmp, res));
    return read_uint32_le(&tmp);
#endif
}

static inline uint32_t atomic_sub_uint32_le(void* addr, uint32_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_sub_uint32(addr, val);
#else
    return atomic_add_uint32_le(addr, -val);
#endif
}

static inline uint32_t atomic_and_uint32_le(void* addr, uint32_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_and_uint32(addr, val);
#else
    write_uint32_le(&val, val);
    val = atomic_and_uint32(addr, val);
    return read_uint32_le(&val);
#endif
}

static inline uint32_t atomic_xor_uint32_le(void* addr, uint32_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_xor_uint32(addr, val);
#else
    write_uint32_le(&val, val);
    val = atomic_xor_uint32(addr, val);
    return read_uint32_le(&val);
#endif
}

static inline uint32_t atomic_or_uint32_le(void* addr, uint32_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_or_uint32(addr, val);
#else
    write_uint32_le(&val, val);
    val = atomic_or_uint32(addr, val);
    return read_uint32_le(&val);
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

static inline uint64_t atomic_load_uint64_le(const void* addr)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_load_uint64(addr);
#else
    uint64_t val = atomic_load_uint64(addr);
    return read_uint64_le(&val);
#endif
}

static inline void atomic_store_uint64_le(void* addr, uint64_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    atomic_store_uint64(addr, val);
#else
    write_uint64_le(&val, val);
    atomic_store_uint64(addr, val);
#endif
}

static inline uint64_t atomic_swap_uint64_le(void* addr, uint64_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_swap_uint64(addr, val);
#else
    write_uint64_le(&val, val);
    val = atomic_swap_uint64(addr, val);
    return read_uint64_le(&val);
#endif
}

static inline bool atomic_cas_uint64_le(void* addr, uint64_t exp, uint64_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_cas_uint64(addr, exp, val);
#else
    write_uint64_le(&exp, exp);
    write_uint64_le(&val, val);
    return atomic_cas_uint64(addr, exp, val);
#endif
}

static inline uint64_t atomic_add_uint64_le(void* addr, uint64_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_add_uint64(addr, val);
#else
    uint64_t tmp = atomic_load_uint64_relax(addr);
    uint64_t res;
    do {
        write_uint64_le(&res, read_uint64_le(&tmp) + val);
    } while (!atomic_cas_uint64_loop(addr, &tmp, res));
    return read_uint64_le(&tmp);
#endif
}

static inline uint64_t atomic_sub_uint64_le(void* addr, uint64_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_sub_uint64(addr, val);
#else
    return atomic_add_uint64_le(addr, -val);
#endif
}

static inline uint64_t atomic_and_uint64_le(void* addr, uint64_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_and_uint64(addr, val);
#else
    write_uint64_le(&val, val);
    val = atomic_and_uint64(addr, val);
    return read_uint64_le(&val);
#endif
}

static inline uint64_t atomic_xor_uint64_le(void* addr, uint64_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_xor_uint64(addr, val);
#else
    write_uint64_le(&val, val);
    val = atomic_xor_uint64(addr, val);
    return read_uint64_le(&val);
#endif
}

static inline uint64_t atomic_or_uint64_le(void* addr, uint64_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_or_uint64(addr, val);
#else
    write_uint64_le(&val, val);
    val = atomic_or_uint64(addr, val);
    return read_uint64_le(&val);
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

static inline int8_t atomic_max_int8(void* addr, int8_t val)
{
    int8_t tmp;
    do {
        tmp = atomic_load_uint8(addr);
    } while (!atomic_cas_uint8(addr, tmp, tmp > val ? tmp : val));
    return tmp;
}

static inline int8_t atomic_min_int8(void* addr, int8_t val)
{
    int8_t tmp;
    do {
        tmp = atomic_load_uint8(addr);
    } while (!atomic_cas_uint8(addr, tmp, tmp < val ? tmp : val));
    return tmp;
}

static inline uint8_t atomic_maxu_uint8(void* addr, uint8_t val)
{
    uint8_t tmp;
    do {
        tmp = atomic_load_uint8(addr);
    } while (!atomic_cas_uint8(addr, tmp, tmp > val ? tmp : val));
    return tmp;
}

static inline uint8_t atomic_minu_uint8(void* addr, uint8_t val)
{
    uint8_t tmp;
    do {
        tmp = atomic_load_uint8(addr);
    } while (!atomic_cas_uint8(addr, tmp, tmp < val ? tmp : val));
    return tmp;
}

static inline uint16_t atomic_load_uint16_le(const void* addr)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_load_uint16(addr);
#else
    uint32_t val = atomic_load_uint16(addr);
    return read_uint16_le(&val);
#endif
}

static inline void atomic_store_uint16_le(void* addr, uint16_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    atomic_store_uint16(addr, val);
#else
    write_uint16_le(&val, val);
    atomic_store_uint16(addr, val);
#endif
}

static inline bool atomic_cas_uint16_le(void* addr, uint16_t exp, uint16_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_cas_uint16(addr, exp, val);
#else
    write_uint16_le(&exp, exp);
    write_uint16_le(&val, val);
    return atomic_cas_uint16(addr, exp, val);
#endif
}

static inline uint16_t atomic_swap_uint16_le(void* addr, uint16_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_swap_uint16(addr, val);
#else
    write_uint16_le(&val, val);
    val = atomic_swap_uint16(addr, val);
    return read_uint16_le(&val);
#endif
}

static inline uint16_t atomic_add_uint16_le(void* addr, uint16_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_add_uint16(addr, val);
#else
    uint16_t tmp = atomic_load_uint16_relax(addr);
    uint16_t res;
    do {
        write_uint16_le(&res, read_uint16_le(&tmp) + val);
    } while (!atomic_cas_uint16_loop(addr, &tmp, res));
    return read_uint16_le(&tmp);
#endif
}

static inline uint16_t atomic_sub_uint16_le(void* addr, uint16_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_sub_uint16(addr, val);
#else
    return atomic_add_uint16_le(addr, -val);
#endif
}

static inline uint16_t atomic_and_uint16_le(void* addr, uint16_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_and_uint16(addr, val);
#else
    write_uint16_le(&val, val);
    val = atomic_and_uint16(addr, val);
    return read_uint16_le(&val);
#endif
}

static inline uint16_t atomic_xor_uint16_le(void* addr, uint16_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_xor_uint16(addr, val);
#else
    write_uint16_le(&val, val);
    val = atomic_xor_uint16(addr, val);
    return read_uint16_le(&val);
#endif
}

static inline uint16_t atomic_or_uint16_le(void* addr, uint16_t val)
{
#if defined(HOST_LITTLE_ENDIAN)
    return atomic_or_uint16(addr, val);
#else
    write_uint16_le(&val, val);
    val = atomic_or_uint16(addr, val);
    return read_uint16_le(&val);
#endif
}

static inline int16_t atomic_max_int16_le(void* addr, int16_t val)
{
    int16_t tmp;
    do {
        tmp = atomic_load_uint16_le(addr);
    } while (!atomic_cas_uint16_le(addr, tmp, tmp > val ? tmp : val));
    return tmp;
}

static inline int16_t atomic_min_int16_le(void* addr, int16_t val)
{
    int16_t tmp;
    do {
        tmp = atomic_load_uint16_le(addr);
    } while (!atomic_cas_uint16_le(addr, tmp, tmp < val ? tmp : val));
    return tmp;
}

static inline uint16_t atomic_maxu_uint16_le(void* addr, uint16_t val)
{
    uint16_t tmp;
    do {
        tmp = atomic_load_uint16_le(addr);
    } while (!atomic_cas_uint16_le(addr, tmp, tmp > val ? tmp : val));
    return tmp;
}

static inline uint16_t atomic_minu_uint16_le(void* addr, uint16_t val)
{
    uint16_t tmp;
    do {
        tmp = atomic_load_uint16_le(addr);
    } while (!atomic_cas_uint16_le(addr, tmp, tmp < val ? tmp : val));
    return tmp;
}

#endif
