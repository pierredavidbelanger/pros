#include "drivers/bus/virtio_mmio.h"

#include "core/kprintf.h"
#include "mm/pmm.h"

#include "drivers/virtio/virtio_transport.h"
#include "drivers/block/virtio_blk.h"
#include "mm/heap.h"
#include "core/memory.h"

#define VIRTIO_MMIO_BASE_PHYS 0x0A000000ULL
#define VIRTIO_MMIO_SLOT_SIZE 0x200ULL
#define VIRTIO_MMIO_SLOT_COUNT 32

struct virtio_mmio_transport {
    struct virtio_transport trans;
    volatile uint32_t *mmio_base;
};

static uint32_t vmmio_get_features(struct virtio_transport *trans) {
    struct virtio_mmio_transport *t = (struct virtio_mmio_transport *)trans->priv_data;
    t->mmio_base[VIRTIO_MMIO_REG_DEVICE_FEATURES / 4] = 0; // Select feature word 0
    return t->mmio_base[VIRTIO_MMIO_REG_DEVICE_FEATURES / 4];
}

static void vmmio_set_features(struct virtio_transport *trans, uint32_t features) {
    struct virtio_mmio_transport *t = (struct virtio_mmio_transport *)trans->priv_data;
    t->mmio_base[VIRTIO_MMIO_REG_DRIVER_FEATURES / 4] = 0; // Select feature word 0
    t->mmio_base[VIRTIO_MMIO_REG_DRIVER_FEATURES / 4] = features;
}

static uint8_t vmmio_get_status(struct virtio_transport *trans) {
    struct virtio_mmio_transport *t = (struct virtio_mmio_transport *)trans->priv_data;
    return (uint8_t)t->mmio_base[VIRTIO_MMIO_REG_STATUS / 4];
}

static void vmmio_set_status(struct virtio_transport *trans, uint8_t status) {
    struct virtio_mmio_transport *t = (struct virtio_mmio_transport *)trans->priv_data;
    t->mmio_base[VIRTIO_MMIO_REG_STATUS / 4] |= status;
}

static void vmmio_read_config(struct virtio_transport *trans, uint32_t offset, uint8_t size, void *buf) {
    struct virtio_mmio_transport *t = (struct virtio_mmio_transport *)trans->priv_data;
    volatile uint8_t *config_base = (volatile uint8_t *)t->mmio_base + 0x100;
    if (size == 8) {
        *(uint64_t *)buf = *(volatile uint64_t *)(config_base + offset);
    } else if (size == 4) {
        *(uint32_t *)buf = *(volatile uint32_t *)(config_base + offset);
    }
}

static int vmmio_setup_queue(struct virtio_transport *trans, uint16_t queue_index, struct virtq *vq) {
    struct virtio_mmio_transport *t = (struct virtio_mmio_transport *)trans->priv_data;
    t->mmio_base[VIRTIO_MMIO_REG_QUEUE_SEL / 4] = queue_index;
    
    uint32_t max_queue = t->mmio_base[VIRTIO_MMIO_REG_QUEUE_NUM_MAX / 4];
    if (max_queue == 0) return -1;

    uint16_t qsize = (max_queue > 128) ? 128 : (uint16_t)max_queue;

    if (virtq_init(vq, qsize) != 0) return -1;

    t->mmio_base[VIRTIO_MMIO_REG_QUEUE_NUM / 4] = qsize;
    t->mmio_base[VIRTIO_MMIO_REG_QUEUE_PFN / 4] = (uint32_t)(vq->desc_phys / 4096);

    return 0;
}

static void vmmio_notify_queue(struct virtio_transport *trans, uint16_t queue_index) {
    struct virtio_mmio_transport *t = (struct virtio_mmio_transport *)trans->priv_data;
    t->mmio_base[VIRTIO_MMIO_REG_QUEUE_NOTIFY / 4] = queue_index;
}

static struct virtio_transport_ops vmmio_ops = {
    .get_features = vmmio_get_features,
    .set_features = vmmio_set_features,
    .get_status = vmmio_get_status,
    .set_status = vmmio_set_status,
    .read_device_config = vmmio_read_config,
    .setup_queue = vmmio_setup_queue,
    .notify_queue = vmmio_notify_queue,
};

void virtio_mmio_init(void) {
    kprintf("[VMMIO] Probing VirtIO MMIO slots at phys:%p...\n", (void *)VIRTIO_MMIO_BASE_PHYS);

    for (size_t i = 0; i < VIRTIO_MMIO_SLOT_COUNT; i++) {
        uint64_t phys = VIRTIO_MMIO_BASE_PHYS + (i * VIRTIO_MMIO_SLOT_SIZE);
        volatile uint32_t *virt_base = pmm_phys_to_virt(phys);

        uint32_t magic = virt_base[VIRTIO_MMIO_REG_MAGIC_VALUE / 4];
        if (magic != VIRTIO_MMIO_MAGIC) continue;

        uint32_t device_id = virt_base[VIRTIO_MMIO_REG_DEVICE_ID / 4];
        if (device_id == 0) continue;

        uint32_t version = virt_base[VIRTIO_MMIO_REG_VERSION / 4];
        uint32_t vendor_id = virt_base[VIRTIO_MMIO_REG_VENDOR_ID / 4];

        kprintf("[VMMIO] Discovered slot %zu at phys:%p [DevID %u Ver %u Vendor %04x]\n",
                i, (void *)phys, device_id, version, vendor_id);
                
        struct virtio_mmio_transport *t = kmalloc(sizeof(struct virtio_mmio_transport));
        memset(t, 0, sizeof(struct virtio_mmio_transport));
        
        t->mmio_base = virt_base;
        t->trans.ops = &vmmio_ops;
        t->trans.priv_data = t;

        vmmio_set_status(&t->trans, 0); // Reset
        vmmio_set_status(&t->trans, 1); // Acknowledge
        vmmio_set_status(&t->trans, 2); // Driver
        
        if (device_id == 1 || device_id == 2) { // 1 = network, 2 = block in legacy. Block is 2 in modern, 1 in mmio?
            // Actually, in virtio_blk.c the MMIO path checked `virtio_mmio_find_device(1, ...)`, so device_id 1 is block in this older MMIO layout.
            if (device_id == 1) {
                virtio_blk_probe(&t->trans);
            }
        }
    }
}
