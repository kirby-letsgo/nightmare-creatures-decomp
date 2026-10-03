/* Display output of the software GPU. */
#ifndef NC_PORT_HW_GPU_H
#define NC_PORT_HW_GPU_H

#include "port/recomp.h"

#include <stdbool.h>

typedef struct GpuDisplay {
    u32 x, y;          /* top-left of the display area in VRAM */
    u32 width, height; /* in output pixels */
    bool rgb24;
    bool enabled;
    u32 scale; /* output pixels per native pixel (internal resolution; 1 for 24-bit) */
} GpuDisplay;

void gpu_display_info(GpuDisplay *out);
/* Converts the display area to RGBA8888 (dst holds (width*scale) * (height*scale) pixels). */
void gpu_display_rgba(u32 *dst, const GpuDisplay *d);
/* Internal resolution multiplier (1..8); takes effect immediately. */
void gpu_set_scale(int scale);
/* Texture upscaling with xBRZ: 1 = off, 2..4 = factor. Needs an internal scale above 1. */
void gpu_set_texture_scale(int factor);
int gpu_scale(void);
/* Number of display-start changes so far (one per game frame when double buffering). */
u32 gpu_flip_count(void);
/* Widescreen: whether the presented frame contains GTE-projected 3D polygons. */
bool gpu_frame_has_3d(void);

#endif
