#include "core/test/test.h"

#include "core/memory.h"
#include "mm/pmm.h"

#define TEST_PMM_TAG "PMM"

// how many pages the contiguous block test asks for
#define TEST_PMM_BLOCK_PAGES 4

// runs before heap_init, so nothing here may touch kmalloc/kcalloc

static bool test_pmm_check_page(void *virt_addr, uint8_t value) {
    uint8_t *bytes = virt_addr;
    for (size_t i = 0; i < PAGE_SIZE; i++) {
        if (bytes[i] != value) return false;
    }
    return true;
}

void test_pmm(void) {
    size_t free_before = pmm_get_free_page_count();

    // a single page must come back, aligned, and inside the physical range the memmap reported
    uint64_t one_page = pmm_alloc(1);
    test_report(TEST_PMM_TAG, "alloc one page", one_page != 0);
    if (!one_page) return;
    test_report(TEST_PMM_TAG, "page is page aligned", (one_page % PAGE_SIZE) == 0);
    test_report(TEST_PMM_TAG, "page below max phys addr", one_page < pmm_get_max_phys_addr());

    // the HHDM translation has to survive a round trip, or every kernel pointer built from a
    // physical address is quietly wrong
    void *one_virt = pmm_phys_to_virt(one_page);
    test_report(TEST_PMM_TAG, "phys to virt round trips", pmm_virt_to_phys(one_virt) == one_page);

    // a page that cannot hold a byte pattern is not really ours
    memset(one_virt, 0x5A, PAGE_SIZE);
    test_report(TEST_PMM_TAG, "page is writable end to end", test_pmm_check_page(one_virt, 0x5A));

    // the free list must shrink by exactly what was taken
    test_report(TEST_PMM_TAG, "alloc consumes exactly one page", pmm_get_free_page_count() == free_before - 1);

    // handing the same page out twice is the failure that corrupts everything downstream
    uint64_t other_page = pmm_alloc(1);
    test_report(TEST_PMM_TAG, "second alloc is a distinct page", other_page != 0 && other_page != one_page);

    // the second page must be independently writable, not an alias of the first
    void *other_virt = pmm_phys_to_virt(other_page);
    memset(other_virt, 0xA5, PAGE_SIZE);
    test_report(TEST_PMM_TAG, "pages do not alias", test_pmm_check_page(one_virt, 0x5A) && test_pmm_check_page(other_virt, 0xA5));

    pmm_free(other_page, 1);
    pmm_free(one_page, 1);
    test_report(TEST_PMM_TAG, "free restores the count", pmm_get_free_page_count() == free_before);

    // a multi page request must reserve every page it claims, not just the first
    uint64_t block = pmm_alloc(TEST_PMM_BLOCK_PAGES);
    test_report(TEST_PMM_TAG, "alloc contiguous block", block != 0);
    if (!block) return;
    test_report(TEST_PMM_TAG, "block is page aligned", (block % PAGE_SIZE) == 0);
    test_report(TEST_PMM_TAG, "block consumes exactly its pages", pmm_get_free_page_count() == free_before - TEST_PMM_BLOCK_PAGES);

    // give every page in the block its own pattern: if any two overlap, the last write wins
    // and the earlier pages read back wrong
    bool distinct = true;
    for (size_t i = 0; i < TEST_PMM_BLOCK_PAGES; i++) {
        memset(pmm_phys_to_virt(block + i * PAGE_SIZE), (uint8_t) (0x10 + i), PAGE_SIZE);
    }
    for (size_t i = 0; i < TEST_PMM_BLOCK_PAGES; i++) {
        if (!test_pmm_check_page(pmm_phys_to_virt(block + i * PAGE_SIZE), (uint8_t) (0x10 + i))) {
            distinct = false;
        }
    }
    test_report(TEST_PMM_TAG, "block pages are all distinct", distinct);

    // and the block must really be off the free list: a fresh page cannot land inside it
    uint64_t probe = pmm_alloc(1);
    test_report(TEST_PMM_TAG, "block is off the free list", probe != 0 && (probe < block || probe >= block + TEST_PMM_BLOCK_PAGES * PAGE_SIZE));
    pmm_free(probe, 1);

    pmm_free(block, TEST_PMM_BLOCK_PAGES);
    test_report(TEST_PMM_TAG, "freeing the block restores the count", pmm_get_free_page_count() == free_before);

    // phys 0 doubles as pmm_alloc's failure value, so freeing it must be a no-op rather than
    // pushing a bogus node onto the free list
    pmm_free(0, 1);
    test_report(TEST_PMM_TAG, "free of phys 0 is a no-op", pmm_get_free_page_count() == free_before);

    // churn: a leak of even one page per cycle would show up here
    for (int i = 0; i < 100; i++) {
        uint64_t churn = pmm_alloc(1);
        if (!churn) break;
        pmm_free(churn, 1);
    }
    test_report(TEST_PMM_TAG, "alloc/free churn leaks nothing", pmm_get_free_page_count() == free_before);
}
