/*
ringbuf.h - FIFO Ring buffer
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>
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

#ifndef RVVM_RINGBUF_H
#define RVVM_RINGBUF_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct ringbuf {
    void*  data;
    size_t size;
    size_t start;
    size_t consumed;
} ringbuf_t;

void ringbuf_create(ringbuf_t* rb, size_t size);
void ringbuf_destroy(ringbuf_t* rb);

size_t ringbuf_space(ringbuf_t* rb);
size_t ringbuf_avail(ringbuf_t* rb);

// Serial operation (Returns actual amount of read/written bytes)
size_t ringbuf_read(ringbuf_t* rb, void* data, size_t len);
size_t ringbuf_peek(ringbuf_t* rb, void* data, size_t len);
size_t ringbuf_skip(ringbuf_t* rb, size_t len);
size_t ringbuf_write(ringbuf_t* rb, const void* data, size_t len);

// Error out instead of partial operation
bool ringbuf_get(ringbuf_t* rb, void* data, size_t len);
bool ringbuf_put(ringbuf_t* rb, const void* data, size_t len);

static inline bool ringbuf_put_u8(ringbuf_t* rb, uint8_t x) { return ringbuf_put(rb, &x, sizeof(x)); }
static inline bool ringbuf_put_u16(ringbuf_t* rb, uint16_t x) { return ringbuf_put(rb, &x, sizeof(x)); }
static inline bool ringbuf_put_u32(ringbuf_t* rb, uint32_t x) { return ringbuf_put(rb, &x, sizeof(x)); }
static inline bool ringbuf_put_u64(ringbuf_t* rb, uint64_t x) { return ringbuf_put(rb, &x, sizeof(x)); }

static inline bool ringbuf_get_u8(ringbuf_t* rb, uint8_t* x) { return ringbuf_get(rb, x, sizeof(*x)); }
static inline bool ringbuf_get_u16(ringbuf_t* rb, uint16_t* x) { return ringbuf_get(rb, x, sizeof(*x)); }
static inline bool ringbuf_get_u32(ringbuf_t* rb, uint32_t* x) { return ringbuf_get(rb, x, sizeof(*x)); }
static inline bool ringbuf_get_u64(ringbuf_t* rb, uint64_t* x) { return ringbuf_get(rb, x, sizeof(*x)); }

#endif
