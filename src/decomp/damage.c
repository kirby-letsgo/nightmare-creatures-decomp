#include "decomp/damage.h"
#include "port/game_vars.h"

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
