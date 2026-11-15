
/*
 * YellowfinOS - Intel i9xx / GM45 display plane
 *
 * First tuna port: read-only primary-plane diagnostics.
 * No Linux headers. No GPU register writes.
 */

#include <stdint.h>
#include <stddef.h>
#include "klog.h"

#define PLANE_A_CTL       0x70180u
#define PLANE_A_STRIDE    0x70188u
#define PLANE_A_POS       0x7018Cu
#define PLANE_A_SIZE      0x70190u
#define PLANE_A_SURF      0x7019Cu
#define PLANE_A_LINOFF    0x70184u
#define PLANE_A_TILEOFF   0x701A4u

#define PLANE_B_CTL       0x71180u
#define PLANE_B_STRIDE    0x71188u
#define PLANE_B_POS       0x7118Cu
#define PLANE_B_SIZE      0x71190u
#define PLANE_B_SURF      0x7119Cu
#define PLANE_B_LINOFF    0x71184u
#define PLANE_B_TILEOFF   0x711A4u

#define PLANE_ENABLE      (1u << 31)
#define PLANE_TILED       (1u << 10)
#define PLANE_FORMAT_SHIFT 26
#define PLANE_FORMAT_MASK  (0xFu << PLANE_FORMAT_SHIFT)

struct yf_plane_info {
    uint32_t control;
    uint32_t stride;
    uint32_t position;
    uint32_t size;
    uint32_t surface;
    uint32_t linear_offset;
    uint32_t tile_offset;
};

static uint32_t plane_read32(uintptr_t mmio_base, uint32_t offset)
{
    volatile const uint32_t *reg =
        (volatile const uint32_t *)(mmio_base + offset);

    return *reg;
}

/*
 * Caller must first verify the GPU and ensure its MMIO
 * region is accessible at mmio_base.
 */
int yf_plane_read(uintptr_t mmio_base, int plane,
                  struct yf_plane_info *out)
{
    uint32_t ctl;
    uint32_t stride;
    uint32_t pos;
    uint32_t size;
    uint32_t surf;
    uint32_t linoff;
    uint32_t tileoff;

    if (!mmio_base || !out)
        return -1;

    if (plane == 0) {
        ctl     = PLANE_A_CTL;
        stride  = PLANE_A_STRIDE;
        pos     = PLANE_A_POS;
        size    = PLANE_A_SIZE;
        surf    = PLANE_A_SURF;
        linoff  = PLANE_A_LINOFF;
        tileoff = PLANE_A_TILEOFF;
    } else if (plane == 1) {
        ctl     = PLANE_B_CTL;
        stride  = PLANE_B_STRIDE;
        pos     = PLANE_B_POS;
        size    = PLANE_B_SIZE;
        surf    = PLANE_B_SURF;
        linoff  = PLANE_B_LINOFF;
        tileoff = PLANE_B_TILEOFF;
    } else {
        return -1;
    }

    out->control       = plane_read32(mmio_base, ctl);
    out->stride        = plane_read32(mmio_base, stride);
    out->position      = plane_read32(mmio_base, pos);
    out->size           = plane_read32(mmio_base, size);
    out->surface        = plane_read32(mmio_base, surf);
    out->linear_offset  = plane_read32(mmio_base, linoff);
    out->tile_offset    = plane_read32(mmio_base, tileoff);

    return 0;
}

int yf_plane_is_enabled(const struct yf_plane_info *info)
{
    if (!info)
        return 0;

    return (info->control & PLANE_ENABLE) != 0;
}

void yf_plane_log(const char *name, const struct yf_plane_info *p)
{
    kinfo("plane", "%s ctl=%08x fmt=%x tiled=%u en=%u stride=%u",
          name, p->control,
          (p->control & PLANE_FORMAT_MASK) >> PLANE_FORMAT_SHIFT,
          (p->control & PLANE_TILED) ? 1u : 0u,
          (p->control & PLANE_ENABLE) ? 1u : 0u,
          p->stride);
    kinfo("plane", "%s surf=%08x lin=%08x tile=%08x pos=%08x size=%08x",
          name, p->surface, p->linear_offset, p->tile_offset,
          p->position, p->size);
}
