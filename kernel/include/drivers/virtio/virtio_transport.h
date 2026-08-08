#ifndef PROS_VIRTIO_TRANSPORT_H
#define PROS_VIRTIO_TRANSPORT_H

#include "stdc.h"
#include "drivers/virtio/virtio.h"

struct virtio_transport;

struct virtio_transport_ops {
    uint32_t (*get_features)(struct virtio_transport *transport);
    void (*set_features)(struct virtio_transport *transport, uint32_t features);
    uint8_t (*get_status)(struct virtio_transport *transport);
    void (*set_status)(struct virtio_transport *transport, uint8_t status);
    
    // Reads capacity/config specific to the device
    void (*read_device_config)(struct virtio_transport *transport, uint32_t offset, uint8_t size, void *buf);
    
    // Setup virtqueue for this transport
    int (*setup_queue)(struct virtio_transport *transport, uint16_t queue_index, struct virtq *vq);
    
    void (*notify_queue)(struct virtio_transport *transport, uint16_t queue_index);
};

struct virtio_transport {
    struct virtio_transport_ops *ops;
    void *priv_data; // Transport specific data (like mmio_base or pci_device)
};

#endif //PROS_VIRTIO_TRANSPORT_H
