#include "core/boot.h"

#include "arch/arch.h"
#include "core/fb.h"
#include "core/console.h"
#include "core/kprintf.h"
#include "core/memory.h"
#include "core/timer.h"
#include "core/test/test.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "mm/vmm.h"
#include "fs/vfs/vfs.h"
#include "fs/ramfs/ramfs.h"
#include "fs/tar/tar.h"
#include "proc/sched.h"
#include "proc/task.h"

// Tick frequency.
// Fast enough for a responsive scheduler time slice later, slow enough that the handler's cost is irrelevant.
#define TIMER_HZ 100

void task_entry(const char *name) {
    uint64_t last_ticks = 0;
    while (true) {
        uint64_t ticks = timer_get_ticks() / 50;
        if (ticks != last_ticks) {
            kprintf(name, "%zu\n", timer_get_ticks());
            last_ticks = ticks;
        }
    }
}

void task1_entry(void) {
    task_entry("T1");
}

void task2_entry(void) {
    task_entry("T2");
}

void _start(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        arch_halt();
    }

    arch_init();

    console_init();
    kprintf("CON", "Initialized console, ready to kprintf on the serial console\n");

    kprintf("K", "Wellcome to PjErOS!\n");

    // Self-tests are enabled in the limine.conf, this serves two purpose:
    // 1- i learn how to actually use limine cmdline feature
    // 2- i can make the kernel less verbose when i want
    const char *cmdline = cmdline_request.response ? cmdline_request.response->cmdline : NULL;
    bool tests_enabled = cmdline && strstr(cmdline, "pros.tests") != NULL;
    kprintf("K", "Self-tests %s (cmdline: \"%s\")\n", tests_enabled ? "enabled" : "disabled", cmdline ? cmdline : "");

    size_t pages = pmm_init(hhdm_request.response, memmap_request.response);
    kprintf("PMM", "Initialized PMM, ready to alloc/free physical pages\n");
    kprintf("PMM", "PMM manage %zu pages of %zu B for a total of %zu MB kernel heap available\n", pages, PAGE_SIZE, pages * PAGE_SIZE / 1024 / 1024);
    if (tests_enabled) test_pmm();

    heap_init();
    kprintf("HEAP", "Initialized Heap, ready to kmalloc/kfree dynamic virtual memory block\n");
    if (tests_enabled) test_heap();

    vmm_init();
    kprintf("VMM", "Initialized VMM, default context root at phys:%p virt:%p (kernel table root at phys:%p)\n", (void *)vmm_kernel_context->root_phys, vmm_kernel_context->root_virt, (void *)arch_vmm_get_kernel_root());
    if (tests_enabled) test_vmm();

    if (framebuffer_request.response) {
        fb_init(framebuffer_request.response);
        kprintf("FB", "Initialized the framebuffer, ready to kprintf also on the screen\n");
    }

    struct vfs_node *ramfs = ramfs_create_root();
    if (ramfs) {
        if (vfs_mount("/", ramfs) == 0) {
            if (module_request.response && module_request.response->modules && module_request.response->modules[0]) {
                if (tar_load((uint64_t) module_request.response->modules[0]->address, module_request.response->modules[0]->size) != 0) {
                    kpanic("tar_load failed");
                }
            }
        }
    }
    // test_vfs even though ramfs may not have been correctly loaded or mounted
    if (tests_enabled) test_vfs();

    if (tests_enabled) test_kstack();
    if (tests_enabled) test_task();

    sched_init();
    kprintf("SCHED", "Initialized scheduler, ready to switch context when timer will be up\n");

    struct task *task1 = task_create("T1", task1_entry);
    struct task *task2 = task_create("T2", task2_entry);
    sched_add_task(task1);
    sched_add_task(task2);

    // throwaway code that schedule a userland process
    /*
#ifdef __aarch64__
    // user blob: mrs x0, sctlr_el1 (must trap at EL0), b .
    static const uint8_t user_blob[] = {
        0x00, 0x10, 0x38, 0xD5,  // mrs x0, sctlr_el1
        0x00, 0x00, 0x00, 0x14,  // b .
    };
#else
    // user blob: hlt (must trap at ring 3), jmp . (backstop if it somehow doesn't)
    static const uint8_t user_blob[] = {
        0xF4, 0xEB, 0xFD,  // hlt; jmp .
    };
#endif
#define USER_BASE       0x0000000040000000ULL
#define USER_CODE_ADDR  USER_BASE
#define USER_STACK_ADDR (USER_BASE + PAGE_SIZE)
#define USER_STACK_TOP  (USER_STACK_ADDR + PAGE_SIZE)
    uint64_t code_phys = pmm_alloc(1);
    uint64_t stack_phys = pmm_alloc(1);
    if (!code_phys || !stack_phys) kpanic("user page alloc failed");
    // code page: user + executable (VMM_NO_EXECUTE absent means executable)
    vmm_map_page(vmm_kernel_context, USER_CODE_ADDR, code_phys, VMM_USER);
    // stack page: user + writable, never executable
    vmm_map_page(vmm_kernel_context, USER_STACK_ADDR, stack_phys, VMM_USER | VMM_WRITABLE | VMM_NO_EXECUTE);
    // write through the HHDM
    memcpy(pmm_phys_to_virt(code_phys), user_blob, sizeof(user_blob));
    uint64_t resolved = vmm_virt_to_phys(vmm_kernel_context, USER_CODE_ADDR);
    kprintf("USER", "code page virt:%p phys:%p resolved:%p\n", (void *) USER_CODE_ADDR, (void *) code_phys, (void *) resolved);
    uint8_t *readback = (uint8_t *) USER_CODE_ADDR;
    kprintf("USER", "readback: %02x %02x %02x %02x\n", readback[0], readback[1], readback[2], readback[3]);
    struct task *user = task_create_user("user", USER_CODE_ADDR, USER_STACK_TOP);
    if (!user) kpanic("task_create_user failed");
    sched_add_task(user);
    task_dump_all();
    */

    kprintf("TIMER", "Starting timer at %d Hz\n", TIMER_HZ);
    arch_timer_init(TIMER_HZ);

    kprintf("IRQ", "Enable IRQ\n");
    arch_irq_enable();

    kprintf("K", "Count to 3:\n");
    uint64_t last_seconds = 0;
    while (true) {
        uint64_t seconds = timer_get_ticks() / TIMER_HZ;
        if (seconds != last_seconds) {
            kprintf("TIMER", "%zu\n", seconds);
            task_dump_all();
            last_seconds = seconds;
            if (seconds >= 3) {
                break;
            }
        }
    }

    kprintf("K", "All done here, shutting down.\n");
    arch_shutdown();
}
