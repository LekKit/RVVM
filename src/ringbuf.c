#include <ringbuf.h>

void ringbuf_create(struct ringbuf *rb, size_t size)
{
	rb->size = size;
	rb->data = malloc(size);
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

bool ringbuf_put(struct ringbuf *rb, void *data, size_t len)
{
	if (ringbuf_get_free_spc(rb) < len) {
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

