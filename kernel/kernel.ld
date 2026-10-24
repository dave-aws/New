/* NOVA kernel image layout.
 *
 * The kernel runs in the higher half at 0xFFFFFFFF80100000 but is loaded at
 * physical 1 MiB (the LMA of .text).  objcopy -O binary emits a flat image
 * whose first byte is the entry point, which is exactly what stage 2 loads.
 */
ENTRY(_start)
SECTIONS
{
    . = 0xFFFFFFFF80100000;
    __kernel_virt_base = .;
    __kernel_phys_base = 0x100000;

    .text : AT(__kernel_phys_base) {
        __text_start = .;
        *(.text.boot)
        *(.text .text.*)
        __text_end = .;
    }

    .rodata : AT(ADDR(.rodata) - 0xFFFFFFFF80000000) {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    }

    .data : AT(ADDR(.data) - 0xFFFFFFFF80000000) {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    }

    .bss (NOLOAD) : AT(ADDR(.bss) - 0xFFFFFFFF80000000) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    __kernel_end = .;
    __kernel_end_phys = __kernel_end - 0xFFFFFFFF80000000;

    /DISCARD/ : {
        *(.note*)
        *(.comment)
        *(.eh_frame*)
        *(.gcc_except_table*)
    }
}
