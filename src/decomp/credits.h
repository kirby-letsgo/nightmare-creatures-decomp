#ifndef NC_DECOMP_CREDITS_H
#define NC_DECOMP_CREDITS_H

#include "port/recomp.h"

bool nc_credits_skip_input(CPUState *c);

bool nc_credits_set_cd_volume(CPUState *c);

bool nc_credits_init_heap(CPUState *c);

bool nc_credits_find_allocation(CPUState *c);

bool nc_credits_run(CPUState *c);

bool nc_credits_load_tim(CPUState *c);

bool nc_credits_load_file(CPUState *c);

bool nc_credits_read_sectors(CPUState *c);

bool nc_credits_init_pad(CPUState *c);

bool nc_credits_stop_pad(CPUState *c);

#endif
