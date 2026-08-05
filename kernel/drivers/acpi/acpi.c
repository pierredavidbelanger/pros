#include "drivers/acpi.h"
#include "core/kprintf.h"
#include "core/memory.h"
#include "mm/pmm.h"

static uint64_t mcfg_ecam_base = 0;

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
