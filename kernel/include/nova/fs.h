/* NOVA OS — NOVA FS and the VFS layer. */
#ifndef NOVA_FS_H
#define NOVA_FS_H

#include <nova/types.h>

#define FS_TYPE_FILE 1
#define FS_TYPE_DIR  2

#define FS_NAME_MAX 64
#define FS_PATH_MAX 256

struct fs_node {
    u32 ino;
    u32 type;
    u64 size;
    u64 mtime;
    char name[FS_NAME_MAX];
};

struct process;
struct regs;

int  vfs_init(void);
u32  fs_inode_count(void);
u64  fs_free_blocks(void);
void fs_dump_root(void);

const char *fs_normalize(const char *path, char *out, size_t outsz);
int  fs_lookup(const char *path, struct fs_node *out);
int  fs_read(const struct fs_node *node, u64 off, void *buf, u64 len);
int  fs_readdir(const struct fs_node *dir, u32 index, struct fs_node *out);
int  fs_read_file(const char *path, void **buf, u64 *size);
int  fs_stat_path(const char *path, struct fs_node *out);

/* Per-process file descriptors (NOVA native syscalls). */
long vfs_open_user(struct process *p, const char *path);
long vfs_read_fd(struct process *p, int fd, void *buf, u64 len);
long vfs_write_fd(struct process *p, int fd, const void *buf, u64 len);
long vfs_close_fd(struct process *p, int fd);
long vfs_seek_fd(struct process *p, int fd, i64 off, int whence);
long vfs_syscall(struct regs *r, struct process *p);

#endif /* NOVA_FS_H */
