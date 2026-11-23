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

/* The SDK selects the BIOS critical-section syscall in a0. */
static inline bool critical_section_syscall(CPUState *c, u32 selector) {
    if (c->budget <= 4)
        return false;
    R(4) = selector;
    c->budget--;
    nc_syscall(c);
    c->budget -= 3;
    return true;
}

#endif
