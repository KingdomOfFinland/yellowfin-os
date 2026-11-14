#include "ftfs.h"
#include "ata.h"

#define BLOCK_SIZE   512
#define TOTAL_BLOCKS 65536
#define MAX_FILES    64
#define NAME_LEN     20
#define TABLE_BLOCKS 4                                               /* 64 x 32 bytes = 2048 */
#define MAP_BLOCKS   ((TOTAL_BLOCKS + BLOCK_SIZE - 1) / BLOCK_SIZE)  /* 128 */
#define FIRST_DATA   (1 + TABLE_BLOCKS + MAP_BLOCKS)                 /* 133 */
#define MAGIC        0x53465446u   /* "FTFS" */

typedef struct {
    u32 magic;
    u16 version;
    u16 block_size;
    u32 total_blocks;
    u32 max_files;
    u32 files_used;
} __attribute__((packed)) ftfs_super;

typedef struct {
    char name[NAME_LEN];
    u32  size;
    u16  first_block;
    u16  block_count;
    u8   used;
    u8   pad[3];
} __attribute__((packed)) ftfs_record;

/* metadata only: superblock + file table + block map. File data lives on disk. */
static u8 disk[FIRST_DATA * BLOCK_SIZE] __attribute__((aligned(4)));

#define SUPER    ((ftfs_super *)(disk))
#define TABLE    ((ftfs_record *)(disk + 1 * BLOCK_SIZE))
#define MAP      (disk + (1 + TABLE_BLOCKS) * BLOCK_SIZE)

/* which disk the mounted FTFS lives on, and where it starts on that disk */
static int f_ch = 0, f_sl = 1, f_mounted = 0;
static u32 f_base = 0;

/* all disk access goes through these so other code can't leave the wrong drive selected */
static int dread(u32 lba, u32 n, void *buf)
{
    ata_select2(f_ch, f_sl);
    return ata_read(f_base + lba, n, buf);
}

static int dwrite(u32 lba, u32 n, const void *buf)
{
    ata_select2(f_ch, f_sl);
    return ata_write(f_base + lba, n, buf);
}

static u32 slen(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

static int seq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void copy(u8 *dst, const u8 *src, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}

static void zero(u8 *p, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) p[i] = 0;
}

static int meta_save(void)
{
    return dwrite(0, FIRST_DATA, disk);
}

void ftfs_format(void)
{
    u32 i;
    zero(disk, sizeof(disk));
    SUPER->magic = MAGIC;
    SUPER->version = 1;
    SUPER->block_size = BLOCK_SIZE;
    SUPER->total_blocks = TOTAL_BLOCKS;
    SUPER->max_files = MAX_FILES;
    SUPER->files_used = 0;
    for (i = 0; i < FIRST_DATA; i++) MAP[i] = 1;   /* metadata blocks */
    meta_save();             /* old file data stays on disk but is unreferenced */
}

/* 1 = FTFS found and mounted there, 0 = nothing (or unreadable) */
static int mount_at(int ch, int sl, u32 base)
{
    f_ch = ch; f_sl = sl; f_base = base;
    if (dread(0, FIRST_DATA, disk)) return 0;
    if (SUPER->magic != MAGIC) return 0;
    f_mounted = 1;
    return 1;
}

/* 0 = no disk, 1 = mounted, 2 = blank data disk just formatted
   Order: the installed layout on the boot disk first, then the separate data disk.
   A data disk is only auto-formatted if it is completely blank. */
int ftfs_init(void)
{
    u8 sec[BLOCK_SIZE];
    u32 i, nz = 0;

    f_mounted = 0;

    ata_select2(0, 0);
    if (ata_init() == 0 && mount_at(0, 0, FTFS_INSTALL_BASE))
        return 1;

    ata_select2(0, 1);                   /* the separate data disk */
    if (ata_init() != 0) return 0;
    if (mount_at(0, 1, 0)) return 1;

    f_ch = 0; f_sl = 1; f_base = 0;
    if (dread(0, 1, sec)) return 0;
    for (i = 0; i < BLOCK_SIZE; i++) nz |= sec[i];
    if (nz) return 0;                    /* not blank and not FTFS: leave it alone */

    ftfs_format();
    f_mounted = 1;
    return 2;
}

int ftfs_device(int *channel, int *slave, u32 *base)
{
    if (!f_mounted) return 0;
    *channel = f_ch;
    *slave = f_sl;
    *base = f_base;
    return 1;
}

/* Writes a fresh empty FTFS on another disk, block by block, without touching the mounted copy. */
int ftfs_format_disk(int ch, int sl, u32 base, void (*cb)(int pct))
{
    u8 sec[BLOCK_SIZE] __attribute__((aligned(4)));
    u32 b, i, total = FIRST_DATA;

    for (b = 0; b < total; b++) {
        zero(sec, BLOCK_SIZE);

        if (b == 0) {
            ftfs_super *sb = (ftfs_super *)sec;
            sb->magic = MAGIC;
            sb->version = 1;
            sb->block_size = BLOCK_SIZE;
            sb->total_blocks = TOTAL_BLOCKS;
            sb->max_files = MAX_FILES;
            sb->files_used = 0;
        } else if (b >= 1 + TABLE_BLOCKS) {
            u32 m = b - (1 + TABLE_BLOCKS);          /* map block number */
            for (i = 0; i < BLOCK_SIZE; i++)
                sec[i] = (m * BLOCK_SIZE + i < FIRST_DATA) ? 1 : 0;
        }

        ata_select2(ch, sl);
        if (ata_write(base + b, 1, sec)) return -1;

        if (cb && ((b % 8) == 7 || b == total - 1))
            cb((int)((b + 1) * 100 / total));
    }
    return 0;
}

int ftfs_probe(int ch, int sl, u32 base)
{
    u8 sec[BLOCK_SIZE] __attribute__((aligned(4)));

    ata_select2(ch, sl);
    if (ata_read(base, 1, sec)) return 0;
    return ((ftfs_super *)sec)->magic == MAGIC;
}

static int find(const char *name)
{
    int i;
    for (i = 0; i < MAX_FILES; i++)
        if (TABLE[i].used && seq(TABLE[i].name, name)) return i;
    return -1;
}

/* first run of 'count' free blocks, or -1 */
static int alloc(u32 count)
{
    u32 start, run;
    for (start = FIRST_DATA; start + count <= TOTAL_BLOCKS; start++) {
        for (run = 0; run < count && !MAP[start + run]; run++) { }
        if (run == count) return (int)start;
    }
    return -1;
}

int ftfs_delete(const char *name)
{
    int i = find(name);
    u32 b;
    if (i < 0) return -1;
    for (b = 0; b < TABLE[i].block_count; b++)
        MAP[TABLE[i].first_block + b] = 0;
    TABLE[i].used = 0;
    SUPER->files_used--;
    meta_save();
    return 0;
}

/* 0 = ok, -1 = bad name, -2 = file table full, -3 = no space, -4 = disk error */
int ftfs_write(const char *name, const u8 *data, u32 len)
{
    u32 nlen = slen(name);
    u32 blocks = (len + BLOCK_SIZE - 1) / BLOCK_SIZE;
    u32 full = len / BLOCK_SIZE, rem = len % BLOCK_SIZE;
    u32 b, i;
    int slot, start;

    if (nlen == 0 || nlen >= NAME_LEN) return -1;
    ftfs_delete(name);                      /* overwrite: drop old copy */

    for (slot = 0; slot < MAX_FILES && TABLE[slot].used; slot++) { }
    if (slot == MAX_FILES) return -2;
    start = alloc(blocks);
    if (start < 0) return -3;

    /* data first, then metadata, so a failure never points at junk */
    if (full && dwrite((u32)start, full, data)) return -4;
    if (rem) {
        u8 tmp[BLOCK_SIZE];
        copy(tmp, data + full * BLOCK_SIZE, rem);
        zero(tmp + rem, BLOCK_SIZE - rem);
        if (dwrite((u32)start + full, 1, tmp)) return -4;
    }

    zero((u8 *)&TABLE[slot], sizeof(ftfs_record));
    for (i = 0; i <= nlen; i++) TABLE[slot].name[i] = name[i];
    TABLE[slot].size = len;
    TABLE[slot].first_block = blocks ? (u16)start : 0;
    TABLE[slot].block_count = (u16)blocks;
    TABLE[slot].used = 1;
    for (b = 0; b < blocks; b++) MAP[start + b] = 1;
    SUPER->files_used++;
    return meta_save() ? -4 : 0;
}

/* returns bytes read, -1 = not found, -4 = disk error */
int ftfs_read(const char *name, u8 *out, u32 max)
{
    int i = find(name);
    u32 n, first, full, rem;

    if (i < 0) return -1;
    n = TABLE[i].size < max ? TABLE[i].size : max;
    first = TABLE[i].first_block;
    full = n / BLOCK_SIZE;
    rem = n % BLOCK_SIZE;

    if (full && dread(first, full, out)) return -4;
    if (rem) {
        u8 tmp[BLOCK_SIZE];
        if (dread(first + full, 1, tmp)) return -4;
        copy(out + full * BLOCK_SIZE, tmp, rem);
    }
    return (int)n;
}

/* the index-th used file; 0 = ok, -1 = no more */
int ftfs_entry(int index, char *name, u32 *size)
{
    int i, seen = 0;
    for (i = 0; i < MAX_FILES; i++) {
        if (!TABLE[i].used) continue;
        if (seen == index) {
            u32 k;
            for (k = 0; k < NAME_LEN; k++) name[k] = TABLE[i].name[k];
            *size = TABLE[i].size;
            return 0;
        }
        seen++;
    }
    return -1;
}

u32 ftfs_free_blocks(void)
{
    u32 b, n = 0;
    for (b = FIRST_DATA; b < TOTAL_BLOCKS; b++)
        if (!MAP[b]) n++;
    return n;
}
