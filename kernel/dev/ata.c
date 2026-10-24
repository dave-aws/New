/* NOVA OS — ATA/ATAPI PIO block driver.
 *
 * Polled PIO on the primary channel is enough to boot, mount NOVA FS and load
 * executables.  The driver reads the MBR itself to locate the NOVA partition
 * (type 0x7F), so the kernel has no dependency on the boot loader for layout.
 */
#include <nova/types.h>
#include <nova/ata.h>
#include <nova/io.h>
#include <nova/log.h>
#include <nova/mm.h>

#define ATA_IO_BASE  0x1F0
#define ATA_CTRL     0x3F6

#define ATA_REG_DATA  0
#define ATA_REG_ERROR 1
#define ATA_REG_SECC  2
#define ATA_REG_SECN  3
#define ATA_REG_CYL   4
#define ATA_REG_HEAD  6
#define ATA_REG_CMD   7
#define ATA_REG_STATUS 7

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_DF   0x20
#define ST_RDY  0x40
#define ST_BSY  0x80

#define CMD_READ_PIO  0x20
#define CMD_WRITE_PIO 0x30
#define CMD_IDENTIFY  0xEC
#define CMD_FLUSH     0xE7

static u64 part_lba;
static u64 part_count;
static bool present;
static u32 io_base = ATA_IO_BASE;
static bool lba48;

static int wait_not_busy(void)
{
    for (u32 i = 0; i < 1000000; i++) {
        u8 st = inb(io_base + ATA_REG_STATUS);
        if (!(st & ST_BSY))
            return 0;
        cpu_pause();
    }
    return -1;
}

static int wait_drq(void)
{
    for (u32 i = 0; i < 1000000; i++) {
        u8 st = inb(io_base + ATA_REG_STATUS);
        if (st & (ST_ERR | ST_DF))
            return -1;
        if (!(st & ST_BSY) && (st & ST_DRQ))
            return 0;
        cpu_pause();
    }
    return -1;
}

static void delay_400ns(void)
{
    for (int i = 0; i < 4; i++)
        inb(io_base + ATA_REG_STATUS);
}

static int pio_transfer(u64 lba, u32 count, void *buf, bool write)
{
    if (!present || count == 0)
        return -1;
    u8 *p = buf;

    while (count) {
        u32 chunk = count > 128 ? 128 : count;   /* 64 KiB per command */

        if (wait_not_busy())
            return -1;

        outb(io_base + ATA_REG_HEAD, 0xE0 | ((u8)((lba >> 24) & 0x0F)));
        delay_400ns();
        outb(io_base + 0x6, 0);                  /* no LBA48 for now */
        outb(io_base + ATA_REG_SECC, (u8)chunk);
        outb(io_base + ATA_REG_SECN, (u8)(lba & 0xFF));
        outb(io_base + ATA_REG_CYL, (u8)((lba >> 8) & 0xFF));
        outb(io_base + ATA_REG_CYL + 1, (u8)((lba >> 16) & 0xFF));
        outb(io_base + ATA_REG_HEAD, 0xE0 | ((u8)((lba >> 24) & 0x0F)));
        outb(io_base + ATA_REG_CMD, write ? CMD_WRITE_PIO : CMD_READ_PIO);

        for (u32 s = 0; s < chunk; s++) {
            if (wait_drq())
                return -1;
            if (write) {
                for (int i = 0; i < 256; i++) {
                    u16 v = (u16)p[0] | ((u16)p[1] << 8);
                    outw(io_base + ATA_REG_DATA, v);
                    p += 2;
                }
            } else {
                for (int i = 0; i < 256; i++) {
                    u16 v = inw(io_base + ATA_REG_DATA);
                    p[0] = (u8)v;
                    p[1] = (u8)(v >> 8);
                    p += 2;
                }
            }
        }
        if (write) {
            outb(io_base + ATA_REG_CMD, CMD_FLUSH);
            wait_not_busy();
        }
        lba += chunk;
        count -= chunk;
    }
    return 0;
}

int ata_read(u64 lba, u32 count, void *buf)
{
    return pio_transfer(lba, count, buf, false);
}

int ata_write(u64 lba, u32 count, const void *buf)
{
    return pio_transfer(lba, count, (void *)buf, true);
}

bool ata_present(void) { return present; }
u64 ata_part_lba(void) { return part_lba; }
u64 ata_part_sectors(void) { return part_count; }

int ata_init(u32 boot_drive)
{
    (void)boot_drive;
    present = false;

    outb(io_base + ATA_REG_HEAD, 0xA0);
    delay_400ns();

    /* IDENTIFY: a missing drive leaves the status register at 0xFF. */
    u8 st = inb(io_base + ATA_REG_STATUS);
    if (st == 0xFF) {
        kprintf("[ata] no drive on primary master\n");
        return -1;
    }
    outb(io_base + ATA_REG_SECC, 0);
    outb(io_base + ATA_REG_SECN, 0);
    outb(io_base + ATA_REG_CYL, 0);
    outb(io_base + ATA_REG_CYL + 1, 0);
    outb(io_base + ATA_REG_CMD, CMD_IDENTIFY);
    st = inb(io_base + ATA_REG_STATUS);
    if (st == 0) {
        kprintf("[ata] drive did not respond to IDENTIFY\n");
        return -1;
    }
    if (wait_not_busy())
        return -1;
    if (inb(io_base + ATA_REG_CYL) || inb(io_base + ATA_REG_CYL + 1)) {
        kprintf("[ata] device is not an ATA disk\n");
        return -1;
    }
    if (wait_drq())
        return -1;

    u16 ident[256];
    for (int i = 0; i < 256; i++)
        ident[i] = inw(io_base + ATA_REG_DATA);
    present = true;

    char model[41];
    for (int i = 0; i < 20; i++) {
        model[i * 2] = (char)(ident[27 + i] >> 8);
        model[i * 2 + 1] = (char)(ident[27 + i] & 0xFF);
    }
    model[40] = 0;
    for (int i = 39; i >= 0 && model[i] == ' '; i--)
        model[i] = 0;

    u32 sectors28 = ((u32)ident[61] << 16) | ident[60];
    kprintf("[ata] %s, %u sectors (LBA28)\n", model, sectors28);

    /* Locate the NOVA FS partition (type 0x7F) in the MBR. */
    u8 *mbr = kmalloc(512);
    if (!mbr)
        return -1;
    if (ata_read(0, 1, mbr) != 0) {
        kfree(mbr);
        return -1;
    }
    part_lba = 0;
    part_count = 0;
    for (int i = 0; i < 4; i++) {
        u8 *e = mbr + 0x1BE + i * 16;
        u32 lba = (u32)e[8] | ((u32)e[9] << 8) | ((u32)e[10] << 16) | ((u32)e[11] << 24);
        u32 cnt = (u32)e[12] | ((u32)e[13] << 8) | ((u32)e[14] << 16) | ((u32)e[15] << 24);
        if (e[4] == 0x7F && cnt) {
            part_lba = lba;
            part_count = cnt;
            break;
        }
    }
    kfree(mbr);
    if (!part_lba) {
        kprintf("[ata] no NOVA FS partition found\n");
        return -1;
    }
    kprintf("[ata] NOVA FS partition at LBA %lu (%lu MiB)\n",
            part_lba, (part_count * 512) >> 20);
    return 0;
}
