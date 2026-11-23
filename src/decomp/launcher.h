#ifndef NC_DECOMP_LAUNCHER_H
#define NC_DECOMP_LAUNCHER_H

#include "port/recomp.h"

bool nc_launcher_run_modules(CPUState *c);
bool nc_launcher_init_shared_record(CPUState *c);
bool nc_launcher_global_callbacks(CPUState *c, bool constructors);

bool nc_launcher_stop_interrupts(CPUState *c);
bool nc_launcher_restart_interrupts(CPUState *c);
bool nc_launcher_clear_irq_words(CPUState *c);

#endif
