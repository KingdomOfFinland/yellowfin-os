#include "ata.h"

#define ATA_DATA     0x1F0
#define ATA_ERROR    0x1F1
#define ATA_SECCOUNT 0x1F2
#define ATA_LBA_LO   0x1F3
#define ATA_LBA_MID  0x1F4
#define ATA_LBA_HI   0x1F5
#define ATA_DRIVE    0x1F6
#define ATA_STATUS   0x1F7
#define ATA_CMD      0x1F7
#define ATA_ALTSTAT  0x3F6

#define SR_BSY  0x80
#define SR_DRDY 0x40
#define SR_DF   0x20
#define SR_DRQ  0x08
#define SR_ERR  0x01

#define CMD_READ     0x20
#define CMD_WRITE    0x30
#define CMD_FLUSH    0xE7
#define CMD_IDENTIFY 0xEC

static uint32_t total_sectors = 0;

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline uint16_t inw(uint16_t p) {
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void outw(uint16_t p, uint16_t v) {
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(p));
}

// ~400ns delay: 4 reads of the alternate status register
static void ata_delay(void) {
    inb(ATA_ALTSTAT); inb(ATA_ALTSTAT); inb(ATA_ALTSTAT); inb(ATA_ALTSTAT);
}

// wait until BSY clears; returns status, or -1 on timeout
static int ata_wait_bsy(void) {
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (!(s & SR_BSY)) return s;
    }
    return -1;
}

static uint8_t ata_drv = 0;                    // 0 = master, 1 = slave

void ata_select(int slave) { ata_drv = slave ? 1 : 0; }

// wait for DRQ (data ready); 0 = ok, -1 = timeout/error
static int ata_wait_drq(void) {
    ata_delay();
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (s & (SR_ERR | SR_DF)) return -1;
        if (!(s & SR_BSY) && (s & SR_DRQ)) return 0;
    }
    return -1;
}

int ata_init(void) {
    outb(ATA_ALTSTAT, 0x02);          // nIEN: disable interrupts, we poll
    outb(ATA_DRIVE, 0xA0 | (ata_drv << 4));           // select master
    ata_delay();

    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_CMD, CMD_IDENTIFY);

    uint8_t st = inb(ATA_STATUS); if (st == 0 || st == 0xFF) return -1;      // no drive
    if (ata_wait_bsy() < 0)   return -2;

    // non-zero here means ATAPI/SATA, not a plain ATA disk
    if (inb(ATA_LBA_MID) || inb(ATA_LBA_HI)) return -3;
    if (ata_wait_drq() < 0)   return -4;

    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(ATA_DATA);

    total_sectors = ((uint32_t)id[61] << 16) | id[60];
    return 0;
}

uint32_t ata_sector_count(void) { return total_sectors; }

static void ata_setup(uint32_t lba, uint8_t cmd) {
    ata_wait_bsy();
    outb(ATA_DRIVE, 0xE0 | (ata_drv << 4) | ((lba >> 24) & 0x0F));   // master, LBA mode
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LO,  lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI,  (lba >> 16) & 0xFF);
    outb(ATA_CMD, cmd);
}

int ata_read(uint32_t lba, uint32_t count, void *buf) {
    uint16_t *p = (uint16_t *)buf;
    for (uint32_t s = 0; s < count; s++) {
        ata_setup(lba + s, CMD_READ);
        if (ata_wait_drq() < 0) return -1;
        for (int i = 0; i < 256; i++) *p++ = inw(ATA_DATA);
    }
    return 0;
}

int ata_write(uint32_t lba, uint32_t count, const void *buf) {
    const uint16_t *p = (const uint16_t *)buf;
    for (uint32_t s = 0; s < count; s++) {
        ata_setup(lba + s, CMD_WRITE);
        if (ata_wait_drq() < 0) return -1;
        for (int i = 0; i < 256; i++) outw(ATA_DATA, *p++);
        outb(ATA_CMD, CMD_FLUSH);       // flush cache after each sector
        if (ata_wait_bsy() < 0) return -2;
    }
    return 0;
}