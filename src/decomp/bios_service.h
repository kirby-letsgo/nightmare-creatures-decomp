#ifndef NC_DECOMP_BIOS_SERVICE_H
#define NC_DECOMP_BIOS_SERVICE_H

#include "port/recomp.h"

/* The guest selects the BIOS service in the tail jump's delay slot. */
static inline bool bios_service(CPUState *c, u32 table, u32 service) {
    if (c->budget <= 3)
        return false;
    R(10) = table;
    R(9) = service;
    c->budget -= 3;
    nc_jump(c, R(10));
    return true;
}

#endif
