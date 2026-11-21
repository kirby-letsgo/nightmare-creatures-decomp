#ifndef NC_PORT_REPLAY_H
#define NC_PORT_REPLAY_H

#include "port/recomp.h"

bool replay_init(const char *record, const char *play);
bool replay_movie_active(void);
u16 replay_pad(u16 buttons);
bool replay_level_enabled(void);
u16 replay_input(void);
/* Called at the game's level-loop safe point. */
void replay_level_frame(void);

#endif
