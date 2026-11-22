#include "decomp/contact.h"

/* Resolve the part requested by either of the player's attack slots.
 * The contact loop supplies a default part when this lookup returns zero. */
bool nc_attack_part(CPUState *c) {
    const u32 actor = R(4);
    int cycles;
    unsigned offset;
    switch (R(5)) {
    case 21:
        offset = 0x88;
        cycles = 10;
        break;
    case 19:
        offset = 0x88;
        cycles = 12;
        break;
    case 18:
        offset = 0x84;
        cycles = 12;
        break;
    case 20:
        offset = 0x84;
        cycles = 14;
        break;
    default:
        offset = 0;
        cycles = 11;
        break;
    }
    if (c->budget <= cycles)
        return false;
    R(2) = offset ? MEM_R32(actor + offset) : 0;
    c->budget -= cycles;
    return true;
}

/* The primary slot can fall back to a separate part for selectors 33 and 34.
 * The secondary slot falls back to the second part list. */
bool nc_finish_attack_part(CPUState *c, bool primary) {
    if (c->budget <= (primary ? 23 : 13))
        return false;
    unsigned part_reg = primary ? 30 : 9;
    R(part_reg) = R(2);
    R(8) = MEM_R32(R(29) + 0xf8);
    c->budget -= 6;
    if (R(part_reg))
        return true;
    const u32 actor = MEM_R32(R(29) + 0xc0);
    R(11) = actor;
    if (primary) {
        u32 selector = MEM_R8(actor + 0x391);
        bool alternate = selector == 33 || selector == 34;
        R(2) = alternate;
        R(part_reg) = MEM_R32(actor + (alternate ? 0xcc : 0x274));
        c->budget -= alternate ? 17 : 15;
    } else {
        R(part_reg) = MEM_R32(actor + 0x278);
        c->budget -= 7;
    }
    return true;
}

/* Outgoing contact is disabled by either game flag and in actor states 13, 14 and 24. */
NcContactResult nc_player_can_attack(CPUState *c) {
    if (c->budget <= 25)
        return NC_CONTACT_TIMED;
    const u32 flags = R(3), actor = R(11);
    R(2) = MEM_R32(flags) & 0x8000;
    R(8) = 0;
    MEM_W32(R(29) + 0xc0, actor);
    c->budget -= 7;
    if (R(2))
        return NC_CONTACT_REJECT;

    R(3) = 0x02000000;
    R(2) = MEM_R32(flags + 4) & R(3);
    c->budget -= 7;
    if (R(2))
        return NC_CONTACT_REJECT;

    R(3) = MEM_R8(actor + 0x39b);
    R(2) = R(3) == 13 || R(3) == 14;
    c->budget -= 8;
    if (R(2))
        return NC_CONTACT_REJECT;
    R(2) = 24;
    c->budget -= 3;
    return R(3) == 24 ? NC_CONTACT_REJECT : NC_CONTACT_PASS;
}

/* The caller has copied 24 primary (state, kind) pairs and two secondary
 * pairs to its stack. A zero result skips outgoing contact detection. */
bool nc_player_attack_kind(CPUState *c) {
    if (c->budget <= 271)
        return false;
    const u32 actor = R(8), table = R(29);
    int cycles = 5;
    R(5) = MEM_R8(actor + 0x38A);
    for (unsigned i = 0; i < 24; i++) {
        R(4) = table + 4 * i;
        cycles += 6;
        if (R(5) == (u32)(s32)(s16)MEM_R16(R(4))) {
            R(3) = 2 * (i + 1);
            R(2) = (u32)(s32)(s16)MEM_R16(R(4) + 2);
            cycles += 5;
            goto done;
        }
        cycles += 3;
    }
    R(4) = table + 96;
    cycles += 6;
    if (MEM_R32(actor + 0x44)) {
        R(4) = MEM_R8(actor + 0x38B);
        cycles += 6;
        if (R(4) != 0xff) {
            R(6) = R(4);
            cycles += 2;
            for (unsigned i = 0; i < 2; i++) {
                R(4) = table + 4 * i;
                R(5) = 4 * i;
                cycles += 6;
                if (R(6) == (u32)(s32)(s16)MEM_R16(R(4) + 96)) {
                    R(3) = 2 * i;
                    R(2) = (u32)(s32)(s16)MEM_R16(R(4) + 98);
                    cycles += 6;
                    goto done;
                }
                cycles += 4;
            }
            R(4) = table + 8;
        }
    }
    R(3) = MEM_R32(actor + 0x2C8) & 0x8000;
    R(2) = 0;
    cycles += 7;
    if (R(3)) {
        R(2) = MEM_R8(actor + 0x39B) == 19;
        cycles += 6;
    }
done:
    R(29) = table + 104;
    c->budget -= cycles + 3;
    return true;
}

/* Expand one center/extent record into the lower and upper corner arrays.
 * Each of the four inlined copies takes 54 cycles. Preserve load/store order
 * because guest pointers can alias, and leave each caller's cursors in place. */
bool nc_prepare_attack_box(CPUState *c, bool outgoing) {
    if (c->budget <= 54)
        return false;
    const u32 extent = R(4);
    const u32 coords[] = {outgoing ? R(23) : R(5), extent - 8, extent - 4};
    const u32 lower = outgoing ? R(5) : R(6);
    const u32 upper = outgoing ? R(6) : R(7);
    for (unsigned axis = 0; axis < 3; axis++) {
        u32 center = MEM_R32(coords[axis]);
        MEM_W16(lower + 2 * axis, center - MEM_R16(extent));
    }
    for (unsigned axis = 0; axis < 3; axis++) {
        R(3) = MEM_R32(coords[axis]);
        R(2) = R(3) + MEM_R16(extent);
        MEM_W16(upper + 2 * axis, R(2));
    }
    if (outgoing) {
        R(21)++;
        R(8)++;
        R(5) += 8;
        R(4) += 16;
    } else {
        R(18)++;
        R(23)++;
        R(5) += 16;
        R(6) += 8;
    }
    c->budget -= 54;
    return true;
}

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
