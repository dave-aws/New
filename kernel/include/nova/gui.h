/* NOVA OS — window manager and desktop shell. */
#ifndef NOVA_GUI_H
#define NOVA_GUI_H

#include <nova/types.h>
#include <nova/sched.h>

struct console;

#define GUI_MAX_WINDOWS 16
#define GUI_TITLE_H 30
#define GUI_EVENT_QUEUE 64

/* Window styles (Win32 compatible bits) */
#define WS_OVERLAPPED   0x00000000
#define WS_CAPTION      0x00C00000
#define WS_SYSMENU      0x00080000
#define WS_MINIMIZEBOX  0x00020000
#define WS_MAXIMIZEBOX  0x00010000
#define WS_THICKFRAME   0x00040000
#define WS_VISIBLE      0x10000000
#define WS_CHILD        0x40000000

/* Messages delivered to Win32 windows. */
#define WM_CREATE       0x0001
#define WM_DESTROY      0x0002
#define WM_SIZE         0x0005
#define WM_PAINT        0x000F
#define WM_CLOSE        0x0010
#define WM_QUIT         0x0012
#define WM_KEYDOWN      0x0100
#define WM_KEYUP        0x0101
#define WM_CHAR         0x0102
#define WM_MOUSEMOVE    0x0200
#define WM_LBUTTONDOWN  0x0201
#define WM_LBUTTONUP    0x0202
#define WM_TIMER        0x0113

struct gui_event {
    u32 msg;
    u64 wparam;
    u64 lparam;
    u32 time;
};

struct gui_window {
    int id;
    struct process *owner;
    char title[128];
    i32 x, y, w, h;
    u32 style;
    bool visible;
    bool minimized;
    bool maximized;
    bool dirty;
    bool close_requested;
    u32 *surface;              /* client-area back buffer, w*h pixels */
    u32 *saved_rect;           /* for minimise/restore */
    i32 saved_x, saved_y, saved_w, saved_h;
    struct gui_event events[GUI_EVENT_QUEUE];
    u32 ev_head, ev_tail;
    void *user;                /* Win32 window object */
    int z;
};

void gui_init(void);
void gui_run(void) noreturn;
bool gui_ready(void);
u32  gui_width(void);
u32  gui_height(void);

struct gui_window *gui_window_create(struct process *owner, const char *title,
                                     i32 x, i32 y, i32 w, i32 h, u32 style);
void gui_window_destroy(struct gui_window *win);
void gui_window_present(struct gui_window *win);
void gui_window_set_title(struct gui_window *win, const char *title);
void gui_window_invalidate(struct gui_window *win);
void gui_window_get_client_rect(struct gui_window *win, i32 *x, i32 *y, i32 *w, i32 *h);
u32 *gui_window_surface(struct gui_window *win);
void gui_window_post(struct gui_window *win, u32 msg, u64 wparam, u64 lparam);
int  gui_window_wait_event(struct gui_window *win, struct gui_event *ev, bool blocking);
struct gui_window *gui_window_from_id(int id);
struct gui_window *gui_focused_window(void);

/* Minimal GDI used by the Win32 subsystem. */
void gui_surface_fill(struct gui_window *win, i32 x, i32 y, i32 w, i32 h, u32 color);
void gui_surface_text(struct gui_window *win, i32 x, i32 y, const char *text, u32 color);
void gui_surface_text_ui(struct gui_window *win, i32 x, i32 y, const char *text, u32 color);

void gui_message_box(struct process *p, const char *caption, const char *text);
void gui_start_app(const char *exe_path, const char *args);

/* Desktop shell helpers (terminal, file manager, settings). */
void gui_shell_launch_terminal(struct console *console);
void gui_shell_toggle_start(void);

#endif /* NOVA_GUI_H */
