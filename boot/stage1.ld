/* NOVA OS stage 1 — the MBR is loaded at 0x7C00. */
OUTPUT_FORMAT(binary)
ENTRY(_start)
SECTIONS
{
    . = 0x7C00;
    .text : { *(.text) *(.rodata*) }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
