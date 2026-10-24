/* NOVA OS — kernel heap.
 *
 * A first-fit free list with splitting and coalescing over a lazily mapped
 * 128 MiB arena at HEAP_BASE.  Blocks are 16-byte aligned; the allocator is
 * protected by the caller (kernel allocations happen with IRQs disabled in
 * the paths that can race with the timer).
 */
#include <nova/types.h>
#include <nova/mm.h>
#include <nova/log.h>
#include <nova/string.h>
#include <nova/io.h>

#define BLOCK_MAGIC 0x4E4F5648B10C4B55ULL

struct block {
    u64 magic;
    u32 size;          /* payload bytes */
    u32 free;
    struct block *next;
    struct block *prev;
};

_Static_assert(sizeof(struct block) == 32, "heap block header size");

static struct block *head;
static u64 arena_mapped;
static u64 heap_used;
static u64 heap_pages;

#define HEAP_CHUNK (2ULL * 1024 * 1024)

static bool heap_map_more(u64 bytes)
{
    u64 need = ALIGN_UP(bytes, HEAP_CHUNK);
    if (arena_mapped + need > HEAP_SIZE)
        return false;
    for (u64 off = arena_mapped; off < arena_mapped + need; off += PAGE_SIZE) {
        u64 page = pmm_alloc_page();
        if (!page)
            return false;
        memset(PHYS_TO_VIRT(page), 0, PAGE_SIZE);
        if (!vmm_map(vmm_kernel_pml4(), HEAP_BASE + off, page, PTE_P | PTE_W | PTE_NX)) {
            pmm_free_page(page);
            return false;
        }
        heap_pages++;
    }
    arena_mapped += need;
    return true;
}

void heap_init(void)
{
    heap_map_more(HEAP_CHUNK);
    head = (struct block *)HEAP_BASE;
    head->magic = BLOCK_MAGIC;
    head->size = (u32)(arena_mapped - sizeof(struct block));
    head->free = 1;
    head->next = NULL;
    head->prev = NULL;
    heap_used = 0;
    kprintf("[heap] %lu MiB arena at %#lx\n", HEAP_SIZE >> 20, (u64)HEAP_BASE);
}

static struct block *split(struct block *b, u32 want)
{
    if (b->size < want + sizeof(struct block) + 16)
        return b;
    struct block *nb = (struct block *)((u8 *)b + sizeof(struct block) + want);
    nb->magic = BLOCK_MAGIC;
    nb->size = b->size - want - (u32)sizeof(struct block);
    nb->free = 1;
    nb->next = b->next;
    nb->prev = b;
    if (b->next)
        b->next->prev = nb;
    b->next = nb;
    b->size = want;
    return b;
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return NULL;
    u32 want = (u32)ALIGN_UP(size, 16);
    u64 flags = irq_save();

    for (;;) {
        for (struct block *b = head; b; b = b->next) {
            if (!b->free || b->size < want)
                continue;
            split(b, want);
            b->free = 0;
            heap_used += b->size + sizeof(struct block);
            irq_restore(flags);
            return (u8 *)b + sizeof(struct block);
        }
        /* Grow the arena and retry: the tail block is then extended in
         * place by linking a new block to it. */
        u64 before = arena_mapped;
        if (!heap_map_more(HEAP_CHUNK)) {
            irq_restore(flags);
            return NULL;
        }
        struct block *tail = NULL;
        for (struct block *b = head; b; b = b->next)
            tail = b;
        if (!tail) {
            irq_restore(flags);
            return NULL;
        }
        if (tail->free && ((u8 *)tail + sizeof(struct block) + tail->size) ==
                          (u8 *)(HEAP_BASE + before)) {
            tail->size += (u32)(arena_mapped - before);
        } else {
            struct block *nb = (struct block *)(HEAP_BASE + before);
            nb->magic = BLOCK_MAGIC;
            nb->size = (u32)(arena_mapped - before - sizeof(struct block));
            nb->free = 1;
            nb->next = NULL;
            nb->prev = tail;
            tail->next = nb;
        }
    }
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void *kcalloc(size_t n, size_t size)
{
    if (n && size > (size_t)-1 / n)
        return NULL;
    return kzalloc(n * size);
}

void kfree(void *ptr)
{
    if (!ptr)
        return;
    struct block *b = (struct block *)((u8 *)ptr - sizeof(struct block));
    if (b->magic != BLOCK_MAGIC) {
        kprintf("[heap] kfree of invalid pointer %p\n", ptr);
        return;
    }
    u64 flags = irq_save();
    if (!b->free) {
        b->free = 1;
        if (b->size + sizeof(struct block) <= heap_used)
            heap_used -= b->size + sizeof(struct block);
    }
    /* Coalesce forward. */
    if (b->next && b->next->free) {
        struct block *n = b->next;
        b->size += (u32)sizeof(struct block) + n->size;
        b->next = n->next;
        if (n->next)
            n->next->prev = b;
        n->magic = 0;
    }
    /* Coalesce backward. */
    if (b->prev && b->prev->free) {
        struct block *p = b->prev;
        p->size += (u32)sizeof(struct block) + b->size;
        p->next = b->next;
        if (b->next)
            b->next->prev = p;
        b->magic = 0;
    }
    irq_restore(flags);
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    struct block *b = (struct block *)((u8 *)ptr - sizeof(struct block));
    if (b->magic != BLOCK_MAGIC)
        return NULL;
    if (b->size >= size)
        return ptr;

    void *n = kmalloc(size);
    if (!n)
        return NULL;
    memcpy(n, ptr, b->size);
    kfree(ptr);
    return n;
}

char *kstrdup(const char *s)
{
    if (!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *d = kmalloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

void heap_stats(u64 *used, u64 *free_bytes, u64 *pages)
{
    if (used) *used = heap_used;
    if (free_bytes) *free_bytes = arena_mapped - heap_used;
    if (pages) *pages = heap_pages;
}
