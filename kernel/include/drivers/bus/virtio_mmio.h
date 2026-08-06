#ifndef VIRTIO_MMIO_H
#define VIRTIO_MMIO_H

#include <stdint.h>
#include <stdbool.h>

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

typedef struct virtio_mmio_slot {
    uint64_t phys_base;
    uint32_t version;
    uint32_t device_id;
    uint32_t vendor_id;
} virtio_mmio_slot_t;

// Probe QEMU VirtIO MMIO slots (e.g. 0x0A000000 - 0x0A003E00)
void virtio_mmio_init(void);

// Find a VirtIO MMIO slot matching device_id (returns true if found)
bool virtio_mmio_find_device(uint32_t device_id, virtio_mmio_slot_t *out_slot);

#endif // VIRTIO_MMIO_H
