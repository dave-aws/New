/* NOVA OS — freestanding string/memory routines.
 *
 * gcc is allowed to emit calls to these even with -fno-builtin (struct
 * copies, loop idiom recognition), so they must always be present.
 */
#include <nova/types.h>
#include <nova/string.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    u8 *d = dst;
    const u8 *s = src;
    while (n >= 8) {
        *(u64 *)d = *(const u64 *)s;
        d += 8; s += 8; n -= 8;
    }
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    u8 *d = dst;
    const u8 *s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s) {
        while (n >= 8) {
            *(u64 *)d = *(const u64 *)s;
            d += 8; s += 8; n -= 8;
        }
        while (n--)
            *d++ = *s++;
    } else {
        d += n; s += n;
        while (n >= 8) {
            d -= 8; s -= 8;
            *(u64 *)d = *(const u64 *)s;
            n -= 8;
        }
        while (n--)
            *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    u8 *d = dst;
    u8 v = (u8)c;
    u64 word = 0x0101010101010101ULL * v;
    while (n >= 8) {
        *(u64 *)d = word;
        d += 8; n -= 8;
    }
    while (n--)
        *d++ = v;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *x = a, *y = b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const u8 *p = s;
    for (size_t i = 0; i < n; i++)
        if (p[i] == (u8)c)
            return (void *)(p + i);
    return NULL;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
        n++;
    return n;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst + strlen(dst);
    while ((*d++ = *src++))
        ;
    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++; b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (int)(u8)a[i] - (int)(u8)b[i];
        if (!a[i])
            return 0;
    }
    return 0;
}

/* ASCII case-insensitive compare (Win32 paths, DLL names). */
int strcasecmp(const char *a, const char *b)
{
    for (;;) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb || !ca)
            return (int)ca - (int)cb;
    }
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return (int)ca - (int)cb;
        if (!ca)
            return 0;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    for (;; s++) {
        if (*s == (char)c)
            last = s;
        if (!*s)
            break;
    }
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    if (!n)
        return (char *)haystack;
    for (; *haystack; haystack++)
        if (!strncmp(haystack, needle, n))
            return (char *)haystack;
    return NULL;
}

char *strtok_r(char *s, const char *delim, char **save)
{
    if (!s)
        s = *save;
    while (*s && strchr(delim, *s))
        s++;
    if (!*s) {
        *save = s;
        return NULL;
    }
    char *start = s;
    while (*s && !strchr(delim, *s))
        s++;
    if (*s)
        *s++ = 0;
    *save = s;
    return start;
}

/* Simple non-reentrant helpers used by the shell and FS code. */
static char *last_token;
char *strtok(char *s, const char *delim) { return strtok_r(s, delim, &last_token); }

void *memset32(void *dst, u32 c, size_t count)
{
    u32 *d = dst;
    while (count--)
        *d++ = c;
    return dst;
}

/* Locale-independent decimal conversion with optional base (2..36). */
char *utoa(u64 value, char *buf, int base)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[72];
    int i = 0;
    if (base < 2 || base > 36)
        base = 10;
    do {
        tmp[i++] = digits[value % (u64)base];
        value /= (u64)base;
    } while (value);
    int j = 0;
    while (i)
        buf[j++] = tmp[--i];
    buf[j] = 0;
    return buf;
}

char *itoa(i64 value, char *buf)
{
    if (value < 0) {
        buf[0] = '-';
        utoa((u64)(-value), buf + 1, 10);
        return buf;
    }
    return utoa((u64)value, buf, 10);
}
