/* Raw 2352-byte data and little-endian CD audio for the supported USA pressing. */
#ifndef NC_PORT_RAW_DISC_H
#define NC_PORT_RAW_DISC_H

#include "port/disc.h"

typedef struct RawDisc RawDisc;
DiscCheck raw_disc_open(const char *path, RawDisc **out);
void raw_disc_close(RawDisc *raw);
int raw_disc_toc(DiscTrack *tracks);
bool raw_disc_read(RawDisc *raw, int track, u32 relative, u8 *out);

#endif
