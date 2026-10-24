/* NOVA OS — kernel entry point.
 *
 * Stage 2 already put us in 64-bit long mode with a 4 GiB identity map and a
 * higher-half window at 0xFFFFFFFF80000000.  We switch to a stack inside the
 * kernel image, zero .bss, and hand control to C.
 */
.code64
.intel_syntax noprefix

.section .text.boot, "ax"
.globl _start
_start:
    cli
    cld
    lea     rsp, [rip + boot_stack_top]
    xor     rbp, rbp
    mov     dx, 0x3F8
    mov     al, 'x'
    out     dx, al

    /* Zero the BSS: objcopy strips NOBITS, so nobody else does it. */
    lea     rdi, [rip + __bss_start]
    lea     rcx, [rip + __bss_end]
    sub     rcx, rdi
    shr     rcx, 3
    xor     rax, rax
    rep stosq

    mov     dx, 0x3F8
    mov     al, 'y'
    out     dx, al
    mov     rdi, 0x6C00                 /* physical boot_info from stage 2 */
    call    kmain

.globl arch_halt_forever
arch_halt_forever:
1:  cli
    hlt
    jmp     1b

/* ---------------------------------------------------------------- GDT ---- */
.globl gdt_load
gdt_load:
    /* void gdt_load(struct gdt_ptr *p, uint16_t code, uint16_t data) */
    lgdt    [rdi]
    mov     ax, dx          /* data selector */
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax
    push    rsi
    lea     rax, [rip + 1f]
    push    rax
    retfq
1:  ret

.globl tss_load
tss_load:
    /* void tss_load(uint16_t selector) */
    ltr     di
    ret

/* void jump_usermode(uint64_t entry, uint64_t stack, uint64_t arg) */
.globl jump_usermode
jump_usermode:
    cli
    mov     rcx, rdi                    /* entry */
    mov     rdx, rsi                    /* stack */
    mov     r8, rdx                     /* arg -> rdi for the entry */
    mov     rdi, r8
    push    0x23                        /* user SS */
    push    rdx
    push    0x202                       /* IF */
    push    0x2B                        /* user CS */
    push    rcx
    iretq

.section .bss
.align 16
boot_stack:
    .skip 32768
boot_stack_top:
