/* NOVA OS — 8259A programmable interrupt controller. */
#include <nova/types.h>
#include <nova/io.h>
#include <nova/trap.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

void pic_init(void)
{
    /* Start initialisation (ICW1): edge triggered, cascade, ICW4 needed. */
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    /* ICW2: vector offsets. */
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    /* ICW3: wiring. */
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    /* ICW4: 8086 mode. */
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    /* Mask everything; drivers unmask what they own. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_mask(u8 irq, bool masked)
{
    u16 port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    u8 bit = (u8)(1u << (irq & 7));
    u8 cur = inb(port);
    if (masked)
        cur |= bit;
    else
        cur &= (u8)~bit;
    outb(port, cur);

    /* Also unmask the cascade line if a slave IRQ is enabled. */
    if (irq >= 8 && !masked)
        outb(PIC1_DATA, inb(PIC1_DATA) & (u8)~0x04);
}

void pic_eoi(u8 irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}
