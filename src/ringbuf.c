/*
ringbuf.с - FIFO ringbuf
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

#include <ringbuf.h>
#include <utils.h>
#include <string.h>

void ringbuf_create(struct ringbuf *rb, size_t size)
{
	rb->size = size;
	rb->data = safe_malloc(size);
	rb->start = 0;
	rb->consumed = 0;
}

void ringbuf_destroy(struct ringbuf *rb)
{
	free(rb->data);
}

size_t ringbuf_get_free_spc(struct ringbuf *rb)
{
	return rb->size - rb->consumed;
}

bool ringbuf_is_empty(struct ringbuf *rb)
{
    return rb->consumed == 0;
}

bool ringbuf_put(struct ringbuf *rb, void *data, size_t len)
{
	if (ringbuf_get_free_spc(rb) < len) {
		rvvm_warn("Overflow in ringbuf %p! size=%d, consumed=%d, len=%d", rb, rb->size, rb->consumed, len);
		return false;
	}

	size_t first_chunk_len = rb->size - rb->start;
	if (first_chunk_len > len) {
		memcpy((uint8_t*)rb->data + rb->start, data, len);
		rb->start += len;
		rb->consumed += len;
		return true;
	}

	memcpy((uint8_t*)rb->data + rb->start, data, first_chunk_len);

	size_t second_chunk_len = len - first_chunk_len;
	memcpy(rb->data, data, second_chunk_len);

	rb->start = second_chunk_len;
	rb->consumed += len;
	return true;
}

static inline size_t ringbuf_get_read_start(struct ringbuf *rb)
{
	return rb->consumed > rb->start
		? rb->size - rb->consumed + rb->start
		: rb->start - rb->consumed;
}

bool ringbuf_get(struct ringbuf *rb, void *data, size_t len)
{
	if (rb->consumed < len) {
		return false;
	}

	size_t first_chunk_len = rb->size - ringbuf_get_read_start(rb);
	if (first_chunk_len > len) {
		memcpy(data, (uint8_t*)rb->data + rb->size - first_chunk_len, len);
		rb->consumed -= len;
		return true;
	}

	memcpy(data, (uint8_t*)rb->data + rb->size - first_chunk_len, first_chunk_len);

	size_t second_chunk_len = len - first_chunk_len;
	memcpy(data, rb->data, second_chunk_len);

	rb->consumed -= len;
	return true;
}

bool ringbuf_skip(struct ringbuf *rb, size_t len)
{
    if (rb->consumed < len) {
        return false;
    }

    rb->consumed -= len;
    return true;
}
