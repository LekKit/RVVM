/*
mem_ops.h - memory operations functions
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

#ifndef RISCV_MEM_OPS_H
#define RISCV_MEM_OPS_H

#include <stdint.h>

/*
 * Simple memory operations (write, read integers) for internal usage,
 * and load/store instructions. Should be optimised into direct copy
 * on little-endian systems, and into copy+byteswap on big-endian.
 */

static inline uint64_t read_uint64_le(const void* addr) {
	const uint8_t* arr = (const uint8_t*)addr;
	uint64_t res = 0;
	for (int i = 0; i < 8; ++i)
	{
		res |= (uint64_t)arr[i] << 8 * i;
	}
	return res;
}

static inline void write_uint64_le(void *addr, uint64_t val) {
	uint8_t* arr = (uint8_t*)addr;
	for (int i = 0; i < 8; ++i)
	{
		arr[i] = val & 0xFF;
		val >>= 8;
	}
}

static inline uint32_t read_uint32_le(const void* addr) {
	const uint8_t* arr = (const uint8_t*)addr;
	return (uint32_t)arr[0]
		| ((uint32_t)arr[1] << 8)
		| ((uint32_t)arr[2] << 16)
		| ((uint32_t)arr[3] << 24);
}

static inline void write_uint32_le(void* addr, uint32_t val) {
	uint8_t* arr = (uint8_t*)addr;
	arr[0] = val & 0xFF;
	arr[1] = (val >> 8) & 0xFF;
	arr[2] = (val >> 16) & 0xFF;
	arr[3] = (val >> 24) & 0xFF;
}

static inline uint16_t read_uint16_le(const void* addr) {
	const uint8_t* arr = (const uint8_t*)addr;
	return (uint16_t)arr[0] | ((uint16_t)arr[1] << 8);
}

static inline void write_uint16_le(void* addr, uint16_t val) {
	uint8_t* arr = (uint8_t*)addr;
	arr[0] = val & 0xFF;
	arr[1] = (val >> 8) & 0xFF;
}

#endif
