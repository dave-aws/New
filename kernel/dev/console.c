/* NOVA OS — console objects and the boot text console. */
#include <nova/types.h>
#include <nova/console.h>
#include <nova/sched.h>
#include <nova/string.h>
#include <nova/log.h>
#include <nova/mm.h>
#include <nova/io.h>
#include <nova/fb.h>

static struct console *console_list;

struct console *console_create(const char *title, u32 cols, u32 rows)
{
    struct console *c = kzalloc(sizeof(*c));
    if (!c)
        return NULL;
    c->cols = cols ? cols : 80;
    c->rows = rows ? rows : 25;
    c->echo = true;
    c->title = title ? kstrdup(title) : "Console";
    c->next = console_list;
    console_list = c;
    return c;
}

void console_destroy(struct console *c)
{
    if (!c)
        return;
    struct console **pp = &console_list;
    while (*pp) {
        if (*pp == c) {
            *pp = c->next;
            break;
        }
        pp = &(*pp)->next;
    }
    kfree((void *)c->title);
    kfree(c);
}

void console_write(struct console *c, const char *s, size_t n)
{
    if (!c || !n)
        return;
    /* Keep the tail of the stream when the buffer overflows. */
    if (n >= CONSOLE_CAP - 1) {
        s += n - (CONSOLE_CAP - 2);
        n = CONSOLE_CAP - 2;
    }
    if (c->len + n > CONSOLE_CAP - 1) {
        u32 drop = c->len + n - (CONSOLE_CAP - 1);
        if (drop >= c->len) {
            c->len = 0;
        } else {
            memmove(c->buf, c->buf + drop, c->len - drop);
            c->len -= drop;
            if (c->cursor > drop)
                c->cursor -= drop;
            else
                c->cursor = 0;
        }
    }
    memcpy(c->buf + c->len, s, n);
    c->len += (u32)n;
    c->buf[c->len] = 0;
    c->dirty = true;
    if (c->waiter) {
        thread_unblock((struct thread *)c->waiter);
        c->waiter = NULL;
    }
}

void console_write_str(struct console *c, const char *s)
{
    console_write(c, s, strlen(s));
}

void console_clear(struct console *c)
{
    if (!c)
        return;
    c->len = 0;
    c->cursor = 0;
    c->buf[0] = 0;
    c->dirty = true;
}

void console_input(struct console *c, char ch)
{
    if (!c)
        return;
    char tmp[2] = { ch, 0 };
    console_write(c, tmp, 1);
}

bool console_has_data(struct console *c)
{
    return c && c->cursor < c->len;
}

int console_read(struct console *c, char *dst, int max, bool blocking)
{
    if (!c || !dst || max <= 0)
        return -1;
    while (c->cursor >= c->len) {
        if (!blocking)
            return 0;
        c->waiter = current_thread();
        thread_block();
    }
    int n = 0;
    while (n < max && c->cursor < c->len)
        dst[n++] = c->buf[c->cursor++];
    return n;
}

struct console *console_of_process(struct process *p)
{
    return p ? (struct console *)p->console : NULL;
}

void console_write_process(struct process *p, const char *s, size_t n)
{
    if (!p) {
        serial_write_n(s, n);
        return;
    }
    struct console *c = console_of_process(p);
    if (c)
        console_write(c, s, n);
    else
        serial_write_n(s, n);
}

/* ------------------------------------------------------------ boot console */
static bool boot_active;
static u32 boot_row;
static u32 boot_col;

void bootcon_init(void)
{
    boot_active = true;
    boot_row = 0;
    boot_col = 0;
    if (fb_available())
        fb_clear(0xFF101418);
}

bool bootcon_active(void) { return boot_active; }
void bootcon_set_active(bool on) { boot_active = on; }

void bootcon_putc(char ch)
{
    if (!boot_active || !fb_available())
        return;
    if (ch == '\n') {
        boot_col = 0;
        boot_row++;
    } else if (ch == '\r') {
        boot_col = 0;
    } else if (ch == '\t') {
        boot_col = (boot_col + 4) & ~3u;
    } else if (ch == '\b') {
        if (boot_col)
            boot_col--;
    } else {
        fb_draw_char(boot_col * FONT_W, boot_row * FONT_H, ch, 0xFFC8D0D8, 0);
        boot_col++;
    }
    if (boot_col >= fb_width() / FONT_W) {
        boot_col = 0;
        boot_row++;
    }
    if ((boot_row + 1) * FONT_H >= fb_height()) {
        /* Scroll by one text line. */
        fb_scroll_up(FONT_H, 0xFF101418);
        boot_row--;
    }
}

void bootcon_write(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        bootcon_putc(s[i]);
}
