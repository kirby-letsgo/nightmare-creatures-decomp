#ifndef NC_DECOMP_CD_STATE_H
#define NC_DECOMP_CD_STATE_H

#include "port/recomp.h"

/* CdStatus, CdMode and CdLastCom each return one cached unsigned byte. */
static inline bool cd_state_byte(CPUState *c, u32 address) {
    if (c->budget <= 6)
        return false;
    c->budget--; /* lui */
    R(2) = MEM_R8(address);
    c->budget -= NC_LOAD_CYCLES + 2; /* lbu, jr and its delay slot */
    return true;
}

static inline bool cd_last_position(CPUState *c, u32 address) {
    if (c->budget <= 4)
        return false;
    R(2) = address; /* Address of the cached CD position. */
    c->budget -= 4;
    return true;
}

/* These CD setters return the previous word before installing a0. */
static inline bool exchange_cd_word(CPUState *c, u32 address) {
    if (c->budget <= 8)
        return false;
    c->budget--; /* lui */
    R(2) = MEM_R32(address);
    R(1) = (address + 0x8000u) & 0xffff0000u;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(address, R(4));
    c->budget -= 3; /* sw, jr and its delay slot */
    return true;
}

#endif
