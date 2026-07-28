#ifndef PROS_FDT_H
#define PROS_FDT_H

#include <stddef.h>
#include <stdint.h>

struct fdt_header {
    uint32_t magic; // Must be 0xd00dfeed
    uint32_t totalsize; // Total DTB size in bytes
    uint32_t off_dt_struct; // Offset to structure block (tokens)
    uint32_t off_dt_strings; // Offset to strings block (property names)
    uint32_t off_mem_rsvmap; // Offset to memory reservation map
    uint32_t version; // Format version
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

#define FDT_MAGIC       0xd00dfeed
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009

int fdt_get_memory(const void *fdt_ptr, uintptr_t *out_base, size_t *out_size);

#endif //PROS_FDT_H
