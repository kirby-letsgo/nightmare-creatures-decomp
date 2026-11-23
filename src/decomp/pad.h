#ifndef NC_DECOMP_PAD_H
#define NC_DECOMP_PAD_H

#include "port/recomp.h"

typedef enum {
    NC_BIOS_INIT_PAD = 0x12,
    NC_BIOS_START_PAD = 0x13,
    NC_BIOS_STOP_PAD = 0x14,
    NC_BIOS_CHANGE_CLEAR_PAD = 0x5b
} NcPadBiosService;

bool nc_record_player_button_press(CPUState *c);
bool nc_reset_repeat_on_l2_press(CPUState *c);
bool nc_check_player_r2_press(CPUState *c);
u32 nc_dispatch_player_r2_action(CPUState *c);
bool nc_leave_player_r2(CPUState *c, bool record_buttons);
bool nc_enter_player_input(CPUState *c);
bool nc_leave_player_input(CPUState *c, bool store_buttons);
bool nc_call_player_trigger(CPUState *c, u32 entry);
bool nc_call_live_pad(CPUState *c, u32 return_address);
u32 nc_player_input_source(CPUState *c);
void nc_take_live_player_buttons(CPUState *c);
u32 nc_dispatch_player_buttons(CPUState *c);
u32 nc_enter_pad_dispatch(CPUState *c);
u32 nc_pad_playback_poll_gate(CPUState *c);
u32 nc_poll_playback_input(CPUState *c);
bool nc_pad_playback_interrupt(CPUState *c, bool check_sequence_end);
bool nc_call_pad_action(CPUState *c, bool select_pressed);
bool nc_leave_pad_dispatch(CPUState *c);
u32 nc_pad_select_edge(CPUState *c);
u32 nc_pad_start_edge(CPUState *c);
u32 nc_menu_button_scan_entry(CPUState *c);
u32 nc_menu_sequence_match(CPUState *c);
u32 nc_menu_record_sequence(CPUState *c);
bool nc_menu_apply_enabled_record(CPUState *c);
u32 nc_menu_advance_enabled_record(CPUState *c);
bool nc_menu_apply_sequence(CPUState *c);
bool nc_menu_reset_sequences(CPUState *c);
bool nc_menu_store_progress(CPUState *c, bool matched);
u32 nc_menu_next_record(CPUState *c, bool increment);
bool nc_leave_menu_button_scan(CPUState *c);
bool nc_menu_apply_direction(CPUState *c);
bool nc_menu_scan_and_save_buttons(CPUState *c);
u32 nc_menu_record_shoulders(CPUState *c);
bool nc_menu_record_button_pairs(CPUState *c);
u32 nc_menu_record_triangle(CPUState *c);
bool nc_menu_dispatch_feedback_action(CPUState *c);
bool nc_menu_dispatch_record_action(CPUState *c);
u32 nc_menu_record_select(CPUState *c);
bool nc_menu_apply_record_select(CPUState *c);
u32 nc_menu_record_direction_repeat(CPUState *c);
u32 nc_menu_record_shoulder_repeat(CPUState *c);
u32 nc_menu_record_face_edges(CPUState *c);
u32 nc_menu_record_face_repeat(CPUState *c);
u32 nc_menu_record_start(CPUState *c);
bool nc_menu_dispatch_and_save_buttons(CPUState *c);
u32 nc_menu_save_record_buttons(CPUState *c, bool load_record);
bool nc_menu_reset_record_100(CPUState *c);
u32 nc_menu_check_record_101(CPUState *c);
bool nc_menu_dispatch_timed_record(CPUState *c);
bool nc_menu_check_record_102(CPUState *c);
u32 nc_menu_check_record_103(CPUState *c);
u32 nc_menu_mode_face_buttons(CPUState *c, bool mask_cross);
u32 nc_menu_mode_down(CPUState *c);
bool nc_menu_dispatch_face_button(CPUState *c);
u32 nc_enter_menu_input(CPUState *c);
u32 nc_menu_select_mode_record(CPUState *c);
bool nc_menu_call_selected_record(CPUState *c);
bool nc_menu_record_timeout(CPUState *c);
bool nc_menu_store_counter(CPUState *c, bool increment);
u32 nc_menu_record_29_thresholds(CPUState *c);
bool nc_menu_advance_record_counter(CPUState *c);
u32 nc_menu_check_count_130(CPUState *c);
u32 nc_menu_dispatch_record_29(CPUState *c);
u32 nc_menu_calibration_triangle(CPUState *c, bool after_action);
u32 nc_menu_record_pad_gate(CPUState *c, u32 entry);
bool nc_menu_calibration_action(CPUState *c);
bool nc_menu_record_37_action(CPUState *c);
u32 nc_menu_record_37_read_stick(CPUState *c);
u32 nc_menu_record_37_pad_type(CPUState *c);
bool nc_menu_record_26_right(CPUState *c);
u32 nc_menu_record_26_left(CPUState *c);
u32 nc_menu_dispatch_record_24(CPUState *c);
u32 nc_menu_record_input_gate(CPUState *c, bool check_primary);
u32 nc_menu_check_record_29(CPUState *c);
bool nc_menu_mode_record_call(CPUState *c);
bool nc_leave_menu_input(CPUState *c);
bool nc_menu_mode_record_result(CPUState *c);
bool nc_menu_dispatch_input_action(CPUState *c);
bool nc_menu_dispatch_direction(CPUState *c);
u32 nc_menu_mode_up(CPUState *c);
bool nc_menu_record_cross(CPUState *c);
u32 nc_menu_record_four_timing(CPUState *c);
bool nc_menu_clear_repeat(CPUState *c);
u32 nc_menu_direction_selection(CPUState *c, bool right);
u32 nc_menu_horizontal_repeat(CPUState *c);
u32 nc_menu_vertical_repeat(CPUState *c);
u32 nc_menu_face_press(CPUState *c);
u32 nc_menu_start_down(CPUState *c);
u32 nc_menu_down_repeat(CPUState *c);
u32 nc_menu_up_from_buttons(CPUState *c);
u32 nc_menu_up_repeat(CPUState *c);
u32 nc_menu_right_input(CPUState *c);
bool nc_menu_horizontal_feedback(CPUState *c, bool right);
bool nc_menu_format_horizontal_feedback(CPUState *c);
bool nc_menu_horizontal_action(CPUState *c, bool left);
u32 nc_menu_left_input(CPUState *c);
bool nc_menu_pad_type_flags(CPUState *c);
bool nc_menu_store_stick_calibration(CPUState *c);
bool nc_menu_expand_stick_bounds(CPUState *c);
bool nc_menu_sample_stick_bounds(CPUState *c);
u32 nc_menu_confirm_edge(CPUState *c);
bool nc_pad_inactivity(CPUState *c);
bool nc_check_player_button_suppression(CPUState *c);
bool nc_clear_player_buttons(CPUState *c);
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
