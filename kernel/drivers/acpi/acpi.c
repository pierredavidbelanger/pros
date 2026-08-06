#include "drivers/acpi.h"
#include "arch/arch.h"
#include "core/kprintf.h"
#include "core/memory.h"
#include "mm/pmm.h"

static uint64_t mcfg_ecam_base = 0;

static bool has_fadt = false;
static uint32_t pm1a_cnt_blk = 0;
static uint32_t pm1b_cnt_blk = 0;
static acpi_gas_t x_pm1a_cnt_blk = {0};
static acpi_gas_t x_pm1b_cnt_blk = {0};

int acpi_init(void *rsdp_virt) {
    if (!rsdp_virt) {
        kprintf("[ACPI ] Warning: No RSDP address provided\n");
        return -1;
    }

    acpi_rsdp_t *rsdp = (acpi_rsdp_t *)rsdp_virt;

    if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0) {
        kprintf("[ACPI ] Error: Invalid RSDP signature\n");
        return -1;
    }

    kprintf("[ACPI ] Found RSDP (OEM: %.6s, Rev: %u)\n", rsdp->oem_id, rsdp->revision);

    uint64_t xsdt_phys = (rsdp->revision >= 2 && rsdp->xsdt_address != 0)
                             ? rsdp->xsdt_address
                             : (uint64_t)rsdp->rsdt_address;

    if (!xsdt_phys) {
        kprintf("[ACPI ] Error: Neither XSDT nor RSDT address found\n");
        return -1;
    }

    acpi_header_t *xsdt = pmm_phys_to_virt(xsdt_phys);
    bool is_xsdt = (memcmp(xsdt->signature, "XSDT", 4) == 0);

    size_t entry_size = is_xsdt ? 8 : 4;
    size_t num_entries = (xsdt->length - sizeof(acpi_header_t)) / entry_size;

    kprintf("[ACPI ] Scanning %s (%zu table pointers)...\n", is_xsdt ? "XSDT" : "RSDT", num_entries);

    uint8_t *table_ptrs = (uint8_t *)xsdt + sizeof(acpi_header_t);

    for (size_t i = 0; i < num_entries; i++) {
        uint64_t table_phys = 0;
        if (is_xsdt) {
            table_phys = *(uint64_t *)(table_ptrs + i * 8);
        } else {
            table_phys = *(uint32_t *)(table_ptrs + i * 4);
        }

        if (!table_phys) continue;

        acpi_header_t *header = pmm_phys_to_virt(table_phys);
        kprintf("[ACPI ]   Table [%.4s] at phys:%p (len: %u B)\n",
                header->signature, (void *)table_phys, header->length);

        if (memcmp(header->signature, "MCFG", 4) == 0) {
            acpi_mcfg_t *mcfg = (acpi_mcfg_t *)header;
            size_t num_mcfg_entries = (mcfg->header.length - sizeof(acpi_header_t) - 8) / sizeof(acpi_mcfg_entry_t);
            if (num_mcfg_entries > 0) {
                mcfg_ecam_base = mcfg->entries[0].base_address;
                kprintf("[ACPI ]     Discovered PCIe ECAM base at phys:%p (Buses %u..%u)\n",
                        (void *)mcfg_ecam_base,
                        mcfg->entries[0].start_bus,
                        mcfg->entries[0].end_bus);
            }
        } else if (memcmp(header->signature, "FACP", 4) == 0 || memcmp(header->signature, "FADT", 4) == 0) {
            acpi_fadt_t *fadt = (acpi_fadt_t *)header;
            has_fadt = true;
            pm1a_cnt_blk = fadt->pm1a_cnt_blk;
            pm1b_cnt_blk = fadt->pm1b_cnt_blk;

            if (fadt->header.length >= offsetof(acpi_fadt_t, x_pm1a_cnt_blk) + sizeof(acpi_gas_t)) {
                x_pm1a_cnt_blk = fadt->x_pm1a_cnt_blk;
                x_pm1b_cnt_blk = fadt->x_pm1b_cnt_blk;
            }
            kprintf("[ACPI ]     Discovered FADT table (PM1a_CNT_BLK port: 0x%X, GAS address: 0x%llX, space: %u)\n",
                    pm1a_cnt_blk, (unsigned long long)x_pm1a_cnt_blk.address, x_pm1a_cnt_blk.address_space_id);
        }
    }

    if (mcfg_ecam_base == 0) {
        kprintf("[ACPI ] Warning: MCFG table not found\n");
    }

    return 0;
}

uint64_t acpi_get_mcfg_ecam_base(void) {
    return mcfg_ecam_base;
}

int acpi_shutdown(void) {
    if (!has_fadt) {
        return -1;
    }

    // Value for ACPI S5 Soft-Off (SLP_TYPa = 0 in QEMU/Bochs | SLP_EN = 1 << 13)
    uint16_t slp_val = (0 << 10) | (1 << 13);

    // 1. Try legacy 32-bit PM1a_CNT_BLK I/O port first (Standard on all x86_64 PCs & QEMU)
    if (pm1a_cnt_blk != 0) {
        arch_outw((uint16_t)pm1a_cnt_blk, slp_val);
        if (pm1b_cnt_blk != 0) {
            arch_outw((uint16_t)pm1b_cnt_blk, slp_val);
        }
        return 0;
    }

    // 2. Try 64-bit GAS structure if System I/O port space
    if (x_pm1a_cnt_blk.address != 0 && x_pm1a_cnt_blk.address_space_id == 1) {
        arch_outw((uint16_t)x_pm1a_cnt_blk.address, slp_val);
        if (x_pm1b_cnt_blk.address != 0) {
            arch_outw((uint16_t)x_pm1b_cnt_blk.address, slp_val);
        }
        return 0;
    }

    return -1;
}
