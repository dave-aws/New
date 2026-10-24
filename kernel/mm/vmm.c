/* NOVA OS — virtual memory: 4-level paging, address spaces, user mappings. */
#include <nova/types.h>
#include <nova/mm.h>
#include <nova/boot.h>
#include <nova/log.h>
#include <nova/string.h>
#include <nova/io.h>

static u64 kernel_pml4;
static bool physmap_ready;

/* While the direct map is being built the identity map from stage 2 is the
 * only way to touch page-table pages, so fall back to it temporarily. */
static inline u64 *pt(u64 phys)
{
    return (u64 *)(physmap_ready ? PHYS_TO_VIRT(phys) : (u64)phys);
}

static inline u64 index4(u64 va) { return (va >> 39) & 0x1FF; }
static inline u64 index3(u64 va) { return (va >> 30) & 0x1FF; }
static inline u64 index2(u64 va) { return (va >> 21) & 0x1FF; }
static inline u64 index1(u64 va) { return (va >> 12) & 0x1FF; }

static u64 alloc_table(void)
{
    u64 p = pmm_alloc_page();
    if (!p)
        panic("out of physical memory allocating a page table");
    memset(pt(p), 0, PAGE_SIZE);
    return p;
}

/* Walk to the PTE for va; optionally create missing levels. */
static u64 *walk(u64 pml4_phys, u64 va, bool create, u64 table_flags)
{
    u64 *pml4 = pt(pml4_phys);
    u64 e = pml4[index4(va)];
    if (!(e & PTE_P)) {
        if (!create)
            return NULL;
        u64 np = alloc_table();
        pml4[index4(va)] = np | PTE_P | PTE_W | (table_flags & PTE_U);
    }
    if (e & PTE_PS)
        return &pml4[index4(va)];
    u64 *pdpt = pt(pml4[index4(va)] & PTE_ADDR);

    e = pdpt[index3(va)];
    if (!(e & PTE_P)) {
        if (!create)
            return NULL;
        u64 np = alloc_table();
        pdpt[index3(va)] = np | PTE_P | PTE_W | (table_flags & PTE_U);
    }
    if (e & PTE_PS)
        return &pdpt[index3(va)];
    u64 *pd = pt(pdpt[index3(va)] & PTE_ADDR);

    e = pd[index2(va)];
    if (!(e & PTE_P)) {
        if (!create)
            return NULL;
        u64 np = alloc_table();
        pd[index2(va)] = np | PTE_P | PTE_W | (table_flags & PTE_U);
    }
    if (e & PTE_PS)
        return &pd[index2(va)];
    u64 *tbl = pt(pd[index2(va)] & PTE_ADDR);
    return &tbl[index1(va)];
}

void vmm_init(struct boot_info *bi)
{
    kernel_pml4 = read_cr3();
    kprintf("[vmm] building direct map at %#lx\n", (u64)PHYS_MAP_BASE);
    (void)bi;

    /* Direct physical map: 0..4 GiB at PHYS_MAP_BASE, 2 MiB pages. This is
     * how the kernel reaches page tables, user pages and MMIO after the
     * initial identity map is dropped from user address spaces. */
    for (u64 phys = 0; phys < PHYS_MAP_SIZE; phys += HUGE_PAGE_SIZE) {
        if (!vmm_map_2m(kernel_pml4, PHYS_MAP_BASE + phys, phys,
                        PTE_P | PTE_W | PTE_NX)) {
            kprintf("[vmm] failed at phys %#lx\n", phys);
            panic("cannot build the physical map at %#lx", phys);
        }
    }

    physmap_ready = true;
    kprintf("[vmm] kernel PML4 %#lx, physmap %#lx..%#lx\n",
            kernel_pml4, PHYS_MAP_BASE, PHYS_MAP_BASE + PHYS_MAP_SIZE);
}

u64 vmm_kernel_pml4(void) { return kernel_pml4; }

static void free_table_tree(u64 table_phys, int level)
{
    u64 *t = pt(table_phys);
    for (int i = 0; i < 512; i++) {
        u64 e = t[i];
        if (!(e & PTE_P) || (e & PTE_PS))
            continue;
        u64 child = e & PTE_ADDR;
        if (level == 1)
            pmm_free_page(child);
        else
            free_table_tree(child, level - 1);
    }
    pmm_free_page(table_phys);
}

u64 vmm_new_address_space(void)
{
    u64 pml4 = pmm_alloc_page();
    if (!pml4)
        return 0;
    u64 *dst = pt(pml4);
    u64 *src = pt(kernel_pml4);
    memset(dst, 0, PAGE_SIZE);
    /* Share the kernel half: code, physmap, heap. */
    for (int i = 256; i < 512; i++)
        dst[i] = src[i];
    return pml4;
}

void vmm_destroy_address_space(u64 pml4_phys)
{
    u64 *pml4 = pt(pml4_phys);
    for (int i = 0; i < 256; i++) {
        u64 e = pml4[i];
        if (!(e & PTE_P))
            continue;
        if (e & PTE_PS) {
            pmm_free_page(e & PTE_ADDR);
            continue;
        }
        free_table_tree(e & PTE_ADDR, 3);
        pml4[i] = 0;
    }
    pmm_free_page(pml4_phys);
}

bool vmm_map(u64 pml4_phys, u64 virt, u64 phys, u64 flags)
{
    if (!IS_ALIGNED(virt, PAGE_SIZE) || !IS_ALIGNED(phys, PAGE_SIZE))
        return false;
    u64 table_flags = (flags & PTE_U) ? PTE_U : 0;
    u64 *e = walk(pml4_phys, virt, true, table_flags);
    if (!e)
        return false;
    if (*e & PTE_PS)
        return false;
    *e = (phys & PTE_ADDR) | (flags & ~PTE_ADDR);
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return true;
}

bool vmm_map_2m(u64 pml4_phys, u64 virt, u64 phys, u64 flags)
{
    if (!IS_ALIGNED(virt, HUGE_PAGE_SIZE) || !IS_ALIGNED(phys, HUGE_PAGE_SIZE))
        return false;
    u64 table_flags = (flags & PTE_U) ? PTE_U : 0;
    u64 *pml4 = pt(pml4_phys);
    u64 e = pml4[index4(virt)];
    if (!(e & PTE_P)) {
        u64 np = alloc_table();
        pml4[index4(virt)] = np | PTE_P | PTE_W | table_flags;
    }
    if (e & PTE_PS)
        return false;
    u64 *pdpt = pt(pml4[index4(virt)] & PTE_ADDR);
    e = pdpt[index3(virt)];
    if (!(e & PTE_P)) {
        u64 np = alloc_table();
        pdpt[index3(virt)] = np | PTE_P | PTE_W | table_flags;
    }
    if (e & PTE_PS)
        return false;
    u64 *pd = pt(pdpt[index3(virt)] & PTE_ADDR);
    pd[index2(virt)] = (phys & PTE_ADDR & ~0x1FFFFFULL) | flags | PTE_PS;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return true;
}

void vmm_unmap(u64 pml4_phys, u64 virt)
{
    u64 *e = walk(pml4_phys, virt, false, 0);
    if (e && !(*e & PTE_PS))
        *e = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

u64 vmm_translate(u64 pml4_phys, u64 virt)
{
    u64 *pml4 = pt(pml4_phys);
    u64 e = pml4[index4(virt)];
    if (!(e & PTE_P))
        return 0;
    if (e & PTE_PS)
        return (e & PTE_ADDR & ~((1ULL << 30) - 1)) | (virt & ((1ULL << 30) - 1));
    u64 *pdpt = pt(e & PTE_ADDR);
    e = pdpt[index3(virt)];
    if (!(e & PTE_P))
        return 0;
    if (e & PTE_PS)
        return (e & PTE_ADDR & ~((1ULL << 30) - 1)) | (virt & ((1ULL << 30) - 1));
    u64 *pd = pt(e & PTE_ADDR);
    e = pd[index2(virt)];
    if (!(e & PTE_P))
        return 0;
    if (e & PTE_PS)
        return (e & PTE_ADDR & ~((1ULL << 21) - 1)) | (virt & ((1ULL << 21) - 1));
    u64 *t = pt(e & PTE_ADDR);
    e = t[index1(virt)];
    if (!(e & PTE_P))
        return 0;
    return (e & PTE_ADDR) | (virt & 0xFFF);
}

bool vmm_is_user_range(u64 pml4_phys, u64 ptr, u64 len, bool write)
{
    if (len == 0)
        return true;
    if (ptr + len < ptr || ptr + len > USER_MAX)
        return false;
    u64 first = ALIGN_DOWN(ptr, PAGE_SIZE);
    u64 last = ALIGN_UP(ptr + len, PAGE_SIZE);
    for (u64 va = first; va < last; va += PAGE_SIZE) {
        u64 *e = walk(pml4_phys, va, false, 0);
        if (!e || !(*e & PTE_P) || !(*e & PTE_U))
            return false;
        if (write && !(*e & PTE_W))
            return false;
    }
    return true;
}

bool vmm_alloc_user_range(u64 pml4_phys, u64 virt, u64 len, u64 flags)
{
    u64 start = ALIGN_DOWN(virt, PAGE_SIZE);
    u64 end = ALIGN_UP(virt + len, PAGE_SIZE);
    for (u64 va = start; va < end; va += PAGE_SIZE) {
        u64 page = pmm_alloc_page();
        if (!page)
            return false;
        memset(pt(page), 0, PAGE_SIZE);
        if (!vmm_map(pml4_phys, va, page, flags | PTE_U)) {
            pmm_free_page(page);
            return false;
        }
    }
    return true;
}

void vmm_free_user_range(u64 pml4_phys, u64 virt, u64 len)
{
    u64 start = ALIGN_DOWN(virt, PAGE_SIZE);
    u64 end = ALIGN_UP(virt + len, PAGE_SIZE);
    for (u64 va = start; va < end; va += PAGE_SIZE) {
        u64 p = vmm_translate(pml4_phys, va);
        if (p) {
            vmm_unmap(pml4_phys, va);
            pmm_free_page(ALIGN_DOWN(p, PAGE_SIZE));
        }
    }
}

void vmm_switch(u64 pml4_phys)
{
    if (read_cr3() != pml4_phys)
        write_cr3(pml4_phys);
}
