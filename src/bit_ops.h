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

#include <stddef.h>
#include <limits.h>
#include <stdbool.h>

// Simple math operations (sign-extend int bits, etc) for internal usage
// this entire header is cursed (though everything optimizes fine)

// Generate bitmask of given size
static inline size_t gen_mask(size_t size)
{
    return -(size_t)1 >> (sizeof(size) * CHAR_BIT - size);
}

// Check if Nth bit of val is 1
static inline bool is_bit_set(size_t val, size_t pos)
{
    return (val >> pos) & 1;
}

/*
 * Sign-extend bits in the lower part of uint32_t into signed int32_t
 * usage:
 *     int32_t ext = sign_extend(val, 20);
 *
 * 20 is the size of the val in bits. ext is sign-extended using
 * bit at index 19.
 */
static inline ptrdiff_t sign_extend(size_t val, size_t bits)
{
    //return ((int32_t)(val << (32 - bits))) >> (32 - bits);
    //return ((ptrdiff_t)(val << (sizeof(val) * CHAR_BIT - bits))) >> (sizeof(val) * CHAR_BIT - bits);
    return (~gen_mask(bits)) * is_bit_set(val, bits - 1) | (val & gen_mask(bits));
}

// Cut N bits from val at given position (from lower bit)
static inline size_t cut_bits(size_t val, size_t pos, size_t bits)
{
    return (val >> pos) & gen_mask(bits);
}

// Replace N bits in val at given position (from lower bit) by p
static inline size_t replace_bits(size_t val, size_t pos, size_t bits, size_t p)
{
    return (val & (~(gen_mask(bits) << pos))) | ((p & gen_mask(bits)) << pos);
}

// Reverse N bits in val (from lower bit), remaining bits are zero
static inline size_t rev_bits(size_t val, size_t bits)
{
    size_t ret = 0;

    for (size_t i=0; i<bits; ++i) {
        ret <<= 1;
        ret |= val & 0x1;
        val >>= 1;
    }

    return ret;
}

#endif
