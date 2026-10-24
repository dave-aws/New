/* NOVA OS stage 2 — assembled to run at linear 0x10000 via segment 0x1000,
 * so all internal offsets are relative to 0. */
OUTPUT_FORMAT(binary)
ENTRY(_start)
SECTIONS
{
    . = 0x8000;
    .text : { *(.text) *(.rodata*) }
    .data : { *(.data) }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
