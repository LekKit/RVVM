#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

static inline bool ringbuf_put_u8(struct ringbuf *rb, uint8_t x) { return ringbuf_put(rb, &x, sizeof(x)); }
static inline bool ringbuf_put_u16(struct ringbuf *rb, uint16_t x) { return ringbuf_put(rb, &x, sizeof(x)); }
static inline bool ringbuf_put_u32(struct ringbuf *rb, uint32_t x) { return ringbuf_put(rb, &x, sizeof(x)); }
static inline bool ringbuf_put_u64(struct ringbuf *rb, uint64_t x) { return ringbuf_put(rb, &x, sizeof(x)); }

static inline bool ringbuf_get_u8(struct ringbuf *rb, uint8_t *x) { return ringbuf_get(rb, x, sizeof(*x)); }
static inline bool ringbuf_get_u16(struct ringbuf *rb, uint16_t *x) { return ringbuf_get(rb, x, sizeof(*x)); }
static inline bool ringbuf_get_u32(struct ringbuf *rb, uint32_t *x) { return ringbuf_get(rb, x, sizeof(*x)); }
static inline bool ringbuf_get_u64(struct ringbuf *rb, uint64_t *x) { return ringbuf_get(rb, x, sizeof(*x)); }

