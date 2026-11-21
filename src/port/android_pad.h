#ifndef NC_PORT_ANDROID_PAD_H
#define NC_PORT_ANDROID_PAD_H

#include "port/recomp.h"

#include <SDL3/SDL.h>

u16 android_pad_read(void);
void android_pad_draw(SDL_Renderer *renderer, u16 buttons);

#endif
