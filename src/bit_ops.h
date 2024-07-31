/*
bit_ops.h - Bit operations
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.

Alternatively, the contents of this file may be used under the terms
of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or any later version.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef RVVM_BIT_OPS_H
#define RVVM_BIT_OPS_H

#include "compiler.h"
#include "rvvm_types.h"

// For optimized bit_orc_b() implementation
#if (defined(__x86_64__) && defined(GNU_EXTS)) || defined(_M_X64)
#include <emmintrin.h>
#elif defined(__aarch64__) && defined(GNU_EXTS)
#include <arm_neon.h>
#endif

// Simple bit operations (sign-extend, etc) for internal usage

/*
 * Sign-extend bits in the lower part of val into signed i64
 * Usage:
 *     int ext = sign_extend(val, 20);
 *
 *     [ext is now equal to signed lower 20 bits of val]
 */
static forceinline int64_t sign_extend(uint64_t val, bitcnt_t bits)
{
    return ((int64_t)(val << (64 - bits))) >> (64 - bits);
}

// Generate bitmask of given size
static forceinline uint64_t bit_mask(bitcnt_t count)
{
    return (1ULL << count) - 1;
}

// Cut bits from val at given position (from lower bit)
static forceinline uint64_t bit_cut(uint64_t val, bitcnt_t pos, bitcnt_t bits)
{
    return (val >> pos) & bit_mask(bits);
}

// Replace bits in val at given position (from lower bit) by rep
static inline uint64_t bit_replace(uint64_t val, bitcnt_t pos, bitcnt_t bits, uint64_t rep)
{
    return (val & (~(bit_mask(bits) << pos))) | ((rep & bit_mask(bits)) << pos);
}

// Check if Nth bit of val is 1
static forceinline bool bit_check(uint64_t val, bitcnt_t pos)
{
    return (val >> pos) & 0x1;
}

// Normalize the value to nearest next power of two
static inline uint64_t bit_next_pow2(uint64_t val)
{
    // Fast path for proper pow2 values
    if (!(val & (val - 1))) return val;
    // Bit twiddling hacks
    val -= 1;
    val |= (val >> 1);
    val |= (val >> 2);
    val |= (val >> 4);
    val |= (val >> 8);
    val |= (val >> 16);
    val |= (val >> 32);
    return val + 1;
}

// Rotate u32 left
static forceinline uint32_t bit_rotl32(uint32_t val, bitcnt_t bits)
{
    return (val << (bits & 0x1F)) | (val >> ((32 - bits) & 0x1F));
}

// Rotate u64 left
static forceinline uint64_t bit_rotl64(uint64_t val, bitcnt_t bits)
{
    return (val << (bits & 0x3F)) | (val >> ((64 - bits) & 0x3F));
}

// Rotate u32 right
static forceinline uint32_t bit_rotr32(uint32_t val, bitcnt_t bits)
{
    return (val >> (bits & 0x1F)) | (val << ((32 - bits) & 0x1F));
}

// Rotate u64 right
static forceinline uint64_t bit_rotr64(uint64_t val, bitcnt_t bits)
{
    return (val >> (bits & 0x3F)) | (val << ((64 - bits) & 0x3F));
}

// Count leading zeroes (from highest bit position) in u32
static inline bitcnt_t bit_clz32(uint32_t val)
{
    if (unlikely(!val)) return 32;
#if GNU_BUILTIN(__builtin_clz)
    return __builtin_clz(val);
#else
    bitcnt_t ret = 0;
    bitcnt_t tmp = (!(val >> 16)) << 4;
    val >>= 16 - tmp;
    ret += tmp;
    tmp = (!(val >> 8)) << 3;
    val >>= 8 - tmp;
    ret += tmp;
    tmp = (!(val >> 4)) << 2;
    val >>= 4 - tmp;
    ret += tmp;
    tmp = (!(val >> 2)) << 1;
    val >>= 2 - tmp;
    ret += tmp;
    tmp = !(val >> 1);
    val >>= 1 - tmp;
    ret += tmp;
    return ret + !(val & 1);
#endif
}

// Count leading zeroes (from highest bit position) in u64
static inline bitcnt_t bit_clz64(uint64_t val)
{
    if (unlikely(!val)) return 64;
#if GNU_BUILTIN(__builtin_clzll) && defined(HOST_64BIT)
    return __builtin_clzll(val);
#else
    bitcnt_t tmp = (!(val >> 32)) << 5;
    return bit_clz32(val >> (32 - tmp)) + tmp;
#endif
}

// Count trailing zeroes (from lowest bit position) in u32
static inline bitcnt_t bit_ctz32(uint32_t val)
{
    if (unlikely(!val)) return 32;
#if GNU_BUILTIN(__builtin_ctz)
    return __builtin_ctz(val);
#else
    bitcnt_t ret = 0;
    bitcnt_t tmp = (!((uint16_t)val)) << 4;
    val >>= tmp;
    ret += tmp;
    tmp = (!((uint8_t)val)) << 3;
    val >>= tmp;
    ret += tmp;
    tmp = (!(val & 0xF)) << 2;
    val >>= tmp;
    ret += tmp;
    tmp = (!(val & 0x3)) << 1;
    val >>= tmp;
    ret += tmp;
    return ret + !(val & 0x1);
#endif
}

// Count trailing zeroes (from lowest bit position) in u64
static inline bitcnt_t bit_ctz64(uint64_t val)
{
    if (unlikely(!val)) return 64;
#if GNU_BUILTIN(__builtin_ctzll) && defined(HOST_64BIT)
    return __builtin_ctzll(val);
#else
    bitcnt_t tmp = (!((uint32_t)val)) << 5;
    return bit_ctz32(val >> tmp) + tmp;
#endif
}

// Count raised bits in u32
static inline bitcnt_t bit_popcnt32(uint32_t val)
{
#if GNU_BUILTIN(__builtin_popcount)
    return __builtin_popcount(val);
#else
    val -= (val >> 1) & 0x55555555;
    val = (val & 0x33333333) + ((val >> 2) & 0x33333333);
    val = (val + (val >> 4)) & 0x0F0F0F0F;
    val += val >>  8;
    return (val + (val >> 16)) & 0x3F;
#endif
}

// Count raised bits in u64
static inline bitcnt_t bit_popcnt64(uint64_t val)
{
#if GNU_BUILTIN(__builtin_popcountll) && defined(HOST_64BIT)
    return __builtin_popcountll(val);
#else
    return bit_popcnt32(val) + bit_popcnt32(val >> 32);
#endif
}

// Bitwise OR-combine, byte granule for orc.b instruction emulation
static inline uint64_t bit_orc_b(uint64_t val)
{
#if (defined(__x86_64__) && defined(GNU_EXTS)) || defined(_M_X64)
    __m128i in = _mm_set_epi64x(0, val);
    __m128i zero = _mm_set_epi64x(0, 0);
    __m128i cmp = _mm_cmpeq_epi8(in, zero);
    __m128i orc = _mm_cmpeq_epi8(cmp, zero);
    return _mm_cvtsi128_si64(orc);
#elif defined(__aarch64__) && defined(GNU_EXTS)
    uint8x8_t in = vreinterpret_u8_u64(vcreate_u64(val));
    uint8x8_t orc = vtst_u8(in, in);
    return vget_lane_u64(vreinterpret_u64_u8(orc), 0);
#else
    val |= ((val >> 1) | (val << 1)) & 0x7E7E7E7E7E7E7E7EULL;
    val |= ((val >> 2) | (val << 2)) & 0x3C3C3C3C3C3C3C3CULL;
    val |= (val >> 4) & 0x0F0F0F0F0F0F0F0FULL;
    val |= (val << 4) & 0xF0F0F0F0F0F0F0F0ULL;
    return val;
#endif
}

/*
 * clmul using SSE intrins for future investigation:
    __m128i a_vec = _mm_cvtsi64_si128(a);
    __m128i b_vec = _mm_cvtsi64_si128(b);
    return _mm_cvtsi128_si64(_mm_clmulepi64_si128(a_vec, b_vec, 0));
 *
 * clmulh using SSE intrins:
    __m128i a_vec = _mm_cvtsi64_si128(a);
    __m128i b_vec = _mm_cvtsi64_si128(b);
    return _mm_extract_epi64(_mm_clmulepi64_si128(a_vec, b_vec, 0), 1);
 */

// Carry-less multiply
static inline uint32_t bit_clmul32(uint32_t a, uint32_t b)
{
    uint32_t ret = 0;
    do {
        if (b & 1) ret ^= a;
        b >>= 1;
    } while ((a <<= 1));
    return ret;
}

static inline uint64_t bit_clmul64(uint64_t a, uint64_t b)
{
    uint64_t ret = 0;
    do {
        if (b & 1) ret ^= a;
        b >>= 1;
    } while ((a <<= 1));
    return ret;
}

static inline uint32_t bit_clmulh32(uint32_t a, uint32_t b)
{
    uint32_t ret = 0;
    bitcnt_t i = 31;
    do {
        b >>= 1;
        if (b & 1) ret ^= (a >> i);
        i--;
    } while (b);
    return ret;
}

static inline uint64_t bit_clmulh64(uint64_t a, uint64_t b)
{
    uint64_t ret = 0;
    bitcnt_t i = 63;
    do {
        b >>= 1;
        if (b & 1) ret ^= (a >> i);
        i--;
    } while (b);
    return ret;
}

static inline uint32_t bit_clmulr32(uint32_t a, uint32_t b)
{
    uint32_t ret = 0;
    bitcnt_t i = 31;
    do {
        if (b & 1) ret ^= (a >> i);
        b >>= 1;
        i--;
    } while (b);
    return ret;
}

static inline uint64_t bit_clmulr64(uint64_t a, uint64_t b)
{
    uint64_t ret = 0;
    bitcnt_t i = 63;
    do {
        if (b & 1) ret ^= (a >> i);
        b >>= 1;
        i--;
    } while (b);
    return ret;
}

// Bswap 32-bit value (From BE to LE or vice versa)
static inline uint32_t byteswap_uint32(uint32_t val)
{
    return (((val & 0xFF000000) >> 24) |
            ((val & 0x00FF0000) >> 8)  |
            ((val & 0x0000FF00) << 8)  |
            ((val & 0x000000FF) << 24));
}

// Bswap 64-bit value (From BE to LE or vice versa)
static inline uint64_t byteswap_uint64(uint64_t val)
{
    return (((val & 0xFF00000000000000) >> 56) |
            ((val & 0x00FF000000000000) >> 40) |
            ((val & 0x0000FF0000000000) >> 24) |
            ((val & 0x000000FF00000000) >> 8)  |
            ((val & 0x00000000FF000000) << 8)  |
            ((val & 0x0000000000FF0000) << 24) |
            ((val & 0x000000000000FF00) << 40) |
            ((val & 0x00000000000000FF) << 56));
}

// Get high 64 bits from signed i64 x i64 -> 128 bit multiplication
static inline uint64_t mulh_uint64(int64_t a, int64_t b)
{
#ifdef INT128_SUPPORT
    return ((int128_t)a * (int128_t)b) >> 64;
#else
    int64_t lo_lo = (a & 0xFFFFFFFF) * (b & 0xFFFFFFFF);
    int64_t hi_lo = (a >> 32)        * (b & 0xFFFFFFFF);
    int64_t lo_hi = (a & 0xFFFFFFFF) * (b >> 32);
    int64_t hi_hi = (a >> 32)        * (b >> 32);
    int64_t cross = (lo_lo >> 32) + (hi_lo & 0xFFFFFFFF) + lo_hi;
    return (hi_lo >> 32) + (cross >> 32) + hi_hi;
#endif
}

// Get high 64 bits from unsigned u64 x u64 -> 128 bit multiplication
static inline uint64_t mulhu_uint64(uint64_t a, uint64_t b)
{
#ifdef INT128_SUPPORT
    return ((uint128_t)a * (uint128_t)b) >> 64;
#else
    uint64_t lo_lo = (a & 0xFFFFFFFF) * (b & 0xFFFFFFFF);
    uint64_t hi_lo = (a >> 32)        * (b & 0xFFFFFFFF);
    uint64_t lo_hi = (a & 0xFFFFFFFF) * (b >> 32);
    uint64_t hi_hi = (a >> 32)        * (b >> 32);
    uint64_t cross = (lo_lo >> 32) + (hi_lo & 0xFFFFFFFF) + lo_hi;
    return (hi_lo >> 32) + (cross >> 32) + hi_hi;
#endif
}

// Get high 64 bits from signed * unsigned i64 x u64 -> 128 bit multiplication
static inline uint64_t mulhsu_uint64(int64_t a, uint64_t b)
{
#ifdef INT128_SUPPORT
    return ((int128_t)a * (uint128_t)b) >> 64;
#else
    int64_t lo_lo = (a & 0xFFFFFFFF) * (b & 0xFFFFFFFF);
    int64_t hi_lo = (a >> 32)        * (b & 0xFFFFFFFF);
    int64_t lo_hi = (a & 0xFFFFFFFF) * (b >> 32);
    int64_t hi_hi = (a >> 32)        * (b >> 32);
    uint64_t cross = (lo_lo >> 32) + (hi_lo & 0xFFFFFFFF) + lo_hi;
    return (hi_lo >> 32) + (cross >> 32) + hi_hi;
#endif
}

#endif
