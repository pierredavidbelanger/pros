#include "core/console_input.h"

#include "core/kprintf.h"
#include "core/ring_buffer.h"

#define CONSOLE_INPUT_CAPACITY 256

static uint8_t storage[CONSOLE_INPUT_CAPACITY];
static struct ring_buffer queue;

void console_input_init(void) {
    ring_buffer_init(&queue, storage, CONSOLE_INPUT_CAPACITY);
}

void console_input_push(uint8_t byte) {
    kprintf("CON", "%c\n", byte);
    ring_buffer_push(&queue, byte);
}

bool console_input_pop(uint8_t *out) {
    return ring_buffer_pop(&queue, out);
}
