/* SPU audio output. */
#ifndef NC_PORT_HW_SPU_H
#define NC_PORT_HW_SPU_H

#include "port/recomp.h"

#define SPU_RATE 44100

/* Renders `frames` stereo frames of interleaved 16-bit audio at 44.1 kHz. */
void spu_render(s16 *out, int frames);
/* Output gains, 0..256 (= 0..100%): master, music (CD audio), effects (SPU voices). */
void spu_set_gains(int master, int music, int sfx);

#endif
