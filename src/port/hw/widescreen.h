/* Widescreen support: telling 3D (GTE-projected) geometry apart from 2D (HUD, text, menus).
 *
 * In widescreen mode the GTE squeezes projected X by 3/4 and the frame is shown at 16:9. 2D
 * elements never pass through the GTE, so they would come out 4/3 too wide. The GTE records
 * every screen position it produces; GPU primitives whose vertices were not produced by it are
 * 2D and get squeezed by the same 3/4 around the screen centre. */
#ifndef NC_PORT_HW_WIDESCREEN_H
#define NC_PORT_HW_WIDESCREEN_H

#include "port/recomp.h"

#include <stdbool.h>

void ws_set_enabled(bool on);
bool ws_enabled(void);

/* GTE side: a projected screen position (SX, SY, before the GPU drawing offset). */
void ws_note_projected(int x, int y);
/* GPU side: was this packet position produced by the GTE recently? */
bool ws_is_projected(int x, int y);
/* Ages the table (only if the GTE projected anything since the last call); once per VBlank. */
void ws_vblank(void);

/* Maps a 2D X position (before the drawing offset) into the squeezed frame. */
int ws_squeeze_x(int x);

#endif
