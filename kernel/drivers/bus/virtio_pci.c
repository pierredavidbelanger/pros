#include "drivers/bus/virtio_pci.h"
#include "drivers/virtio/virtio_transport.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "core/memory.h"
#include "drivers/block/virtio_blk.h"

struct virtio_pci_transport {
    struct virtio_transport trans;
    struct pci_device pci_dev;
    volatile struct virtio_pci_common_cfg *cfg;
    volatile uint32_t *notify_base;
    uint32_t notify_mult;
    volatile uint64_t *device_cfg;
};

static uint32_t vpci_get_features(struct virtio_transport *trans) {
    struct virtio_pci_transport *t = (struct virtio_pci_transport *)trans->priv_data;
    t->cfg->device_feature_select = 0;
    return t->cfg->device_feature;
}

static void vpci_set_features(struct virtio_transport *trans, uint32_t features) {
    struct virtio_pci_transport *t = (struct virtio_pci_transport *)trans->priv_data;
    t->cfg->driver_feature_select = 0;
    t->cfg->driver_feature = features;
    t->cfg->driver_feature_select = 1;
    t->cfg->driver_feature = 1; // VIRTIO_F_VERSION_1
}

static uint8_t vpci_get_status(struct virtio_transport *trans) {
    struct virtio_pci_transport *t = (struct virtio_pci_transport *)trans->priv_data;
    return t->cfg->device_status;
}

static void vpci_set_status(struct virtio_transport *trans, uint8_t status) {
    struct virtio_pci_transport *t = (struct virtio_pci_transport *)trans->priv_data;
    t->cfg->device_status |= status;
}

static void vpci_read_config(struct virtio_transport *trans, uint32_t offset, uint8_t size, void *buf) {
    struct virtio_pci_transport *t = (struct virtio_pci_transport *)trans->priv_data;
    if (t->device_cfg) {
        if (size == 8) {
            *(uint64_t *)buf = *(volatile uint64_t *)((uint8_t *)t->device_cfg + offset);
        } else if (size == 4) {
            *(uint32_t *)buf = *(volatile uint32_t *)((uint8_t *)t->device_cfg + offset);
        }
    }
}

static int vpci_setup_queue(struct virtio_transport *trans, uint16_t queue_index, struct virtq *vq) {
    struct virtio_pci_transport *t = (struct virtio_pci_transport *)trans->priv_data;
    t->cfg->queue_select = queue_index;
    uint16_t qsize = t->cfg->queue_size;
    if (qsize == 0) qsize = 128;
    
    if (virtq_init(vq, qsize) != 0) return -1;
    
    volatile uint32_t *q_desc_ptr = (volatile uint32_t *)&t->cfg->queue_desc;
    q_desc_ptr[0] = (uint32_t)(vq->desc_phys & 0xFFFFFFFFULL);
    q_desc_ptr[1] = (uint32_t)(vq->desc_phys >> 32);

    volatile uint32_t *q_driver_ptr = (volatile uint32_t *)&t->cfg->queue_driver;
    q_driver_ptr[0] = (uint32_t)(vq->avail_phys & 0xFFFFFFFFULL);
    q_driver_ptr[1] = (uint32_t)(vq->avail_phys >> 32);

    volatile uint32_t *q_device_ptr = (volatile uint32_t *)&t->cfg->queue_device;
    q_device_ptr[0] = (uint32_t)(vq->used_phys & 0xFFFFFFFFULL);
    q_device_ptr[1] = (uint32_t)(vq->used_phys >> 32);

    t->cfg->queue_enable = 1;
    return 0;
}

static void vpci_notify_queue(struct virtio_transport *trans, uint16_t queue_index) {
    struct virtio_pci_transport *t = (struct virtio_pci_transport *)trans->priv_data;
    if (t->notify_base) {
        t->cfg->queue_select = queue_index;
        uint16_t notify_off = t->cfg->queue_notify_off;
        volatile uint16_t *notify_reg = (volatile uint16_t *)((uint8_t *)t->notify_base + (notify_off * t->notify_mult));
        *notify_reg = queue_index;
    }
}

static struct virtio_transport_ops vpci_ops = {
    .get_features = vpci_get_features,
    .set_features = vpci_set_features,
    .get_status = vpci_get_status,
    .set_status = vpci_set_status,
    .read_device_config = vpci_read_config,
    .setup_queue = vpci_setup_queue,
    .notify_queue = vpci_notify_queue,
};

static void virtio_pci_probe(struct pci_device *pci_dev) {
    pci_enable_bus_master(pci_dev);
    
    uint8_t common_bar_idx = 0;
    uint32_t common_offset = 0;
    
    if (pci_find_capability(pci_dev, 0x09, VIRTIO_PCI_CAP_COMMON_CFG, &common_bar_idx, &common_offset)) {
        uint64_t phys_base = pci_get_bar_phys(pci_dev, common_bar_idx);
        
        struct virtio_pci_transport *t = kmalloc(sizeof(struct virtio_pci_transport));
        memset(t, 0, sizeof(struct virtio_pci_transport));
        
        t->pci_dev = *pci_dev;
        t->cfg = (volatile struct virtio_pci_common_cfg *)pmm_phys_to_virt(phys_base + common_offset);
        
        uint8_t notify_cap_ptr = pci_find_capability_offset(pci_dev, 0x09, VIRTIO_PCI_CAP_NOTIFY_CFG);
        if (notify_cap_ptr) {
            uint8_t notify_bar_idx = pci_ecam_read8(pci_dev->bus, pci_dev->dev, pci_dev->func, notify_cap_ptr + 4);
            uint32_t notify_offset = pci_ecam_read32(pci_dev->bus, pci_dev->dev, pci_dev->func, notify_cap_ptr + 8);
            t->notify_mult = pci_ecam_read32(pci_dev->bus, pci_dev->dev, pci_dev->func, notify_cap_ptr + 16);
            t->notify_base = (volatile uint32_t *)pmm_phys_to_virt(pci_get_bar_phys(pci_dev, notify_bar_idx) + notify_offset);
        }
        
        uint8_t dev_bar_idx = 0;
        uint32_t dev_offset = 0;
        if (pci_find_capability(pci_dev, 0x09, VIRTIO_PCI_CAP_DEVICE_CFG, &dev_bar_idx, &dev_offset)) {
            t->device_cfg = (volatile uint64_t *)pmm_phys_to_virt(pci_get_bar_phys(pci_dev, dev_bar_idx) + dev_offset);
        }
        
        t->trans.ops = &vpci_ops;
        t->trans.priv_data = t;
        
        vpci_set_status(&t->trans, 0); // Reset
        vpci_set_status(&t->trans, 1); // Acknowledge
        vpci_set_status(&t->trans, 2); // Driver
        
        if (pci_dev->device_id == 0x1042) {
            virtio_blk_probe(&t->trans);
        }
    }
}

static struct pci_driver virtio_blk_pci_driver = {
    .vendor_id = 0x1AF4,
    .device_id = 0x1042,
    .probe = virtio_pci_probe
};

void virtio_pci_init(void) {
    pci_register_driver(&virtio_blk_pci_driver);
}
