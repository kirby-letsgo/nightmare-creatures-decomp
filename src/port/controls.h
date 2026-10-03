/* Modern controls: camera-relative movement via the player-control input hook. */
#ifndef NC_PORT_CONTROLS_H
#define NC_PORT_CONTROLS_H

#include "port/recomp.h"

void nc_hook_player_input(CPUState *c);
void nc_hook_camera_view_begin(CPUState *c);
void nc_hook_camera_update_begin(CPUState *c);

#endif
