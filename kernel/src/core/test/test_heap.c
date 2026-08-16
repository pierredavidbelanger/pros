#include "core/test/test.h"

#include "core/memory.h"
#include "mm/heap.h"
#include "mm/pmm.h"

static bool test_heap_check_pattern(const uint8_t *buf, size_t size, uint8_t value) {
    for (size_t i = 0; i < size; i++) {
        if (buf[i] != value) return false;
    }
    return true;
}

void test_heap(void) {
    // heap_init() is deliberately not called here: it overwrites heap_block_list, which would orphan
    // every live allocation in the kernel. It is exercised implicitly by every check below.

    // a small block must be writable end to end
    uint8_t *small = kmalloc(32);
    bool ok = small != NULL;
    if (ok) {
        memset(small, 0xAA, 32);
        ok = test_heap_check_pattern(small, 32, 0xAA);
    }
    test_report("HEAP", "kmalloc small", ok);

    // a block larger than a page forces a fresh multi-page pmm_alloc
    uint8_t *big = kmalloc(8000);
    ok = big != NULL;
    if (ok) {
        memset(big, 0xBB, 8000);
        ok = test_heap_check_pattern(big, 8000, 0xBB);
    }
    test_report("HEAP", "kmalloc multi-page", ok);

    // the allocator promises HEAP_ALIGNMENT on the payload address, not just on the requested size
    test_report("HEAP", "kmalloc payload alignment", small != NULL && ((uintptr_t) small % HEAP_ALIGNMENT) == 0);

    kfree(big);

    // kcalloc must hand back zeroed memory
    uint8_t *zeroed = kcalloc(16, 8);
    test_report("HEAP", "kcalloc zeroes", zeroed != NULL && test_heap_check_pattern(zeroed, 16 * 8, 0x00));
    kfree(zeroed);

    // num * size must be rejected before it can wrap
    test_report("HEAP", "kcalloc overflow rejected", kcalloc(SIZE_MAX, 2) == NULL);

    // krealloc(NULL, n) behaves as kmalloc(n)
    uint8_t *from_null = krealloc(NULL, 64);
    test_report("HEAP", "krealloc NULL allocates", from_null != NULL);

    // krealloc(p, 0) behaves as kfree(p) and yields NULL
    test_report("HEAP", "krealloc zero frees", krealloc(from_null, 0) == NULL);

    // shrinking always fits in place, so the pointer must not move and the data must survive
    uint8_t *shrink = kmalloc(256);
    memset(shrink, 0xCC, 256);
    uint8_t *shrunk = krealloc(shrink, 64);
    test_report("HEAP", "krealloc shrink keeps pointer", shrunk == shrink && test_heap_check_pattern(shrunk, 64, 0xCC));
    kfree(shrunk);

    // kmalloc splits the block it carves from, so this block's own remainder is guaranteed to be the
    // free, physically contiguous neighbour that krealloc absorbs when it grows a block in place
    uint8_t *grow = kmalloc(64);
    memset(grow, 0xDD, 64);
    uint8_t *grown = krealloc(grow, 128);
    test_report("HEAP", "krealloc grow in place", grown == grow && test_heap_check_pattern(grown, 64, 0xDD));
    kfree(grown);

    // no contiguous free run is anywhere near 100000 B here, so the block cannot grow in place:
    // krealloc must fall back to allocate-copy-free and carry the data across to a new address
    uint8_t *pinned = kmalloc(64);
    memset(pinned, 0xEE, 64);
    uint8_t *moved = krealloc(pinned, 100000);
    test_report("HEAP", "krealloc grow by move", moved != NULL && moved != pinned && test_heap_check_pattern(moved, 64, 0xEE));
    kfree(moved);

    // freeing NULL is a no-op, not a fault
    kfree(NULL);
    test_report("HEAP", "kfree NULL survives", true);

    kfree(small);

    // once blocks are freed and coalesced they must be reused, not answered with fresh PMM pages every cycle
    kfree(kmalloc(2000));
    size_t free_before = pmm_get_free_page_count();
    for (int i = 0; i < 100; i++) {
        kfree(kmalloc(2000));
    }
    test_report("HEAP", "kfree reuses blocks", pmm_get_free_page_count() == free_before);
}
