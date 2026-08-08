#include "core/boot.h"

#include "arch/arch.h"
#include "core/fb.h"
#include "core/console.h"
#include "core/kprintf.h"
#include "core/memory.h"
#include "core/syscalls.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "mm/vmm.h"
#include "fs/vfs/vfs.h"
#include "fs/tar/tar.h"

void test_pmm(void);
void test_heap(void);
void test_vmm(void);
void test_vfs(void);

void _start(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        arch_halt();
    }

    arch_init();

    console_init();
    kprintf("[CON  ] Initialized console, ready to kprintf on the serial console\n");

    kprintf("[K    ] Wellcome to PjErOS!\n");

    // Self-tests are enabled in the limine.conf, this serves two purpose:
    // 1- i learn how to actually use limine cmdline feature
    // 2- i can make the kernel less verbose when i want
    const char *cmdline = cmdline_request.response ? cmdline_request.response->cmdline : NULL;
    bool tests_enabled = cmdline && strstr(cmdline, "pros.tests") != NULL;
    kprintf("[K    ] Self-tests %s (cmdline: \"%s\")\n", tests_enabled ? "enabled" : "disabled", cmdline ? cmdline : "");

    size_t pages = pmm_init(hhdm_request.response, memmap_request.response);
    kprintf("[PMM  ] Initialized PMM, ready to alloc/free physical pages\n");
    kprintf("[PMM  ] PMM manage %zu pages of %zu B for a total of %zu MB kernel heap available\n", pages, PAGE_SIZE, pages * PAGE_SIZE / 1024 / 1024);
    if (tests_enabled) test_pmm();

    heap_init();
    kprintf("[HEAP ] Initialized Heap, ready to kmalloc/kfree dynamic virtual memory block\n");
    if (tests_enabled) test_heap();

    vmm_init();
    kprintf("[VMM  ] Initialized VMM, default context root at phys:%p virt:%p (kernel table root at phys:%p)\n", (void *)vmm_kernel_context->root_phys, vmm_kernel_context->root_virt, (void *)arch_vmm_get_kernel_root());
    if (tests_enabled) test_vmm();
    if (tests_enabled) test_vfs();

    if (framebuffer_request.response) {
        fb_init(framebuffer_request.response);
        kprintf("[FB   ] Initialized the framebuffer, ready to kprintf also on the screen\n");
    }

    if (module_request.response && module_request.response->modules && module_request.response->modules[0]) {
        tar_mount((uint64_t) module_request.response->modules[0]->address, module_request.response->modules[0]->size);
    }

    kprintf("[K    ] All done here, shutting down.\n");
    arch_shutdown();
}

void test_pmm(void) {
    uint64_t one_page = pmm_alloc(1);
    kprintf("[PMM  ] Got one page at %p\n", one_page);
    pmm_free(one_page, 1);
    uint64_t two_page = pmm_alloc(2);
    kprintf("[PMM  ] Got two pages at %p\n", two_page);
    pmm_free(two_page, 2);
}

void test_heap(void) {
    // Test small allocation
    char *buf1 = kmalloc(32);
    kprintf("[HEAP ] buf1 allocated 32B at %p\n", buf1);
    // Test multi-page allocation
    char *buf2 = kmalloc(8000);
    kprintf("[HEAP ] buf2 allocated 8000B at %p\n", buf2);
    // Test small allocation again
    char *buf3 = kmalloc(32);
    kprintf("[HEAP ] buf3 allocated at 32B %p\n", buf3);
    // Test multi-page allocation again
    char *buf4 = kmalloc(8000);
    kprintf("[HEAP ] buf4 allocated at 8000B %p\n", buf4);
    kfree(buf1);
    kfree(buf2);
    kfree(buf3);
    kfree(buf4);
}

void test_vmm(void) {
    // Test 1: Basic map & write on kernel context
    uint64_t test_phys = pmm_alloc(1);
    uint64_t test_virt = 0x40000000ULL;
    if (vmm_map_page(vmm_kernel_context, test_virt, test_phys, VMM_WRITABLE) == 0) {
        kprintf("[VMM  ] Mapped virt %p -> phys %p\n", (void *)test_virt, (void *)test_phys);
        *(volatile uint32_t *)test_virt = 0xDEADBEEF;
        uint32_t *hhdm_ptr = pmm_phys_to_virt(test_phys);
        kprintf("[VMM  ] Write 0xDEADBEEF, read back 0x%X via HHDM\n", *hhdm_ptr);
    }
    vmm_unmap_page(vmm_kernel_context, test_virt);
    pmm_free(test_phys, 1);

    // Test 2: Context creation + isolation
    size_t free_pages_before = pmm_get_free_page_count();
    struct vmm_context *user_ctx = vmm_create_context();
    kprintf("[VMM  ] Created user context phys:%p\n", (void *)user_ctx->root_phys);
    // Opt this context into demand paging over a fixed test range (disabled by default).
    user_ctx->demand_page_lo = 0x60000000ULL;
    user_ctx->demand_page_hi = 0x70000000ULL;
    vmm_switch_context(user_ctx);
    kprintf("[VMM  ] Switched to user context (kernel kprintf still works!)\n");

    // Test 3: Demand paging (lazy allocation on fault)
    kprintf("[VMM  ] Touching unmapped page 0x60000000 (should trigger demand paging)...\n");
    *(volatile uint32_t *)0x60000000ULL = 0xCAFEBABE;
    kprintf("[VMM  ] Read back: 0x%X (expected 0xCAFEBABE)\n",
            *(volatile uint32_t *)0x60000000ULL);

    *(volatile uint32_t *)0x60001000ULL = 0x12345678;
    kprintf("[VMM  ] Page 2 read back: 0x%X (expected 0x12345678)\n",
            *(volatile uint32_t *)0x60001000ULL);

    kprintf("[VMM  ] Demand paging test PASSED!\n");

    // Negative test, this is not a valid demand-paging, it should fall through and panic.
    // *(volatile uint32_t *)0x80000000ULL = 0xBAADF00D;

    vmm_switch_context(vmm_kernel_context);
    vmm_destroy_context(user_ctx);

    // Test 4: Context teardown must not leak the pages it allocated (root + intermediate page tables + the two demand-paged frames from Test 3).
    size_t free_pages_after = pmm_get_free_page_count();
    kprintf("[VMM  ] Context teardown leak check: %zu pages before, %zu after (%s)\n",
            free_pages_before, free_pages_after,
            free_pages_before == free_pages_after ? "PASS" : "LEAK");
}

void test_vfs(void) {
    int fd = sys_open("/", 0);
    if (fd >= 0) {
        kprintf("------------- ls / ------------\n");
        struct vfs_dirent entry;
        while (sys_readdir(fd, &entry) == 1) {
            if (entry.flags == VFS_DIRECTORY) {
                kprintf("[DIR]  %s\n", entry.name);
            } else {
                kprintf("[FILE] %s\n", entry.name);
            }
        }
        kprintf("-------------------------------\n");
        sys_close(fd);
    }
    fd = sys_open("/test.txt", O_RDONLY);
    if (fd >= 0) {
        char buffer[2000];
        int64_t bytes_read = sys_read(fd, buffer, 2000);
        if (bytes_read > 0) {
            // Null-terminate it just to be safe before printing
            buffer[bytes_read] = '\0';
            kprintf("-------- cat /test.txt --------\n");
            kprintf("%s\n", buffer);
            kprintf("-------------------------------\n");
        }
        sys_close(fd);
    }
}
