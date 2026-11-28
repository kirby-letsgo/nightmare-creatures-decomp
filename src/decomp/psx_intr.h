#ifndef NC_DECOMP_PSX_INTR_H
#define NC_DECOMP_PSX_INTR_H

#include "port/recomp.h"

/* PSX.EXE (front end): the Psy-Q interrupt driver and the CD interrupt reader. */
bool nc_psx_start_interrupts(CPUState *c);
bool nc_psx_resume_interrupt_start(CPUState *c);
bool nc_psx_dispatch_interrupts(CPUState *c);
bool nc_psx_set_interrupt_handler(CPUState *c);
bool nc_psx_stop_interrupts(CPUState *c);
bool nc_psx_restart_interrupts(CPUState *c);
bool nc_psx_get_cd_interrupt(CPUState *c);

#endif
