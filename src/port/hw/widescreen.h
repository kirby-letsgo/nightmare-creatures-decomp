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

/* 2D layout. Each frame's 2D primitives are grouped into clusters of neighbouring boxes (a
 * text line, a health bar). Clusters entirely in the left half are pinned to the left edge of
 * the wide frame, entirely in the right half to the right edge, otherwise kept centred. The
 * clusters from the previous frame decide where this frame's primitives go. */

/* Records a 2D primitive's box (raw coordinates, before squeezing) for the next layout. */
void ws_record_2d(int minx, int miny, int maxx, int maxy);
/* Computes the layout from the recorded boxes; called when the game presents a frame. */
void ws_end_frame(void);
/* Horizontal shift (added after scaling X by 3/4) for a 2D primitive with this box. */
int ws_anchor_offset(int minx, int miny, int maxx, int maxy);
/* Maps a 2D X position into the squeezed frame with the given anchor offset. */
int ws_squeeze_x(int x, int offset);

#endif
