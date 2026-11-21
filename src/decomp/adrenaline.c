#include "decomp/adrenaline.h"
#include "port/game_vars.h"
#include "port/patches.h"

enum {
    GAME_CLOCK = 0x8008EF10u,
    LAST_DRAIN = 0x800CC038u,
    DRAIN_INTERVAL = 0x800D4C10u,
    LEVEL_FLAGS = 0x800D779Cu,
    DISPLAYED_HEALTH = 0x800CC004u,
    HUD_FLAGS = 0x800CBFF8u
};

/* PSX2.EXE 80055DD4..80055F48, after drawing the gauge. Each debit is the original
 * basic block's cost. If the hardware can interrupt during this update, use the
 * instruction-timed path from the beginning. The guest's death callbacks also use
 * that path. Returning false must leave registers and RAM untouched. */
bool nc_update_adrenaline(CPUState *c) {
    const u32 player = R(16);
    if (c->budget <= 128)
        return false;
    if (player && !MEM_R16(DISPLAYED_HEALTH) && (s16)MEM_R16(ADDR_ADRENALINE) <= 1)
        return false;
    c->budget -= 2;
    if (!player)
        return true;

    c->budget -= 6;
    bool reset_timer = MEM_R32(player) != 0;
    if (!reset_timer) {
        c->budget -= 8;
        R(3) = 0x08000000;
        reset_timer = (MEM_R16(LEVEL_FLAGS) & 0x50) != 0;
    }
    if (reset_timer) {
        R(2) = MEM_R32(GAME_CLOCK);
        R(1) = 0x800D0000;
        MEM_W32(LAST_DRAIN, R(2));
        c->budget -= 6;
        return true;
    }

    const u32 rules = MEM_R32(R(28) + 0x244);
    const u32 flags = MEM_R32(rules + 4);
    u32 interval = MEM_R32(DRAIN_INTERVAL);
    c->budget -= 14;
    if (!(flags & 0x08000000)) {
        c->budget -= 7;
        if (!(MEM_R32(rules) & 0x30)) {
            interval += (u32)((s32)interval / 2);
            c->budget -= 3;
        }
    }

    const u32 now = MEM_R32(GAME_CLOCK);
    R(4) = interval;
    R(5) = LAST_DRAIN;
    R(3) = now;
    u16 meter = MEM_R16(ADDR_ADRENALINE);
    c->budget -= 14;
    if ((s32)(now - MEM_R32(LAST_DRAIN)) > (s32)interval) {
        c->budget -= 11;
        if (!(flags & 8)) {
            MEM_W32(LAST_DRAIN, now);
            if (!nc_flag_no_adrenaline)
                meter--;
            R(1) = 0x800D0000;
            c->budget -= 11;
            if ((s16)meter < 0) {
                meter = 0;
                c->budget -= 2;
            }
            MEM_W16(ADDR_ADRENALINE, meter);
        }
    }

    R(2) = (u32)(s32)(s16)meter;
    c->budget -= 7;
    if (meter)
        return true;

    // Once the gauge is empty, take four health units per update and refresh the HUD.
    c->budget -= 7 + 6;
    s16 health = (s16)MEM_R16(player + ACTOR_HEALTH_OFFSET);
    if (health) {
        u16 remaining = (u16)(health - 4);
        c->budget -= 5;
        if ((s16)remaining < 0) {
            remaining = 0;
            c->budget--;
        }
        MEM_W16(player + ACTOR_HEALTH_OFFSET, remaining);
    }
    R(3) = HUD_FLAGS;
    R(2) = MEM_R16(HUD_FLAGS) | 0x28;
    MEM_W16(HUD_FLAGS, R(2));
    c->budget -= 9;
    return true;
}
