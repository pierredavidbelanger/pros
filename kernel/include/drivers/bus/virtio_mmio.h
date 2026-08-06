#ifndef VIRTIO_MMIO_H
#define VIRTIO_MMIO_H

#include "stdc.h"

#define VIRTIO_MMIO_MAGIC 0x74726976 // "virt" in Little Endian

// Offsets inside a VirtIO MMIO slot
#define VIRTIO_MMIO_REG_MAGIC_VALUE        0x000
#define VIRTIO_MMIO_REG_VERSION            0x004
#define VIRTIO_MMIO_REG_DEVICE_ID          0x008
#define VIRTIO_MMIO_REG_VENDOR_ID          0x00c
#define VIRTIO_MMIO_REG_DEVICE_FEATURES    0x010
#define VIRTIO_MMIO_REG_DRIVER_FEATURES    0x020
#define VIRTIO_MMIO_REG_QUEUE_SEL          0x030
#define VIRTIO_MMIO_REG_QUEUE_NUM_MAX      0x034
#define VIRTIO_MMIO_REG_QUEUE_NUM          0x038
#define VIRTIO_MMIO_REG_QUEUE_PFN          0x040
#define VIRTIO_MMIO_REG_QUEUE_NOTIFY       0x050
#define VIRTIO_MMIO_REG_INTERRUPT_STATUS   0x060
#define VIRTIO_MMIO_REG_INTERRUPT_ACK      0x064
#define VIRTIO_MMIO_REG_STATUS             0x070

// VirtIO Device Status Bits
#define VIRTIO_MMIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_MMIO_STATUS_DRIVER      2
#define VIRTIO_MMIO_STATUS_DRIVER_OK   4
#define VIRTIO_MMIO_STATUS_FEATURES_OK 8
#define VIRTIO_MMIO_STATUS_FAILED      128

struct virtio_mmio_slot {
    uint64_t phys_base;
    uint32_t version;
    uint32_t device_id;
    uint32_t vendor_id;
};

void virtio_mmio_init(void);
bool virtio_mmio_find_device(uint32_t device_id, struct virtio_mmio_slot *out_slot);

#endif // VIRTIO_MMIO_H
