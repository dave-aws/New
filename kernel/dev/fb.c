/* NOVA OS — 32-bit linear framebuffer, 2D primitives and text. */
#include <nova/types.h>
#include <nova/fb.h>
#include <nova/boot.h>
#include <nova/mm.h>
#include <nova/string.h>
#include <nova/log.h>

static u32 *fb;
static u32 width, height, pitch_px;
static bool ready;

static inline void clip(i32 *x, i32 *y, i32 *w, i32 *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > (i32)width) *w = (i32)width - *x;
    if (*y + *h > (i32)height) *h = (i32)height - *y;
}

void fb_init(struct boot_info *bi)
{
    if (bi->fb_bpp != 32 || bi->fb_width < 640 || bi->fb_height < 400) {
        kprintf("[fb] unusable framebuffer: %ux%u %u bpp\n",
                bi->fb_width, bi->fb_height, bi->fb_bpp);
        ready = false;
        return;
    }
    fb = (u32 *)PHYS_TO_VIRT((u64)bi->fb_addr);
    width = bi->fb_width;
    height = bi->fb_height;
    pitch_px = bi->fb_pitch / 4;
    ready = true;
    kprintf("[fb] %ux%u @ %#x, pitch %u, phys %#x\n",
            width, height, (u32)(u64)fb, bi->fb_pitch, bi->fb_addr);
}

bool fb_available(void) { return ready; }
u32 fb_width(void) { return width; }
u32 fb_height(void) { return height; }
u32 fb_pitch(void) { return pitch_px * 4; }

void fb_putpixel(i32 x, i32 y, u32 color)
{
    if (!ready || x < 0 || y < 0 || x >= (i32)width || y >= (i32)height)
        return;
    fb[y * (i32)pitch_px + x] = color;
}

void fb_blend_pixel(i32 x, i32 y, u32 color, u8 alpha)
{
    if (!ready || x < 0 || y < 0 || x >= (i32)width || y >= (i32)height || !alpha)
        return;
    u32 *p = &fb[y * (i32)pitch_px + x];
    if (alpha == 255) {
        *p = color;
        return;
    }
    u32 dst = *p;
    u32 a = alpha;
    u32 rb = ((((color & 0x00FF00FF) * a) + ((dst & 0x00FF00FF) * (255 - a))) >> 8) & 0x00FF00FF;
    u32 g = ((((color & 0x0000FF00) * a) + ((dst & 0x0000FF00) * (255 - a))) >> 8) & 0x0000FF00;
    *p = rb | g;
}

void fb_fill_rect(i32 x, i32 y, i32 w, i32 h, u32 color)
{
    if (!ready || w <= 0 || h <= 0)
        return;
    clip(&x, &y, &w, &h);
    for (i32 row = 0; row < h; row++) {
        u32 *p = &fb[(y + row) * (i32)pitch_px + x];
        for (i32 col = 0; col < w; col++)
            p[col] = color;
    }
}

void fb_blend_rect(i32 x, i32 y, i32 w, i32 h, u32 color, u8 alpha)
{
    if (!ready || w <= 0 || h <= 0)
        return;
    clip(&x, &y, &w, &h);
    for (i32 row = 0; row < h; row++)
        for (i32 col = 0; col < w; col++)
            fb_blend_pixel(x + col, y + row, color, alpha);
}

void fb_hline(i32 x, i32 y, i32 w, u32 color)
{
    fb_fill_rect(x, y, w, 1, color);
}

void fb_vline(i32 x, i32 y, i32 h, u32 color)
{
    fb_fill_rect(x, y, 1, h, color);
}

void fb_rect_outline(i32 x, i32 y, i32 w, i32 h, u32 color)
{
    fb_hline(x, y, w, color);
    fb_hline(x, y + h - 1, w, color);
    fb_vline(x, y, h, color);
    fb_vline(x + w - 1, y, h, color);
}

void fb_clear(u32 color)
{
    fb_fill_rect(0, 0, (i32)width, (i32)height, color);
}

void fb_gradient_v(i32 x, i32 y, i32 w, i32 h, u32 top, u32 bottom)
{
    if (!ready || h <= 0)
        return;
    for (i32 row = 0; row < h; row++) {
        u32 t = (u32)((row * 255) / (h > 1 ? h - 1 : 1));
        u32 r = (((top >> 16) & 0xFF) * (255 - t) + ((bottom >> 16) & 0xFF) * t) / 255;
        u32 g = (((top >> 8) & 0xFF) * (255 - t) + ((bottom >> 8) & 0xFF) * t) / 255;
        u32 b = ((top & 0xFF) * (255 - t) + (bottom & 0xFF) * t) / 255;
        fb_fill_rect(x, y + row, w, 1, 0xFF000000u | (r << 16) | (g << 8) | b);
    }
}

void fb_fill_round_rect(i32 x, i32 y, i32 w, i32 h, i32 r, u32 color)
{
    if (r <= 0) {
        fb_fill_rect(x, y, w, h, color);
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    fb_fill_rect(x + r, y, w - 2 * r, h, color);
    fb_fill_rect(x, y + r, r, h - 2 * r, color);
    fb_fill_rect(x + w - r, y + r, r, h - 2 * r, color);
    for (i32 dy = 0; dy < r; dy++) {
        for (i32 dx = 0; dx < r; dx++) {
            if ((dx - r) * (dx - r) + (dy - r) * (dy - r) <= r * r) {
                fb_putpixel(x + dx, y + dy, color);
                fb_putpixel(x + w - 1 - dx, y + dy, color);
                fb_putpixel(x + dx, y + h - 1 - dy, color);
                fb_putpixel(x + w - 1 - dx, y + h - 1 - dy, color);
            }
        }
    }
}

void fb_blit(i32 x, i32 y, i32 w, i32 h, const u32 *src, u32 src_pitch)
{
    if (!ready)
        return;
    for (i32 row = 0; row < h; row++) {
        i32 dy = y + row;
        if (dy < 0 || dy >= (i32)height)
            continue;
        for (i32 col = 0; col < w; col++) {
            i32 dx = x + col;
            if (dx < 0 || dx >= (i32)width)
                continue;
            fb[dy * (i32)pitch_px + dx] = src[row * (i32)src_pitch + col];
        }
    }
}

void fb_scroll_up(i32 pixels, u32 fill)
{
    if (!ready || pixels <= 0)
        return;
    if (pixels >= (i32)height) {
        fb_clear(fill);
        return;
    }
    for (u32 row = 0; row < height - (u32)pixels; row++)
        memcpy(&fb[row * pitch_px], &fb[(row + pixels) * pitch_px], width * 4);
    fb_fill_rect(0, (i32)height - pixels, (i32)width, pixels, fill);
}

/* ------------------------------------------------------------------ text -- */

void fb_draw_char(i32 x, i32 y, char ch, u32 color, u32 bg)
{
    if (!ready)
        return;
    if (ch < 32 || ch > 126)
        ch = '?';
    const u8 *rows = nova_font_mono_8x16[(int)ch - 32];
    for (int row = 0; row < 16; row++) {
        u8 bits = rows[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col))
                fb_putpixel(x + col, y + row, color);
            else if (bg & 0xFF000000u)
                fb_putpixel(x + col, y + row, bg);
        }
    }
}

void fb_draw_text_len(i32 x, i32 y, const char *text, size_t len, u32 color)
{
    for (size_t i = 0; i < len; i++) {
        char ch = text[i];
        if (ch == '\n') {
            y += FONT_H;
            x = 0;
            continue;
        }
        fb_draw_char(x, y, ch, color, 0);
        x += FONT_W;
    }
}

void fb_draw_text(i32 x, i32 y, const char *text, u32 color)
{
    fb_draw_text_len(x, y, text, strlen(text), color);
}

int fb_text_width(const char *text) { return (int)strlen(text) * FONT_W; }
int fb_text_width_len(const char *text, size_t len) { (void)text; return (int)len * FONT_W; }

static void draw_glyph(const struct nova_font *font, i32 x, i32 y, char ch, u32 color)
{
    if (!ready)
        return;
    int idx = (unsigned char)ch - 32;
    if (idx < 0 || idx >= font->count)
        idx = '?' - 32;
    const struct nova_glyph *g = &font->glyphs[idx];
    const u8 *px = font->bitmap + g->offset;
    for (u16 row = 0; row < font->height; row++) {
        for (u16 col = 0; col < g->width; col++) {
            u8 a = px[row * g->width + col];
            if (a)
                fb_blend_pixel(x + col, y + row, color, a);
        }
    }
}

static int text_width(const struct nova_font *font, const char *s)
{
    int w = 0;
    for (; *s; s++) {
        int idx = (unsigned char)*s - 32;
        if (idx < 0 || idx >= font->count)
            idx = '?' - 32;
        w += font->glyphs[idx].advance;
    }
    return w;
}

static void draw_text(const struct nova_font *font, i32 x, i32 y, const char *s, u32 color)
{
    for (; *s; s++) {
        int idx = (unsigned char)*s - 32;
        if (idx < 0 || idx >= font->count)
            idx = '?' - 32;
        draw_glyph(font, x, y, *s, color);
        x += font->glyphs[idx].advance;
    }
}

void fb_draw_char_ui(i32 x, i32 y, char ch, u32 color)
{
    draw_glyph(&nova_font_ui, x, y - nova_font_ui.baseline, ch, color);
}

void fb_draw_text_ui(i32 x, i32 y, const char *text, u32 color)
{
    draw_text(&nova_font_ui, x, y - nova_font_ui.baseline, text, color);
}

void fb_draw_text_ui_bold(i32 x, i32 y, const char *text, u32 color)
{
    draw_text(&nova_font_ui_bold, x, y - nova_font_ui_bold.baseline, text, color);
}

int fb_ui_text_width(const char *text) { return text_width(&nova_font_ui, text); }
int fb_ui_text_width_bold(const char *text) { return text_width(&nova_font_ui_bold, text); }
