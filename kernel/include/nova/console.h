/* NOVA OS — text consoles.
 *
 * Two kinds exist: the boot console (kprintf mirror on the framebuffer until
 * the desktop starts) and per-process console objects used by console Win32
 * applications and by the built-in terminal.
 */
#ifndef NOVA_CONSOLE_H
#define NOVA_CONSOLE_H

#include <nova/types.h>

#define CONSOLE_CAP 16384

struct console {
    char buf[CONSOLE_CAP];
    u32 len;
    u32 cursor;          /* read position for console_read */
    u32 cols;
    u32 rows;
    bool echo;
    bool dirty;
    void *waiter;
    const char *title;
    struct console *next;
};

struct process;

struct console *console_create(const char *title, u32 cols, u32 rows);
void console_destroy(struct console *c);
void console_write(struct console *c, const char *s, size_t n);
void console_write_str(struct console *c, const char *s);
void console_clear(struct console *c);
void console_input(struct console *c, char ch);
int  console_read(struct console *c, char *dst, int max, bool blocking);
bool console_has_data(struct console *c);

struct console *console_of_process(struct process *p);
void console_write_process(struct process *p, const char *s, size_t n);

/* Boot console (framebuffer text) */
void bootcon_init(void);
void bootcon_putc(char c);
void bootcon_write(const char *s, size_t n);
bool bootcon_active(void);
void bootcon_set_active(bool on);

#endif /* NOVA_CONSOLE_H */
