#ifndef PROS_VMM_H
#define PROS_VMM_H

#include <stdint.h>
#include <stddef.h>

struct vmm_context {
    uint64_t *root_phys;
    uint64_t *root_virt;
};

extern struct vmm_context *vmm_kernel_context;
extern struct vmm_context *vmm_current_context;

void vmm_init(void);

struct vmm_context *vmm_create_context(void);

void vmm_destroy_context(struct vmm_context *ctx);

int vmm_map_page(struct vmm_context *ctx, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

int vmm_unmap_page(struct vmm_context *ctx, uint64_t virt_addr);

uint64_t vmm_virt_to_phys(struct vmm_context *ctx, uint64_t virt_addr);

void vmm_switch_context(struct vmm_context *ctx);

#endif //PROS_VMM_H
