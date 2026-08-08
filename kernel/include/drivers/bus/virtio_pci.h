#ifndef VIRTIO_PCI_H
#define VIRTIO_PCI_H

#include "stdc.h"
#include "drivers/bus/pci.h"

// Capability types
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
#define VIRTIO_PCI_CAP_PCI_CFG    5

struct virtio_pci_cap {
    uint8_t cap_vndr;      // PCI_CAP_ID_VNDR (0x09)
    uint8_t cap_next;
    uint8_t cap_len;
    uint8_t cfg_type;
    uint8_t bar;
    uint8_t padding[3];
    uint32_t offset;
    uint32_t length;
} __attribute__((packed));

struct virtio_pci_notify_cap {
    struct virtio_pci_cap cap;
    uint32_t notify_off_multiplier;
} __attribute__((packed));

void virtio_pci_init(void);

#endif // VIRTIO_PCI_H
