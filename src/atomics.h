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

#if defined(__chibicc__)
/* C11 atomics are broken on chibicc compiler, fallback silently */
#define HOST_NO_ATOMICS
#elif __STDC_VERSION__ >= 201112LL && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#define C11_ATOMICS
#elif GNU_EXTS
/* do nothing */
#elif _WIN32
#include <windows.h>
#else
#warning No atomics support for current build target!
#define HOST_NO_ATOMICS
#endif

// For byte reverse
#ifdef HOST_BIG_ENDIAN
#include "bit_ops.h"
#endif

static inline void atomic_fence()
{
#ifdef C11_ATOMICS
    atomic_thread_fence(memory_order_seq_cst);
#elif GNU_EXTS
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

/*
 * Host-endian 32-bit operations
 */

static inline void atomic_store_uint32(void* addr, uint32_t val)
{
#ifdef C11_ATOMICS
    atomic_store((_Atomic uint32_t*)addr, val);
#elif GNU_EXTS
    __atomic_store_n((uint32_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    InterlockedExchange((uint32_t*)addr, val);
#else
    *(uint32_t*)addr = val;
#endif
}

static inline uint32_t atomic_load_uint32(const void* addr)
{
#ifdef C11_ATOMICS
    return atomic_load((_Atomic const uint32_t*)addr);
#elif GNU_EXTS
    return __atomic_load_n((const uint32_t*)addr, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedAdd((const uint32_t*)addr, 0);
#else
    return *(const uint32_t*)addr;
#endif
}

static inline uint32_t atomic_swap_uint32(void* addr, uint32_t val)
{
#ifdef C11_ATOMICS
    return atomic_exchange((_Atomic uint32_t*)addr, val);
#elif GNU_EXTS
    return __atomic_exchange_n((uint32_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedExchange((uint32_t*)addr, val);
#else
    uint32_t tmp = *(uint32_t*)addr;
    *(uint32_t*)addr = val;
    return tmp;
#endif
}

static inline bool atomic_cas_uint32(void* addr, uint32_t exp, uint32_t val)
{
#ifdef C11_ATOMICS
    return atomic_compare_exchange_strong((_Atomic uint32_t*)addr, &exp, val);
#elif GNU_EXTS
    return __atomic_compare_exchange_n((uint32_t*)addr, &exp, val, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedCompareExchange((uint32_t*)addr, val, exp) == exp;
#else
    if (*(uint32_t*)addr == exp) {
        *(uint32_t*)addr = val;
        return true;
    } else return false;
#endif
}

static inline uint32_t atomic_add_uint32(void* addr, uint32_t val)
{
#ifdef C11_ATOMICS
    return atomic_fetch_add((_Atomic uint32_t*)addr, val);
#elif GNU_EXTS
    return __atomic_fetch_add((uint32_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedExchangeAdd((uint32_t*)addr, val);
#else
    uint32_t tmp = *(uint32_t*)addr;
    *(uint32_t*)addr += val;
    return tmp;
#endif
}

static inline uint32_t atomic_sub_uint32(void* addr, uint32_t val)
{
#ifdef C11_ATOMICS
    return atomic_fetch_sub((_Atomic uint32_t*)addr, val);
#elif GNU_EXTS
    return __atomic_fetch_sub((uint32_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedExchangeAdd((uint32_t*)addr, -val);
#else
    uint32_t tmp = *(uint32_t*)addr;
    *(uint32_t*)addr -= val;
    return tmp;
#endif
}

static inline uint32_t atomic_or_uint32(void* addr, uint32_t val)
{
#ifdef C11_ATOMICS
    return atomic_fetch_or((_Atomic uint32_t*)addr, val);
#elif GNU_EXTS
    return __atomic_fetch_or((uint32_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedOr((uint32_t*)addr, val);
#else
    uint32_t tmp = *(uint32_t*)addr;
    *(uint32_t*)addr |= val;
    return tmp;
#endif
}

static inline uint32_t atomic_xor_uint32(void* addr, uint32_t val)
{
#ifdef C11_ATOMICS
    return atomic_fetch_xor((_Atomic uint32_t*)addr, val);
#elif GNU_EXTS
    return __atomic_fetch_xor((uint32_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedXor((uint32_t*)addr, val);
#else
    uint32_t tmp = *(uint32_t*)addr;
    *(uint32_t*)addr ^= val;
    return tmp;
#endif
}

static inline uint32_t atomic_and_uint32(void* addr, uint32_t val)
{
#ifdef C11_ATOMICS
    return atomic_fetch_and((_Atomic uint32_t*)addr, val);
#elif GNU_EXTS
    return __atomic_fetch_and((uint32_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedAnd((uint32_t*)addr, val);
#else
    uint32_t tmp = *(uint32_t*)addr;
    *(uint32_t*)addr &= val;
    return tmp;
#endif
}

/*
 * Host-endian 64-bit operations
 */

static inline void atomic_store_uint64(void* addr, uint64_t val)
{
#ifdef C11_ATOMICS
    atomic_store((_Atomic uint64_t*)addr, val);
#elif GNU_EXTS
    __atomic_store_n((uint64_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    InterlockedExchange64((uint64_t*)addr, val);
#else
    *(uint64_t*)addr = val;
#endif
}

static inline uint64_t atomic_load_uint64(const void* addr)
{
#ifdef C11_ATOMICS
    return atomic_load((_Atomic const uint64_t*)addr);
#elif GNU_EXTS
    return __atomic_load_n((const uint64_t*)addr, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedAdd64((const uint64_t*)addr, 0);
#else
    return *(const uint64_t*)addr;
#endif
}

static inline uint64_t atomic_swap_uint64(void* addr, uint64_t val)
{
#ifdef C11_ATOMICS
    return atomic_exchange((_Atomic uint64_t*)addr, val);
#elif GNU_EXTS
    return __atomic_exchange_n((uint64_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedExchange64((uint64_t*)addr, val);
#else
    uint64_t tmp = *(uint64_t*)addr;
    *(uint64_t*)addr = val;
    return tmp;
#endif
}

static inline bool atomic_cas_uint64(void* addr, uint64_t exp, uint64_t val)
{
#ifdef C11_ATOMICS
    return atomic_compare_exchange_strong((_Atomic uint64_t*)addr, &exp, val);
#elif GNU_EXTS
    return __atomic_compare_exchange_n((uint64_t*)addr, &exp, val, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedCompareExchange64((uint64_t*)addr, val, exp) == exp;
#else
    if (*(uint64_t*)addr == exp) {
        *(uint64_t*)addr = val;
        return true;
    } else return false;
#endif
}

static inline uint64_t atomic_add_uint64(void* addr, uint64_t val)
{
#ifdef C11_ATOMICS
    return atomic_fetch_add((_Atomic uint64_t*)addr, val);
#elif GNU_EXTS
    return __atomic_fetch_add((uint64_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedExchangeAdd64((uint64_t*)addr, val);
#else
    uint64_t tmp = *(uint64_t*)addr;
    *(uint64_t*)addr += val;
    return tmp;
#endif
}

static inline uint64_t atomic_sub_uint64(void* addr, uint64_t val)
{
#ifdef C11_ATOMICS
    return atomic_fetch_sub((_Atomic uint64_t*)addr, val);
#elif GNU_EXTS
    return __atomic_fetch_sub((uint64_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedExchangeAdd64((uint64_t*)addr, -val);
#else
    uint64_t tmp = *(uint64_t*)addr;
    *(uint64_t*)addr -= val;
    return tmp;
#endif
}

static inline uint64_t atomic_or_uint64(void* addr, uint64_t val)
{
#ifdef C11_ATOMICS
    return atomic_fetch_or((_Atomic uint64_t*)addr, val);
#elif GNU_EXTS
    return __atomic_fetch_or((uint64_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedOr64((uint64_t*)addr, val);
#else
    uint64_t tmp = *(uint64_t*)addr;
    *(uint64_t*)addr |= val;
    return tmp;
#endif
}

static inline uint64_t atomic_xor_uint64(void* addr, uint64_t val)
{
#ifdef C11_ATOMICS
    return atomic_fetch_xor((_Atomic uint64_t*)addr, val);
#elif GNU_EXTS
    return __atomic_fetch_xor((uint64_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedXor64((uint64_t*)addr, val);
#else
    uint64_t tmp = *(uint64_t*)addr;
    *(uint64_t*)addr ^= val;
    return tmp;
#endif
}

static inline uint64_t atomic_and_uint64(void* addr, uint64_t val)
{
#ifdef C11_ATOMICS
    return atomic_fetch_and((_Atomic uint64_t*)addr, val);
#elif GNU_EXTS
    return __atomic_fetch_and((uint64_t*)addr, val, __ATOMIC_SEQ_CST);
#elif _WIN32
    return InterlockedAnd64((uint64_t*)addr, val);
#else
    uint64_t tmp = *(uint64_t*)addr;
    *(uint64_t*)addr &= val;
    return tmp;
#endif
}

/*
 * Hack for big-endian hosts to emulate little-endian atomics
 */

static inline void atomic_store_uint32_le(void* addr, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    atomic_store_uint32(addr, val);
#else
    atomic_store_uint32(addr, byteswap_uint32(val));
#endif
}

static inline uint32_t atomic_load_uint32_le(const void* addr)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_load_uint32(addr);
#else
    return byteswap_uint32(atomic_load_uint32(addr));
#endif
}

static inline uint32_t atomic_swap_uint32_le(void* addr, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_swap_uint32(addr, val);
#else
    return byteswap_uint32(atomic_swap_uint32(addr, byteswap_uint32(val)));
#endif
}

static inline bool atomic_cas_uint32_le(void* addr, uint32_t exp, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_cas_uint32(addr, exp, val);
#else
    return atomic_cas_uint32(addr, byteswap_uint32(exp), byteswap_uint32(val));
#endif
}

static inline uint32_t atomic_or_uint32_le(void* addr, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_or_uint32(addr, val);
#else
    return byteswap_uint32(atomic_or_uint32(addr, byteswap_uint32(val)));
#endif
}

static inline uint32_t atomic_xor_uint32_le(void* addr, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_xor_uint32(addr, val);
#else
    return byteswap_uint32(atomic_xor_uint32(addr, byteswap_uint32(val)));
#endif
}

static inline uint32_t atomic_and_uint32_le(void* addr, uint32_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_and_uint32(addr, val);
#else
    return byteswap_uint32(atomic_and_uint32(addr, byteswap_uint32(val)));
#endif
}

static inline void atomic_store_uint64_le(void* addr, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    atomic_store_uint64(addr, val);
#else
    atomic_store_uint64(addr, byteswap_uint64(val));
#endif
}

static inline uint64_t atomic_load_uint64_le(const void* addr)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_load_uint64(addr);
#else
    return byteswap_uint64(atomic_load_uint64(addr));
#endif
}

static inline uint64_t atomic_swap_uint64_le(void* addr, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_swap_uint64(addr, val);
#else
    return byteswap_uint64(atomic_swap_uint64(addr, byteswap_uint64(val)));
#endif
}

static inline bool atomic_cas_uint64_le(void* addr, uint64_t exp, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_cas_uint64(addr, exp, val);
#else
    return atomic_cas_uint64(addr, byteswap_uint64(exp), byteswap_uint64(val));
#endif
}

static inline uint64_t atomic_or_uint64_le(void* addr, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_or_uint64(addr, val);
#else
    return byteswap_uint64(atomic_or_uint64(addr, byteswap_uint64(val)));
#endif
}

static inline uint64_t atomic_xor_uint64_le(void* addr, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_xor_uint64(addr, val);
#else
    return byteswap_uint64(atomic_xor_uint64(addr, byteswap_uint64(val)));
#endif
}

static inline uint64_t atomic_and_uint64_le(void* addr, uint64_t val)
{
#ifdef HOST_LITTLE_ENDIAN
    return atomic_and_uint64(addr, val);
#else
    return byteswap_uint64(atomic_and_uint64(addr, byteswap_uint64(val)));
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
