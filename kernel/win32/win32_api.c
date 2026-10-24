/* NOVA OS — Win32 personality entry points (API table lives in api_table.c). */
#include <nova/types.h>
#include <nova/win32.h>
#include <nova/log.h>
#include <nova/sched.h>
#include <nova/string.h>

void win32_init(void)
{
    kprintf("[win32] subsystem initialised, %d API entries\n", win32_api_count());
}

long win32_dispatch(struct process *p, struct regs *r, u32 api_id)
{
    (void)p; (void)r; (void)api_id;
    return -38;
}

long win32_window_syscall(struct regs *r, struct process *p)
{
    (void)r; (void)p;
    return -38;
}

long win32_thread_syscall(struct regs *r, struct process *p)
{
    (void)r; (void)p;
    return -38;
}

long win32_getenv_user(struct process *p, const char *name, char *buf, u64 len)
{
    (void)p; (void)name; (void)buf; (void)len;
    return -1;
}

int win32_api_count(void) { return 0; }
const char *win32_api_name(int id) { (void)id; return ""; }
int win32_api_lookup(const char *dll, const char *name) { (void)dll; (void)name; return -1; }
void win32_api_stats(u32 *implemented, u32 *stubbed) { if (implemented) *implemented = 0; if (stubbed) *stubbed = 0; }

struct process *win32_exec(const char *path, const char *cmdline, bool gui_app)
{
    (void)path; (void)cmdline; (void)gui_app;
    return NULL;
}
