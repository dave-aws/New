/* NOVA OS — system call dispatch.
 *
 * Everything a user-mode program asks of the kernel funnels through here.
 * The Win32 personality shares this gate: SYS_WIN32 selects a handler by API
 * id from the table in win32/win32_api.c, which is how imported Win32
 * functions are bound to the subsystem.
 */
#include <nova/types.h>
#include <nova/syscall.h>
#include <nova/trap.h>
#include <nova/sched.h>
#include <nova/mm.h>
#include <nova/log.h>
#include <nova/string.h>
#include <nova/io.h>
#include <nova/console.h>
#include <nova/fb.h>

#define ENOSYS 38
#define EINVAL 22
#define ENOMEM 12

static long sys_write(struct regs *r, struct process *p)
{
    i32 fd = (i32)r->rdi;
    u64 buf = r->rsi;
    u64 len = r->rdx;
    if (!p || !vmm_is_user_range(p->pml4, buf, len, false))
        return -EINVAL;
    const char *data = (const char *)buf;
    if (fd == 1 || fd == 2) {
        console_write_process(p, data, len);
        return (long)len;
    }
    extern long vfs_write_fd(struct process *p, int fd, const void *buf, u64 len);
    return vfs_write_fd(p, fd, data, len);
}

static long sys_vm_alloc(struct process *p, u64 size)
{
    if (!p || p->pml4 == vmm_kernel_pml4())
        return -EINVAL;
    if (size == 0)
        size = PAGE_SIZE;
    size = ALIGN_UP(size, PAGE_SIZE);
    if (size > (64ULL << 20))
        return -EINVAL;
    if (p->heap_base == 0)
        p->heap_base = 0x0000000010000000ULL;
    if (p->heap_top < p->heap_base)
        p->heap_top = p->heap_base;
    u64 base = p->heap_top;
    if (!vmm_alloc_user_range(p->pml4, base, size, PTE_P | PTE_W | PTE_NX))
        return -ENOMEM;
    p->heap_top = base + size;
    return (long)base;
}

static long sys_vm_free(struct process *p, u64 addr, u64 size)
{
    if (!p || !addr)
        return -EINVAL;
    size = ALIGN_UP(size ? size : PAGE_SIZE, PAGE_SIZE);
    vmm_free_user_range(p->pml4, addr, size);
    return 0;
}

struct fb_info_user {
    u32 width, height, pitch, bpp;
    u32 reserved[4];
};

static long sys_fbinfo(struct process *p, u64 ptr)
{
    if (!p || !fb_available())
        return -EINVAL;
    if (!vmm_is_user_range(p->pml4, ptr, sizeof(struct fb_info_user), true))
        return -EINVAL;
    struct fb_info_user *out = (struct fb_info_user *)ptr;
    out->width = fb_width();
    out->height = fb_height();
    out->pitch = fb_pitch();
    out->bpp = 32;
    return 0;
}

u64 syscall_dispatch(struct regs *r)
{
    struct process *p = process_current();
    long ret = -ENOSYS;

    switch (r->rax) {
    case SYS_EXIT:
        if (p) {
            p->exit_code = (int)r->rdi;
            p->state = PROC_ZOMBIE;
        }
        if (current_thread())
            current_thread()->state = THREAD_DEAD;
        set_need_resched();
        return 0;

    case SYS_YIELD:
        set_need_resched();
        return 0;

    case SYS_WRITE:
        ret = sys_write(r, p);
        break;

    case SYS_READ: {
        extern long vfs_read_fd(struct process *p, int fd, void *buf, u64 len);
        if (!p || !vmm_is_user_range(p->pml4, r->rsi, r->rdx, true)) {
            ret = -EINVAL;
            break;
        }
        ret = vfs_read_fd(p, (int)r->rdi, (void *)r->rsi, r->rdx);
        break;
    }

    case SYS_OPEN: {
        extern long vfs_open_user(struct process *p, const char *path);
        if (!p || !vmm_is_user_range(p->pml4, r->rdi, 1, false)) {
            ret = -EINVAL;
            break;
        }
        ret = vfs_open_user(p, (const char *)r->rdi);
        break;
    }

    case SYS_CLOSE: {
        extern long vfs_close_fd(struct process *p, int fd);
        ret = vfs_close_fd(p, (int)r->rdi);
        break;
    }

    case SYS_LSEEK: {
        extern long vfs_seek_fd(struct process *p, int fd, i64 off, int whence);
        ret = vfs_seek_fd(p, (int)r->rdi, (i64)r->rsi, (int)r->rdx);
        break;
    }

    case SYS_SLEEP:
        if (current_thread())
            thread_sleep_ms(r->rdi);
        return 0;

    case SYS_GETPID:
        ret = p ? p->pid : 0;
        break;

    case SYS_TIME_MS:
        ret = (long)(pit_ticks() * 1000 / pit_hz());
        break;

    case SYS_TICKS:
        ret = (long)pit_ticks();
        break;

    case SYS_VMALLOC:
        ret = sys_vm_alloc(p, r->rdi);
        break;

    case SYS_VMFREE:
        ret = sys_vm_free(p, r->rdi, r->rsi);
        break;

    case SYS_FBINFO:
        ret = sys_fbinfo(p, r->rdi);
        break;

    case SYS_DEBUG:
        if (p && vmm_is_user_range(p->pml4, r->rdi, r->rsi, false))
            console_write_process(p, (const char *)r->rdi, r->rsi);
        ret = 0;
        break;

    case SYS_GETENV: {
        extern long win32_getenv_user(struct process *p, const char *name, char *buf, u64 len);
        if (!p || !vmm_is_user_range(p->pml4, r->rdi, 1, false)) {
            ret = -EINVAL;
            break;
        }
        ret = win32_getenv_user(p, (const char *)r->rdi, (char *)r->rsi, r->rdx);
        break;
    }

    case SYS_FS_READDIR:
    case SYS_FS_STAT:
    case SYS_FS_MKDIR:
    case SYS_FS_UNLINK: {
        extern long vfs_syscall(struct regs *r, struct process *p);
        ret = vfs_syscall(r, p);
        break;
    }

    case SYS_WIN_CREATE:
    case SYS_WIN_PRESENT:
    case SYS_WIN_POLL:
    case SYS_WIN_DRAW:
        if (p) {
            extern long win32_window_syscall(struct regs *r, struct process *p);
            ret = win32_window_syscall(r, p);
        } else {
            ret = -EINVAL;
        }
        break;

    case SYS_WIN32:
    case SYS_WIN32_W:
        if (p) {
            extern long win32_dispatch(struct process *p, struct regs *r, u32 api);
            ret = win32_dispatch(p, r, (u32)r->r11);
        } else {
            ret = -EINVAL;
        }
        break;

    case SYS_THREAD_CREATE: {
        extern long win32_thread_syscall(struct regs *r, struct process *p);
        ret = win32_thread_syscall(r, p);
        break;
    }

    default:
        kprintf("[syscall] unknown %lu from pid %d\n", r->rax, p ? p->pid : -1);
        ret = -ENOSYS;
        break;
    }

    return (u64)ret;
}
