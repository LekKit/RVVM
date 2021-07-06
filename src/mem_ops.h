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
#include "compiler.h"
#include <string.h>

/*
 * Simple memory operations (write, read integers) for internal usage,
 * and load/store instructions.
 */

/*
 * Handle misaligned operaions properly, to prevent
 * crashes on old ARM CPUs, etc
 */

static inline uint64_t read_uint64_le_m(const void* addr) {
    const uint8_t* arr = (const uint8_t*)addr;
    return (uint64_t)arr[0] | ((uint64_t)arr[1] << 8)
    | ((uint64_t)arr[2] << 16) | ((uint64_t)arr[3] << 24)
    | ((uint64_t)arr[4] << 32) | ((uint64_t)arr[5] << 40)
    | ((uint64_t)arr[6] << 48) | ((uint64_t)arr[7] << 56);
}

static inline void write_uint64_le_m(void* addr, uint64_t val) {
    uint8_t* arr = (uint8_t*)addr;
    arr[0] = val & 0xFF;
    arr[1] = (val >> 8) & 0xFF;
    arr[2] = (val >> 16) & 0xFF;
    arr[3] = (val >> 24) & 0xFF;
    arr[4] = (val >> 32) & 0xFF;
    arr[5] = (val >> 40) & 0xFF;
    arr[6] = (val >> 48) & 0xFF;
    arr[7] = (val >> 56) & 0xFF;
}

static inline uint32_t read_uint32_le_m(const void* addr) {
    const uint8_t* arr = (const uint8_t*)addr;
    return (uint32_t)arr[0] | ((uint32_t)arr[1] << 8)
    | ((uint32_t)arr[2] << 16) | ((uint32_t)arr[3] << 24);
}

static inline void write_uint32_le_m(void* addr, uint32_t val) {
    uint8_t* arr = (uint8_t*)addr;
    arr[0] = val & 0xFF;
    arr[1] = (val >> 8) & 0xFF;
    arr[2] = (val >> 16) & 0xFF;
    arr[3] = (val >> 24) & 0xFF;
}

static inline uint16_t read_uint16_le_m(const void* addr) {
    const uint8_t* arr = (const uint8_t*)addr;
    return (uint16_t)arr[0] | ((uint16_t)arr[1] << 8);
}

static inline void write_uint16_le_m(void* addr, uint16_t val) {
    uint8_t* arr = (uint8_t*)addr;
    arr[0] = val & 0xFF;
    arr[1] = (val >> 8) & 0xFF;
}

/*
 * Strictly aligned access for performace
 * Falls back to byte-bang operations on big-endian systems
 */

static inline uint64_t read_uint64_le(const void* addr) {
#ifdef HOST_LITTLE_ENDIAN
    return *(const uint64_t*)addr;
#else
    return read_uint64_le_m(addr);
#endif
}

static inline void write_uint64_le(void* addr, uint64_t val) {
#ifdef HOST_LITTLE_ENDIAN
    *(uint64_t*)addr = val;
#else
    write_uint64_le_m(addr, val);
#endif
}

static inline uint32_t read_uint32_le(const void* addr) {
#ifdef HOST_LITTLE_ENDIAN
    return *(const uint32_t*)addr;
#else
    return read_uint32_le_m(addr);
#endif
}

static inline void write_uint32_le(void* addr, uint32_t val) {
#ifdef HOST_LITTLE_ENDIAN
    *(uint32_t*)addr = val;
#else
    write_uint32_le_m(addr, val);
#endif
}

static inline uint16_t read_uint16_le(const void* addr) {
#ifdef HOST_LITTLE_ENDIAN
    return *(const uint16_t*)addr;
#else
    return read_uint16_le_m(addr);
#endif
}

static inline void write_uint16_le(void* addr, uint16_t val) {
#ifdef HOST_LITTLE_ENDIAN
    *(uint16_t*)addr = val;
#else
    write_uint16_le_m(addr, val);
#endif
}

static inline uint8_t read_uint8(const void* addr) {
    return *(const uint8_t*)addr;
}

static inline void write_uint8(void* addr, uint8_t val) {
    *(uint8_t*)addr = val;
}

/*
 * Floating-point memory operations (misaligned)
 */

static inline float read_float_m(const void *addr) {
    uint32_t i_v = read_uint32_le_m(addr);
    float ret;
    memcpy(&ret, &i_v, sizeof(float));
    return ret;
}

static inline double read_double_m(const void *addr) {
    uint64_t i_v = read_uint64_le_m(addr);
    double ret;
    memcpy(&ret, &i_v, sizeof(double));
    return ret;
}

static inline void write_float_m(void* addr, float val) {
    uint32_t i_v;
    memcpy(&i_v, &val, sizeof(i_v));
    write_uint32_le_m(addr, i_v);
}

static inline void write_double_m(void* addr, double val) {
    uint64_t i_v;
    memcpy(&i_v, &val, sizeof(i_v));
    write_uint64_le_m(addr, i_v);
}

/*
 * Floating-point memory operations (aligned)
 */

static inline float read_float(const void *addr) {
#ifdef HOST_LITTLE_ENDIAN
    return *(const float*)addr;
#else
    return read_float_m(addr);
#endif
}

static inline double read_double(const void *addr) {
#ifdef HOST_LITTLE_ENDIAN
    return *(const double*)addr;
#else
    return read_double_m(addr);
#endif
}

static inline void write_float(void *addr, float val) {
#ifdef HOST_LITTLE_ENDIAN
    *(float*)addr = val;
#else
    write_float_m(addr, val);
#endif
}

static inline void write_double(void *addr, double val) {
#ifdef HOST_LITTLE_ENDIAN
    *(double*)addr = val;
#else
    write_double_m(addr, val);
#endif
}

#endif
