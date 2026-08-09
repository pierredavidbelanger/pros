#include "core/test/test.h"

#include "core/kprintf.h"
#include "mm/pmm.h"

void test_pmm(void) {
    uint64_t one_page = pmm_alloc(1);
    test_report("PMM", "alloc one page", one_page != 0);
    if (one_page) {
        kprintf("PMM", "Got one page at %p\n", one_page);
        pmm_free(one_page, 1);
    }

    uint64_t two_pages = pmm_alloc(2);
    test_report("PMM", "alloc two pages", two_pages != 0);
    if (two_pages) {
        kprintf("PMM", "Got two pages at %p\n", two_pages);
        pmm_free(two_pages, 2);
    }
}
