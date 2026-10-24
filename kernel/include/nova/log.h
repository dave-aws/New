/* NOVA OS — kernel logging (serial console + graphical console). */
#ifndef NOVA_LOG_H
#define NOVA_LOG_H

#include <nova/types.h>

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
void serial_write_n(const char *s, size_t n);

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t size, const char *fmt, ...);
int kprintf(const char *fmt, ...);

/* Set once the graphical console exists; mirrors kernel log output. */
extern void (*console_sink)(const char *s, size_t n);

noreturn void panic(const char *fmt, ...);

#define KLOG_OK   0
#define KLOG_BOOT 1

#endif /* NOVA_LOG_H */
