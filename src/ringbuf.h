/*
ringbuf.h - FIFO ringbuf
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#ifndef RINGBUF_H
#define RINGBUF_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct ringbuf
{
    void *data;
    size_t size;
    size_t start;
    size_t consumed;
};

void ringbuf_create(struct ringbuf *rb, size_t size);
void ringbuf_destroy(struct ringbuf *rb);
size_t ringbuf_get_free_spc(struct ringbuf *rb);
bool ringbuf_put(struct ringbuf *rb, void *data, size_t len);
bool ringbuf_get(struct ringbuf *rb, void *data, size_t len);
bool ringbuf_is_empty(struct ringbuf *rb);
bool ringbuf_skip(struct ringbuf *rb, size_t len);

static inline bool ringbuf_put_u8(struct ringbuf *rb, uint8_t x) { return ringbuf_put(rb, &x, sizeof(x)); }
static inline bool ringbuf_put_u16(struct ringbuf *rb, uint16_t x) { return ringbuf_put(rb, &x, sizeof(x)); }
static inline bool ringbuf_put_u32(struct ringbuf *rb, uint32_t x) { return ringbuf_put(rb, &x, sizeof(x)); }
static inline bool ringbuf_put_u64(struct ringbuf *rb, uint64_t x) { return ringbuf_put(rb, &x, sizeof(x)); }

static inline bool ringbuf_get_u8(struct ringbuf *rb, uint8_t *x) { return ringbuf_get(rb, x, sizeof(*x)); }
static inline bool ringbuf_get_u16(struct ringbuf *rb, uint16_t *x) { return ringbuf_get(rb, x, sizeof(*x)); }
static inline bool ringbuf_get_u32(struct ringbuf *rb, uint32_t *x) { return ringbuf_get(rb, x, sizeof(*x)); }
static inline bool ringbuf_get_u64(struct ringbuf *rb, uint64_t *x) { return ringbuf_get(rb, x, sizeof(*x)); }

#endif
