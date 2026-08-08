#include "drivers/bus/pci.h"

#include "core/kprintf.h"
#include "mm/pmm.h"
#include "drivers/acpi/acpi.h"

static uint64_t ecam_base = 0;

#define MAX_PCI_DRIVERS 16
static struct pci_driver *pci_drivers[MAX_PCI_DRIVERS];
static size_t num_pci_drivers = 0;

void pci_register_driver(struct pci_driver *driver) {
    if (num_pci_drivers < MAX_PCI_DRIVERS) {
        pci_drivers[num_pci_drivers++] = driver;
    }
}

static inline volatile uint32_t *pci_ecam_ptr(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset) {
    if (!ecam_base) return NULL;
    uint64_t phys = ecam_base + (((uint64_t)bus) << 20) + (((uint64_t)dev) << 15) + (((uint64_t)func) << 12) + (offset & 0xFFFC);
    return (volatile uint32_t *)pmm_phys_to_virt(phys);
}

uint32_t pci_ecam_read32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset) {
    volatile uint32_t *ptr = pci_ecam_ptr(bus, dev, func, offset);
    if (!ptr) return 0xFFFFFFFF;
    return *ptr;
}

void pci_ecam_write32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t val) {
    volatile uint32_t *ptr = pci_ecam_ptr(bus, dev, func, offset);
    if (ptr) {
        *ptr = val;
    }
}

uint16_t pci_ecam_read16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset) {
    uint32_t val = pci_ecam_read32(bus, dev, func, offset);
    return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

void pci_ecam_write16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint16_t val) {
    uint32_t orig = pci_ecam_read32(bus, dev, func, offset);
    uint32_t shift = (offset & 2) * 8;
    uint32_t mask = ~(0xFFFF << shift);
    uint32_t new_val = (orig & mask) | (((uint32_t)val) << shift);
    pci_ecam_write32(bus, dev, func, offset, new_val);
}

uint8_t pci_ecam_read8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset) {
    uint32_t val = pci_ecam_read32(bus, dev, func, offset);
    return (uint8_t)((val >> ((offset & 3) * 8)) & 0xFF);
}

void pci_scan(void (*callback)(struct pci_device *dev)) {
    if (!ecam_base) return;

    for (uint16_t b = 0; b < 256; b++) {
        for (uint8_t d = 0; d < 32; d++) {
            for (uint8_t f = 0; f < 8; f++) {
                uint32_t vd = pci_ecam_read32((uint8_t)b, d, f, PCI_REG_VENDOR_ID);
                uint16_t vendor = vd & 0xFFFF;
                uint16_t device = (vd >> 16) & 0xFFFF;

                if (vendor == 0xFFFF || vendor == 0x0000) continue;

                if (callback) {
                    struct pci_device dev;
                    dev.bus = (uint8_t)b;
                    dev.dev = d;
                    dev.func = f;
                    dev.vendor_id = vendor;
                    dev.device_id = device;
                    dev.class_code = pci_ecam_read8((uint8_t)b, d, f, PCI_REG_CLASS);
                    dev.subclass = pci_ecam_read8((uint8_t)b, d, f, PCI_REG_SUBCLASS);
                    dev.prog_if = pci_ecam_read8((uint8_t)b, d, f, PCI_REG_PROG_IF);
                    dev.header_type = pci_ecam_read8((uint8_t)b, d, f, PCI_REG_HEADER_TYPE);

                    for (int i = 0; i < 6; i++) {
                        dev.bar[i] = pci_ecam_read32((uint8_t)b, d, f, PCI_REG_BAR0 + (i * 4));
                    }
                    callback(&dev);
                }

                if (f == 0) {
                    uint8_t header_type = pci_ecam_read8((uint8_t)b, d, f, PCI_REG_HEADER_TYPE);
                    if ((header_type & 0x80) == 0) {
                        break;
                    }
                }
            }
        }
    }
}

static struct pci_device *target_dev_ptr;
static uint16_t target_vendor;
static uint16_t target_device;
static bool target_found;

static void find_device_cb(struct pci_device *dev) {
    if (target_found) return;
    if (dev->vendor_id == target_vendor && dev->device_id == target_device) {
        if (target_dev_ptr) *target_dev_ptr = *dev;
        target_found = true;
    }
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device *out_dev) {
    target_vendor = vendor_id;
    target_device = device_id;
    target_dev_ptr = out_dev;
    target_found = false;
    pci_scan(find_device_cb);
    return target_found;
}

static void init_scan_cb(struct pci_device *dev) {
    kprintf("[PCI  ] Discovered device %02x:%02x.%x [Vendor %04x Device %04x Class %02x Sub %02x]\n",
            dev->bus, dev->dev, dev->func, dev->vendor_id, dev->device_id, dev->class_code, dev->subclass);

    for (size_t i = 0; i < num_pci_drivers; i++) {
        if (pci_drivers[i]->vendor_id == dev->vendor_id && pci_drivers[i]->device_id == dev->device_id) {
            pci_drivers[i]->probe(dev);
        }
    }
}

void pci_init(void) {
    uint64_t mcfg_phys = acpi_find_table("MCFG");
    if (mcfg_phys) {
        acpi_mcfg_t *mcfg = (acpi_mcfg_t *)pmm_phys_to_virt(mcfg_phys);
        size_t num_mcfg_entries = (mcfg->header.length - sizeof(acpi_header_t) - 8) / sizeof(acpi_mcfg_entry_t);
        if (num_mcfg_entries > 0) {
            ecam_base = mcfg->entries[0].base_address;
        }
    }

    if (!ecam_base) {
        kprintf("[PCI  ] Warning: Initialized PCI with null ECAM base\n");
        return;
    }

    kprintf("[PCI  ] Initialized PCIe ECAM MMIO bus controller at phys:%p\n", (void *)ecam_base);
    pci_scan(init_scan_cb);
}

void pci_enable_bus_master(struct pci_device *dev) {
    if (!dev) return;
    uint16_t cmd = pci_ecam_read16(dev->bus, dev->dev, dev->func, PCI_REG_COMMAND);
    cmd |= (PCI_CMD_BUS_MASTER | PCI_CMD_MEM_SPACE);
    pci_ecam_write16(dev->bus, dev->dev, dev->func, PCI_REG_COMMAND, cmd);
}

bool pci_find_capability(struct pci_device *dev, uint8_t cap_id, uint8_t cfg_type, uint8_t *out_bar, uint32_t *out_offset) {
    uint8_t cap_ptr = pci_find_capability_offset(dev, cap_id, cfg_type);
    if (!cap_ptr) return false;

    if (out_bar) *out_bar = pci_ecam_read8(dev->bus, dev->dev, dev->func, cap_ptr + 4);
    if (out_offset) *out_offset = pci_ecam_read32(dev->bus, dev->dev, dev->func, cap_ptr + 8);
    return true;
}

uint8_t pci_find_capability_offset(struct pci_device *dev, uint8_t cap_id, uint8_t cfg_type) {
    if (!dev || !ecam_base) return 0;

    uint16_t status = pci_ecam_read16(dev->bus, dev->dev, dev->func, PCI_REG_STATUS);
    if (!(status & (1 << 4))) return 0;

    uint8_t cap_ptr = pci_ecam_read8(dev->bus, dev->dev, dev->func, PCI_REG_CAP_PTR);
    while (cap_ptr != 0 && cap_ptr != 0xFF) {
        uint8_t id = pci_ecam_read8(dev->bus, dev->dev, dev->func, cap_ptr);
        if (id == cap_id) {
            uint8_t type = pci_ecam_read8(dev->bus, dev->dev, dev->func, cap_ptr + 3);
            if (cfg_type == 0 || type == cfg_type) {
                return cap_ptr;
            }
        }
        cap_ptr = pci_ecam_read8(dev->bus, dev->dev, dev->func, cap_ptr + 1);
    }

    return 0;
}

uint64_t pci_get_bar_phys(struct pci_device *dev, uint8_t bar_idx) {
    if (!dev || bar_idx >= 6) return 0;
    uint32_t bar_low = dev->bar[bar_idx];
    if (bar_low & 1) {
        return bar_low & ~0x3ULL;
    }
    uint64_t phys = bar_low & ~0xFULL;
    if ((bar_low & 0x6) == 0x4 && bar_idx < 5) {
        phys |= ((uint64_t)dev->bar[bar_idx + 1]) << 32;
    }
    return phys;
}
