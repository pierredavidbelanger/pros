#include "drivers/block/virtio_blk.h"
#include "drivers/bus/pci.h"
#include "drivers/bus/virtio_mmio.h"
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

    if (dev->is_io_port) {
        arch_outw(dev->io_port + 0x10, 0);
    } else if (dev->notify_reg) {
        *(volatile uint16_t *)dev->notify_reg = 0;
    }

    while (dev->vq.last_used_idx == dev->vq.used->idx) {
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

int virtio_blk_init(void) {
    struct virtio_mmio_slot mmio_slot;
    struct pci_device pci_dev;

    uint64_t phys_base = 0;
    volatile uint32_t *mmio_base = NULL;
    volatile uint32_t *notify_reg = NULL;
    uint64_t total_capacity_sectors = 0;

    // Check Direct VirtIO MMIO Slot (e.g. ARM QEMU)
    if (virtio_mmio_find_device(1, &mmio_slot)) {
        phys_base = mmio_slot.phys_base;
        mmio_base = (volatile uint32_t *)pmm_phys_to_virt(phys_base);
        notify_reg = &mmio_base[VIRTIO_MMIO_REG_QUEUE_NOTIFY / 4];

        mmio_base[VIRTIO_MMIO_REG_STATUS / 4] = 0;
        mmio_base[VIRTIO_MMIO_REG_STATUS / 4] |= VIRTIO_MMIO_STATUS_ACKNOWLEDGE;
        mmio_base[VIRTIO_MMIO_REG_STATUS / 4] |= VIRTIO_MMIO_STATUS_DRIVER;

        mmio_base[VIRTIO_MMIO_REG_QUEUE_SEL / 4] = 0;
        uint32_t max_queue = mmio_base[VIRTIO_MMIO_REG_QUEUE_NUM_MAX / 4];
        if (max_queue == 0) return -1;

        uint16_t qsize = (max_queue > 128) ? 128 : (uint16_t)max_queue;

        struct virtio_blk_device *dev = (struct virtio_blk_device *)kmalloc(sizeof(struct virtio_blk_device));
        if (!dev) return -1;
        memset(dev, 0, sizeof(struct virtio_blk_device));

        if (virtq_init(&dev->vq, qsize) != 0) {
            kfree(dev);
            return -1;
        }

        mmio_base[VIRTIO_MMIO_REG_QUEUE_NUM / 4] = qsize;
        mmio_base[VIRTIO_MMIO_REG_QUEUE_PFN / 4] = (uint32_t)(dev->vq.desc_phys / 4096);
        mmio_base[VIRTIO_MMIO_REG_STATUS / 4] |= VIRTIO_MMIO_STATUS_DRIVER_OK;

        volatile uint64_t *cap_ptr = (volatile uint64_t *)((uint8_t *)mmio_base + 0x100);
        total_capacity_sectors = *cap_ptr;

        dev->is_io_port = false;
        dev->mmio_base = mmio_base;
        dev->notify_reg = notify_reg;

        dev->block_dev.block_size = 512;
        dev->block_dev.total_blocks = total_capacity_sectors ? total_capacity_sectors : 204800;
        dev->block_dev.priv_data = dev;
        dev->block_dev.read_blocks = virtio_blk_read_blocks;
        dev->block_dev.write_blocks = virtio_blk_write_blocks;
        strcpy(dev->block_dev.name, "hd0");

        g_blk_dev = dev;
        blockdev_register(&dev->block_dev);
        kprintf("[VBLK ] Initialized VirtIO-Block MMIO device 'hd0'\n");
        return 0;
    }

    // Check PCI Bus (Vendor 0x1AF4, Device 0x1042 for Modern 1.0 or 0x1001 for Legacy)
    if (pci_find_device(0x1AF4, 0x1042, &pci_dev) || pci_find_device(0x1AF4, 0x1001, &pci_dev)) {
        pci_enable_bus_master(&pci_dev);

        uint8_t common_bar_idx = 0;
        uint32_t common_offset = 0;

        // Try Modern VirtIO 1.0 PCI Capability (Vendor-specific cap ID 0x09, cfg_type 1)
        if (pci_find_capability(&pci_dev, 0x09, 1, &common_bar_idx, &common_offset)) {
            phys_base = pci_get_bar_phys(&pci_dev, common_bar_idx);

            volatile struct virtio_pci_common_cfg *cfg = 
                (volatile struct virtio_pci_common_cfg *)pmm_phys_to_virt(phys_base + common_offset);

            // Feature negotiation (VIRTIO_F_VERSION_1 bit 32 mandatory for VirtIO 1.0)
            cfg->device_feature_select = 0;
            uint32_t f0 = cfg->device_feature;
            cfg->device_feature_select = 1;
            uint32_t f1 = cfg->device_feature;

            cfg->driver_feature_select = 0;
            cfg->driver_feature = f0;
            cfg->driver_feature_select = 1;
            cfg->driver_feature = f1 | 1; // Set VIRTIO_F_VERSION_1

            cfg->device_status |= VIRTIO_MMIO_STATUS_FEATURES_OK;
            if (!(cfg->device_status & VIRTIO_MMIO_STATUS_FEATURES_OK)) {
                kprintf("[VBLK ] Error: VirtIO 1.0 device rejected FEATURES_OK\n");
                return -1;
            }

            cfg->queue_select = 0;
            uint16_t qsize = cfg->queue_size;
            if (qsize == 0) qsize = 128;

            struct virtio_blk_device *dev = (struct virtio_blk_device *)kmalloc(sizeof(struct virtio_blk_device));
            if (!dev) return -1;
            memset(dev, 0, sizeof(struct virtio_blk_device));

            if (virtq_init(&dev->vq, qsize) != 0) {
                kfree(dev);
                return -1;
            }

            volatile uint32_t *q_desc_ptr = (volatile uint32_t *)&cfg->queue_desc;
            q_desc_ptr[0] = (uint32_t)(dev->vq.desc_phys & 0xFFFFFFFFULL);
            q_desc_ptr[1] = (uint32_t)(dev->vq.desc_phys >> 32);

            volatile uint32_t *q_driver_ptr = (volatile uint32_t *)&cfg->queue_driver;
            q_driver_ptr[0] = (uint32_t)(dev->vq.avail_phys & 0xFFFFFFFFULL);
            q_driver_ptr[1] = (uint32_t)(dev->vq.avail_phys >> 32);

            volatile uint32_t *q_device_ptr = (volatile uint32_t *)&cfg->queue_device;
            q_device_ptr[0] = (uint32_t)(dev->vq.used_phys & 0xFFFFFFFFULL);
            q_device_ptr[1] = (uint32_t)(dev->vq.used_phys >> 32);

            cfg->queue_enable = 1;

            cfg->device_status |= VIRTIO_MMIO_STATUS_DRIVER_OK;

            // Locate Queue Notify capability (cfg_type = 2) and calculate offset
            uint8_t notify_cap_ptr = pci_find_capability_offset(&pci_dev, 0x09, 2);
            if (notify_cap_ptr) {
                uint8_t notify_bar_idx = pci_ecam_read8(pci_dev.bus, pci_dev.dev, pci_dev.func, notify_cap_ptr + 4);
                uint32_t notify_offset = pci_ecam_read32(pci_dev.bus, pci_dev.dev, pci_dev.func, notify_cap_ptr + 8);
                uint32_t notify_mult = pci_ecam_read32(pci_dev.bus, pci_dev.dev, pci_dev.func, notify_cap_ptr + 16);

                uint64_t notify_bar_phys = pci_get_bar_phys(&pci_dev, notify_bar_idx);
                uint64_t final_notify_phys = notify_bar_phys + notify_offset + ((uint64_t)cfg->queue_notify_off * notify_mult);
                dev->notify_reg = (volatile uint32_t *)pmm_phys_to_virt(final_notify_phys);
            }

            // Locate Device Config capability (cfg_type = 4) for disk sector capacity
            uint8_t device_cap_ptr = pci_find_capability_offset(&pci_dev, 0x09, 4);
            if (device_cap_ptr) {
                uint8_t dev_bar_idx = pci_ecam_read8(pci_dev.bus, pci_dev.dev, pci_dev.func, device_cap_ptr + 4);
                uint32_t dev_offset = pci_ecam_read32(pci_dev.bus, pci_dev.dev, pci_dev.func, device_cap_ptr + 8);

                uint64_t dev_bar_phys = pci_get_bar_phys(&pci_dev, dev_bar_idx);
                volatile uint64_t *cap_ptr = (volatile uint64_t *)pmm_phys_to_virt(dev_bar_phys + dev_offset);
                total_capacity_sectors = *cap_ptr;
            }

            dev->is_io_port = false;
            dev->block_dev.block_size = 512;
            dev->block_dev.total_blocks = (total_capacity_sectors && total_capacity_sectors < 0x100000000ULL) ? total_capacity_sectors : 204800;
            dev->block_dev.priv_data = dev;
            dev->block_dev.read_blocks = virtio_blk_read_blocks;
            dev->block_dev.write_blocks = virtio_blk_write_blocks;
            strcpy(dev->block_dev.name, "hd0");

            g_blk_dev = dev;
            blockdev_register(&dev->block_dev);
            kprintf("[VBLK ] Initialized Modern VirtIO 1.0 PCI device 'hd0'\n");
            return 0;
        }

        // Fallback to Legacy VirtIO PCI (Port I/O or direct BAR)
        uint32_t bar0 = pci_dev.bar[0];
        struct virtio_blk_device *dev = (struct virtio_blk_device *)kmalloc(sizeof(struct virtio_blk_device));
        if (!dev) return -1;
        memset(dev, 0, sizeof(struct virtio_blk_device));

        if (bar0 & 1) {
            dev->is_io_port = true;
            dev->io_port = (uint16_t)(bar0 & ~0x3ULL);

            arch_outb(dev->io_port + 0x12, 0);
            arch_outb(dev->io_port + 0x12, 1);
            arch_outb(dev->io_port + 0x12, 2);

            arch_outw(dev->io_port + 0x0E, 0);
            uint16_t qsize = arch_inw(dev->io_port + 0x0C);
            if (qsize == 0) qsize = 128;

            if (virtq_init(&dev->vq, qsize) != 0) {
                kfree(dev);
                return -1;
            }

            arch_outl(dev->io_port + 0x08, (uint32_t)(dev->vq.desc_phys / 4096));
            arch_outb(dev->io_port + 0x12, 4);

            uint32_t cap_lo = arch_inl(dev->io_port + 0x14);
            uint32_t cap_hi = arch_inl(dev->io_port + 0x18);
            total_capacity_sectors = ((uint64_t)cap_hi << 32) | cap_lo;
        }

        dev->block_dev.block_size = 512;
        dev->block_dev.total_blocks = total_capacity_sectors ? total_capacity_sectors : 204800;
        dev->block_dev.priv_data = dev;
        dev->block_dev.read_blocks = virtio_blk_read_blocks;
        dev->block_dev.write_blocks = virtio_blk_write_blocks;
        strcpy(dev->block_dev.name, "hd0");

        g_blk_dev = dev;
        blockdev_register(&dev->block_dev);
        kprintf("[VBLK ] Initialized VirtIO-Block PCI device 'hd0'\n");
        return 0;
    }

    return -1;
}
