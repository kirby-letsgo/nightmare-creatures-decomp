#include "decomp/damage.h"
#include "port/game_vars.h"

enum {
    DAMAGE_TABLE = 0x800D42C8u,
    DAMAGE_TIER = 0x800CBFF6u,
    DAMAGE_ROW_SIZE = 52,
};

static u32 damage_entry(u32 actor, u32 kind) {
    u32 row = MEM_R8(actor + 0x388);
    return MEM_R8(DAMAGE_TABLE + row * DAMAGE_ROW_SIZE + kind);
}

/* US PSX2.EXE 8004E6E0 through the return. The entry instruction has already
 * copied the recipient into t0 and run the replay hook. The cycle totals cover
 * complete paths, including their load costs, branch delays, and return. */
bool nc_select_base_damage(CPUState *c) {
    if (c->budget <= 93)
        return false;

    const u32 recipient = R(8), attacker = R(5), kind = R(6);
    u32 power, divisor = 255;
    int cycles;
    R(4) = DAMAGE_TABLE;
    if (MEM_R32(recipient + 0x2C8) & 1) {
        if (attacker) {
            const u32 rules = MEM_R32(R(28) + 0x244);
            bool use_recipient = (MEM_R32(rules) & 0x07800000u) != 0;
            power = damage_entry(use_recipient ? recipient : attacker, kind);
            cycles = use_recipient ? 55 : 53;
            if (MEM_R32(attacker + 0x2D0) & 0x00100000u) {
                power *= 2;
                cycles += 2;
            }
        } else {
            power = damage_entry(recipient, kind);
            cycles = 39;
            if (kind >= 24 && kind <= 32) {
                power /= 2;
                cycles += 2;
            }
        }
    } else if (!attacker) {
        power = damage_entry(recipient, kind);
        const u32 rules = MEM_R32(R(28) + 0x244);
        cycles = 45;
        if (MEM_R32(rules) & 0x01000000u) {
            power = 0;
            cycles++;
        }
    } else if (MEM_R32(attacker + 0x2C8) & 1) {
        power = damage_entry(attacker, kind);
        const u32 weight = damage_entry(recipient, kind);
        cycles = 68;
        if (kind >= 1 && kind <= 23) {
            cycles += 12;
            switch (MEM_R16(DAMAGE_TIER)) {
            case 1:
                power = power * 120 / 100;
                cycles += 11;
                break;
            case 2:
                power = power * 140 / 100;
                cycles += 13;
                break;
            case 3:
                power = power * 150 / 100;
                cycles += 11;
                break;
            }
        }
        /* Both percentage operations truncate independently. */
        power = power * weight / 100;
        R(4) = 0x80808081u;
        R(5) = weight;
    } else {
        power = damage_entry(attacker, kind);
        const u32 rules = MEM_R32(R(28) + 0x244);
        cycles = 51;
        if (!(MEM_R32(rules + 4) & 0x80000000u)) {
            power /= 2;
            cycles++;
        }
        divisor = 510;
    }

    MEM_W32(R(7), power);
    const u32 scaled = power * 4096;
    /* The original constant-divide multiply also leaves HI, LO and t1 live. */
    nc_mult(c, scaled, 0x80808081u);
    R(9) = c->hi;
    R(8) = power;
    R(3) = 0;
    R(2) = scaled / divisor;
    c->budget -= cycles;
    return true;
}

bool nc_calculate_hit_health(CPUState *c, bool check_mode) {
    /* The direct entry at 8004EB6C costs five cycles. Entering at 8004EB30
     * can also execute two flag checks, costing eleven and seven cycles. */
    if (c->budget <= (check_mode ? 23 : 5))
        return false;

    const u32 target = R(17);
    u32 damage = R(20);
    int cycles = 5;
    if (check_mode) {
        const u32 rules = MEM_R32(R(28) + 0x244);
        R(4) = rules;
        R(3) = 0x08000000u;
        cycles += 11;
        if (!(MEM_R32(rules + 4) & 0x08000000u)) {
            /* The branch delay slot doubles v1 even if base damage is selected. */
            R(3) = damage << 1;
            cycles += 7;
            if (!(MEM_R32(rules) & 0x30u))
                damage = R(3);
        }
    }

    R(2) = MEM_R16(target + ACTOR_HEALTH_OFFSET) - damage;
    c->budget -= cycles;
    return true;
}

/* Health is a signed halfword here. Both death paths clamp it to zero in
 * the branch delay slot before choosing the state-15 reaction. */
NcHitOutcome nc_check_lethal_hit(CPUState *c) {
    if (c->budget <= 13)
        return NC_HIT_TIMED;
    const u32 actor = R(17);
    s16 health = (s16)MEM_R16(actor + ACTOR_HEALTH_OFFSET);
    R(2) = 40;
    c->budget -= 6;
    if (health > 0)
        return NC_HIT_SURVIVED;
    R(2) = MEM_R32(actor + 0x2c8) & 1;
    MEM_W16(actor + ACTOR_HEALTH_OFFSET, 0);
    c->budget -= 7;
    return R(2) ? NC_HIT_PLAYER_DEATH : NC_HIT_OTHER_DEATH;
}

/* The two surviving-hit branches have identical flag cleanup. Both enter
 * with v0 set to the mask that clears bit 8 in the recipient. */
bool nc_clear_hit_reaction_flags(CPUState *c) {
    if (c->budget <= 22)
        return false;
    const u32 target = R(17);
    R(3) = MEM_R32(target + 0x2c8);
    R(5) = R(3) & R(2);
    MEM_W32(target + 0x2c8, R(5));
    c->budget -= 7;
    if (!R(5))
        return true;
    R(2) = R(3) & 0x200;
    R(4) = 0xfffb0000;
    c->budget -= 3;
    if (!R(2))
        return true;

    R(3) = MEM_R32(target + 4);
    MEM_W32(target + 0x2c8, R(5) & ~0x320u);
    R(4) |= 0xffff;
    R(2) = MEM_R32(R(3) + 0x2d0) & R(4);
    MEM_W32(R(3) + 0x2d0, R(2));
    c->budget -= 12;
    return true;
}

/* Both lethal reactions enter state 15. The player first restores its part
 * links. The other reaction checks for an adrenaline reward after the change. */
bool nc_apply_lethal_reaction(CPUState *c, bool player) {
    if (c->budget <= 4)
        return false;
    if (player) {
        R(31) = 0x8004ec94;
        R(4) = R(17);
        NC_POLL(c, 2);
        nc_call(c, 0x800362e4);
    }
    R(4) = R(17);
    NC_POLL(c, 1);
    R(5) = 15;
    NC_POLL(c, 1);
    R(31) = player ? 0x8004eca4 : 0x8004ecbc;
    R(6) = player ? 0x100000 : 0x80000;
    NC_POLL(c, 2);
    nc_call(c, 0x8004d0cc);
    if (!player) {
        R(4) = R(17);
        NC_POLL(c, 1);
        R(31) = 0x8004ecc8;
        R(5) = R(18);
        NC_POLL(c, 2);
        nc_call(c, 0x80052b80);
    }
    NC_POLL(c, 2);
    return true;
}

/* Reset the animation fields before installing the new actor state. */
bool nc_set_actor_state(CPUState *c) {
    if (c->budget <= 14)
        return false;
    const u32 actor = R(4);
    R(2) = MEM_R32(actor + 0x330);
    R(3) = ~0x18u;
    MEM_W8(actor + 0x38a, 0);
    MEM_W8(actor + 0x38e, 0);
    MEM_W8(actor + 0x39a, 0);
    MEM_W8(actor + 0x399, 0);
    MEM_W8(actor + 0x3a3, 0);
    MEM_W8(actor + 0x39c, 0);
    MEM_W8(actor + 0x39b, R(5));
    R(2) &= R(3);
    MEM_W32(actor + 0x330, R(2));
    c->budget -= 14;
    return true;
}

/* The optional effect depends on the actor flags and a signed index into
 * the level's table of 36-byte records. Delay slots leave constants in v0. */
static bool death_effect_enabled(CPUState *c) {
    R(4) = MEM_R32(R(17) + 0x2c8);
    R(2) = 0x00200000;
    c->budget -= 7;
    if (!(R(4) & 1))
        return false;
    R(2) &= R(4);
    c->budget -= 3;
    if (R(2))
        return false;

    s32 index = (s16)MEM_R16(R(17) + 0x360);
    u32 records = MEM_R32(0x8008f0c8);
    R(3) = MEM_R32(records + (u32)index * 36);
    R(2) = 0x48000000;
    c->budget -= 19;
    if (R(3) & 0x90000000)
        return false;
    R(2) = 0x00400000;
    c->budget -= 3;
    return (R(3) & 0x48000000) != 0;
}

bool nc_enter_death_state(CPUState *c) {
    /* Prologue (12), fixed-cost state setter (14) and effect setup (up to 36).
     * The effect calls can take arbitrary time. Poll each instruction from there. */
    if (c->budget <= 62)
        return false;
    R(29) -= 32;
    MEM_W32(R(29) + 0x14, R(17));
    R(17) = R(4);
    MEM_W32(R(29) + 0x18, R(31));
    MEM_W32(R(29) + 0x10, R(16));
    R(2) = MEM_R32(R(17) + 0x2c8);
    MEM_W16(R(17) + ACTOR_HEALTH_OFFSET, 0);
    R(2) |= R(6);
    R(31) = 0x8004d0f4;
    MEM_W32(R(17) + 0x2c8, R(2));
    c->budget -= 12;
    nc_call(c, 0x8004d1a0);

    if (death_effect_enabled(c)) {
        R(2) |= R(4);
        MEM_W32(R(17) + 0x2c8, R(2));
        R(16) = 2;
        R(4) = R(17);
        c->budget -= 4;
        bool more;
        do {
            R(5) = 17;
            NC_POLL(c, 1);
            R(6) = 0x00800000;
            NC_POLL(c, 1);
            R(6) |= 0x8080;
            NC_POLL(c, 1);
            R(31) = 0x8004d178;
            R(7) = R(16);
            NC_POLL(c, 2);
            nc_call(c, 0x80058b9c);
            R(16)++;
            NC_POLL(c, 1);
            R(2) = (s32)R(16) < 48;
            NC_POLL(c, 1);
            more = R(2) != 0;
            R(4) = R(17);
            NC_POLL(c, 2);
        } while (more);
    }

    R(31) = MEM_R32(R(29) + 0x18);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(17) = MEM_R32(R(29) + 0x14);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(16) = MEM_R32(R(29) + 0x10);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(29) += 32;
    NC_POLL(c, 1);
    NC_POLL(c, 2);
    return true;
}
