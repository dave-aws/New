/* NOVA OS — physical and virtual memory management. */
#ifndef NOVA_MM_H
#define NOVA_MM_H

#include <nova/types.h>
#include <nova/boot.h>

/* ------------------------------------------------------------------ PMM -- */
#define PMM_MAX_PHYS (4ULL * 1024 * 1024 * 1024)   /* covered by the direct map */

void pmm_init(struct boot_info *bi);
u64  pmm_alloc_page(void);
u64  pmm_alloc_pages(u64 count);
void pmm_free_page(u64 phys);
void pmm_free_pages(u64 phys, u64 count);
u64  pmm_total_bytes(void);
u64  pmm_free_bytes(void);
void pmm_reserve_range(u64 start, u64 end);

/* ------------------------------------------------------------------ VMM -- */
#define PTE_P     (1ULL << 0)
#define PTE_W     (1ULL << 1)
#define PTE_U     (1ULL << 2)
#define PTE_PWT   (1ULL << 3)
#define PTE_PCD   (1ULL << 4)
#define PTE_A     (1ULL << 5)
#define PTE_D     (1ULL << 6)
#define PTE_PS    (1ULL << 7)
#define PTE_G     (1ULL << 8)
#define PTE_NX    (1ULL << 63)
#define PTE_ADDR  0x000FFFFFFFFFF000ULL

#define KERNEL_BASE    0xFFFFFFFF80000000ULL
#define HEAP_BASE      0xFFFFFFFF90000000ULL
#define HEAP_SIZE      (128ULL * 1024 * 1024)
#define PHYS_MAP_BASE  0xFFFFC00000000000ULL
#define PHYS_MAP_SIZE  (4ULL * 1024 * 1024 * 1024)
#define USER_MAX       0x0000008000000000ULL   /* 512 GiB */

#define PHYS_TO_VIRT(p) ((void *)((u64)(p) + PHYS_MAP_BASE))
#define VIRT_TO_PHYS(v) ((u64)(v) - PHYS_MAP_BASE)

void vmm_init(struct boot_info *bi);
u64  vmm_kernel_pml4(void);
u64  vmm_new_address_space(void);
void vmm_destroy_address_space(u64 pml4_phys);
bool vmm_map(u64 pml4_phys, u64 virt, u64 phys, u64 flags);
bool vmm_map_2m(u64 pml4_phys, u64 virt, u64 phys, u64 flags);
void vmm_unmap(u64 pml4_phys, u64 virt);
u64  vmm_translate(u64 pml4_phys, u64 virt);
bool vmm_is_user_range(u64 pml4_phys, u64 ptr, u64 len, bool write);
bool vmm_alloc_user_range(u64 pml4_phys, u64 virt, u64 len, u64 flags);
void vmm_free_user_range(u64 pml4_phys, u64 virt, u64 len);
void vmm_switch(u64 pml4_phys);

/* ----------------------------------------------------------------- heap -- */
void  heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *kcalloc(size_t n, size_t size);
void *krealloc(void *ptr, size_t size);
void  kfree(void *ptr);
char *kstrdup(const char *s);
void  heap_stats(u64 *used, u64 *free_bytes, u64 *pages);

#endif /* NOVA_MM_H */
