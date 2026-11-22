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

bool nc_credits_printf(CPUState *c);

bool nc_credits_change_case(CPUState *c, bool uppercase);

typedef enum {
    NC_POLY_F3,
    NC_POLY_FT3,
    NC_POLY_G3,
    NC_POLY_GT3,
    NC_POLY_F4,
    NC_POLY_FT4,
    NC_POLY_G4,
    NC_POLY_GT4,
    NC_SPRT8,
    NC_SPRT16,
    NC_SPRT,
    NC_TILE1,
    NC_TILE8,
    NC_TILE16,
    NC_TILE,
    NC_LINE_F2,
    NC_LINE_G2,
    NC_LINE_F3,
    NC_LINE_G3,
    NC_LINE_F4,
    NC_LINE_G4,
    NC_BLOCK_FILL,
} NcCreditsPrimitive;
bool nc_credits_init_primitive(CPUState *c, NcCreditsPrimitive primitive);

bool nc_credits_packet_flag(CPUState *c, u8 mask);

bool nc_credits_prepend_packets(CPUState *c, bool range);

bool nc_credits_packet_link(CPUState *c, bool check_end);

#endif
