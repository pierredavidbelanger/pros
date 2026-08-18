#include "core/test/test.h"

#include "core/ring_buffer.h"

#define TEST_RING_BUFFER_TAG "RING"
#define TEST_RING_BUFFER_CAPACITY 4

void test_ring_buffer(void) {
    uint8_t storage[TEST_RING_BUFFER_CAPACITY];
    struct ring_buffer rb;
    ring_buffer_init(&rb, storage, TEST_RING_BUFFER_CAPACITY);

    // pop from empty must fail and leave *out alone
    uint8_t out = 0xAA;
    test_report(TEST_RING_BUFFER_TAG, "pop from empty fails", !ring_buffer_pop(&rb, &out));
    test_report(TEST_RING_BUFFER_TAG, "pop from empty leaves *out untouched", out == 0xAA);

    // a single byte must round trip
    test_report(TEST_RING_BUFFER_TAG, "push into empty succeeds", ring_buffer_push(&rb, 0x11));
    test_report(TEST_RING_BUFFER_TAG, "pop returns what was pushed", ring_buffer_pop(&rb, &out) && out == 0x11);
    test_report(TEST_RING_BUFFER_TAG, "buffer is empty again", !ring_buffer_pop(&rb, &out));

    // fill every slot, one push each
    bool filled = true;
    for (size_t i = 0; i < TEST_RING_BUFFER_CAPACITY; i++) {
        if (!ring_buffer_push(&rb, (uint8_t) (0x20 + i))) filled = false;
    }
    test_report(TEST_RING_BUFFER_TAG, "push fills every slot", filled);

    // decision 1: full buffer drops the newest byte, the oldest data must survive
    test_report(TEST_RING_BUFFER_TAG, "push past capacity is dropped", !ring_buffer_push(&rb, 0xFF));

    bool fifo = true;
    for (size_t i = 0; i < TEST_RING_BUFFER_CAPACITY; i++) {
        if (!ring_buffer_pop(&rb, &out) || out != (uint8_t) (0x20 + i)) fifo = false;
    }
    test_report(TEST_RING_BUFFER_TAG, "pop order matches push order", fifo);
    test_report(TEST_RING_BUFFER_TAG, "drained buffer is empty", !ring_buffer_pop(&rb, &out));

    // push 3 / pop 3 per round: head and tail lead/trail each other and cycle past
    // the array's end several times, not just fill once
    bool wrapped = true;
    uint8_t next_value = 0x40;
    uint8_t expect_pop = 0x40;
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 3; i++) {
            if (!ring_buffer_push(&rb, next_value++)) wrapped = false;
        }
        for (int i = 0; i < 3; i++) {
            if (!ring_buffer_pop(&rb, &out) || out != expect_pop++) wrapped = false;
        }
    }
    test_report(TEST_RING_BUFFER_TAG, "push/pop across a wraparound stays correct", wrapped);

    // two instances, even with distinct storage, must not share state
    uint8_t other_storage[TEST_RING_BUFFER_CAPACITY];
    struct ring_buffer other;
    ring_buffer_init(&other, other_storage, TEST_RING_BUFFER_CAPACITY);
    ring_buffer_push(&rb, 0x77);
    test_report(TEST_RING_BUFFER_TAG, "instances do not share state", !ring_buffer_pop(&other, &out));
}
