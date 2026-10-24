/* NOVA OS — NOVA FS on-disk driver.
 *
 * Block      0        superblock
 *            1..N     inode table, 32 x 128-byte inodes per 4 KiB block
 *            N+1      allocation bitmap
 *            ...      data blocks (10 direct + single + double indirect)
 *
 * Reads go through the ATA PIO driver; the mount is read-only in 0.1 and
 * writes are rejected cleanly (the FS layout itself is read/write ready).
 */
#include <nova/types.h>
#include <nova/fs.h>
#include <nova/ata.h>
#include <nova/log.h>
#include <nova/string.h>
#include <nova/mm.h>

#define BLOCK_SIZE 4096
#define INODE_SIZE 128
#define DIRENT_SIZE 64
#define MAX_DIRECT 10
#define NOVA_FS_MAGIC "NOVAFS\0\0"

struct fs_super {
    char magic[8];
    u32 version;
    u32 block_size;
    u64 total_blocks;
    u64 root_ino;
    u64 inode_count;
    u64 first_inode_block;
    u64 inode_blocks;
    u64 bitmap_block;
    u64 bitmap_blocks;
    u64 data_start;
    u64 free_blocks;
    u64 feature_flags;
    u8 uuid[16];
    u8 label[32];
} packed;

struct fs_inode_disk {
    u16 mode;
    u16 links;
    u32 flags;
    u64 size;
    u64 blocks[MAX_DIRECT];
    u64 indirect;
    u64 dindirect;
    u64 mtime;
    u16 uid;
    u16 gid;
    u32 reserved;
} packed;

struct fs_dirent {
    u32 ino;
    u32 type;
    u8 name_len;
    u8 reserved[3];
    char name[52];
} packed;

_Static_assert(sizeof(struct fs_super) <= BLOCK_SIZE, "superblock fits a block");
_Static_assert(sizeof(struct fs_inode_disk) == 128, "inode is 128 bytes");
_Static_assert(sizeof(struct fs_dirent) == DIRENT_SIZE, "dirent is 64 bytes");

static struct fs_super sb;
static bool mounted;

static int read_block(u64 rel_block, void *buf)
{
    if (rel_block >= sb.total_blocks)
        return -1;
    u64 lba = ata_part_lba() + (rel_block * (BLOCK_SIZE / 512));
    return ata_read(lba, BLOCK_SIZE / 512, buf);
}

static void inode_to_node(const struct fs_inode_disk *d, u32 ino, struct fs_node *out)
{
    memset(out, 0, sizeof(*out));
    out->ino = ino;
    out->type = (d->mode & 0x4000) ? FS_TYPE_DIR : FS_TYPE_FILE;
    out->size = d->size;
    out->mtime = d->mtime;
}

static int read_inode(u32 ino, struct fs_inode_disk *out)
{
    if (!mounted || ino == 0 || ino > sb.inode_count)
        return -1;
    u64 index = ino - 1;
    u64 block = sb.first_inode_block + index / (BLOCK_SIZE / INODE_SIZE);
    u64 offset = (index % (BLOCK_SIZE / INODE_SIZE)) * INODE_SIZE;
    u8 *buf = kmalloc(BLOCK_SIZE);
    if (!buf)
        return -1;
    int rc = read_block(block, buf);
    if (rc == 0)
        memcpy(out, buf + offset, INODE_SIZE);
    kfree(buf);
    return rc;
}

/* Map a file-relative block to an absolute FS block. */
static u64 inode_block(const struct fs_inode_disk *inode, u64 index)
{
    if (index < MAX_DIRECT)
        return inode->blocks[index];

    index -= MAX_DIRECT;
    u64 per = BLOCK_SIZE / 8;
    if (index < per) {
        if (!inode->indirect)
            return 0;
        u64 *tbl = kmalloc(BLOCK_SIZE);
        if (!tbl)
            return 0;
        u64 b = 0;
        if (read_block(inode->indirect, tbl) == 0)
            b = tbl[index];
        kfree(tbl);
        return b;
    }

    index -= per;
    if (index >= per * per || !inode->dindirect)
        return 0;
    u64 *l1 = kmalloc(BLOCK_SIZE);
    u64 *l2 = kmalloc(BLOCK_SIZE);
    u64 b = 0;
    if (l1 && l2 && read_block(inode->dindirect, l1) == 0 && l1[index / per]) {
        if (read_block(l1[index / per], l2) == 0)
            b = l2[index % per];
    }
    kfree(l1);
    kfree(l2);
    return b;
}

int fs_read(const struct fs_node *node, u64 off, void *buf, u64 len)
{
    if (!node || node->type != FS_TYPE_FILE || off >= node->size)
        return 0;
    if (len > node->size - off)
        len = node->size - off;

    struct fs_inode_disk inode;
    if (read_inode(node->ino, &inode) != 0)
        return -1;

    u8 *dst = buf;
    u64 copied = 0;
    u8 *tmp = kmalloc(BLOCK_SIZE);
    if (!tmp)
        return -1;
    while (copied < len) {
        u64 index = (off + copied) / BLOCK_SIZE;
        u64 boff = (off + copied) % BLOCK_SIZE;
        u64 chunk = MIN(BLOCK_SIZE - boff, len - copied);
        u64 blk = inode_block(&inode, index);
        if (!blk)
            break;
        if (read_block(blk, tmp) != 0)
            break;
        memcpy(dst + copied, tmp + boff, chunk);
        copied += chunk;
    }
    kfree(tmp);
    return (int)copied;
}

int fs_readdir(const struct fs_node *dir, u32 index, struct fs_node *out)
{
    if (!dir || dir->type != FS_TYPE_DIR)
        return -1;
    u32 seen = 0;
    struct fs_inode_disk inode;
    if (read_inode(dir->ino, &inode) != 0)
        return -1;

    u8 *block = kmalloc(BLOCK_SIZE);
    if (!block)
        return -1;

    int rc = -1;
    u64 nblocks = (inode.size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (u64 b = 0; b < nblocks; b++) {
        u64 blk = inode_block(&inode, b);
        if (!blk || read_block(blk, block) != 0)
            break;
        for (u32 off = 0; off + DIRENT_SIZE <= BLOCK_SIZE && b * BLOCK_SIZE + off < inode.size;
             off += DIRENT_SIZE) {
            struct fs_dirent *de = (struct fs_dirent *)(block + off);
            if (!de->ino || de->name_len == 0)
                continue;
            if (!strcmp(de->name, ".") || !strcmp(de->name, ".."))
                continue;
            if (seen++ == index) {
                struct fs_inode_disk child;
                if (read_inode(de->ino, &child) != 0)
                    goto out;
                inode_to_node(&child, de->ino, out);
                strncpy(out->name, de->name, FS_NAME_MAX - 1);
                out->name[FS_NAME_MAX - 1] = 0;
                rc = 0;
                goto out;
            }
        }
    }
out:
    kfree(block);
    return rc;
}

static int find_child(u32 dir_ino, const char *name, struct fs_node *out)
{
    struct fs_node dir;
    struct fs_inode_disk d;
    if (read_inode(dir_ino, &d) != 0)
        return -1;
    inode_to_node(&d, dir_ino, &dir);

    for (u32 i = 0;; i++) {
        struct fs_node child;
        if (fs_readdir(&dir, i, &child) != 0)
            break;
        if (!strcasecmp(child.name, name)) {
            if (out)
                *out = child;
            return 0;
        }
    }
    return -1;
}

const char *fs_normalize(const char *path, char *out, size_t outsz)
{
    if (!path || !out || outsz < 2)
        return NULL;
    size_t o = 0;
    const char *p = path;

    if (p[0] && p[1] == ':')
        p += 2;
    while (*p == '\\' || *p == '/')
        p++;
    out[o++] = '\\';

    while (*p && o < outsz - 1) {
        if (*p == '/' || *p == '\\') {
            while (*p == '/' || *p == '\\')
                p++;
            if (!*p)
                break;
            if (o > 1 && out[o - 1] != '\\')
                out[o++] = '\\';
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = 0;
    return out;
}

int fs_lookup(const char *path, struct fs_node *out)
{
    if (!mounted)
        return -1;
    char norm[FS_PATH_MAX];
    if (!fs_normalize(path, norm, sizeof(norm)))
        return -1;

    struct fs_node node;
    struct fs_inode_disk root;
    if (read_inode((u32)sb.root_ino, &root) != 0)
        return -1;
    inode_to_node(&root, (u32)sb.root_ino, &node);

    char name[FS_NAME_MAX];
    const char *p = norm;
    while (*p) {
        while (*p == '\\')
            p++;
        if (!*p)
            break;
        size_t n = 0;
        while (p[n] && p[n] != '\\' && n < FS_NAME_MAX - 1) {
            name[n] = p[n];
            n++;
        }
        name[n] = 0;
        p += n;

        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;

        struct fs_node child;
        if (find_child(node.ino, name, &child) != 0)
            return -1;
        node = child;
    }
    if (out)
        *out = node;
    return 0;
}

int fs_read_file(const char *path, void **buf, u64 *size)
{
    struct fs_node node;
    if (fs_lookup(path, &node) != 0 || node.type != FS_TYPE_FILE)
        return -1;
    if (node.size > (32ULL << 20))
        return -1;
    u8 *data = kmalloc((size_t)node.size + 1);
    if (!data)
        return -1;
    int n = fs_read(&node, 0, data, node.size);
    if (n < 0) {
        kfree(data);
        return -1;
    }
    data[n] = 0;
    if (buf)
        *buf = data;
    if (size)
        *size = (u64)n;
    return 0;
}

int fs_stat_path(const char *path, struct fs_node *out)
{
    return fs_lookup(path, out);
}

u32 fs_inode_count(void) { return mounted ? (u32)sb.inode_count : 0; }
u64 fs_free_blocks(void) { return mounted ? sb.free_blocks : 0; }

int vfs_init(void)
{
    u8 *buf = kmalloc(BLOCK_SIZE);
    if (!buf)
        return -1;
    u64 lba = ata_part_lba();
    if (ata_read(lba, BLOCK_SIZE / 512, buf) != 0) {
        kfree(buf);
        return -1;
    }
    memcpy(&sb, buf, sizeof(sb));
    kfree(buf);
    if (memcmp(sb.magic, NOVA_FS_MAGIC, 8) != 0 || sb.block_size != BLOCK_SIZE) {
        kprintf("[novafs] bad superblock magic\n");
        return -1;
    }
    mounted = true;
    kprintf("[novafs] '%s': %lu blocks, %lu inodes, %lu free\n",
            (const char *)sb.label, sb.total_blocks, sb.inode_count, sb.free_blocks);
    return 0;
}

void fs_dump_root(void)
{
    struct fs_node root;
    struct fs_inode_disk d;
    if (!mounted || read_inode((u32)sb.root_ino, &d) != 0)
        return;
    inode_to_node(&d, (u32)sb.root_ino, &root);
    for (u32 i = 0; i < 64; i++) {
        struct fs_node child;
        if (fs_readdir(&root, i, &child) != 0)
            break;
        kprintf("  C:\\%-24s %s %lu bytes\n", child.name,
                child.type == FS_TYPE_DIR ? "<DIR>" : "     ", child.size);
    }
}
