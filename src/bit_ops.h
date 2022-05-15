/*
bit_ops.h - bit operations functions
Copyright (C) 2021  Mr0maks <mr.maks0443@gmail.com>
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

#ifndef RISCV_BIT_OPS_H
#define RISCV_BIT_OPS_H

#include "rvvm_types.h"

// Simple math operations (sign-extend int bits, etc) for internal usage

/*
* Sign-extend bits in the lower part of val into signed long
* usage:
*     long ext = sign_extend(val, 20);
*
*     [ext is now equal to signed lower 20 bits of val]
*/
static inline smaxlen_t sign_extend(maxlen_t val, bitcnt_t bits)
{
    return ((smaxlen_t)(val << (MAX_XLEN - bits))) >> (MAX_XLEN - bits);
}

// Generate bitmask of given size
static inline maxlen_t bit_mask(bitcnt_t count)
{
    return (1ULL << count) - 1;
}

// Cut N bits from val at given position (from lower bit)
static inline maxlen_t bit_cut(maxlen_t val, bitcnt_t pos, bitcnt_t count)
{
    return (val >> pos) & bit_mask(count);
}

// Replace N bits in val at given position (from lower bit) by p
static inline maxlen_t bit_replace(maxlen_t val, bitcnt_t pos, bitcnt_t bits, maxlen_t p)
{
    return (val & (~(bit_mask(bits) << pos))) | ((p & bit_mask(bits)) << pos);
}

// Check if Nth bit of val is 1
static inline bool bit_check(maxlen_t val, bitcnt_t pos)
{
    return (val >> pos) & 0x1;
}

// Reverse N bits in val (from lower bit), remaining bits are zero
static inline maxlen_t bit_reverse(maxlen_t val, bitcnt_t bits)
{
    maxlen_t ret = 0;

    for (bitcnt_t i=0; i<bits; ++i) {
        ret <<= 1;
        ret |= val & 0x1;
        val >>= 1;
    }

    return ret;
}

static inline uint32_t byteswap_uint32(uint32_t val)
{
    return (((val & 0xFF000000) >> 24) |
            ((val & 0x00FF0000) >> 8)  |
            ((val & 0x0000FF00) << 8)  |
            ((val & 0x000000FF) << 24));
}

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
