/*
rvvm_types.c - RVVM integer types
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

#ifndef RVVM_TYPES_H
#define RVVM_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <inttypes.h>

// Fix for MSVCRT printf specifier
#if defined(_WIN32) && defined(PRIx64)
#undef PRIx64
#define PRIx64 "I64x"
#endif

#ifndef PRIx64
#define PRIx64 "llx"
#endif

#ifndef PRIx32
#define PRIx32 "x"
#endif

#ifdef __SIZEOF_INT128__
#define INT128_SUPPORT 1
typedef unsigned __int128 uint128_t;
typedef __int128 int128_t;
#endif

// Max XLEN/SXLEN values
#ifdef USE_RV64
typedef uint64_t maxlen_t;
typedef int64_t smaxlen_t;
#define MAX_XLEN 64
#define MAX_SHAMT_BITS 6
#define PRIxXLEN PRIx64
#else
typedef uint32_t maxlen_t;
typedef int32_t smaxlen_t;
#define MAX_XLEN 32
#define MAX_SHAMT_BITS 5
#define PRIxXLEN PRIx32
#endif

typedef double fmaxlen_t;

// Distinguish between virtual and physical addresses
typedef maxlen_t virt_addr_t;
typedef maxlen_t phys_addr_t;

typedef uint8_t regid_t;  // Register index
typedef uint8_t bitcnt_t; // Bits count
typedef uint8_t* vmptr_t; // Pointer to VM memory

#endif
