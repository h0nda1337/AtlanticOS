/* SPDX-License-Identifier: MIT - QEMU translation probe W^X layout. */
ENTRY(_image_start)
SECTIONS {
    . = ATL_LINK_BASE;
    __image_start = .;
    __rx_start = .;
    .entry : { KEEP(*(.entry)) }
    .text : ALIGN(16) { *(.text .text.*) }
    .vectors : ALIGN(2048) { KEEP(*(.vectors .vectors.*)) }
    . = ALIGN(4096);
    __rx_end = .;
    __ro_start = .;
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(4096);
    __ro_end = .;
    __rw_start = .;
    .data : { *(.data .data.*) }
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    .stack (NOLOAD) : ALIGN(16) {
        __stack_bottom = .;
        . += 0x10000;
        __stack_top = .;
    }
    . = ALIGN(4096);
    __image_memory_end = .;
    /DISCARD/ : { *(.comment .note* .eh_frame*) }
}
ASSERT(_image_start == ATL_LINK_BASE, "unexpected QEMU Image placement")
ASSERT((__image_memory_end - __image_start < 0x200000), "preflight exceeds 2 MiB")
