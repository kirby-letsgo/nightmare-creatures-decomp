#ifndef NC_DECOMP_PAD_H
#define NC_DECOMP_PAD_H

#include "port/recomp.h"

bool nc_select_pad(CPUState *c);
bool nc_decode_pad(CPUState *c);
u32 nc_read_pad(CPUState *c);

#endif
