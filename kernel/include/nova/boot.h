/* NOVA OS — boot protocol shared by stage 2 and the kernel.
 *
 * stage 2 places this structure at physical 0x9500 and passes that address in
 * RDI.  Keep the layout in sync with boot/stage2.S (see the static asserts
 * below, which fail the build if the C layout drifts).
 */
#ifndef NOVA_BOOT_H
#define NOVA_BOOT_H

#include <nova/types.h>

#define BOOT_MAGIC   0x4E4F5642u /* 'NOVB' */
#define BOOT_VERSION 1u

struct boot_info {
    u32 magic;
    u32 version;
    u32 fb_addr;        /* physical linear framebuffer */
    u32 fb_pitch;       /* bytes per scan line */
    u32 fb_width;
    u32 fb_height;
    u32 fb_bpp;
    u32 reserved0;
    u64 mmap_addr;      /* E820 array, physical */
    u32 mmap_count;
    u32 mmap_entry_size;
    u64 kernel_phys;    /* where stage 2 loaded us */
    u64 kernel_size;
    u32 boot_drive;     /* BIOS drive number */
    u32 reserved1;
    char cmdline[64];
} packed;

_Static_assert(sizeof(struct boot_info) == 136, "boot_info layout drift");
_Static_assert(offsetof(struct boot_info, fb_addr) == 8, "boot_info fb_addr");
_Static_assert(offsetof(struct boot_info, mmap_addr) == 32, "boot_info mmap");
_Static_assert(offsetof(struct boot_info, mmap_count) == 40, "boot_info mmap_count");
_Static_assert(offsetof(struct boot_info, kernel_phys) == 48, "boot_info kernel");
_Static_assert(offsetof(struct boot_info, cmdline) == 72, "boot_info cmdline");

#endif /* NOVA_BOOT_H */
