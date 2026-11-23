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

/* CdComstr and CdIntstr return guest pointers into the CD diagnostic strings. */
static inline bool cd_lookup_name(CPUState *c, u32 table, u32 count, u32 unknown) {
    if (c->budget <= 13)
        return false;
    R(4) &= 0xff;
    c->budget -= 4; /* mask, range check, branch and delay slot */
    if (R(4) < count) {
        u32 offset = R(4) * 4;
        R(1) = ((table + 0x8000u) & 0xffff0000u) + offset;
        c->budget -= 2;
        R(2) = MEM_R32(table + offset);
        c->budget -= NC_LOAD_CYCLES + 2; /* load and jump to the return */
    } else {
        R(2) = unknown;
        c->budget -= 2;
    }
    c->budget -= 2; /* jr and delay slot */
    return true;
}

#endif
