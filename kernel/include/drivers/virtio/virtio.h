#ifndef PROS_VIRTIO_H
#define PROS_VIRTIO_H

#include <stdint.h>
#include <stddef.h>

// Descriptor Flags
#define VIRTQ_DESC_F_NEXT  1  // Chained descriptor follows in 'next' field
#define VIRTQ_DESC_F_WRITE 2  // Device writes into buffer (otherwise read-only for device)

// VirtIO Ring Descriptor
struct virtq_desc {
    uint64_t addr; // Physical address of buffer
    uint32_t len; // Length of buffer in bytes
    uint16_t flags; // VIRTQ_DESC_F_NEXT, VIRTQ_DESC_F_WRITE
    uint16_t next; // Index of next descriptor in chain if VIRTQ_DESC_F_NEXT is set
} __attribute__((packed));

// VirtIO Available Ring (Driver -> Device)
struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[]; // Array of head descriptor indices
} __attribute__((packed));

// VirtIO Used Ring Element & Ring (Device -> Driver)
struct virtq_used_elem {
    uint32_t id; // Index of head descriptor in chain
    uint32_t len; // Total bytes written by device
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
} __attribute__((packed));

// Combined VirtQueue Controller Structure
struct virtq {
    uint16_t num; // Queue size (number of descriptors, e.g. 16 or 128)

    // Virtual pointers (accessed by Kernel CPU)
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;

    // Physical addresses (passed to Hardware Device)
    uint64_t desc_phys;
    uint64_t avail_phys;
    uint64_t used_phys;

    uint16_t free_head; // Head index of free descriptor linked list
    uint16_t num_free; // Count of available free descriptors
    uint16_t last_used_idx; // Driver's shadow index for polling the used ring
};

int virtq_init(struct virtq *vq, uint16_t queue_size);

int virtq_alloc_desc(struct virtq *vq);

void virtq_free_chain(struct virtq *vq, uint16_t head);

#endif //PROS_VIRTIO_H
