/* NOVA OS — stage 2 bootloader.
 *
 * Runs in 16-bit real mode at linear 0x8000 (flat segments, so symbol values
 * are linear addresses).  Responsibilities:
 *   1. A20 gate, E820 physical memory map
 *   2. VBE linear-framebuffer mode selection (prefers 1280x800x32)
 *   3. Load the flat kernel image from LBA 64 to physical 1 MiB
 *   4. Build 4-level page tables: identity map [0,4 GiB) + higher-half
 *      window at 0xFFFFFFFF80000000 for the kernel
 *   5. Enter long mode and jump to the kernel entry point
 */
.code16
.intel_syntax noprefix

.set IMAGE_ORG,      0x8000
.set STACK_SEG,      0x6000
.set STACK_TOP,      0xF000
.set E820_ADDR,     0x6000
.set VBE_CTRL,      0x6700
.set VBE_MODE,      0x6A00
.set BOOTINFO,      0x6C00
.set BOUNCE_SEG,    0x2000
.set BOUNCE_SIZE,   0x8000
.set KERNEL_DEST,   0x100000
.set KERNEL_LBA,    64

.set PT_PML4,       0x70000
.set PT_PDPT_LO,    0x71000
.set PT_PD0,        0x72000
.set PT_PDPT_HI,    0x76000
.set BOOT_STACK,    0x7F000

.include "kernel_sectors.inc"

.section .text
.globl _start
_start:
    cli
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, STACK_TOP
    sti
    cld
    mov     [boot_drive], dl
    mov     al, 'S'
    call    ser_putc

/* ---------------------------------------------------------------- A20 --- */
    call    enable_a20
    mov     al, 'A'
    call    ser_putc

/* ----------------------------------------------------------- E820 map --- */
    xor     ebx, ebx
    mov     word ptr [mmap_count], 0
    mov     di, E820_ADDR
    xor     ax, ax
    mov     es, ax
.e820_loop:
    mov     eax, 0xE820
    mov     edx, 0x534D4150     /* 'SMAP' */
    mov     ecx, 24
    mov     dword ptr es:[di + 20], 1
    int     0x15
    jc      .e820_done
    cmp     eax, 0x534D4150
    jne     .e820_done
    inc     word ptr [mmap_count]
    add     di, 24
    test    ebx, ebx
    jnz     .e820_loop
.e820_done:
    xor     ax, ax
    mov     ds, ax

/* ---------------------------------------------------- VBE mode select --- */
    mov     al, 'M'
    call    ser_putc
    call    vbe_select
    mov     al, 'V'
    call    ser_putc

/* ----------------------------- unreal mode (FS limit 4 GiB) ------------ */
    call    enable_unreal
    mov     al, 'U'
    call    ser_putc

/* ------------------------------------------------------ load kernel ----- */
    mov     dword ptr [lba], KERNEL_LBA
    mov     dword ptr [dest], KERNEL_DEST
    mov     eax, KERNEL_SECTORS
    mov     dword ptr [remaining], eax

.load_loop:
    mov     eax, dword ptr [remaining]
    test    eax, eax
    jz      .load_done
    cmp     eax, 64
    jbe     1f
    mov     eax, 64
1:  mov     word ptr [dap_count], ax
    mov     word ptr [dap_lba], 0
    mov     word ptr [dap_lba+2], 0
    mov     eax, dword ptr [lba]
    mov     word ptr [dap_lba], ax
    shr     eax, 16
    mov     word ptr [dap_lba+2], ax

    mov     si, offset dap
    mov     dl, [boot_drive]
    mov     ah, 0x42
    int     0x13
    jc      disk_fail
    xor     ax, ax
    mov     ds, ax

    /* Copy count*512 bytes from BOUNCE_SEG:0 to FS:dest (unreal mode).
     * String moves cannot target FS, so use an explicit byte loop.
     * 32 KB per chunk keeps the real-mode bounce buffer small. */
    push    ds
    mov     ax, BOUNCE_SEG
    mov     es, ax
    xor     ax, ax
    mov     ds, ax
    xor     esi, esi
    mov     edi, dword ptr [dest]
    movzx   ecx, word ptr [dap_count]
    shl     ecx, 9
    cld
.copy_loop:
    mov     al, es:[esi]
    mov     fs:[edi], al
    inc     esi
    inc     edi
    dec     ecx
    jnz     .copy_loop
    pop     ds
    movzx   eax, word ptr [dap_count]
    add     dword ptr [lba], eax
    shl     eax, 9
    add     dword ptr [dest], eax
    movzx   eax, word ptr [dap_count]
    sub     dword ptr [remaining], eax
    jmp     .load_loop

.load_done:
    mov     al, 'L'
    call    ser_putc
/* ------------------------------------------------- boot info struct ----- */
    mov     ax, ds
    mov     es, ax              /* ES = data segment for store helpers */
    mov     eax, dword ptr [fb_addr]
    mov     dword ptr es:[BOOTINFO + 8], eax
    mov     eax, dword ptr [fb_pitch]
    mov     dword ptr es:[BOOTINFO + 12], eax
    mov     eax, dword ptr [fb_width]
    mov     dword ptr es:[BOOTINFO + 16], eax
    mov     eax, dword ptr [fb_height]
    mov     dword ptr es:[BOOTINFO + 20], eax
    mov     eax, dword ptr [fb_bpp]
    mov     dword ptr es:[BOOTINFO + 24], eax
    mov     dword ptr es:[BOOTINFO + 0], 0x4E4F5642   /* 'NOVB' */
    mov     dword ptr es:[BOOTINFO + 4], 1
    mov     dword ptr es:[BOOTINFO + 32], E820_ADDR
    mov     dword ptr es:[BOOTINFO + 36], 0
    movzx   eax, word ptr [mmap_count]
    mov     dword ptr es:[BOOTINFO + 40], eax
    mov     dword ptr es:[BOOTINFO + 44], 24
    mov     dword ptr es:[BOOTINFO + 48], KERNEL_DEST
    mov     dword ptr es:[BOOTINFO + 52], 0
    mov     eax, KERNEL_SECTORS
    shl     eax, 9
    mov     dword ptr es:[BOOTINFO + 56], eax
    mov     dword ptr es:[BOOTINFO + 60], 0
    movzx   eax, byte ptr [boot_drive]
    mov     dword ptr es:[BOOTINFO + 64], eax
    mov     dword ptr es:[BOOTINFO + 68], 0

/* --------------------------------------------------- long mode entry ---- */
    cli
    lgdt    [gdt_desc]
    mov     eax, cr4
    or      eax, (1 << 5)       /* PAE */
    mov     cr4, eax
    mov     eax, cr0
    or      eax, 1
    mov     cr0, eax
    .byte   0xEA
    .word   pm_entry
    .word   0x08

disk_fail:
    mov     si, offset err_disk
    jmp     fatal
fatal:
    /* ES already == DS; BIOS teletype needs a printable string in DS:SI */
    push    ds
    pop     es
    call    puts
    cli
    hlt
    jmp     fatal

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

/* ------------------------------------------------------------- A20 ----- */
enable_a20:
    pusha
    in      al, 0x92
    test    al, 2
    jnz     .a20_done
    or      al, 2
    and     al, 0xFE
    out     0x92, al
    /* Keyboard controller method as a fallback. */
    call    a20_wait
    mov     al, 0xAD
    out     0x64, al
    call    a20_wait
    mov     al, 0xD0
    out     0x64, al
    call    a20_wait
    in      al, 0x60
    mov     ah, al
    call    a20_wait
    mov     al, 0xD1
    out     0x64, al
    call    a20_wait
    mov     al, ah
    or      al, 2
    out     0x60, al
    call    a20_wait
    mov     al, 0xAE
    out     0x64, al
    call    a20_wait
.a20_done:
    popa
    ret

a20_wait:
    in      al, 0x64
    test    al, 2
    jnz     a20_wait
    ret

/* --------------------------------------------------------- VBE setup ---- */
vbe_select:
    pusha
    xor     ax, ax
    mov     es, ax
    mov     di, VBE_CTRL
    mov     ax, 0x4F00
    int     0x10
    cmp     ax, 0x004F
    jne     .vbe_fallback
    cmp     dword ptr es:[VBE_CTRL], 0x41534556   /* 'VESA' */
    jne     .vbe_fallback

    mov     bx, word ptr es:[VBE_CTRL + 14]
    mov     ax, word ptr es:[VBE_CTRL + 16]
    mov     word ptr [mlist_off], bx
    mov     word ptr [mlist_seg], ax
    mov     word ptr [best_mode], 0xFFFF
    mov     dword ptr [best_score], 0

.mode_loop:
    mov     ax, word ptr [mlist_seg]
    mov     es, ax
    mov     si, word ptr [mlist_off]
    mov     cx, word ptr es:[si]
    cmp     cx, 0xFFFF
    je      .modes_done
    add     si, 2
    mov     word ptr [mlist_off], si
    mov     word ptr [candidate], cx

    xor     ax, ax
    mov     es, ax
    mov     di, VBE_MODE
    mov     cx, word ptr [candidate]
    mov     ax, 0x4F01
    int     0x10
    cmp     ax, 0x004F
    jne     .mode_loop

    mov     ax, word ptr es:[VBE_MODE]
    and     ax, 0x0081               /* supported | linear framebuffer */
    cmp     ax, 0x0081
    jne     .mode_loop
    test    word ptr es:[VBE_MODE], 0x0010   /* graphics mode */
    jz      .mode_loop
    cmp     byte ptr es:[VBE_MODE + 25], 32  /* bpp */
    jne     .mode_loop
    cmp     byte ptr es:[VBE_MODE + 27], 6   /* direct colour */
    jne     .mode_loop

    movzx   eax, word ptr es:[VBE_MODE + 18]
    movzx   edx, word ptr es:[VBE_MODE + 20]
    cmp     eax, 1024
    jb      .mode_loop
    cmp     eax, 1920
    ja      .mode_loop
    cmp     edx, 720
    jb      .mode_loop
    cmp     edx, 1200
    ja      .mode_loop

    imul    eax, edx                 /* score = w*h */
    cmp     word ptr es:[VBE_MODE + 18], 1280
    jne     1f
    cmp     word ptr es:[VBE_MODE + 20], 800
    jne     1f
    add     eax, 4000000             /* strong preference for 1280x800 */
1:  cmp     eax, dword ptr [best_score]
    jbe     .mode_loop
    mov     dword ptr [best_score], eax
    mov     ax, word ptr [candidate]
    mov     word ptr [best_mode], ax
    jmp     .mode_loop

.modes_done:
    cmp     word ptr [best_mode], 0xFFFF
    je      .vbe_fallback
    /* Re-query the winner so that VBE_MODE holds its information. */
    xor     ax, ax
    mov     es, ax
    mov     di, VBE_MODE
    mov     cx, word ptr [best_mode]
    mov     ax, 0x4F01
    int     0x10
    cmp     ax, 0x004F
    jne     .vbe_fallback
    mov     bx, word ptr [best_mode]
    or      bx, 0x4000
    mov     ax, 0x4F02
    int     0x10
    cmp     ax, 0x004F
    jne     .vbe_fallback
    jmp     .vbe_store

.vbe_fallback:
    xor     ax, ax
    mov     es, ax
    mov     bx, 0x4118               /* 1024x768x32 | LFB */
    mov     ax, 0x4F02
    int     0x10
    cmp     ax, 0x004F
    jne     .vbe_none
    mov     dword ptr [fb_addr], 0xE0000000
    mov     dword ptr [fb_pitch], 4096
    mov     dword ptr [fb_width], 1024
    mov     dword ptr [fb_height], 768
    mov     dword ptr [fb_bpp], 32
    jmp     .vbe_out

.vbe_none:
    mov     si, offset err_vbe
    call    puts
    cli
    hlt

.vbe_store:
    xor     ax, ax
    mov     es, ax
    mov     eax, dword ptr es:[VBE_MODE + 40]   /* PhysBasePtr */
    mov     dword ptr [fb_addr], eax
    movzx   eax, word ptr es:[VBE_MODE + 16]    /* BytesPerScanLine */
    mov     dword ptr [fb_pitch], eax
    movzx   eax, word ptr es:[VBE_MODE + 18]
    mov     dword ptr [fb_width], eax
    movzx   eax, word ptr es:[VBE_MODE + 20]
    mov     dword ptr [fb_height], eax
    movzx   eax, byte ptr es:[VBE_MODE + 25]
    mov     dword ptr [fb_bpp], eax
.vbe_out:
    popa
    ret

/* --------------------------------------------- unreal mode + 4G FS ----- */
/* We need a 4 GiB-limit FS segment to copy the kernel above 1 MiB while
 * BIOS disk reads still require real mode.  Transitions are done through a
 * 16-bit protected-mode code segment so the CPU never interprets a far jump
 * with a stale 32-bit CS descriptor (the classic unreal-mode pitfall). */
enable_unreal:
    pusha
    cli
    lgdt    [gdt_desc]
    mov     eax, cr0
    or      eax, 1
    mov     cr0, eax
    /* still real mode: 16-bit encoded far jump into 32-bit protected mode */
    .byte   0xEA
    .word   unreal_pm32
    .word   0x08

.code32
unreal_pm32:
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     fs, ax              /* FS: base 0, limit 4 GiB */
    mov     ss, ax
    /* switch to the 16-bit code selector while still protected */
    .byte   0xEA
    .long   unreal_pm16
    .word   0x18

.code16
unreal_pm16:
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     eax, cr0
    and     eax, 0xFFFFFFFE
    mov     cr0, eax            /* now real mode with a 16-bit CS cached */
    .byte   0xEA
    .word   unreal_rm
    .word   0x0000

unreal_rm:
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    /* FS is deliberately NOT reloaded: its 4 GiB limit must survive. */
    sti
    popa
    ret

/* ------------------------------------ protected mode -> long mode ------ */
.code32
pm_entry:
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     fs, ax
    mov     gs, ax
    mov     esp, 0x1F000

    /* Zero 32 KiB of page-table pages at 0x70000. */
    mov     edi, PT_PML4
    xor     eax, eax
    mov     ecx, 0x2000
    cld
    rep stosd

    /* PML4[0] -> low PDPT ; PML4[511] -> high PDPT */
    mov     dword ptr [PT_PML4], PT_PDPT_LO + 3
    mov     dword ptr [PT_PML4 + 511*8], PT_PDPT_HI + 3

    /* identity map 0..4 GiB with four 1 GiB page directories */
    mov     dword ptr [PT_PDPT_LO], PT_PD0 + 3
    mov     dword ptr [PT_PDPT_LO + 8], PT_PD0 + 0x1000 + 3
    mov     dword ptr [PT_PDPT_LO + 16], PT_PD0 + 0x2000 + 3
    mov     dword ptr [PT_PDPT_LO + 24], PT_PD0 + 0x3000 + 3

    /* higher half: 0xFFFFFFFF80000000 -> physical 0 */
    mov     dword ptr [PT_PDPT_HI + 510*8], PT_PD0 + 3

    /* four page directories, 512 x 2 MiB pages each */
    mov     edi, PT_PD0
    mov     eax, 0x83
    mov     ecx, 2048
1:  mov     dword ptr [edi], eax
    add     eax, 0x200000
    add     edi, 8
    dec     ecx
    jnz     1b

    /* enable long mode: PAE must be on before setting LME */
    mov     al, 'p'
    mov     dx, 0x3F8
    out     dx, al
    mov     eax, cr4
    or      eax, (1 << 5)
    mov     cr4, eax
    mov     ecx, 0xC0000080      /* IA32_EFER */
    rdmsr
    or      eax, 0x900          /* LME | NXE */
    wrmsr
    mov     al, 'q'
    mov     dx, 0x3F8
    out     dx, al
    mov     eax, PT_PML4
    mov     cr3, eax
    mov     eax, cr0
    or      eax, 0x80010000     /* PG | WP */
    mov     cr0, eax

    .byte   0xEA
    .long   long_entry
    .word   0x20

long_entry:
    mov     ax, 0x28
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     fs, ax
    mov     gs, ax
    /* Already in 64-bit mode: EA (far jump) is invalid here, use a near jump. */
    jmp     long64_entry

.code64
long64_entry:
    mov     al, 'X'
    mov     dx, 0x3F8
    out     dx, al
    mov     rsp, BOOT_STACK
    xor     rbp, rbp
    mov     rdi, BOOTINFO
    mov     dx, 0x3F8
    mov     al, 'K'
    out     dx, al
    mov     rax, 0xFFFFFFFF80100000
    jmp     rax

/* ------------------------------------------------------------- data ----- */
.code16
.align 8
gdt_start:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF     /* 0x08: 32-bit code, base 0, limit 4G */
    .quad 0x00CF92000000FFFF     /* 0x10: flat data */
    .quad 0x00009A000000FFFF     /* 0x18: 16-bit code (unreal-mode exit) */
    .quad 0x00AF9A000000FFFF     /* 0x20: 64-bit code */
    .quad 0x00CF92000000FFFF     /* 0x28: data */
gdt_end:

gdt_desc:
    .word   gdt_end - gdt_start - 1
    .long   gdt_start

dap:
    .byte   16
    .byte   0
dap_count:
    .word   64
    .word   0x0000              /* offset */
    .word   BOUNCE_SEG          /* segment */
dap_lba:
    .quad   0

dest:       .long 0
lba:        .long 0
remaining:  .long 0
fb_addr:    .long 0
fb_pitch:   .long 0
fb_width:   .long 0
fb_height:  .long 0
fb_bpp:     .long 0
mlist_off:  .word 0
mlist_seg:  .word 0
best_mode:  .word 0
candidate:  .word 0
best_score: .long 0
mmap_count: .word 0
boot_drive: .byte 0x80

err_disk: .asciz "NOVA: disk read error\r\n"
err_vbe:  .asciz "NOVA: no usable framebuffer\r\n"
