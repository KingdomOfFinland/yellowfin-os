#ifndef FTFS_H
#define FTFS_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* On an installed disk FTFS starts here (1 MiB in), after the boot sector, stage2 and kernel. */
#define FTFS_INSTALL_BASE 2048u

void ftfs_format(void);
int  ftfs_write(const char *name, const u8 *data, u32 len);
int  ftfs_read(const char *name, u8 *out, u32 max);
int  ftfs_delete(const char *name);
int  ftfs_entry(int index, char *name, u32 *size);
int  ftfs_init(void);
u32  ftfs_free_blocks(void);

/* Where the mounted FTFS lives. Returns 1 if mounted (and fills the outputs), 0 if not. */
int  ftfs_device(int *channel, int *slave, u32 *base);

/* Write a fresh, empty FTFS at 'base' on another disk without touching the mounted one.
   cb (optional) gets 0..100. Returns 0 = ok, <0 = disk error. */
int  ftfs_format_disk(int channel, int slave, u32 base, void (*cb)(int pct));

/* 1 if a valid FTFS superblock is at 'base' on that disk. */
int  ftfs_probe(int channel, int slave, u32 base);

#endif
