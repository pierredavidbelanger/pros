#include "core/test/test.h"

#include "fs/vfs/vfs.h"
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

    // every task gets 0/1/2 on /dev/console without ever calling sys_open
    test_report(TEST_TASK_TAG, "fresh task has its three std fds", task->fds[0] && task->fds[1] && task->fds[2]);
    test_report(TEST_TASK_TAG, "the three std fds share one file",
        task->fds[0] == task->fds[1] && task->fds[1] == task->fds[2]);
    test_report(TEST_TASK_TAG, "std fds point at the console node", task->fds[0]->node == vfs_lookup("/dev/console"));
    // one ref per fd, so destroy actually reaches zero
    test_report(TEST_TASK_TAG, "one open file, three references", task->fds[0]->ref_count == 3);

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
