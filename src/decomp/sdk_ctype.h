#ifndef NC_DECOMP_SDK_CTYPE_H
#define NC_DECOMP_SDK_CTYPE_H

#include "port/recomp.h"

/* toupper/tolower index the SDK's writable character-class table with the
 * low byte. v1 retains the full input (possibly adjusted); v0 returns only
 * its low byte. */
static inline bool sdk_change_case(CPUState *c, bool uppercase, u32 classes) {
    if (c->budget <= 14)
        return false;
    const u32 classes_hi = (classes + 0x8000u) & 0xffff0000u;
    R(3) = R(4);
    R(1) = classes_hi + (R(4) & 0xff);
    const u8 flags = MEM_R8(R(1) + (classes - classes_hi));
    const bool change = (flags & (uppercase ? 2 : 1)) != 0;
    if (change)
        R(3) += uppercase ? (u32)-32 : 32;
    R(2) = (u8)R(3);
    c->budget -= change ? 14 : 13;
    return true;
}

#endif
