#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "drivers/block/blockdev.h"
#include "drivers/virtio/virtio.h"

#include "stdc.h"

#define VIRTIO_BLK_T_IN   0
#define VIRTIO_BLK_T_OUT  1

#define VIRTIO_BLK_S_OK     0
#define VIRTIO_BLK_S_IOERR  1
#define VIRTIO_BLK_S_UNSUPP 2

struct virtio_blk_req {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
} __attribute__((packed));

#include "drivers/virtio/virtio_transport.h"

struct virtio_blk_device {
    struct blockdev block_dev;
    struct virtq vq;
    struct virtio_transport *transport;
};

int virtio_blk_probe(struct virtio_transport *transport);

#endif // VIRTIO_BLK_H
