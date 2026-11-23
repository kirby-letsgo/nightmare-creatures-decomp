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

/* The default callbacks notify the BIOS CD event class through DeliverEvent. */
static inline bool cd_default_event(CPUState *c, u32 spec, u32 resume, u32 deliver) {
    if (c->budget <= 6)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(4) = 0xf0000003;
    R(31) = resume;
    R(5) = spec;
    c->budget -= 5;
    nc_call(c, deliver); /* DeliverEvent */
    R(31) = MEM_R32(R(29) + 16);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(29) += 24;
    NC_POLL(c, 1);
    NC_POLL(c, 2);
    return true;
}

enum CdDriverResult { CD_RETURN_STATUS, CD_RETURN_ONE, CD_ZERO_IS_SUCCESS };

/* Forward the arguments, then apply the SDK wrapper's return convention. */
static inline bool call_cd_status_driver(CPUState *c, u32 driver, u32 return_pc,
                                         enum CdDriverResult result) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(31) = return_pc;
    c->budget -= 3;
    nc_call(c, driver);
    if (result != CD_RETURN_STATUS) {
        R(2) = result == CD_RETURN_ONE || R(2) == 0;
        NC_POLL(c, 1);
    }
    R(31) = MEM_R32(R(29) + 16);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(29) += 24;
    NC_POLL(c, 1);
    NC_POLL(c, 2);
    return true;
}

#endif
