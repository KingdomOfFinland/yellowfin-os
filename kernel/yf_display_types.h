
#ifndef YF_DISPLAY_TYPES_H
#define YF_DISPLAY_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* YellowfinOS display basics — freestanding C */

typedef enum {
    YF_OUTPUT_UNUSED = 0,
    YF_OUTPUT_ANALOG = 1,
    YF_OUTPUT_LVDS = 4,
    YF_OUTPUT_HDMI = 6,
    YF_OUTPUT_DP = 7
} yf_output_type_t;

typedef enum {
    YF_PIPE_A = 0,
    YF_PIPE_B = 1
} yf_pipe_t;

typedef enum {
    YF_PLANE_PRIMARY_A = 0,
    YF_PLANE_PRIMARY_B = 1,
    YF_PLANE_CURSOR_A  = 2,
    YF_PLANE_CURSOR_B  = 3
} yf_plane_id_t;

/* Raw hardware state; no Linux DRM dependencies. */
typedef struct {
    uint32_t control;
    uint32_t stride;
    uint32_t position;
    uint32_t size;
    uint32_t surface;
    uint32_t linear_offset;
    uint32_t tile_offset;
} yf_plane_info_t;

typedef struct {
    uint32_t control;
    uint32_t source_size;
    uint32_t destination_size;
} yf_pipe_info_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar0;
    uint8_t revision_id;
} yf_gm45_device_t;

#endif
