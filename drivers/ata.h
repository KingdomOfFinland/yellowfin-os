#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_SECTOR_SIZE 512

void     ata_select(int slave);                    /* primary channel, master (0) or slave (1) */
void     ata_select2(int channel, int slave);      /* channel 0 = primary (0x1F0), 1 = secondary (0x170) */
int      ata_init(void);                           /* 0 = ok, <0 = no drive/error */
uint32_t ata_sector_count(void);                   /* total 28-bit LBA sectors of the last ata_init() */
int      ata_read (uint32_t lba, uint32_t count, void *buf);
int      ata_write(uint32_t lba, uint32_t count, const void *buf);

#endif
