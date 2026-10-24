/* NOVA OS — Win32 compatibility subsystem. */
#ifndef NOVA_WIN32_H
#define NOVA_WIN32_H

#include <nova/types.h>
#include <nova/trap.h>
#include <nova/sched.h>

struct gui_window;
struct pe_image;

#define WIN32_MAX_HANDLES 96
#define WIN32_MAX_DLLS    16
#define WIN32_MAX_WINDOWS 16

enum win32_obj_type {
    W32_OBJ_NONE = 0,
    W32_OBJ_FILE,
    W32_OBJ_THREAD,
    W32_OBJ_EVENT,
    W32_OBJ_MUTEX,
    W32_OBJ_SEMAPHORE,
    W32_OBJ_WINDOW,
    W32_OBJ_HEAP,
    W32_OBJ_LIBRARY,
};

struct win32_object {
    u32 type;
    u32 flags;
    void *ptr;
    char name[48];
    int refs;
};

struct win32_message {
    u64 hwnd;
    u32 msg;
    u64 wparam;
    u64 lparam;
    u32 time;
    i32 x, y;
};

struct win32_process {
    struct process *proc;
    struct pe_image *main_image;
    struct pe_image *dlls[WIN32_MAX_DLLS];
    int dll_count;
    u64 image_base;
    u64 image_size;
    u64 entry_point;
    u64 process_heap;
    u64 env_block;
    u64 env_block_w;
    u64 command_line;
    u64 command_line_w;
    u64 startup_info;
    int argc;
    char exe_path[256];
    int last_error;
    u32 tls[64];
    bool tls_used[64];

    struct win32_object objects[WIN32_MAX_HANDLES];
    int object_count;

    struct gui_window *windows[WIN32_MAX_WINDOWS];
    int window_count;
    u64 focus_hwnd;
    struct win32_message msg_queue[64];
    int msg_head, msg_tail;
    u32 quit_posted;
    u32 exit_code;
    u32 next_window_id;
    bool console_app;
};

void  win32_init(void);
long  win32_dispatch(struct process *p, struct regs *r, u32 api_id);
long  win32_window_syscall(struct regs *r, struct process *p);
long  win32_thread_syscall(struct regs *r, struct process *p);
long  win32_getenv_user(struct process *p, const char *name, char *buf, u64 len);

/* Process creation from a PE file. Returns the new process or NULL. */
struct process *win32_exec(const char *path, const char *cmdline, bool gui_app);

/* Handle table helpers shared with the API implementation. */
u64  win32_handle_alloc(struct win32_process *wp, u32 type, void *ptr);
void *win32_handle_get(struct win32_process *wp, u64 handle, u32 type);
void win32_handle_close(struct win32_process *wp, u64 handle);

/* Resolve imported names to Win32 personality API ids. */
int  win32_api_lookup(const char *dll, const char *name);
const char *win32_api_name(int id);
int  win32_api_count(void);
void win32_api_stats(u32 *implemented, u32 *stubbed);

#endif /* NOVA_WIN32_H */
