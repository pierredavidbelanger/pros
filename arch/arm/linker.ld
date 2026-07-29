ENTRY(_start)

SECTIONS
{
    /* QEMU ARM Virt board RAM starts at 0x40000000 */
    . = 0x40000000;

    .text : {
        *(.text._start)
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        *(.bss .bss.*)
    }

    . = ALIGN(8);
    _stack_bottom = .;
    . += 0x1000;
    _stack_top = .;

    . = ALIGN(4096);
    _kernel_end = .;
}
