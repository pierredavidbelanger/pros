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

// ACPI Generic Address Structure (GAS)
typedef struct acpi_gas {
    uint8_t address_space_id;     // 0 = System Memory (MMIO), 1 = System I/O (Port IO)
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t address;             // Physical memory address or I/O port
} __attribute__((packed)) acpi_gas_t;

// ACPI Fixed ACPI Description Table (FADT / "FACP")
typedef struct acpi_fadt {
    acpi_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  reserved1;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;        // 32-bit PM1a Control Block IO port
    uint32_t pm1b_cnt_blk;        // 32-bit PM1b Control Block IO port
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_blk_len;
    uint8_t  gpe1_blk_len;
    uint8_t  gpe1_base;
    uint8_t  cst_cnt;
    uint16_t c_lvl2_lat;
    uint16_t c_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alrm;
    uint8_t  mon_alrm;
    uint8_t  century;
    uint16_t iapc_boot_arch;
    uint8_t  reserved2;
    uint32_t flags;
    acpi_gas_t reset_reg;
    uint8_t  reset_val;
    uint8_t  arm_boot_arch;
    uint8_t  minor_version;
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    acpi_gas_t x_pm1a_evt_blk;
    acpi_gas_t x_pm1b_evt_blk;
    acpi_gas_t x_pm1a_cnt_blk;     // 64-bit GAS for PM1a Control Block
    acpi_gas_t x_pm1b_cnt_blk;     // 64-bit GAS for PM1b Control Block
} __attribute__((packed)) acpi_fadt_t;

// Initialize ACPI subsystem with virtual RSDP pointer provided by Limine
int acpi_init(void *rsdp_virt);

// Get discovered PCIe ECAM base physical address (returns 0 if not found)
uint64_t acpi_get_mcfg_ecam_base(void);

// Perform ACPI poweroff/shutdown using parsed FADT PM1a control block registers
int acpi_shutdown(void);

#endif // ACPI_H
