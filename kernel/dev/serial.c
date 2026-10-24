/* NOVA OS — 16550 UART serial console (COM1).
 *
 * The serial port is the kernel's primary diagnostic channel and stays
 * available in release builds: it is how the QEMU smoke test observes boot.
 */
#include <nova/types.h>
#include <nova/io.h>
#include <nova/log.h>
#include <nova/string.h>

#define COM1 0x3F8

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* disable interrupts */
    outb(COM1 + 3, 0x80);   /* DLAB on */
    outb(COM1 + 0, 0x03);   /* 38400 baud divisor low */
    outb(COM1 + 1, 0x00);   /* divisor high */
    outb(COM1 + 3, 0x03);   /* 8N1 */
    outb(COM1 + 2, 0xC7);   /* FIFO on, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B);   /* IRQs off, RTS/DSR set */
}

static int tx_ready(void)
{
    return inb(COM1 + 5) & 0x20;
}

void serial_putc(char c)
{
    if (c == '\n') {
        for (int spin = 0; spin < 100000 && !tx_ready(); spin++)
            cpu_pause();
        outb(COM1, '\r');
    }
    for (int spin = 0; spin < 100000 && !tx_ready(); spin++)
        cpu_pause();
    outb(COM1, (u8)c);
}

void serial_write_n(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        serial_putc(s[i]);
}

void serial_write(const char *s)
{
    serial_write_n(s, strlen(s));
}
