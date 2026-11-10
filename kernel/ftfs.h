#ifndef FTFS_H
#define FTFS_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

void ftfs_format(void);
int  ftfs_write(const char *name, const u8 *data, u32 len);
int  ftfs_read(const char *name, u8 *out, u32 max);
int  ftfs_delete(const char *name);
int  ftfs_entry(int index, char *name, u32 *size);
int  ftfs_init(void);
u32  ftfs_free_blocks(void);

#endif