#include "core/test/test.h"

#include "proc/task.h"
#include "mm/pmm.h"

#define TEST_TASK_TAG "TASK"

// Never actually runs: task_create() just needs a valid function pointer to fabricate a frame around.
static void test_task_entry(void) {
    while (true) {}
}

void test_task(void) {
    size_t free_before = pmm_get_free_page_count();

    struct task *task = task_create("test", test_task_entry);
    test_report(TEST_TASK_TAG, "create returns a task", task != NULL);
    if (!task) return;

    test_report(TEST_TASK_TAG, "fresh frame lies inside its own kernel stack", task_owns_frame(task, task->frame));
    test_report(TEST_TASK_TAG, "fresh stack is intact", task_stack_intact(task));

    task_destroy(task);
    test_report(TEST_TASK_TAG, "destroy returns every page", pmm_get_free_page_count() == free_before);

    // Boot task situation: no kernel_stack_base, because it never called kstack_alloc().
    // task_stack_intact() must call that "nothing to check", not "corrupt" - the C0 bug, pinned here.
    struct task *boot = task_init_boot();
    test_report(TEST_TASK_TAG, "boot returns a task", boot != NULL);
    if (!boot) return;

    test_report(TEST_TASK_TAG, "a task with no known stack extent is reported intact", task_stack_intact(boot));
    task_destroy(boot);
}
