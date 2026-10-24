/* NOVA OS — formatted output.
 *
 * A compact printf used by the kernel and by the subsystem.  Supports the
 * conversions the kernel actually needs, including 64-bit lengths and
 * zero/space padding, so Win32 format strings look familiar in logs.
 */
#include <nova/types.h>
#include <nova/log.h>
#include <nova/string.h>
#include <nova/io.h>

struct out {
    char *buf;
    size_t cap;
    size_t len;
};

static void out_ch(struct out *o, char c)
{
    if (o->len + 1 < o->cap)
        o->buf[o->len] = c;
    o->len++;
}

static void out_str(struct out *o, const char *s, size_t max)
{
    size_t n = 0;
    while (s[n] && n < max)
        n++;
    for (size_t i = 0; i < n; i++)
        out_ch(o, s[i]);
}

static void out_pad(struct out *o, char c, int count)
{
    while (count-- > 0)
        out_ch(o, c);
}

static int is_digit(int c) { return c >= '0' && c <= '9'; }

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct out o = { buf, size, 0 };
    if (size == 0)
        return 0;

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            out_ch(&o, *p);
            continue;
        }
        p++;
        if (*p == '%') {
            out_ch(&o, '%');
            continue;
        }

        char pad = ' ';
        bool left = false;
        bool alt = false;
        int width = 0;
        int prec = -1;
        bool longarg = false;   /* l / z / t */
        bool quad = false;      /* ll */

        if (*p == '-') { left = true; p++; }
        if (*p == '#') { alt = true; p++; }
        if (*p == '0') { pad = '0'; p++; }
        while (is_digit(*p))
            width = width * 10 + (*p++ - '0');
        if (*p == '.') {
            p++;
            prec = 0;
            while (is_digit(*p))
                prec = prec * 10 + (*p++ - '0');
        }
        if (*p == 'l') {
            p++;
            if (*p == 'l') { quad = true; p++; }
            else longarg = true;
        } else if (*p == 'z' || *p == 't' || *p == 'j') {
            longarg = true;
            p++;
        }

        char tmp[80];
        const char *s = NULL;
        int len = 0;
        bool numeric = false;
        int base = 10;
        bool neg = false;
        u64 uv = 0;

        switch (*p) {
        case 'd':
        case 'i': {
            i64 sv;
            if (quad)
                sv = va_arg(ap, i64);
            else if (longarg)
                sv = va_arg(ap, long);
            else
                sv = va_arg(ap, int);
            if (sv < 0) { neg = true; uv = (u64)(-sv); }
            else uv = (u64)sv;
            numeric = true;
            break;
        }
        case 'u':
            if (quad) uv = va_arg(ap, u64);
            else if (longarg) uv = va_arg(ap, unsigned long);
            else uv = va_arg(ap, unsigned int);
            numeric = true;
            break;
        case 'x':
        case 'X':
            base = 16;
            if (quad) uv = va_arg(ap, u64);
            else if (longarg) uv = va_arg(ap, unsigned long);
            else uv = va_arg(ap, unsigned int);
            numeric = true;
            break;
        case 'o':
            base = 8;
            uv = va_arg(ap, unsigned int);
            numeric = true;
            break;
        case 'p':
            uv = (u64)(uptr)va_arg(ap, void *);
            base = 16;
            numeric = true;
            pad = '0';
            width = 16;
            break;
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            tmp[1] = 0;
            s = tmp;
            len = 1;
            break;
        case 's':
            s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            {
                size_t n = strlen(s);
                if (prec >= 0 && (size_t)prec < n)
                    n = (size_t)prec;
                len = (int)n;
            }
            break;
        case 'b':
            base = 2;
            uv = va_arg(ap, unsigned int);
            numeric = true;
            break;
        default:
            out_ch(&o, '%');
            out_ch(&o, *p);
            continue;
        }

        if (numeric) {
            utoa(uv, tmp, base);
            if (*p == 'X') {
                for (char *q = tmp; *q; q++)
                    if (*q >= 'a' && *q <= 'f')
                        *q -= 32;
            }
            s = tmp;
            len = (int)strlen(tmp);
            if (prec > len) {
                int zeros = prec - len;
                if (neg) { out_ch(&o, '-'); neg = false; }
                out_pad(&o, '0', zeros);
            }
        }

        const char *prefix = "";
        int prefix_len = 0;
        if (alt && numeric) {
            if (base == 16) { prefix = "0x"; prefix_len = 2; }
            else if (base == 8) { prefix = "0"; prefix_len = 1; }
            else if (base == 2) { prefix = "0b"; prefix_len = 2; }
        }

        int total = len + (neg ? 1 : 0) + prefix_len;
        if (!left && width > total && pad == ' ')
            out_pad(&o, ' ', width - total);
        if (neg)
            out_ch(&o, '-');
        for (int i = 0; i < prefix_len; i++)
            out_ch(&o, prefix[i]);
        if (!left && width > total && pad == '0')
            out_pad(&o, '0', width - total);
        if (s)
            out_str(&o, s, (size_t)len);
        if (left && width > total)
            out_pad(&o, ' ', width - total);
    }

    size_t pos = o.len < o.cap - 1 ? o.len : o.cap - 1;
    buf[pos] = 0;
    return (int)o.len;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* The graphical console registers a sink once the framebuffer exists. */
void (*console_sink)(const char *s, size_t n);

int kprintf(const char *fmt, ...)
{
    static char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return n;
    size_t len = (size_t)n < sizeof(buf) - 1 ? (size_t)n : sizeof(buf) - 1;
    buf[len] = 0;
    serial_write_n(buf, len);
    if (console_sink)
        console_sink(buf, len);
    return n;
}

noreturn void panic(const char *fmt, ...)
{
    static char buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    serial_write("\n*** NOVA PANIC: ");
    serial_write(buf);
    serial_write("\nSystem halted.\n");
    if (console_sink) {
        console_sink("\n*** NOVA PANIC: ", 16);
        console_sink(buf, strlen(buf));
        console_sink("\nSystem halted.\n", 16);
    }
    cpu_cli();
    for (;;)
        cpu_halt();
}
