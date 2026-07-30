#include "arch.h"

#include "kernel.h"
#include "mem.h"

#include <stdint.h>

/* QEMU RISC-V Virt board maps 16550A UART to 0x10000000 */
#define UART_BASE 0x10000000
#define UART_THR  ((volatile unsigned char*)(UART_BASE + 0))
#define UART_LSR  ((volatile unsigned char*)(UART_BASE + 5))

uintptr_t uart_addr() {
    return UART_BASE;
}

void uart_putchar(const char c) {
    while ((*UART_LSR & (1 << 5)) == 0);
    *UART_THR = c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putchar(*s++);
    }
}

#define QEMU_RISCV_POWEROFF_ADDR 0x100000
#define QEMU_RISCV_PASS          0x5555
#define QEMU_RISCV_FAIL          0x3333

void shutdown(void) {
    // Write 0x5555 to 0x100000 to cleanly exit QEMU with code 0
    *(volatile uint32_t *) QEMU_RISCV_POWEROFF_ADDR = QEMU_RISCV_PASS;
}

struct trap_frame {
    uint32_t regs[31]; // x1 (ra) through x31 (t6)
    uint32_t mepc;
    uint32_t mcause;
    uint32_t mstatus;
};

void trap_handler(struct trap_frame *frame) {
    uint32_t cause = frame->mcause;
    uint32_t epc = frame->mepc;

    kprintf("=== TRAP RECEIVED ===\n");
    kprintf("mcause: 0x%x, mepc: 0x%x\n", cause, epc);

    // If caused by an ecall (environment call / syscall), advance mepc past ecall instruction (4 bytes)
    if (cause == 8 || cause == 11) {
        // Environment call from U-mode or M-mode
        frame->mepc += 4;
    } else {
        kpanic("Unhandled Trap!");
    }
}

#define PTE_V (1 << 0)
#define PTE_R (1 << 1)
#define PTE_W (1 << 2)
#define PTE_X (1 << 3)
#define PTE_U (1 << 4)
#define PTE_G (1 << 5)
#define PTE_A (1 << 6)
#define PTE_D (1 << 7)
// Construct Page Table Entry from physical address & flags
#define MAKE_PTE(paddr, flags) ((((uintptr_t)(paddr) >> 12) << 10) | (flags))
// Extract physical address from Page Table Entry
#define PTE_TO_PADDR(pte) (((uintptr_t)(pte) >> 10) << 12)

uintptr_t *vmm_create_root_table(void) {
    return kpmalloc(); // Allocate 4KB page for Root L1 Table
}

// Page Table
//  31          22 21          12 11           0
// +--------------+--------------+--------------+
// | VPN[1] (10b) | VPN[0] (10b) | Offset (12b) |
// +--------------+--------------+--------------+
//    Level 1        Level 0       Byte Offset
//  Table Index    Table Index    (in 4KB Page)
//
// Page Table Entry
//  31        20 19        10 9 8   7 6 5 4 3 2 1 0
// +------------+------------+---+---+-+-+-+-+-+-+-+
// |   PPN[1]   |   PPN[0]   |RSW| D |A|G|U|X|W|R|V|
// +------------+------------+---+---+-+-+-+-+-+-+-+
//    12 bits      10 bits    2b  1b 1b1b1b1b1b1b1b

void vmm_map_page(uintptr_t *root_pt, uintptr_t vaddr, uintptr_t paddr, uint8_t r, uint8_t w, uint8_t x) {
    uintptr_t vpn1 = (vaddr >> 22) & 0x3FF;
    uintptr_t vpn0 = (vaddr >> 12) & 0x3FF;
    // Check Level 1 entry
    if (!(root_pt[vpn1] & PTE_V)) {
        // Allocate a new Level 0 table page
        void *l0_table = kpmalloc();
        // Point L1 entry to new L0 table (R=0, W=0, X=0, V=1 makes it a pointer to next level)
        root_pt[vpn1] = MAKE_PTE(l0_table, PTE_V);
    }
    // Get pointer to L0 table
    uintptr_t *l0_table = (uintptr_t *) PTE_TO_PADDR(root_pt[vpn1]);
    // Combine caller's R/W/X permissions in `flags` with Valid, Accessed, and Dirty status
    uintptr_t flags = PTE_V | PTE_A | PTE_D;
    if (r) flags |= PTE_R;
    if (w) flags |= PTE_W;
    if (x) flags |= PTE_X;
    l0_table[vpn0] = MAKE_PTE(paddr, flags);
}

void smain(void) {
    kprintf("Hello from RISC-V S-mode (Supervisor Mode)!\n");
    // Test ecall in S-mode
    asm volatile("ecall");
    kprintf("Returned from S-mode ecall!\n");
    while (1) {
        arch_idle();
    }
}

void vmm_enable(uintptr_t *root_pt) {
    // wow, ok, since the root_pt is 4kb aligned, the lower 12 bit are always 0,
    // satp expect we truncate those, then when needed it add the 0s and recreate the right address
    uintptr_t pfn = ((uintptr_t) root_pt) >> 12;
    uintptr_t satp_val = (1u << 31) | pfn; // Bit 31 = MODE 1 (Sv32)
    asm volatile(
        "sfence.vma\n\t" // Flush TLB before switching
        "csrw satp, %0\n\t" // Enable Sv32 Paging
        "sfence.vma\n\t" // Flush TLB after switching
        :
        : "r"(satp_val)
        : "memory"
    );

    // Allow S-mode / U-mode to access all physical addresses (0x00000000 to 0xFFFFFFFF)
    asm volatile("csrw pmpaddr0, %0" : : "r"(0xFFFFFFFFu));
    asm volatile("csrw pmpcfg0, %0" : : "r"(0x0F)); // 0x0F = TOR (Top of Range) | R | W | X

    // Delegate all exceptions and interrupts to Supervisor Mode
    asm volatile("csrw medeleg, %0" : : "r"(0xFFFF));
    asm volatile("csrw mideleg, %0" : : "r"(0xFFFF));

    uint32_t mstatus;
    asm volatile("csrr %0, mstatus" : "=r"(mstatus));
    mstatus &= ~(3u << 11); // Clear MPP
    mstatus |= (1u << 11);  // Set MPP = 1 (Supervisor Mode)
    asm volatile("csrw mstatus, %0" : : "r"(mstatus));

    // Drop to S-mode and continue right here!
    asm volatile(
        "la t0, vmm_enable_end\n\t"
        "csrw mepc, t0\n\t" // mepc = address of 'vmm_enable_end'
        "mret\n\t" // Drop to S-mode & jump just here below
        "vmm_enable_end:\n\t" // Execution resumes HERE in S-mode!
        :
        :
        : "t0", "memory"
    );
}
