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

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>

// Simple math operations (sign-extend int bits, etc) for internal usage
// this entire header is cursed (though everything optimizes fine)

#define LONG_BITS (CHAR_BIT * sizeof(long))

/*
* Sign-extend bits in the lower part of val into signed long
* usage:
*     long ext = sign_extend(val, 20);
*
*     [ext is now equal to signed lower 20 bits of val]
*/
static inline long sign_extend(long val, uint8_t bits)
{
    return (val << (LONG_BITS - bits)) >> (LONG_BITS - bits);
}

// Generate bitmask of given size
static inline size_t bit_mask(uint8_t count)
{
    return (1UL << count) - 1;
}

// Cut N bits from val at given position (from lower bit)
static inline size_t bit_cut(size_t val, uint8_t pos, uint8_t count)
{
    return (val >> pos) & bit_mask(count);
}

// Replace N bits in val at given position (from lower bit) by p
static inline size_t bit_replace(size_t val, uint8_t pos, uint8_t bits, size_t p)
{
    return (val & (~(bit_mask(bits) << pos))) | ((p & bit_mask(bits)) << pos);
}

// Check if Nth bit of val is 1
static inline bool bit_check(size_t val, uint8_t pos)
{
    return (val >> pos) & 0x1;
}

// Reverse N bits in val (from lower bit), remaining bits are zero
static inline size_t bit_reverse(size_t val, uint8_t bits)
{
    size_t ret = 0;

    for (uint8_t i=0; i<bits; ++i) {
        ret <<= 1;
        ret |= val & 0x1;
        val >>= 1;
    }

    return ret;
}

static inline uint64_t bit_mulh64(uint64_t a, uint64_t b)
{
#ifdef __SIZEOF_INT128__
    return ((__int128)a * (__int128)b) >> 64;
#else
    uint64_t al = (uint32_t)a, ah = a >> 32;
    uint64_t bl = (uint32_t)b, bh = b >> 32;
    uint64_t ahbh = ah * bh, albh = al * bh;
    uint64_t ahbl = ah * bl, albl = al * bl;
    uint64_t middle = ahbl + (albl >> 32) + (uint32_t)albh;

    return ahbh + (middle >> 32) + (albh >> 32);
#endif
}

#endif
