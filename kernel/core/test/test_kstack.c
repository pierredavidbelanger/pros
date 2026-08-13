#include "core/test/test.h"

#include "proc/kstack.h"
#include "mm/pmm.h"

#define TEST_KSTACK_TAG "KSTK"

void test_kstack(void) {
    size_t free_before = pmm_get_free_page_count();

    void *stack_top = kstack_alloc();
    test_report(TEST_KSTACK_TAG, "alloc returns a stack", stack_top != NULL);
    if (!stack_top) return;

    test_report(TEST_KSTACK_TAG, "top is page aligned", ((uint64_t)stack_top % PAGE_SIZE) == 0);
    test_report(TEST_KSTACK_TAG, "guard is intact when fresh", kstack_guard_intact(stack_top));

    // an overflow writes the lowest bytes first, so that is what the checker has to notice
    *(uint64_t *)((uint8_t *)stack_top - KSTACK_SIZE) = 0;
    test_report(TEST_KSTACK_TAG, "guard notices a stomp", !kstack_guard_intact(stack_top));

    kstack_free(stack_top);
    test_report(TEST_KSTACK_TAG, "free returns every page", pmm_get_free_page_count() == free_before);
}
