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
} GpuDisplay;

void gpu_display_info(GpuDisplay *out);
/* Converts the display area to RGBA8888 (dst holds width*height pixels). */
void gpu_display_rgba(u32 *dst, const GpuDisplay *d);
const u16 *gpu_vram(void);

#endif
