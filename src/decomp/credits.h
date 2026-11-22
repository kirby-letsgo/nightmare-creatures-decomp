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

bool nc_credits_link_packets(CPUState *c);

bool nc_credits_draw_tpage(CPUState *c);
bool nc_credits_draw_mode_packet(CPUState *c);
bool nc_credits_draw_mode_word(CPUState *c);
bool nc_credits_draw_area_packet(CPUState *c);
bool nc_credits_draw_corner(CPUState *c, bool bottom_right);
bool nc_credits_read_environment(CPUState *c, bool display);
bool nc_credits_gpu_version(CPUState *c);
bool nc_credits_gpu_parameter(CPUState *c);
bool nc_credits_gpu_fill_bytes(CPUState *c);
bool nc_credits_set_queue_alarm(CPUState *c);
bool nc_credits_queue_command(CPUState *c);
bool nc_credits_gpu_command_chain(CPUState *c);
bool nc_credits_gpu_command_block(CPUState *c);
bool nc_credits_gpu_control(CPUState *c);
bool nc_credits_gpu_control_byte(CPUState *c);
bool nc_credits_graphics_status(CPUState *c);
bool nc_credits_display_field(CPUState *c);
bool nc_credits_display_x(CPUState *c);
bool nc_credits_draw_offset_word(CPUState *c);
bool nc_credits_draw_offset_packet(CPUState *c);
bool nc_credits_texture_window_word(CPUState *c);
bool nc_credits_texture_window_packet(CPUState *c);
bool nc_credits_draw_move(CPUState *c);
bool nc_credits_draw_load(CPUState *c);
bool nc_credits_merge_primitives(CPUState *c);
bool nc_credits_end_packet(CPUState *c);

bool nc_credits_texture_page(CPUState *c);
bool nc_credits_dump_texture_page(CPUState *c);
bool nc_credits_dump_palette(CPUState *c);
bool nc_credits_palette_id(CPUState *c);

bool nc_credits_video_mode(CPUState *c, bool replace);

bool nc_credits_priority_packet(CPUState *c);
bool nc_credits_dump_draw(CPUState *c);
bool nc_credits_dump_display(CPUState *c);
bool nc_credits_draw_sync_callback(CPUState *c);
bool nc_credits_set_graph_debug(CPUState *c);
bool nc_credits_tim_info(CPUState *c);
bool nc_credits_spu_write_register(CPUState *c);
bool nc_credits_spu_set_address(CPUState *c);
bool nc_credits_spu_read_address(CPUState *c);
bool nc_credits_spu_set_dma_priority(CPUState *c);
bool nc_credits_spu_wait(CPUState *c);
bool nc_credits_spu_set_delay(CPUState *c, bool reading);
bool nc_credits_bios_gpu_command(CPUState *c);
bool nc_credits_deliver_event(CPUState *c);
bool nc_credits_cd_default_sync(CPUState *c);
bool nc_credits_cd_default_ready(CPUState *c);
bool nc_credits_cd_default_read(CPUState *c);
bool nc_credits_cd_flush_controller(CPUState *c);
bool nc_credits_cd_flush(CPUState *c);
bool nc_credits_cd_status(CPUState *c);
bool nc_credits_cd_mode(CPUState *c);
bool nc_credits_cd_last_command(CPUState *c);
bool nc_credits_cd_set_debug(CPUState *c);
bool nc_credits_cd_sync_callback(CPUState *c);
bool nc_credits_cd_ready_callback(CPUState *c);
bool nc_credits_cd_command_name(CPUState *c);
bool nc_credits_cd_interrupt_name(CPUState *c);
bool nc_credits_cd_sector_to_position(CPUState *c);
bool nc_credits_cd_search_directory(CPUState *c);
bool nc_credits_cd_copy_identifier(CPUState *c);
bool nc_credits_strcmp(CPUState *c);
bool nc_credits_strncmp(CPUState *c);
bool nc_credits_cd_names_equal(CPUState *c);
bool nc_credits_cd_position_to_sector(CPUState *c);
bool nc_credits_cd_set_test_parameter_count(CPUState *c);
bool nc_credits_cd_last_position(CPUState *c);
bool nc_credits_graph_status(CPUState *c, bool debug_level);
bool nc_credits_default_draw(CPUState *c);
bool nc_credits_default_display(CPUState *c);

#endif
