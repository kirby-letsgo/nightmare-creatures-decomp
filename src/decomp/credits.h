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

bool nc_credits_allocate(CPUState *c);

bool nc_credits_free(CPUState *c);

bool nc_credits_allocation_size(CPUState *c);

bool nc_credits_resize(CPUState *c);

bool nc_credits_init_shared_record(CPUState *c);

bool nc_credits_reset_music(CPUState *c);

bool nc_credits_play_track(CPUState *c);

bool nc_credits_empty_hook(CPUState *c);

typedef enum { NC_WARN_CALLOC, NC_WARN_MALLOC, NC_WARN_REALLOC, NC_WARN_FREE } NcHeapWarning;
bool nc_credits_heap_warning(CPUState *c, NcHeapWarning warning);

bool nc_credits_copy_bytes(CPUState *c);

bool nc_credits_fill_bytes(CPUState *c);

bool nc_credits_string_length(CPUState *c);

bool nc_credits_append_string(CPUState *c);

#endif
