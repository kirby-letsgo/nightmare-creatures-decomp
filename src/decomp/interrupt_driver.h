#ifndef NC_DECOMP_INTERRUPT_DRIVER_H
#define NC_DECOMP_INTERRUPT_DRIVER_H

#include "port/recomp.h"

/* The SDK looks up the current driver on each call. VSyncCallback(handler)
 * inserts slot zero before the handler argument in the jalr delay slot. */
static inline bool interrupt_driver_call(CPUState *c, u32 driver_slot, u32 offset,
                                         u32 resume, bool vsync_slot_zero) {
    const int call_cycles = vsync_slot_zero ? 13 : 12;
    if (c->budget <= call_cycles)
        return false;
    if (vsync_slot_zero) {
        R(29) -= 24;
        c->budget -= 2;
        R(2) = MEM_R32(driver_slot);
        R(5) = R(4);
    } else {
        c->budget--;
        R(2) = MEM_R32(driver_slot);
        R(29) -= 24;
    }
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 16, R(31));
    c->budget--;
    R(2) = MEM_R32(R(2) + offset);
    R(31) = resume;
    if (vsync_slot_zero)
        R(4) = 0;
    c->budget -= NC_LOAD_CYCLES + 3;
    nc_call(c, R(2));
    R(31) = MEM_R32(R(29) + 16);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(29) += 24;
    NC_POLL(c, 1);
    NC_POLL(c, 2);
    return true;
}

#endif
