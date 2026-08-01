ENTRY(_start)

SECTIONS
{
    /* QEMU RISC-V Virt board RAM starts at 0x80000000 */
    . = 0x80000000;
    _kernel_rx_start = .;

    .text : {
        *(.text._start)
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    _kernel_rx_end = .;

    . = ALIGN(4096);
    _kernel_rw_start = .;

    .data . : {
        *(.data .data.*)
        *(.sdata .sdata.*)
    }

    .bss : {
        *(.bss .bss.*)
        *(.sbss .sbss.*)
    }

    _kernel_rw_end = .;

    . = ALIGN(4096);
    _stack_bottom = .;
    . += 0x1000;
    _stack_top = .;

    . = ALIGN(4096);
    _heap_start = .;
}
