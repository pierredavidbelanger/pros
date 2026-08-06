#include "drivers/virtio/virtio.h"

#include "mm/pmm.h"
#include "core/memory.h"
#include "core/kprintf.h"

#include <stdbool.h>

int virtq_init(struct virtq *vq, uint16_t queue_size) {
    if (!vq || queue_size == 0) return -1;

    // Calculate byte sizes for the 3 tables
    size_t desc_size = sizeof(struct virtq_desc) * queue_size;
    size_t avail_size = sizeof(uint16_t) * (3 + queue_size);

    // Used ring is page-aligned (4096 bytes)
    size_t desc_avail_size = (desc_size + avail_size + PAGE_SIZE) & ~(PAGE_SIZE - 1);
    size_t used_size = sizeof(uint16_t) * 3 + sizeof(struct virtq_used_elem) * queue_size;
    size_t total_bytes = desc_avail_size + ((used_size + PAGE_SIZE) & ~(PAGE_SIZE - 1));
    size_t pages = (total_bytes + PAGE_SIZE) / PAGE_SIZE;

    // Allocate contiguous physical pages
    uint64_t phys_base = pmm_alloc(pages);
    if (!phys_base) {
        kprintf("[VIRTIO] Error: Failed to allocate %zu pages for VirtQueue\n", pages);
        return -1;
    }
    uint8_t *virt_base = (uint8_t *) pmm_phys_to_virt(phys_base);
    memset(virt_base, 0, total_bytes);

    // Set virtual pointers and physical base addresses
    vq->num = queue_size;

    vq->desc = (struct virtq_desc *) virt_base;
    vq->desc_phys = phys_base;

    vq->avail = (struct virtq_avail *) (virt_base + desc_size);
    vq->avail_phys = phys_base + desc_size;

    vq->used = (struct virtq_used *) (virt_base + desc_avail_size);
    vq->used_phys = phys_base + desc_avail_size;

    // Linked free-list of descriptors
    for (uint16_t i = 0; i < queue_size - 1; i++) {
        vq->desc[i].next = i + 1;
    }
    vq->desc[queue_size - 1].next = 0xFFFF; // end of list marker

    vq->free_head = 0;
    vq->num_free = queue_size;
    vq->last_used_idx = 0;

    return 0;
}

int virtq_alloc_desc(struct virtq *vq) {
    if (!vq || vq->num_free == 0) {
        return -1; // Out of descriptors
    }

    uint16_t head = vq->free_head;
    vq->free_head = vq->desc[head].next;
    vq->num_free--;

    // Clear descriptor fields
    vq->desc[head].addr = 0;
    vq->desc[head].len = 0;
    vq->desc[head].flags = 0;
    vq->desc[head].next = 0;

    return (int) head;
}

void virtq_free_chain(struct virtq *vq, uint16_t head) {
    if (!vq || head >= vq->num) return;

    uint16_t curr = head;
    while (1) {
        uint16_t next = vq->desc[curr].next;
        bool has_next = (vq->desc[curr].flags & VIRTQ_DESC_F_NEXT) != 0;

        // Push 'curr' back onto free_head stack
        vq->desc[curr].next = vq->free_head;
        vq->free_head = curr;
        vq->num_free++;

        if (!has_next) break;
        curr = next;
    }
}
