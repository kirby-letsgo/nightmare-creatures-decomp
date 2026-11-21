#ifndef NC_PORT_REPLAY_H
#define NC_PORT_REPLAY_H

#include "port/recomp.h"

void replay_init(void);
bool replay_level_enabled(void);
u16 replay_input(void);
/* Called at the game's level-loop safe point. */
void replay_level_frame(void);

#endif
