/* NOVA OS — kernel entry and boot sequence. */
#include <nova/types.h>
#include <nova/boot.h>
#include <nova/io.h>
#include <nova/log.h>
#include <nova/string.h>
#include <nova/trap.h>
#include <nova/mm.h>
#include <nova/sched.h>
#include <nova/fb.h>
#include <nova/console.h>
#include <nova/fs.h>
#include <nova/gui.h>
#include <nova/win32.h>
#include <nova/ata.h>

static struct boot_info g_bi;

const struct boot_info *boot_info(void) { return &g_bi; }

static void banner(void)
{
    kprintf("\n");
    kprintf("  _   _  _____ _   _    _\n");
    kprintf(" | \\ | |/  _  \\ | |  / /\n");
    kprintf(" |  \\| || | | | | | / /   NOVA OS 0.1 'Genesis'\n");
    kprintf(" | |\\  || |_| | | |/ /    an independent x86-64 desktop system\n");
    kprintf(" |_| \\_|\\_____/ |___/\n\n");
}

static void boot_memory_map(void)
{
    struct e820_dump {
        u64 base;
        u64 length;
        u32 type;
        u32 acpi;
    } packed;
    const struct e820_dump *mm = (const struct e820_dump *)g_bi.mmap_addr;
    kprintf("[e820] %u regions\n", g_bi.mmap_count);
    for (u32 i = 0; i < g_bi.mmap_count && i < 16; i++) {
        static const char *types[] = { "?", "usable", "reserved", "ACPI reclaim",
                                       "ACPI NVS", "bad" };
        u32 t = mm[i].type < 6 ? mm[i].type : 0;
        kprintf("  %#010lx-%#010lx %-12s\n", mm[i].base,
                mm[i].base + mm[i].length, types[t]);
    }
}

static void init_thread(void *arg)
{
    (void)arg;
    kprintf("[init] starting system services\n");

    if (ata_init(g_bi.boot_drive) == 0) {
        if (vfs_init() == 0) {
            kprintf("[vfs] mounted NOVA FS at C:\\ (%u inodes)\n", fs_inode_count());
            fs_dump_root();
        } else {
            kprintf("[vfs] no filesystem found on the data partition\n");
        }
    }

    win32_init();
    gui_init();
    kprintf("[init] desktop ready\n");

    /* The init thread turns into the desktop/shell thread. */
    gui_run();
    for (;;)
        thread_sleep_ms(1000);
}

void kmain(struct boot_info *bi)
{
    serial_init();
    banner();

    if (bi->magic != BOOT_MAGIC) {
        panic("bad boot magic %#x (expected %#x)", bi->magic, BOOT_MAGIC);
    }
    memcpy(&g_bi, bi, sizeof(g_bi));

    kprintf("[boot] boot_info @ %p, cmdline '%s'\n", (void *)bi, g_bi.cmdline);
    kprintf("[boot] kernel loaded at %#lx, %lu KiB\n",
            g_bi.kernel_phys, g_bi.kernel_size >> 10);
    boot_memory_map();

    kprintf("[boot] gdt...\n");
    gdt_init();
    kprintf("[boot] gdt ok\n");
    idt_init();
    kprintf("[boot] idt ok\n");
    pic_init();
    kprintf("[boot] pic ok\n");

    pmm_init(&g_bi);
    vmm_init(&g_bi);
    heap_init();

    /* The framebuffer is reachable only through the direct physical map,
     * which vmm_init() has just installed. */
    fb_init(&g_bi);
    if (fb_available()) {
        bootcon_init();
        console_sink = bootcon_write;
        bootcon_set_active(true);
        kprintf("[boot] graphical console online\n");
    }
    sched_init();

    pit_init(100);
    kprintf("[boot] descriptors, paging, heap and scheduler online\n");

    if (!thread_create_kernel("init", init_thread, NULL, 2))
        panic("cannot create the init thread");

    kprintf("[boot] enabling interrupts\n");
    cpu_sti();

    /* The boot context idles; init and the compositor take over. */
    for (;;) {
        cpu_halt();
    }
}
