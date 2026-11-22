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
