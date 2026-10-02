/* High-level BIOS emulation. */
#ifndef NC_PORT_BIOS_H
#define NC_PORT_BIOS_H

#include "port/recomp.h"

#include <stdbool.h>

void bios_init(void);
void bios_call(CPUState *c, u32 table);
/* Runs the exception path for a pending interrupt: BIOS handlers, then the game's hook. */
void bios_exception(CPUState *c);
bool bios_in_exception(void);
void bios_deliver_event(CPUState *c, u32 cls, u32 spec);
/* Port 1 digital pad state, PS1 bit layout, 1 = pressed. */
void bios_set_pad(u16 buttons);

#endif
