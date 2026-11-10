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
    return ata_write(0, FIRST_DATA, disk);
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

/* 0 = no disk, 1 = mounted, 2 = blank disk just formatted */
int ftfs_init(void)
{
    ata_select(1);                       /* FTFS lives on the data disk */
    if (ata_init() != 0) return 0;
    if (ata_read(0, FIRST_DATA, disk)) return 0;
    if (SUPER->magic != MAGIC) {
        ftfs_format();
        return 2;
    }
    return 1;
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
    if (full && ata_write((u32)start, full, data)) return -4;
    if (rem) {
        u8 tmp[BLOCK_SIZE];
        copy(tmp, data + full * BLOCK_SIZE, rem);
        zero(tmp + rem, BLOCK_SIZE - rem);
        if (ata_write((u32)start + full, 1, tmp)) return -4;
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

    if (full && ata_read(first, full, out)) return -4;
    if (rem) {
        u8 tmp[BLOCK_SIZE];
        if (ata_read(first + full, 1, tmp)) return -4;
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