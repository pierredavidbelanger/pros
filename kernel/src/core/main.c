#include "arch/arch.h"
#include "asm/unistd.h"
#include "core/boot.h"
#include "core/console.h"
#include "core/console_input.h"
#include "core/fb.h"
#include "core/hhdm.h"
#include "core/kprintf.h"
#include "core/memory.h"
#include "core/serial.h"
#include "core/timer.h"
#include "core/test/test.h"
#include "drivers/console_dev.h"
#include "fs/ramfs/ramfs.h"
#include "fs/tar/tar.h"
#include "fs/vfs/vfs.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "proc/elf.h"
#include "proc/sched.h"
#include "proc/task.h"
#include "syscall/syscall.h"

// Tick frequency.
// Fast enough for a responsive scheduler time slice later, slow enough that the handler's cost is irrelevant.
#define TIMER_HZ 100

// (1 GiB + 8 MiB)
// so we have 8 MiB of empty space between where the programe segment ends and where the stack begin
#define USER_STACK_TOP  0x0000000040800000ULL

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

    hhdm_init();

    arch_init();

    // enable sending output to serial
    serial_init_put();

    // enable the fanout console
    // (will effectively fan out to the FB when its up later)
    console_init();
    kprintf("CON", "Initialized console, ready to kprintf on the serial console\n");

    kprintf("K", "Wellcome to PjErOS!\n");

    // Self-tests are enabled in the limine.conf, this serves two purpose:
    // 1- i learn how to actually use limine cmdline feature
    // 2- i can make the kernel less verbose when i want
    const char *cmdline = cmdline_request.response ? cmdline_request.response->cmdline : NULL;
    bool tests_enabled = cmdline && strstr(cmdline, "pros.tests") != NULL;
    kprintf("K", "Self-tests %s (cmdline: \"%s\")\n", tests_enabled ? "enabled" : "disabled", cmdline ? cmdline : "");

    pmm_init();
    uint64_t pages = pmm_claim(LIMINE_MEMMAP_USABLE);
    kprintf("PMM", "Initialized PMM, ready to alloc/free physical pages\n");
    kprintf("PMM", "Claimed %zu pages of %zu B for a total of %zu MB kernel heap available\n", pages, PAGE_SIZE, pages * PAGE_SIZE / 1024 / 1024);

    heap_init();
    kprintf("HEAP", "Initialized Heap, ready to kmalloc/kfree dynamic virtual memory block\n");

    vmm_init();
    kprintf("VMM", "Initialized VMM, default context root at phys:%p virt:%p (kernel table root at phys:%p)\n", (void *)vmm_kernel_context->root_phys, vmm_kernel_context->root_virt, (void *)arch_vmm_get_kernel_root());

    if (framebuffer_request.response) {
        fb_init(framebuffer_request.response);
        kprintf("FB", "Initialized the framebuffer, ready to kprintf also on the screen\n");
    }

    struct vfs_node *ramfs = ramfs_create_root();
    if (!ramfs) kpanic("cannot create ramfs");
    if (vfs_mount("/", ramfs)) kpanic("cannot mount ramfs on /");
    if (module_request.response && module_request.response->modules && module_request.response->modules[0]) {
        if (tar_load((uint64_t) module_request.response->modules[0]->address, module_request.response->modules[0]->size) != 0) {
            kpanic("cannot load initrd into ramfs at /");
        }
    }

    struct vfs_node *console_dev = console_dev_create();
    if (!console_dev) kpanic("cannot create console dev");
    if (vfs_mount("/dev/console", console_dev)) kpanic("cannot mount console dev on /dev/console");

    sched_init();
    kprintf("SCHED", "Initialized scheduler, ready to switch context when timer will be up\n");

    // the kernel thread that will execute the rest of the boot process once we go preemptive
    struct task *boot_task = task_init_boot();
    if (!boot_task) kpanic("cannot create boot task");
    sched_add_task(boot_task);

    // in this block we hand build the /bin/init process by
    // creating a context,
    // loading the ELF (that will give us the entry point)
    // building the process stack
    // and adding the process to the scheduler
    struct vmm_context *init_ctx = vmm_create_context();
    if (!init_ctx) kpanic("cannot create context for /bin/init");
    struct elf_load_result elf;
    if (elf_load("/bin/init", init_ctx, &elf) != 0) kpanic("cannot load /bin/init");
    uint64_t sp = elf_build_user_stack(init_ctx, USER_STACK_TOP, 0, NULL, NULL);  // C3
    if (!sp) kpanic("cannot create user stack");
    struct task *init = task_create_user("/bin/init", init_ctx, elf.entry, sp);
    if (!init) kpanic("cannot create /bin/init task");
    sched_add_task(init);

    kprintf("TIMER", "Starting timer at %d Hz\n", TIMER_HZ);
    arch_timer_init(TIMER_HZ);

    // run all tests before we go preemptive
    if (tests_enabled) test_all();

    // enable buffering input from serial
    console_input_init();
    // enable capturing input from serial
    serial_init_get();

    kprintf("IRQ", "Enable IRQ\n");
    arch_irq_enable();

    kprintf("K", "Run for 10 seconds\n");
    uint64_t last_seconds = 0;
    while (true) {
        uint64_t seconds = timer_get_ticks() / TIMER_HZ;
        if (seconds != last_seconds) {
            //task_dump_all();
            last_seconds = seconds;
            if (seconds >= 10) {
                break;
            }
        }
    }

    kprintf("K", "All done here, shutting down.\n");
    arch_shutdown();
}
