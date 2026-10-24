/* NOVA OS — physical memory manager.
 *
 * A bitmap of 4 KiB frames built from the BIOS E820 map.  Everything the
 * firmware does not report as usable RAM, plus the kernel image itself, is
 * marked reserved.  No buddy allocator yet: a bitmap is compact (256 KiB for
 * 8 GiB), allocation is O(n/64) with a rolling hint, and free is O(1).
 */
#include <nova/types.h>
#include <nova/mm.h>
#include <nova/boot.h>
#include <nova/log.h>
#include <nova/string.h>

#define MAX_PAGES (PMM_MAX_PHYS / PAGE_SIZE)

static u8  bitmap[MAX_PAGES / 8];
static u64 total_bytes;
static u64 free_pages;
static u64 search_hint;

extern char __kernel_end[];

static inline bool page_used(u64 idx) { return bitmap[idx >> 3] & (1u << (idx & 7)); }

static void mark_used(u64 idx)
{
    if (!page_used(idx)) {
        bitmap[idx >> 3] |= (u8)(1u << (idx & 7));
        free_pages--;
    }
}

static void mark_free(u64 idx)
{
    if (page_used(idx)) {
        bitmap[idx >> 3] &= (u8)~(1u << (idx & 7));
        free_pages++;
    }
}

static void mark_range(u64 start, u64 end, bool used)
{
    u64 first = ALIGN_DOWN(start, PAGE_SIZE) / PAGE_SIZE;
    u64 last = ALIGN_UP(end, PAGE_SIZE) / PAGE_SIZE;
    if (first > MAX_PAGES)
        return;
    if (last > MAX_PAGES)
        last = MAX_PAGES;
    for (u64 i = first; i < last; i++) {
        if (used)
            mark_used(i);
        else
            mark_free(i);
    }
}

void pmm_init(struct boot_info *bi)
{
    kprintf("[pmm] init, mmap at %#lx count %u\n", bi->mmap_addr, bi->mmap_count);
    memset(bitmap, 0xFF, sizeof(bitmap));
    kprintf("[pmm] bitmap cleared\n");   /* start fully reserved */
    free_pages = 0;
    total_bytes = 0;
    search_hint = 0;

    struct e820_entry {
        u64 base;
        u64 length;
        u32 type;
        u32 acpi;
    } packed;

    /* Early boot: the identity map is still active and the direct physical
     * map does not exist until vmm_init(). */
    const struct e820_entry *mm = (const struct e820_entry *)(u64)bi->mmap_addr;
    for (u32 i = 0; i < bi->mmap_count; i++) {
        u64 start = mm[i].base;
        u64 end = mm[i].base + mm[i].length;
        if (mm[i].type == 1 && mm[i].length) {
            if (end > PMM_MAX_PHYS)
                end = PMM_MAX_PHYS;
            if (end > start) {
                mark_range(start, end, false);
                total_bytes += end - start;
                kprintf("[pmm] usable %#lx-%#lx (%lu MiB)\n",
                        start, end, (end - start) >> 20);
            }
        }
    }

    kprintf("[pmm] e820 walk done, %lu MiB free\n", (free_pages * PAGE_SIZE) >> 20);
    /* Low 1 MiB: IVT, BDA, EBDA, boot loader, BIOS tables. */
    mark_range(0, 0x100000, true);
    /* Kernel image including .bss. */
    mark_range(0x100000, (u64)__kernel_end - (u64)KERNEL_BASE, true);

    kprintf("[pmm] %lu MiB total, %lu MiB free, bitmap %lu KiB\n",
            total_bytes >> 20, (free_pages * PAGE_SIZE) >> 20,
            (u64)sizeof(bitmap) >> 10);
}

void pmm_reserve_range(u64 start, u64 end)
{
    mark_range(start, end, true);
}

static u64 find_run(u64 count)
{
    if (count == 0 || count > free_pages)
        return 0;
    for (u64 start = search_hint; start + count <= MAX_PAGES; ) {
        if (page_used(start)) {
            start++;
            continue;
        }
        u64 run = 0;
        while (run < count && start + run < MAX_PAGES && !page_used(start + run))
            run++;
        if (run == count)
            return start;
        start += run + 1;
    }
    /* wrap once */
    if (search_hint != 0) {
        search_hint = 0;
        return find_run(count);
    }
    return 0;
}

u64 pmm_alloc_pages(u64 count)
{
    u64 start = find_run(count);
    if (!start)
        return 0;
    for (u64 i = 0; i < count; i++)
        mark_used(start + i);
    search_hint = start + count;
    if (search_hint >= MAX_PAGES)
        search_hint = 0;
    return start * PAGE_SIZE;      /* physical address */
}

u64 pmm_alloc_page(void)
{
    return pmm_alloc_pages(1);
}

void pmm_free_pages(u64 phys, u64 count)
{
    u64 start = phys / PAGE_SIZE;
    for (u64 i = 0; i < count; i++) {
        if (start + i < MAX_PAGES)
            mark_free(start + i);
    }
    if (start < search_hint)
        search_hint = start;
}

void pmm_free_page(u64 phys)
{
    pmm_free_pages(phys, 1);
}

u64 pmm_total_bytes(void) { return total_bytes; }
u64 pmm_free_bytes(void) { return free_pages * PAGE_SIZE; }
