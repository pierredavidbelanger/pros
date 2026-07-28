#include "fdt.h"
#include "string.h"

static inline uint32_t fdt32(uint32_t val) {
    return __builtin_bswap32(val);
}

// Align pointer or offset to 4 bytes
static inline uint32_t align4(uint32_t val) {
    return (val + 3) & ~3;
}

int fdt_get_memory(const void *fdt_ptr, uintptr_t *out_base, size_t *out_size) {
    const struct fdt_header *header = (const struct fdt_header *) fdt_ptr;
    if (fdt32(header->magic) != FDT_MAGIC) {
        return -1; // Invalid DTB header
    }

    const char *struct_ptr = (const char *) fdt_ptr + fdt32(header->off_dt_struct);
    const char *strings_ptr = (const char *) fdt_ptr + fdt32(header->off_dt_strings);

    int in_memory_node = 0;
    uint32_t offset = 0;

    while (1) {
        uint32_t token = fdt32(*(uint32_t *) (struct_ptr + offset));
        offset += 4;

        if (token == FDT_BEGIN_NODE) {
            const char *name = struct_ptr + offset;
            // Memory node is usually named "memory" or "memory@<addr>"
            if (name[0] == 'm' && name[1] == 'e' && name[2] == 'm' &&
                name[3] == 'o' && name[4] == 'r' && name[5] == 'y') {
                in_memory_node = 1;
            }
            offset += align4(strlen(name) + 1);
        } else if (token == FDT_END_NODE) {
            in_memory_node = 0;
        } else if (token == FDT_PROP) {
            uint32_t len = fdt32(*(uint32_t *) (struct_ptr + offset));
            uint32_t nameoff = fdt32(*(uint32_t *) (struct_ptr + offset + 4));
            offset += 8;

            const char *prop_name = strings_ptr + nameoff;

            if (in_memory_node && strcmp(prop_name, "reg") == 0) {
                // In QEMU virt machines, #address-cells = 2 and #size-cells = 2 (64-bit cells)
                // or #address-cells = 1 and #size-cells = 1 (32-bit cells).
                const uint32_t *reg = (const uint32_t *) (struct_ptr + offset);

                if (len == 16) {
                    // 2 cells address, 2 cells size
                    *out_base = ((uint64_t) fdt32(reg[0]) << 32) | fdt32(reg[1]);
                    *out_size = ((uint64_t) fdt32(reg[2]) << 32) | fdt32(reg[3]);
                } else if (len == 8) {
                    // 1 cell address, 1 cell size
                    *out_base = fdt32(reg[0]);
                    *out_size = fdt32(reg[1]);
                }
                return 0; // Success!
            }
            offset += align4(len);
        } else if (token == FDT_END) {
            break;
        }
    }
    return -1; // Memory node not found
}
