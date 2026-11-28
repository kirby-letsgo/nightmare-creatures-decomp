#ifndef NC_DECOMP_PSX_LIBC_H
#define NC_DECOMP_PSX_LIBC_H

#include "port/recomp.h"

/* PSX.EXE (front end): Psy-Q C library and BIOS / memory card stubs. */

bool nc_psx_sprintf(CPUState *c);
bool nc_psx_format_output(CPUState *c);
bool nc_psx_put_char(CPUState *c);
bool nc_psx_change_case(CPUState *c, bool uppercase);
bool nc_psx_move_bytes(CPUState *c);
void nc_psx_longjmp(CPUState *c);

/* A0/B0 table stubs: the service is selected in the tail jump's delay slot. */
bool nc_psx_bios_service(CPUState *c, u32 table, u32 service);

bool nc_psx_init_card(CPUState *c);
bool nc_psx_start_card(CPUState *c);
bool nc_psx_stop_card(CPUState *c);
bool nc_psx_card_clear(CPUState *c);
bool nc_psx_patch_card(CPUState *c);
bool nc_psx_patch_card2(CPUState *c);
bool nc_psx_exit_card(CPUState *c);

#endif
