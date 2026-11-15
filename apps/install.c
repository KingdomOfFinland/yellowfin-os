/* install.c - copies the Yellowfin boot image layout to a disk and formats FTFS after it.

   Disk layout written:
     LBA 0            boot sector (boot.asm) + a partition entry if the sector has room
     LBA 1..8         stage2 (Shark Loader second stage)
     LBA 9..          kernel.bin
     LBA 2048..       FTFS (superblock, file table, block map, data)

   Order matters: kernel and FTFS go first and the boot sector goes LAST, so a disk that
   lost power half way through is simply not bootable instead of booting into garbage. */

#include "install.h"
#include "payload.h"
#include "ata.h"
#include "ftfs.h"

#define SECTOR 512u
#define CHUNK  16u
#define MIN_SECTORS (FTFS_INSTALL_BASE + 65536u + 64u)   /* loader area + 32 MiB FTFS + slack */

static uint8_t bootsec[SECTOR];
static uint8_t rb[CHUNK * SECTOR];

static void (*g_prog)(int, int);
static void fmt_cb(int pct) { g_prog(2, pct); }

static const char *const names[4] = {
    "Primary master", "Primary slave", "Secondary master", "Secondary slave"
};

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int same(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

int install_probe(struct inst_disk *d, int max)
{
    int n = 0, ch, sl, fch = 0, fsl = 0, mounted;
    uint32_t fbase = 0;

    mounted = ftfs_device(&fch, &fsl, &fbase);

    for (ch = 0; ch < 2; ch++) {
        for (sl = 0; sl < 2; sl++) {
            if (n >= max) return n;

            ata_select2(ch, sl);
            if (ata_init() != 0)
                continue;                      /* nothing there, or ATAPI (CD/DVD) */

            d[n].ch = ch;
            d[n].sl = sl;
            d[n].sectors = ata_sector_count();
            d[n].name = names[ch * 2 + sl];
            d[n].note = 0;

            if (mounted && fch == ch && fsl == sl)
                d[n].note = "(in use by FTFS)";
            else if (d[n].sectors < MIN_SECTORS)
                d[n].note = "(too small)";

            d[n].usable = (d[n].note == 0);
            n++;
        }
    }
    return n;
}

/* reads 'count' sectors from lba and compares them with ref; 0 = identical */
static uint32_t v_done, v_total;

static int verify(const struct inst_disk *d, uint32_t lba, uint32_t count,
                  const uint8_t *ref, void (*progress)(int, int))
{
    uint32_t s, n;

    for (s = 0; s < count; s += CHUNK) {
        n = count - s < CHUNK ? count - s : CHUNK;
        ata_select2(d->ch, d->sl);
        if (ata_read(lba + s, n, rb)) return -1;
        if (!same(rb, ref + s * SECTOR, n * SECTOR)) return -1;
        v_done += n;
        progress(4, (int)(v_done * 100 / v_total));
    }
    return 0;
}

int install_run(const struct inst_disk *d, void (*progress)(int step, int pct))
{
    const uint8_t *kern, *st2;
    uint32_t ks, s, n, i;
    int zero_room = 1;

    g_prog = progress;

    /* ---- step 0: checks ---- */
    progress(0, 0);
    if (!payload_ok())                       return INST_E_PAYLOAD;
    if (!d->usable || d->sectors < MIN_SECTORS) return INST_E_SIZE;

    ata_select2(d->ch, d->sl);
    if (ata_init() != 0)                     return INST_E_BUSY;
    if (ata_sector_count() < MIN_SECTORS)    return INST_E_SIZE;
    progress(0, 100);

    kern = payload_kernel();
    st2  = payload_stage2();
    ks   = payload_kernel_sectors();

    /* ---- step 1: kernel ---- */
    for (s = 0; s < ks; s += CHUNK) {
        n = ks - s < CHUNK ? ks - s : CHUNK;
        ata_select2(d->ch, d->sl);
        if (ata_write(PAYLOAD_KERNEL_LBA + s, n, kern + s * SECTOR))
            return INST_E_WRITE;
        progress(1, (int)((s + n) * 100 / ks));
    }

    /* ---- step 2: FTFS ---- */
    progress(2, 0);
    if (ftfs_format_disk(d->ch, d->sl, FTFS_INSTALL_BASE, fmt_cb))
        return INST_E_FORMAT;
    progress(2, 100);

    /* ---- step 3: loader (stage2 first, boot sector last) ---- */
    for (s = 0; s < PAYLOAD_STAGE2_SECTORS; s++) {
        ata_select2(d->ch, d->sl);
        if (ata_write(1 + s, 1, st2 + s * SECTOR))
            return INST_E_WRITE;
        progress(3, (int)((s + 1) * 60 / PAYLOAD_STAGE2_SECTORS));
    }

    for (i = 0; i < SECTOR; i++)
        bootsec[i] = payload_boot()[i];

    /* One active partition entry covering the disk, so BIOSes that insist on an active
       partition will still boot it. Only written if boot.asm left bytes 446..509 empty. */
    for (i = 446; i < 510; i++)
        if (bootsec[i]) zero_room = 0;
    if (zero_room) {
        uint8_t *e = bootsec + 446;

        e[0] = 0x80;                         /* active */
        e[1] = 0x00; e[2] = 0x02; e[3] = 0x00;   /* CHS start (head 0, sector 2) */
        e[4] = 0x7F;                         /* type: experimental / alt-OS */
        e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;   /* CHS end: "use LBA" */
        put32(e + 8, 1);                     /* first LBA */
        put32(e + 12, d->sectors - 1);       /* sector count */
    }

    ata_select2(d->ch, d->sl);
    if (ata_write(0, 1, bootsec))
        return INST_E_WRITE;
    progress(3, 100);

    /* ---- step 4: read everything back and compare ---- */
    v_done = 0;
    v_total = 1 + PAYLOAD_STAGE2_SECTORS + ks;
    progress(4, 0);

    if (verify(d, 0, 1, bootsec, progress))                         return INST_E_VERIFY;
    if (verify(d, 1, PAYLOAD_STAGE2_SECTORS, st2, progress))        return INST_E_VERIFY;
    if (verify(d, PAYLOAD_KERNEL_LBA, ks, kern, progress))          return INST_E_VERIFY;
    if (!ftfs_probe(d->ch, d->sl, FTFS_INSTALL_BASE))               return INST_E_VERIFY;

    progress(4, 100);
    return INST_OK;
}

const char *install_error(int e)
{
    switch (e) {
    case INST_E_PAYLOAD: return "No boot image to copy. Boot from the ISO or yellowfin.img.";
    case INST_E_SIZE:    return "The disk is too small (needs about 35 MB).";
    case INST_E_BUSY:    return "The disk stopped answering.";
    case INST_E_WRITE:   return "A disk write failed.";
    case INST_E_FORMAT:  return "Could not create the FTFS filesystem.";
    case INST_E_VERIFY:  return "Verification failed: data on disk does not match.";
    default:             return "Unknown error.";
    }
}