#ifndef NC_DECOMP_PSX_SPU_H
#define NC_DECOMP_PSX_SPU_H

#include "port/recomp.h"

/* PSX.EXE (front end): libspu setup and teardown and the libsnd wrappers around it.
 * The SPU driver core (_spu_init, _spu_writeByIO, _spu_t) is ported from CREDITS.EXE
 * (src/decomp/twins). */
bool nc_psx_reverb_on(CPUState *c);
bool nc_psx_reverb_off(CPUState *c);
bool nc_psx_spu_data_callback(CPUState *c);
bool nc_psx_spu_init(CPUState *c);
bool nc_psx_spu_start(CPUState *c);
bool nc_psx_spu_quit(CPUState *c);

#endif
