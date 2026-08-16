#include "lapic.h"

#include "core/kprintf.h"
#include "mm/pmm.h"
#include "mm/vmm.h"

#define LAPIC_BASE_ENABLE    (1ULL << 11)
#define LAPIC_BASE_ADDR_MASK 0xFFFFFFFFFFFFF000ULL

// Register offsets into the 4 KiB register file. Every register is 32 bits
// wide and 16-byte aligned; accesses must be 32 bit.
#define LAPIC_REG_SPIV 0x0F0 // Spurious Interrupt Vector
#define LAPIC_REG_LVT0 0x350 // Local Vector Table entry for the LINT0 pin
                             // (0x360 is LINT1, which is the NMI line)

// Bit 8 of SPIV is the *software* enable, independent of the hardware enable
// in IA32_APIC_BASE. Both must be on for the LVT entries to deliver anything.
#define LAPIC_SPIV_ENABLE (1u << 8)

// An LVT entry: bits 7:0 vector, bits 10:8 delivery mode, bit 16 mask.
// Delivery mode 0b111 is ExtINT. Note the vector field is *ignored* in ExtINT
// mode — the LAPIC runs a legacy interrupt-acknowledge cycle and takes the
// vector from the 8259, which is why PIC_VECTOR_BASE still decides where IRQ0
// lands. So 0x700 means: ExtINT, unmasked, and no vector of our own.
#define LAPIC_LVT_VIRTUAL_WIRE (0x7u << 8)

static volatile uint32_t *lapic_regs;

static uint32_t lapic_read(uint32_t reg) {
    return lapic_regs[reg / sizeof(uint32_t)];
}

static void lapic_write(uint32_t reg, uint32_t value) {
    lapic_regs[reg / sizeof(uint32_t)] = value;
}

// We do not use it as an interrupt controller yet
// we only need it to stop swallowing the PIC interrupts
void lapic_init(void) {
    uint64_t apic_base = rdmsr(MSR_IA32_APIC_BASE);
    if (!(apic_base & LAPIC_BASE_ENABLE)) {
        kpanic("lapic_init: the local APIC is hardware disabled");
    }

    uint64_t phys_addr = apic_base & LAPIC_BASE_ADDR_MASK;
    uint64_t virt_addr = pmm_get_hhdm_offset() + phys_addr;

    // The register is out of range of the HHDM MMIO and need to be mapped by hand
    if (vmm_map_page(vmm_kernel_context, virt_addr, phys_addr, VMM_WRITABLE | VMM_CACHE_DISABLE) != 0) {
        kpanic("lapic_init: failed to map the local APIC registers");
    }
    lapic_regs = (volatile uint32_t *)virt_addr;

    if (!(lapic_read(LAPIC_REG_SPIV) & LAPIC_SPIV_ENABLE)) {
        kpanic("lapic_init: the local APIC is software disabled");
    }

    // Virtual wire mode.
    lapic_write(LAPIC_REG_LVT0, LAPIC_LVT_VIRTUAL_WIRE);
}
