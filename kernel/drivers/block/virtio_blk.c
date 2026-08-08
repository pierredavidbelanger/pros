#include "drivers/block/virtio_blk.h"
#include "drivers/virtio/virtio_transport.h"
#include "arch/arch.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "core/memory.h"
#include "core/kprintf.h"

static struct virtio_blk_device *g_blk_dev = NULL;

static int virtio_blk_rw(struct virtio_blk_device *dev, uint64_t lba, uint32_t count, void *buffer, bool write) {
    if (!dev || !buffer || count == 0) return -1;

    int d0 = virtq_alloc_desc(&dev->vq);
    int d1 = virtq_alloc_desc(&dev->vq);
    int d2 = virtq_alloc_desc(&dev->vq);

    if (d0 < 0 || d1 < 0 || d2 < 0) {
        if (d0 >= 0) virtq_free_chain(&dev->vq, d0);
        if (d1 >= 0) virtq_free_chain(&dev->vq, d1);
        if (d2 >= 0) virtq_free_chain(&dev->vq, d2);
        return -1;
    }

    struct virtio_blk_req req;
    req.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req.ioprio = 0;
    req.sector = lba;

    dev->vq.desc[d0].addr = pmm_virt_to_phys(&req);
    dev->vq.desc[d0].len = sizeof(struct virtio_blk_req);
    dev->vq.desc[d0].flags = VIRTQ_DESC_F_NEXT;
    dev->vq.desc[d0].next = d1;

    dev->vq.desc[d1].addr = pmm_virt_to_phys(buffer);
    dev->vq.desc[d1].len = count * dev->block_dev.block_size;
    dev->vq.desc[d1].flags = VIRTQ_DESC_F_NEXT | (write ? 0 : VIRTQ_DESC_F_WRITE);
    dev->vq.desc[d1].next = d2;

    volatile uint8_t status = 0xFF;
    dev->vq.desc[d2].addr = pmm_virt_to_phys((void *)&status);
    dev->vq.desc[d2].len = 1;
    dev->vq.desc[d2].flags = VIRTQ_DESC_F_WRITE;
    dev->vq.desc[d2].next = 0;

    uint16_t avail_slot = dev->vq.avail->idx % dev->vq.num;
    dev->vq.avail->ring[avail_slot] = (uint16_t)d0;

    #if defined(__x86_64__)
    asm volatile ("sfence" ::: "memory");
    #elif defined(__aarch64__)
    asm volatile ("dmb ish" ::: "memory");
    #endif

    dev->vq.avail->idx++;

    if (dev->transport && dev->transport->ops->notify_queue) {
        dev->transport->ops->notify_queue(dev->transport, 0);
    }

    uint32_t timeout = 100000000;
    while (dev->vq.last_used_idx == dev->vq.used->idx) {
        if (--timeout == 0) {
            kprintf("[VBLK ] Error: timeout waiting for virtio block device\n");
            return -1;
        }
        #if defined(__x86_64__)
        asm volatile ("pause");
        #elif defined(__aarch64__)
        asm volatile ("yield");
        #endif
    }
    dev->vq.last_used_idx++;

    virtq_free_chain(&dev->vq, (uint16_t)d0);

    return (status == VIRTIO_BLK_S_OK) ? 0 : -1;
}

static int virtio_blk_read_blocks(struct blockdev *dev, uint64_t lba, uint32_t count, void *buf) {
    if (!dev || !dev->priv_data) return -1;
    struct virtio_blk_device *blk = (struct virtio_blk_device *)dev->priv_data;
    return virtio_blk_rw(blk, lba, count, buf, false);
}

static int virtio_blk_write_blocks(struct blockdev *dev, uint64_t lba, uint32_t count, const void *buf) {
    if (!dev || !dev->priv_data) return -1;
    struct virtio_blk_device *blk = (struct virtio_blk_device *)dev->priv_data;
    return virtio_blk_rw(blk, lba, count, (void *)buf, true);
}

int virtio_blk_probe(struct virtio_transport *transport) {
    if (!transport) return -1;

    struct virtio_blk_device *dev = (struct virtio_blk_device *)kmalloc(sizeof(struct virtio_blk_device));
    if (!dev) return -1;
    memset(dev, 0, sizeof(struct virtio_blk_device));
    dev->transport = transport;

    uint32_t features = transport->ops->get_features(transport);
    transport->ops->set_features(transport, features);

    transport->ops->set_status(transport, 8); // VIRTIO_MMIO_STATUS_FEATURES_OK
    uint8_t status = transport->ops->get_status(transport);
    if (!(status & 8)) {
        kprintf("[VBLK ] Error: VirtIO device rejected FEATURES_OK\n");
        kfree(dev);
        return -1;
    }

    if (transport->ops->setup_queue(transport, 0, &dev->vq) != 0) {
        kfree(dev);
        return -1;
    }

    transport->ops->set_status(transport, 4); // VIRTIO_MMIO_STATUS_DRIVER_OK

    uint64_t total_capacity_sectors = 0;
    transport->ops->read_device_config(transport, 0, 8, &total_capacity_sectors);

    dev->block_dev.block_size = 512;
    dev->block_dev.total_blocks = total_capacity_sectors ? total_capacity_sectors : 204800;
    dev->block_dev.is_partition = false;
    dev->block_dev.lba_offset = 0;
    dev->block_dev.priv_data = dev;
    dev->block_dev.read_blocks = virtio_blk_read_blocks;
    dev->block_dev.write_blocks = virtio_blk_write_blocks;
    strcpy(dev->block_dev.name, "hd0");

    g_blk_dev = dev;
    blockdev_register(&dev->block_dev);
    
    kprintf("[VBLK ] Initialized VirtIO-Block device 'hd0' (cap: %llu sectors)\n", total_capacity_sectors);
    return 0;
}
