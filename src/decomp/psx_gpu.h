#ifndef NC_DECOMP_PSX_GPU_H
#define NC_DECOMP_PSX_GPU_H

#include "port/recomp.h"

/* PSX.EXE (front end): Psy-Q libgpu through the shared sdk_gpu.h routines, and libgte. */
bool nc_psx_default_draw(CPUState *c);
bool nc_psx_texture_page(CPUState *c);
bool nc_psx_dump_texture_page(CPUState *c);
bool nc_psx_draw_tpage(CPUState *c);
bool nc_psx_dump_draw(CPUState *c);
bool nc_psx_dump_display(CPUState *c);
bool nc_psx_reset_graph(CPUState *c);
bool nc_psx_set_graph_reverse(CPUState *c);
bool nc_psx_draw_sync_callback(CPUState *c);
bool nc_psx_draw_sync(CPUState *c);
bool nc_psx_check_rect(CPUState *c);
bool nc_psx_clear_image(CPUState *c);
bool nc_psx_clear_image2(CPUState *c);
bool nc_psx_load_image(CPUState *c);
bool nc_psx_store_image(CPUState *c);
bool nc_psx_draw_otag(CPUState *c);
bool nc_psx_put_draw_env(CPUState *c);
bool nc_psx_draw_otag_env(CPUState *c);
bool nc_psx_put_display_env(CPUState *c);
bool nc_psx_draw_area_packet(CPUState *c);
bool nc_psx_draw_offset_packet(CPUState *c);
bool nc_psx_draw_mode_packet(CPUState *c);
bool nc_psx_set_draw_env(CPUState *c);
bool nc_psx_clear_gpu_rectangle(CPUState *c);
bool nc_psx_upload_gpu_image(CPUState *c);
bool nc_psx_read_gpu_image(CPUState *c);
bool nc_psx_submit_gpu_command(CPUState *c);
bool nc_psx_execute_gpu_queue(CPUState *c);
bool nc_psx_reset_gpu_driver(CPUState *c);
bool nc_psx_sync_gpu_queue(CPUState *c);
bool nc_psx_check_queue_alarm(CPUState *c);

bool nc_psx_set_geom_offset(CPUState *c);
bool nc_psx_gpu_control(CPUState *c);
bool nc_psx_gpu_control_byte(CPUState *c);
bool nc_psx_patch_gte(CPUState *c);

#endif
