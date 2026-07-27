ENTRY(_start)

SECTIONS
{
    /* QEMU RISC-V Virt board RAM starts at 0x80000000 */
    . = 0x80000000;

    .text : {
        *(.text._start)
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
    }

    .bss : {
        *(.bss .bss.*)
        *(.sbss .sbss.*)
    }

    . = ALIGN(16);
    . += 0x1000;
    _stack_top = .;
}
