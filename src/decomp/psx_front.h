/* PSX.EXE, the front end: startup, memory card check, the card warning screen and shutdown,
 * with the record heap, display and TIM/TMD helpers it links. */
#ifndef NC_DECOMP_PSX_FRONT_H
#define NC_DECOMP_PSX_FRONT_H

#include "decomp/credits.h" /* NcHeapWarning */
#include "port/recomp.h"

/* Startup and the front-end flow. */
bool nc_psx_start(CPUState *c);
bool nc_psx_main(CPUState *c);
bool nc_psx_front_end(CPUState *c);
bool nc_psx_show_tim_screen(CPUState *c);
bool nc_psx_load_screen_tim(CPUState *c);
bool nc_psx_restart_display(CPUState *c);
bool nc_psx_shutdown_display(CPUState *c);
bool nc_psx_load_file(CPUState *c);
bool nc_psx_count_vblank(CPUState *c);
bool nc_psx_frame_timer(CPUState *c);
bool nc_psx_init_shared_record(CPUState *c);
bool nc_psx_card_patch_jump(CPUState *c);
bool nc_psx_card_wait_ack(CPUState *c);

/* Memory card. */
bool nc_psx_card_open_events(CPUState *c);
bool nc_psx_card_close_events(CPUState *c);
bool nc_psx_card_test_events(CPUState *c);
bool nc_psx_card_test_sw_events(CPUState *c);
bool nc_psx_card_test_hw_events(CPUState *c);
bool nc_psx_card_wait_sw_event(CPUState *c);
bool nc_psx_card_wait_hw_event(CPUState *c);
bool nc_psx_card_probe(CPUState *c);
bool nc_psx_card_formatted(CPUState *c);
bool nc_psx_card_file_exists(CPUState *c);
bool nc_psx_card_free_blocks(CPUState *c);

/* Display. */
bool nc_psx_init_display_buffers(CPUState *c);
bool nc_psx_setup_display(CPUState *c);
bool nc_psx_apply_screen_offset(CPUState *c);
bool nc_psx_clear_display(CPUState *c);
bool nc_psx_init_geom(CPUState *c);
bool nc_psx_set_geom_screen(CPUState *c);

/* Record heap. */
bool nc_psx_init_heap(CPUState *c);
bool nc_psx_resize(CPUState *c);
bool nc_psx_allocate(CPUState *c);
bool nc_psx_free(CPUState *c);
bool nc_psx_find_allocation(CPUState *c);
bool nc_psx_allocation_size(CPUState *c);
bool nc_psx_heap_warning(CPUState *c, NcHeapWarning warning);

/* TIM and TMD readers (psx_tmd.c). */
bool nc_psx_open_tim(CPUState *c);
bool nc_psx_read_tim(CPUState *c);
bool nc_psx_tim_info(CPUState *c);
bool nc_psx_open_tmd(CPUState *c);
bool nc_psx_read_tmd(CPUState *c);
bool nc_psx_tmd_info(CPUState *c);

#endif
