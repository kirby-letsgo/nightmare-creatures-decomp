#include "decomp/contact.h"

/* Shared by the incoming and outgoing attack loops. Each tests a center and
 * signed extent against inclusive bounds on three axes, returning on the first
 * separation. The path-specific debits and register writes include delay slots. */
NcContactResult nc_attack_bounds(CPUState *c) {
    if (c->budget <= 49)
        return NC_CONTACT_TIMED;

    const u32 extent = R(5), max_bounds = R(7);
    const u32 min_base = R(29) + 0x20;
    for (unsigned axis = 0; axis < 3; axis++) {
        /* The caller has loaded X already. s1 points to Z, with Y one word before it. */
        if (axis)
            R(4) = MEM_R32(R(17) + 4 * axis - 8);
        const u32 center = R(4);
        const s32 upper_bound = (s16)MEM_R16(max_bounds + 2 * axis);
        R(3) = upper_bound < (s32)(center - extent);
        R(2) = axis ? center + extent : min_base;
        c->budget -= axis ? 10 : 7;
        if (R(3))
            return NC_CONTACT_REJECT;

        if (!axis)
            R(6) += min_base; /* convert the box offset into its lower-bound pointer */
        const s32 lower_bound = (s16)MEM_R16(R(6) + 2 * axis);
        const bool separated = (s32)(center + extent) < lower_bound;
        R(3) = (u32)lower_bound;
        R(2) = axis == 2 ? 0x20000000u : (u32)separated;
        c->budget -= axis ? 7 : 8;
        if (separated)
            return NC_CONTACT_REJECT;
    }
    return NC_CONTACT_PASS;
}

/* Normal contact path in func_8004E2A0, after its first response callback.
 * Passing lets the caller evaluate the remaining mode-specific rules. */
NcContactResult nc_target_accepts_contact(CPUState *c) {
    if (c->budget <= 25)
        return NC_CONTACT_TIMED;

    const bool handled = R(2) != 0;
    R(2) = 23;
    c->budget -= 2;
    if (handled)
        return NC_CONTACT_REJECT;

    const u32 target = R(16);
    const u32 state = MEM_R8(target + 0x39B);
    R(4) = state;
    c->budget -= 7;
    if (state != 23) {
        c->budget -= 3;
        if (state != 15 && state != 16) {
            c->budget -= 7;
            if (!(MEM_R32(target + 0x2C8) & 0x00280000u)) {
                R(2) = 0;
                R(3) = 0x00280000u;
                return NC_CONTACT_PASS;
            }
        }
    }

    R(3) = state;
    R(2) = 15;
    c->budget -= 6;
    if (state != 15)
        return NC_CONTACT_REJECT;
    R(2) = MEM_R32(target + 0x330) & 0x8000;
    c->budget -= 7;
    return R(2) ? NC_CONTACT_PASS : NC_CONTACT_REJECT;
}
