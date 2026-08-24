#ifndef PROS_RING_BUFFER_H
#define PROS_RING_BUFFER_H

#include "stdc.h"

// single-producer, single-consumer, drop-newest on overflow
struct ring_buffer {
    uint8_t *buf;     // caller-owned storage, we never alloc or free it
    size_t capacity;  // size of buf, fixed for the instance's life
    size_t head;      // next slot to pop
    size_t tail;      // next slot to push
    size_t count;     // count of used slots
};

// zeroes head/tail/count, dont touch buf
void ring_buffer_init(struct ring_buffer *rb, uint8_t *buf, size_t capacity);

// false and byte dropped if full
bool ring_buffer_push(struct ring_buffer *rb, uint8_t byte);

// false *out left untouched if empty
bool ring_buffer_pop(struct ring_buffer *rb, uint8_t *out);

#endif  // PROS_RING_BUFFER_H
