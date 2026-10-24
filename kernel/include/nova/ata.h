/* NOVA OS — ATA PIO disk driver. */
#ifndef NOVA_ATA_H
#define NOVA_ATA_H

#include <nova/types.h>

int  ata_init(u32 boot_drive);
int  ata_read(u64 lba, u32 count, void *buf);
int  ata_write(u64 lba, u32 count, const void *buf);
u64  ata_part_lba(void);
u64  ata_part_sectors(void);
bool ata_present(void);

#endif /* NOVA_ATA_H */
