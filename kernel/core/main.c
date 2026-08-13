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

// Tick frequency.
// Fast enough for a responsive scheduler time slice later, slow enough that the handler's cost is irrelevant.
#define TIMER_HZ 100

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
            last_seconds = seconds;
            if (seconds >= 3) {
                break;
            }
        }
    }

    kprintf("K", "All done here, shutting down.\n");
    arch_shutdown();
}
