#include "core/ring_buffer.h"

void ring_buffer_init(struct ring_buffer *rb, uint8_t *buf, size_t capacity) {
    if (!rb || !buf || capacity == 0) return;
    rb->buf = buf;
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}

bool ring_buffer_push(struct ring_buffer *rb, uint8_t byte) {
    if (!rb) return false;
    if (rb->count == rb->capacity) return false;
    rb->buf[rb->tail] = byte;
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count++;
    return true;
}

bool ring_buffer_pop(struct ring_buffer *rb, uint8_t *out) {
    if (!rb) return false;
    if (rb->count == 0) return false;
    uint8_t byte = rb->buf[rb->head];
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count--;
    if (out) *out = byte;
    return true;
}
