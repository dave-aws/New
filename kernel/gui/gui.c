/* NOVA OS — minimal desktop bring-up (full shell in gui/shell.c). */
#include <nova/types.h>
#include <nova/gui.h>
#include <nova/fb.h>
#include <nova/log.h>
#include <nova/sched.h>
#include <nova/string.h>
#include <nova/console.h>
#include <nova/mm.h>

static bool ready;

bool gui_ready(void) { return ready; }
u32 gui_width(void) { return fb_width(); }
u32 gui_height(void) { return fb_height(); }

void gui_init(void)
{
    if (!fb_available())
        return;
    ready = true;
    fb_gradient_v(0, 0, (i32)fb_width(), (i32)fb_height(), 0xFF0B1B2A, 0xFF12324A);
    fb_fill_rect(0, (i32)fb_height() - 40, (i32)fb_width(), 40, 0xF01A1F26);
    fb_fill_round_rect(12, (i32)fb_height() - 34, 84, 28, 6, 0xFF2D6CDF);
    fb_draw_text_ui(34, (i32)fb_height() - 16, "Start", 0xFFFFFFFF);
    fb_draw_text_ui((i32)fb_width() - 330, (i32)fb_height() - 16,
                    "NOVA OS 0.1  |  Win32 subsystem online", 0xFFB8C6D6);
    kprintf("[gui] desktop initialised (%ux%u)\n", fb_width(), fb_height());
}

void gui_run(void)
{
    for (;;)
        thread_sleep_ms(500);
}

struct gui_window *gui_window_create(struct process *owner, const char *title,
                                     i32 x, i32 y, i32 w, i32 h, u32 style)
{
    (void)owner; (void)title; (void)x; (void)y; (void)w; (void)h; (void)style;
    return NULL;
}
void gui_window_destroy(struct gui_window *win) { (void)win; }
void gui_window_present(struct gui_window *win) { (void)win; }
void gui_window_set_title(struct gui_window *win, const char *title) { (void)win; (void)title; }
void gui_window_invalidate(struct gui_window *win) { (void)win; }
void gui_window_get_client_rect(struct gui_window *win, i32 *x, i32 *y, i32 *w, i32 *h)
{ (void)win; if (x) *x = 0; if (y) *y = 0; if (w) *w = 0; if (h) *h = 0; }
u32 *gui_window_surface(struct gui_window *win) { (void)win; return NULL; }
void gui_window_post(struct gui_window *win, u32 msg, u64 wparam, u64 lparam)
{ (void)win; (void)msg; (void)wparam; (void)lparam; }
int gui_window_wait_event(struct gui_window *win, struct gui_event *ev, bool blocking)
{ (void)win; (void)ev; (void)blocking; return -1; }
struct gui_window *gui_window_from_id(int id) { (void)id; return NULL; }
struct gui_window *gui_focused_window(void) { return NULL; }
void gui_surface_fill(struct gui_window *win, i32 x, i32 y, i32 w, i32 h, u32 color)
{ (void)win; (void)x; (void)y; (void)w; (void)h; (void)color; }
void gui_surface_text(struct gui_window *win, i32 x, i32 y, const char *text, u32 color)
{ (void)win; (void)x; (void)y; (void)text; (void)color; }
void gui_surface_text_ui(struct gui_window *win, i32 x, i32 y, const char *text, u32 color)
{ (void)win; (void)x; (void)y; (void)text; (void)color; }
void gui_message_box(struct process *p, const char *caption, const char *text)
{ (void)p; kprintf("[gui] %s: %s\n", caption, text); }
void gui_start_app(const char *exe_path, const char *args) { (void)exe_path; (void)args; }
void gui_shell_launch_terminal(struct console *console) { (void)console; }
void gui_shell_toggle_start(void) {}
