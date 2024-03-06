/*
ringbuf.с - FIFO Ring buffer
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

#include "ringbuf.h"
#include "utils.h"
#include "mem_ops.h"

void ringbuf_create(ringbuf_t* rb, size_t size)
{
    rb->data = safe_new_arr(uint8_t, size);
    rb->size = size;
    rb->start = 0;
    rb->consumed = 0;
}

void ringbuf_destroy(ringbuf_t* rb)
{
    rb->size = 0;
    rb->start = 0;
    rb->consumed = 0;
    free(rb->data);
}

size_t ringbuf_space(ringbuf_t* rb)
{
    return rb->size - rb->consumed;
}

size_t ringbuf_avail(ringbuf_t* rb)
{
    return rb->consumed;
}

size_t ringbuf_skip(ringbuf_t* rb, size_t len)
{
    size_t skip = EVAL_MIN(len, rb->consumed);
    rb->consumed -= skip;
    return skip;
}

static inline size_t ringbuf_get_read_start(ringbuf_t* rb)
{
    return rb->consumed > rb->start
        ? (rb->size - rb->consumed + rb->start)
        : (rb->start - rb->consumed);
}

size_t ringbuf_peek(ringbuf_t* rb, void* data, size_t len)
{
    size_t start = ringbuf_get_read_start(rb);
    size_t ret = EVAL_MIN(rb->consumed, len);
    size_t lhalf_len = EVAL_MIN(rb->size - start, ret);
    memcpy(data, ((uint8_t*)rb->data) + start, lhalf_len);
    if (ret > lhalf_len) {
        size_t rhalf_len = ret - lhalf_len;
        memcpy(((uint8_t*)data) + lhalf_len, rb->data, rhalf_len);
    }
    return ret;
}

size_t ringbuf_read(ringbuf_t* rb, void* data, size_t len)
{
    size_t ret = ringbuf_peek(rb, data, len);
    ringbuf_skip(rb, ret);
    return ret;
}

size_t ringbuf_write(ringbuf_t* rb, const void* data, size_t len)
{
    size_t ret = EVAL_MIN(rb->size - rb->consumed, len);
    size_t lhalf_len = EVAL_MIN(rb->size - rb->start, ret);
    memcpy(((uint8_t*)rb->data) + rb->start, data, lhalf_len);
    if (ret > lhalf_len) {
        size_t rhalf_len = ret - lhalf_len;
        memcpy(rb->data, ((const uint8_t*)data) + lhalf_len, rhalf_len);
        rb->start = rhalf_len;
    } else {
        rb->start += ret;
    }
    rb->consumed += ret;
    return ret;
}

bool ringbuf_get(ringbuf_t* rb, void* data, size_t len)
{
    if (len <= ringbuf_avail(rb)) {
        ringbuf_read(rb, data, len);
        return true;
    }
    return false;
}

bool ringbuf_put(ringbuf_t* rb, const void* data, size_t len)
{
    if (len <= ringbuf_space(rb)) {
        ringbuf_write(rb, data, len);
        return true;
    }
    DO_ONCE(rvvm_info("Overflow in ring %p! (size: %u, consumed: %u, len: %u)",
              (void*)rb, (uint32_t)rb->size, (uint32_t)rb->consumed, (uint32_t)len));
    return false;
}
