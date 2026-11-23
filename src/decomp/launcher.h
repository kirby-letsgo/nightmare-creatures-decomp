#ifndef NC_DECOMP_LAUNCHER_H
#define NC_DECOMP_LAUNCHER_H

#include "port/recomp.h"

bool nc_launcher_convert_case(CPUState *c, bool upper);
bool nc_launcher_switch_stack(CPUState *c);
bool nc_launcher_run_modules(CPUState *c);
bool nc_launcher_init_shared_record(CPUState *c);
bool nc_launcher_global_callbacks(CPUState *c, bool constructors);

bool nc_launcher_stop_interrupts(CPUState *c);
bool nc_launcher_restart_interrupts(CPUState *c);
bool nc_launcher_start_dma(CPUState *c);
bool nc_launcher_start_vsync(CPUState *c);
bool nc_launcher_dispatch_vblank(CPUState *c);
bool nc_launcher_clear_irq_words(CPUState *c);

bool nc_launcher_cd_init(CPUState *c);
bool nc_launcher_cd_read(CPUState *c);
bool nc_launcher_cd_read_sync(CPUState *c);
bool nc_launcher_cd_interrupt_callback(CPUState *c);
bool nc_launcher_cd_init_interrupt(CPUState *c);

bool nc_launcher_vsync(CPUState *c);
bool nc_launcher_wait_vblank(CPUState *c);

#endif
