#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Standard ACPI Table Header
typedef struct acpi_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_header_t;

// ACPI Root System Description Pointer (RSDP) v2.0
typedef struct acpi_rsdp {
    char signature[8];        // "RSD PTR "
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;    // Physical address of RSDT (32-bit)
    uint32_t length;
    uint64_t xsdt_address;    // Physical address of XSDT (64-bit)
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

// MCFG Table Entry for PCIe ECAM
typedef struct acpi_mcfg_entry {
    uint64_t base_address;    // Base physical address of ECAM space
    uint16_t segment_group;   // PCI segment group number
    uint8_t start_bus;        // Start PCI bus number
    uint8_t end_bus;          // End PCI bus number
    uint32_t reserved;
} __attribute__((packed)) acpi_mcfg_entry_t;

// MCFG Table Header & Entries
typedef struct acpi_mcfg {
    acpi_header_t header;
    uint64_t reserved;
    acpi_mcfg_entry_t entries[];
} __attribute__((packed)) acpi_mcfg_t;

// Initialize ACPI subsystem with virtual RSDP pointer provided by Limine
int acpi_init(void *rsdp_virt);

// Get discovered PCIe ECAM base physical address (returns 0 if not found)
uint64_t acpi_get_mcfg_ecam_base(void);

#endif // ACPI_H
