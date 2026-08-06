#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include <stdint.h>
#include <stdbool.h>
#include "drivers/block/blockdev.h"
#include "drivers/virtio/virtio.h"

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

struct virtio_blk_device {
    struct blockdev block_dev;
    struct virtq vq;
    bool is_io_port;
    uint16_t io_port;
    volatile uint32_t *mmio_base;
    volatile uint32_t *notify_reg;
};

int virtio_blk_init(void);

#endif // VIRTIO_BLK_H
