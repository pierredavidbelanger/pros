#include <drivers/of/fdt.h>
#include <lib/smoldtb.h>

#include "kernel/kernel.h"
#include "lib/string.h"
#include "mm/mem.h"

static void *fdt_malloc(size_t length) {
    kprintf("fdt_malloc(%d)\n", length);
    if (length > kscratch_bss_buf_size()) {
        kprintf("fdt_malloc(%d) failed > %d\n", length, kscratch_bss_buf_size());
    }
    return kscratch_bss_buf();
}

static void fdt_free(void *ptr, size_t length) {
    kprintf("fdt_free(%p, %d)\n", ptr, length);
}

static void fdt_on_error(const char *why) {
    kprintf("fdt_on_error('%s')\n", why);
}

int fdt_get_memory(const void *fdt_ptr, uintptr_t *out_base, size_t *out_size) {
    return 0;
}

static void fdt_dump_node(dtb_node *node, int depth) {
    for (int d = 0; d < depth; d++) kprintf(" ");
    dtb_node_stat nstat;
    if (dtb_stat_node(node, &nstat)) {
        kprintf("+ %s\n", nstat.name);
        for (size_t i = 0; i < nstat.prop_count; i++) {
            dtb_prop* prop = dtb_get_prop(node, i);
            if (prop == NULL) continue;
            for (int d = 0; d < depth; d++) kprintf(" ");
            dtb_prop_stat pstat;
            if (dtb_stat_prop(prop, &pstat)) {
                kprintf(" - %s", pstat.name);
                if (pstat.data_len > 0) {
                    kprintf(" (len=%d)", pstat.data_len);
                    const char* str = dtb_read_prop_string(prop, 0);
                    if (str != NULL) {

                        // if (pstat.data_len < 64) {
                            // kprintf(" = \"%s\"", str);
                        // } else {
                            // kprintf(" = [long string len=%d]", pstat.data_len);
                        // }

                        // char buf[512];
                        // snprintf(buf, sizeof(buf), "%s", str);
                        // kprintf(" = \"%s\"", buf);

                        kprintf(" = \"");
                        for (size_t k = 0; k < pstat.data_len && str[k] != '\0'; k++) {
                            unsigned char c = (unsigned char)str[k];
                            if (c >= 32 && c <= 126) {
                                kprintf("%c", c);
                            } else {
                                kprintf("[%x]", c); // Non-printable control character (prints as [0xXX])
                            }
                        }
                        kprintf("\"");

                        // kprintf(" = \"%s\"", str);

                    } else if (strcmp(pstat.name, "reg") == 0) {
                        size_t ac = dtb_get_addr_cells_for(node);
                        size_t sc = dtb_get_size_cells_for(node);
                        dtb_pair layout = { .a = ac, .b = sc };
                        dtb_pair pair = {};
                        if (dtb_read_prop_2(prop, layout, &pair) > 0) {
                            kprintf(" = <addr=0x%x size=0x%x>", (uintptr_t)pair.a, (uintptr_t)pair.b);
                        }
                    }
                    // if (pstat.data_len == 4) {
                        // smoldtb_value val = 0;
                        // dtb_read_prop_1(prop, 1, &val);
                        // kprintf(" %x", val);
                    // }
                    // if (pstat.data_len == 16) {
                        // dtb_pair layout = { .a = addr_cells, .b = size_cells };
                        // dtb_pair val = {};
                        // dtb_read_prop_2(prop, layout, &val);
                        // kprintf(" %x %x", val.a, val.b);
                    // }
                    // for (size_t d = 0; d < pstat.data_len; d++) {
                        // const char data = (const char) (pstat.data + d);
                        // kprintf(" %d", (char) data);
                    // }
                }
                kprintf("\n");
            } else {
                kprintf(" - CANT STAT PROP\n");
            }
        }
    } else {
        kprintf("+ CANT STAT NODE\n");
    }
    dtb_node* child = dtb_get_child(node);
    while (child != NULL) {
        fdt_dump_node(child, depth + 1);
        child = dtb_get_sibling(child);
    }
}

void fdt_dump(const void *fdt_ptr) {
    size_t total_size = dtb_query_total_size((uintptr_t) fdt_ptr);
    kprintf("total_size: %d\n", total_size);
    dtb_ops ops = {};
    ops.malloc = fdt_malloc;
    ops.free = fdt_free;
    ops.on_error = fdt_on_error;
    bool init = dtb_init((uintptr_t) fdt_ptr, ops);
    if (!init) {
        kprintf("fdt_init() failed\n");
    }
    // dtb_reserved_memory reserved;
    // dtb_read_resv_memory(1, &reserved);
    // kprintf("reserved memory base: %d\n", reserved.base);
    // kprintf("reserved memory length: %d\n", reserved.length);
    dtb_node *node = dtb_find("/");
    while (node != NULL) {
        fdt_dump_node(node, 0);
        node = dtb_get_sibling(node);
    }
}
