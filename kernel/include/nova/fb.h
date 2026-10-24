/* NOVA OS — framebuffer graphics and font rendering. */
#ifndef NOVA_FB_H
#define NOVA_FB_H

#include <nova/types.h>
#include <nova/boot.h>

#define FONT_W 8
#define FONT_H 16

struct nova_glyph {
    u16 advance;
    u16 width;
    u32 offset;
};

struct nova_font {
    const struct nova_glyph *glyphs;
    const u8 *bitmap;
    u16 count;
    u16 height;
    u16 baseline;
};

extern const u8 nova_font_mono_8x16[95][16];
extern const struct nova_font nova_font_ui;
extern const struct nova_font nova_font_ui_bold;

void fb_init(struct boot_info *bi);
bool fb_available(void);
u32  fb_width(void);
u32  fb_height(void);
u32  fb_pitch(void);

void fb_clear(u32 color);
void fb_putpixel(i32 x, i32 y, u32 color);
void fb_blend_pixel(i32 x, i32 y, u32 color, u8 alpha);
void fb_fill_rect(i32 x, i32 y, i32 w, i32 h, u32 color);
void fb_fill_round_rect(i32 x, i32 y, i32 w, i32 h, i32 radius, u32 color);
void fb_rect_outline(i32 x, i32 y, i32 w, i32 h, u32 color);
void fb_blend_rect(i32 x, i32 y, i32 w, i32 h, u32 color, u8 alpha);
void fb_hline(i32 x, i32 y, i32 w, u32 color);
void fb_vline(i32 x, i32 y, i32 h, u32 color);
void fb_gradient_v(i32 x, i32 y, i32 w, i32 h, u32 top, u32 bottom);
void fb_blit(i32 x, i32 y, i32 w, i32 h, const u32 *src, u32 src_pitch);
void fb_scroll_up(i32 pixels, u32 fill);

void fb_draw_char(i32 x, i32 y, char ch, u32 color, u32 bg);
void fb_draw_text(i32 x, i32 y, const char *text, u32 color);
void fb_draw_text_len(i32 x, i32 y, const char *text, size_t len, u32 color);
int  fb_text_width(const char *text);
int  fb_text_width_len(const char *text, size_t len);

void fb_draw_char_ui(i32 x, i32 y, char ch, u32 color);
void fb_draw_text_ui(i32 x, i32 y, const char *text, u32 color);
void fb_draw_text_ui_bold(i32 x, i32 y, const char *text, u32 color);
int  fb_ui_text_width(const char *text);
int  fb_ui_text_width_bold(const char *text);

#endif /* NOVA_FB_H */
