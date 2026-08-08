#ifndef PCI_H
#define PCI_H

#include "stdc.h"

#define PCI_REG_VENDOR_ID   0x00
#define PCI_REG_DEVICE_ID   0x02
#define PCI_REG_COMMAND     0x04
#define PCI_REG_STATUS      0x06
#define PCI_REG_REVISION    0x08
#define PCI_REG_PROG_IF     0x09
#define PCI_REG_SUBCLASS    0x0A
#define PCI_REG_CLASS       0x0B
#define PCI_REG_HEADER_TYPE 0x0E
#define PCI_REG_BAR0        0x10
#define PCI_REG_BAR1        0x14
#define PCI_REG_BAR2        0x18
#define PCI_REG_BAR3        0x1C
#define PCI_REG_BAR4        0x20
#define PCI_REG_BAR5        0x24
#define PCI_REG_CAP_PTR     0x34
#define PCI_REG_INTERRUPT   0x3C

#define PCI_CMD_IO_SPACE     (1 << 0)
#define PCI_CMD_MEM_SPACE    (1 << 1)
#define PCI_CMD_BUS_MASTER   (1 << 2)

struct pci_device {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;
    uint32_t bar[6];
};

typedef struct pci_device pci_device_t;

typedef void (*pci_probe_func)(struct pci_device *);

struct pci_driver {
    uint16_t vendor_id;
    uint16_t device_id;
    pci_probe_func probe;
};

void pci_init(void);

void pci_register_driver(struct pci_driver *driver);
void pci_scan(void (*callback)(struct pci_device *dev));

uint32_t pci_ecam_read32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
void pci_ecam_write32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t val);

uint16_t pci_ecam_read16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
void pci_ecam_write16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint16_t val);

uint8_t pci_ecam_read8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device *out_dev);
bool pci_find_capability(struct pci_device *dev, uint8_t cap_id, uint8_t cfg_type, uint8_t *out_bar, uint32_t *out_offset);
uint8_t pci_find_capability_offset(struct pci_device *dev, uint8_t cap_id, uint8_t cfg_type);
uint64_t pci_get_bar_phys(struct pci_device *dev, uint8_t bar_idx);
void pci_enable_bus_master(struct pci_device *dev);

#endif // PCI_H
