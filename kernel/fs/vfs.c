/* NOVA OS — VFS glue: per-process descriptors over NOVA FS. */
#include <nova/types.h>
#include <nova/fs.h>
#include <nova/sched.h>
#include <nova/mm.h>
#include <nova/log.h>
#include <nova/string.h>
#include <nova/console.h>
#include <nova/trap.h>
#include <nova/syscall.h>

#define MAX_FDS 32

struct vfs_file {
    struct fs_node node;
    u64 pos;
    bool writable;
};

static int alloc_fd(struct process *p, void *file)
{
    for (int i = 3; i < MAX_FDS; i++) {
        if (!p->fds[i]) {
            p->fds[i] = file;
            return i;
        }
    }
    return -1;
}

long vfs_open_user(struct process *p, const char *path)
{
    struct fs_node node;
    char norm[FS_PATH_MAX];
    fs_normalize(path, norm, sizeof(norm));
    if (fs_lookup(norm, &node) != 0 || node.type != FS_TYPE_FILE)
        return -2;   /* FILE_NOT_FOUND */
    struct vfs_file *f = kzalloc(sizeof(*f));
    if (!f)
        return -12;
    f->node = node;
    int fd = alloc_fd(p, f);
    if (fd < 0) {
        kfree(f);
        return -24;  /* EMFILE */
    }
    return fd;
}

long vfs_read_fd(struct process *p, int fd, void *buf, u64 len)
{
    if (!p || fd < 0 || fd >= MAX_FDS)
        return -9;
    if (fd == 0) {
        struct console *c = console_of_process(p);
        int n = console_read(c, buf, (int)len, true);
        return n < 0 ? -5 : n;
    }
    struct vfs_file *f = p->fds[fd];
    if (!f)
        return -9;
    int n = fs_read(&f->node, f->pos, buf, len);
    if (n > 0)
        f->pos += (u64)n;
    return n;
}

long vfs_write_fd(struct process *p, int fd, const void *buf, u64 len)
{
    (void)buf;
    if (!p || fd < 0 || fd >= MAX_FDS)
        return -9;
    if (fd == 1 || fd == 2) {
        console_write_process(p, buf, len);
        return (long)len;
    }
    return -30;  /* read-only filesystem in 0.1 */
}

long vfs_close_fd(struct process *p, int fd)
{
    if (!p || fd < 3 || fd >= MAX_FDS)
        return -9;
    kfree(p->fds[fd]);
    p->fds[fd] = NULL;
    return 0;
}

long vfs_seek_fd(struct process *p, int fd, i64 off, int whence)
{
    if (!p || fd < 0 || fd >= MAX_FDS)
        return -9;
    struct vfs_file *f = p->fds[fd];
    if (!f)
        return -9;
    i64 base = 0;
    if (whence == 1)
        base = (i64)f->pos;
    else if (whence == 2)
        base = (i64)f->node.size;
    i64 pos = base + off;
    if (pos < 0)
        pos = 0;
    if ((u64)pos > f->node.size)
        pos = (i64)f->node.size;
    f->pos = (u64)pos;
    return (long)f->pos;
}

/* SYS_FS_READDIR / SYS_FS_STAT / SYS_FS_MKDIR / SYS_FS_UNLINK */
long vfs_syscall(struct regs *r, struct process *p)
{
    if (!p)
        return -22;
    switch (r->rax) {
    case SYS_FS_READDIR: {
        const char *path = (const char *)r->rdi;
        struct fs_node *out = (struct fs_node *)r->rsi;
        u32 index = (u32)r->rdx;
        if (!vmm_is_user_range(p->pml4, r->rdi, 1, false) ||
            !vmm_is_user_range(p->pml4, r->rsi, sizeof(struct fs_node), true))
            return -22;
        struct fs_node dir;
        if (fs_lookup(path, &dir) != 0 || dir.type != FS_TYPE_DIR)
            return -2;
        if (fs_readdir(&dir, index, out) != 0)
            return 0;    /* end of directory */
        return 1;
    }
    case SYS_FS_STAT: {
        const char *path = (const char *)r->rdi;
        struct fs_node *out = (struct fs_node *)r->rsi;
        if (!vmm_is_user_range(p->pml4, r->rdi, 1, false) ||
            !vmm_is_user_range(p->pml4, r->rsi, sizeof(struct fs_node), true))
            return -22;
        return fs_lookup(path, out) == 0 ? 0 : -2;
    }
    case SYS_FS_MKDIR:
    case SYS_FS_UNLINK:
        return -30;    /* read-only in 0.1 */
    }
    return -38;
}
