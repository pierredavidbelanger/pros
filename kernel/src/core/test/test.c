#include "core/test/test.h"

#include "core/kprintf.h"

void test_report(const char *tag, const char *name, bool ok) {
    kprintf("TEST", "[%-*s] %-38s %s\n", KPRINTF_TAG_WIDTH, tag, name, ok ? "PASS" : "FAIL");
}

void test_all(void) {
    test_pmm();
    test_heap();
    test_vmm();
    test_vfs();
    test_kstack();
    test_task();
}
