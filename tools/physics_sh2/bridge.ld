/* Flat image for the SH-2 instruction model: everything in high work RAM. MIT. */
ENTRY(_vd_bridge_init)
SECTIONS
{
    . = 0x06004000;
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.stack) *(.comment) }
}
