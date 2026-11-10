#ifndef PLANE_H
#define PLANE_H
#include <stdint.h>

struct yf_plane_info {
    uint32_t control, stride, position, size;
    uint32_t surface, linear_offset, tile_offset;
};

int  yf_plane_read(uintptr_t mmio_base, int plane, struct yf_plane_info *out);
int  yf_plane_is_enabled(const struct yf_plane_info *info);
void yf_plane_log(const char *name, const struct yf_plane_info *p);
#endif