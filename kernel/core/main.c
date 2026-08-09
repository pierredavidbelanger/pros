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
#include "fs/ramfs/ramfs.h"
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

    if (framebuffer_request.response) {
        fb_init(framebuffer_request.response);
        kprintf("[FB   ] Initialized the framebuffer, ready to kprintf also on the screen\n");
    }

    struct vfs_node *ramfs = ramfs_create_root();
    if (ramfs) {
        if (vfs_mount("/", ramfs) == 0) {
            if (module_request.response && module_request.response->modules && module_request.response->modules[0]) {
                tar_load((uint64_t) module_request.response->modules[0]->address, module_request.response->modules[0]->size);
            }
        }
    }
    // test_vfs even though ramfs may not have been correctly loaded or mounted
    if (tests_enabled) test_vfs();

    kprintf("[K    ] All done here, shutting down.\n");
    arch_shutdown();
}

void test_pmm(void) {
    uint64_t one_page = pmm_alloc(1);
    if (!one_page) {
        kprintf("[PMM  ] Did NOT got one page!\n");
    }
    kprintf("[PMM  ] Got one page at %p\n", one_page);
    pmm_free(one_page, 1);
    uint64_t two_page = pmm_alloc(2);
    if (!two_page) {
        kprintf("[PMM  ] Did NOT got two pages!\n");
    }
    kprintf("[PMM  ] Got two pages at %p\n", two_page);
    pmm_free(two_page, 2);
}

static bool test_heap_check_pattern(const uint8_t *buf, size_t size, uint8_t value) {
    for (size_t i = 0; i < size; i++) {
        if (buf[i] != value) return false;
    }
    return true;
}

static void test_heap_report(const char *name, bool ok) {
    kprintf("[HEAP ] %-38s %s\n", name, ok ? "PASS" : "FAIL");
}

void test_heap(void) {
    // a small block must be writable end to end
    uint8_t *small = kmalloc(32);
    bool ok = small != NULL;
    if (ok) {
        memset(small, 0xAA, 32);
        ok = test_heap_check_pattern(small, 32, 0xAA);
    }
    test_heap_report("kmalloc small", ok);

    // a block larger than a page forces a fresh multi-page pmm_alloc
    uint8_t *big = kmalloc(8000);
    ok = big != NULL;
    if (ok) {
        memset(big, 0xBB, 8000);
        ok = test_heap_check_pattern(big, 8000, 0xBB);
    }
    test_heap_report("kmalloc multi-page", ok);

    // the allocator promises HEAP_ALIGNMENT on the payload address, not just on the requested size
    test_heap_report("kmalloc payload alignment", small != NULL && ((uintptr_t) small % HEAP_ALIGNMENT) == 0);

    kfree(big);

    // kcalloc must hand back zeroed memory
    uint8_t *zeroed = kcalloc(16, 8);
    test_heap_report("kcalloc zeroes", zeroed != NULL && test_heap_check_pattern(zeroed, 16 * 8, 0x00));
    kfree(zeroed);

    // num * size must be rejected before it can wrap
    test_heap_report("kcalloc overflow rejected", kcalloc(SIZE_MAX, 2) == NULL);

    // krealloc(NULL, n) behaves as kmalloc(n)
    uint8_t *from_null = krealloc(NULL, 64);
    test_heap_report("krealloc NULL allocates", from_null != NULL);

    // krealloc(p, 0) behaves as kfree(p) and yields NULL
    test_heap_report("krealloc zero frees", krealloc(from_null, 0) == NULL);

    // shrinking always fits in place, so the pointer must not move and the data must survive
    uint8_t *shrink = kmalloc(256);
    memset(shrink, 0xCC, 256);
    uint8_t *shrunk = krealloc(shrink, 64);
    test_heap_report("krealloc shrink keeps pointer", shrunk == shrink && test_heap_check_pattern(shrunk, 64, 0xCC));
    kfree(shrunk);

    // kmalloc splits the block it carves from, so this block's own remainder is guaranteed to be the
    // free, physically contiguous neighbour that krealloc absorbs when it grows a block in place
    uint8_t *grow = kmalloc(64);
    memset(grow, 0xDD, 64);
    uint8_t *grown = krealloc(grow, 128);
    test_heap_report("krealloc grow in place", grown == grow && test_heap_check_pattern(grown, 64, 0xDD));
    kfree(grown);

    // no contiguous free run is anywhere near 100000 B here, so the block cannot grow in place:
    // krealloc must fall back to allocate-copy-free and carry the data across to a new address
    uint8_t *pinned = kmalloc(64);
    memset(pinned, 0xEE, 64);
    uint8_t *moved = krealloc(pinned, 100000);
    test_heap_report("krealloc grow by move", moved != NULL && moved != pinned && test_heap_check_pattern(moved, 64, 0xEE));
    kfree(moved);

    // freeing NULL is a no-op, not a fault
    kfree(NULL);
    test_heap_report("kfree NULL survives", true);

    kfree(small);

    // once blocks are freed and coalesced they must be reused, not answered with fresh PMM pages every cycle
    kfree(kmalloc(2000));
    size_t free_before = pmm_get_free_page_count();
    for (int i = 0; i < 100; i++) {
        kfree(kmalloc(2000));
    }
    test_heap_report("kfree reuses blocks", pmm_get_free_page_count() == free_before);
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
    int fd = sys_open("/root", 0);
    if (fd >= 0) {
        kprintf("/root fd=%d\n", fd);
        kprintf("----------- ls /root ----------\n");
        struct vfs_dirent entry;
        while (sys_readdir(fd, &entry) == 1) {
            if (entry.flags == VFS_DIRECTORY) {
                kprintf("%s/\n", entry.name);
            } else {
                kprintf("%s\n", entry.name);
            }
        }
        kprintf("-------------------------------\n");
        sys_close(fd);
    } else {
        kprintf("cannot open /\n");
    }
    fd = sys_open("/root/hello.txt", O_RDONLY);
    if (fd >= 0) {
        kprintf("/root/hello.txt fd=%d\n", fd);
        char buffer[2000];
        int64_t bytes_read = sys_read(fd, buffer, 2000);
        if (bytes_read > 0) {
            // Null-terminate it just to be safe before printing
            buffer[bytes_read] = '\0';
            kprintf("------ cat root/hello.txt -----\n");
            kprintf("%s\n", buffer);
            kprintf("-------------------------------\n");
        }
        sys_close(fd);
    } else {
        kprintf("cannot open /root/hello.txt\n");
    }
}
