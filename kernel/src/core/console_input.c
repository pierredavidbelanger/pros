#include "core/console_input.h"

#include "core/ring_buffer.h"
#include "core/spinlock.h"

#define CONSOLE_INPUT_CAPACITY 256

static uint8_t storage[CONSOLE_INPUT_CAPACITY];
static struct ring_buffer queue;
static struct spinlock queue_lock = SPINLOCK_INIT;

void console_input_init(void) {
    ring_buffer_init(&queue, storage, CONSOLE_INPUT_CAPACITY);
}

void console_input_push(uint8_t byte) {
    uint64_t flags = spinlock_lock_irqsave(&queue_lock);
    ring_buffer_push(&queue, byte);
    spinlock_unlock_irqrestore(&queue_lock, flags);
}

bool console_input_pop(uint8_t *out) {
    uint64_t flags = spinlock_lock_irqsave(&queue_lock);
    bool got = ring_buffer_pop(&queue, out);
    spinlock_unlock_irqrestore(&queue_lock, flags);
    return got;
}
