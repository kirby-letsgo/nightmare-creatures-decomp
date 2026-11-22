#ifndef NC_DECOMP_PAD_H
#define NC_DECOMP_PAD_H

#include "port/recomp.h"

typedef enum {
    NC_BIOS_INIT_PAD = 0x12,
    NC_BIOS_START_PAD = 0x13,
    NC_BIOS_STOP_PAD = 0x14,
    NC_BIOS_CHANGE_CLEAR_PAD = 0x5b
} NcPadBiosService;

bool nc_enter_player_input(CPUState *c);
bool nc_leave_player_input(CPUState *c, bool store_buttons);
u32 nc_player_input_source(CPUState *c);
u32 nc_dispatch_player_buttons(CPUState *c);
bool nc_pad_inactivity(CPUState *c);
bool nc_player_l2_direction(CPUState *c, bool l2_held);
bool nc_pad_bios_call(CPUState *c, NcPadBiosService service);
bool nc_sdk_patch_pad(CPUState *c);
bool nc_sdk_init_pad(CPUState *c);
bool nc_sdk_start_pad(CPUState *c);
bool nc_start_pad_driver(CPUState *c);
bool nc_stop_pad_driver(CPUState *c);
bool nc_pad_packet_type(CPUState *c);
bool nc_read_pad_stick(CPUState *c);
bool nc_select_pad(CPUState *c);
bool nc_decode_pad(CPUState *c);
u32 nc_read_pad(CPUState *c);

bool nc_init_pad_sequence(CPUState *c);
bool nc_stop_pad_sequence(CPUState *c);
bool nc_pad_sequence_ended(CPUState *c);
bool nc_read_pad_sequence(CPUState *c);

#endif
