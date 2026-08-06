#include "drivers/bus/virtio_mmio.h"
#include "core/kprintf.h"
#include "mm/pmm.h"

#define VIRTIO_MMIO_BASE_PHYS 0x0A000000ULL
#define VIRTIO_MMIO_SLOT_SIZE 0x200ULL
#define VIRTIO_MMIO_SLOT_COUNT 32

static struct virtio_mmio_slot discovered_slots[VIRTIO_MMIO_SLOT_COUNT];
static size_t slot_count = 0;

void virtio_mmio_init(void) {
    slot_count = 0;
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

        discovered_slots[slot_count].phys_base = phys;
        discovered_slots[slot_count].version = version;
        discovered_slots[slot_count].device_id = device_id;
        discovered_slots[slot_count].vendor_id = vendor_id;
        slot_count++;

        kprintf("[VMMIO] Discovered slot %zu at phys:%p [DevID %u Ver %u Vendor %04x]\n",
                i, (void *)phys, device_id, version, vendor_id);
    }
}

bool virtio_mmio_find_device(uint32_t device_id, struct virtio_mmio_slot *out_slot) {
    for (size_t i = 0; i < slot_count; i++) {
        if (discovered_slots[i].device_id == device_id) {
            if (out_slot) {
                *out_slot = discovered_slots[i];
            }
            return true;
        }
    }
    return false;
}
