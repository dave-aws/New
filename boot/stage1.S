/* NOVA OS — stage 1 boot sector (MBR).
 *
 * Loads stage 2 from LBA 1 into 0x0000:0x8000 using BIOS extended disk
 * reads, then far-jumps to it.  Everything is relative-addressed (org 0x7C00),
 * so the whole loader stays inside the 512-byte MBR with a valid 0xAA55
 * signature.
 */
.code16
.intel_syntax noprefix

.equ STAGE2_LBA,      1
.equ STAGE2_SECTORS,  60
.equ STAGE2_SEG,      0x0000
.equ STAGE2_OFF,      0x8000

.section .text
.globl _start
_start:
    cli
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00
    sti
    cld

    mov     [boot_drive], dl

    /* Early UART init so failures are visible on a serial console. */
    mov     dx, 0x3F9
    xor     al, al
    out     dx, al
    mov     dx, 0x3FB
    mov     al, 0x80
    out     dx, al
    mov     dx, 0x3F8
    mov     al, 1
    out     dx, al
    mov     dx, 0x3F9
    xor     al, al
    out     dx, al
    mov     dx, 0x3FB
    mov     al, 0x03
    out     dx, al
    mov     dx, 0x3FA
    mov     al, 0xC7
    out     dx, al
    mov     dx, 0x3FC
    mov     al, 0x03
    out     dx, al
    mov     al, '1'
    call    ser_putc

    /* Require EDD (INT 13h AH=41h) — every machine since ~1998 has it. */
    mov     dl, [boot_drive]
    mov     ah, 0x41
    mov     bx, 0x55AA
    int     0x13
    jnc     1f
    mov     al, 'X'
    call    ser_putc
    jmp     disk_error
1:  cmp     bx, 0xAA55
    je      2f
    mov     al, 'Y'
    call    ser_putc
    jmp     disk_error
2:

    mov     si, offset dap
    mov     ah, 0x42
    mov     dl, [boot_drive]
    int     0x13
    jnc     3f
    push    ax
    mov     al, 'R'
    call    ser_putc
    pop     ax
    push    ax
    mov     cx, 4
9:  rol     ah, 4
    mov     al, ah
    and     al, 0x0F
    add     al, '0'
    cmp     al, '9'
    jbe     8f
    add     al, 7
8:  call    ser_putc
    loop    9b
    pop     ax
    jmp     disk_error
3:

    /* Hand control to stage 2: CS = 0, IP = 0x8000. */
    mov     al, '2'
    call    ser_putc
    .byte   0xEA
    .word   STAGE2_OFF
    .word   STAGE2_SEG

disk_error:
    mov     al, 'E'
    call    ser_putc
    mov     si, offset err_msg
    call    puts
    cli
    hlt
    jmp     disk_error

ser_putc:
    push    dx
    push    ax
    mov     dx, 0x3FD
1:  in      al, dx
    test    al, 0x20
    jz      1b
    pop     ax
    mov     dx, 0x3F8
    out     dx, al
    pop     dx
    ret

/* Print an ASCIIZ string at DS:SI with BIOS teletype. */
puts:
    pusha
1:  lodsb
    test    al, al
    jz      2f
    mov     ah, 0x0E
    mov     bx, 0x0007
    int     0x10
    jmp     1b
2:  popa
    ret

.align 4
dap:
    .byte   16              /* size of packet */
    .byte   0               /* reserved */
    .word   STAGE2_SECTORS  /* sector count */
    .word   STAGE2_OFF      /* buffer offset */
    .word   STAGE2_SEG      /* buffer segment */
    .quad   STAGE2_LBA      /* starting LBA */

boot_drive:
    .byte   0x80
err_msg:
    .asciz  "NOVA: boot disk read failed\r\n"

/* Pad to 510 bytes and install the boot signature. */
.org 510
    .word 0xAA55
