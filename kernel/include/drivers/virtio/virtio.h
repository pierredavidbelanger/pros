#ifndef PROS_VIRTIO_H
#define PROS_VIRTIO_H

#include <stdint.h>
#include <stddef.h>

#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
} __attribute__((packed));

struct virtq {
    uint16_t num;

    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;

    uint64_t desc_phys;
    uint64_t avail_phys;
    uint64_t used_phys;

    uint16_t free_head;
    uint16_t num_free;
    uint16_t last_used_idx;
};

// VirtIO 1.0 Modern PCI Common Configuration Structure
struct virtio_pci_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t config_msix_vector;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_driver;
    uint64_t queue_device;
} __attribute__((packed));

int virtq_init(struct virtq *vq, uint16_t queue_size);
int virtq_alloc_desc(struct virtq *vq);
void virtq_free_chain(struct virtq *vq, uint16_t head);

#endif //PROS_VIRTIO_H
