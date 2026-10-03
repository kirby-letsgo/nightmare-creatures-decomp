#include "port/hw/hw.h"
#include "port/savestate.h"

/* Root counters 0-2. Only the free-running/system-clock behaviour the game needs is modelled:
 * counter 0 = system clock (dot clock approximated), 1 = hblank, 2 = system clock (/8 option). */

typedef struct Timer {
    u32 mode, target;
    u64 base; /* cycle count at which the counter was last reset */
} Timer;

static Timer timers[3];

static u32 divisor(int t) {
    switch (t) {
    case 1:
        return (timers[1].mode & 0x100) ? 2154u : 1u; /* hblank ~ 2154 CPU cycles (NTSC) */
    case 2:
        return (timers[2].mode & 0x200) ? 8u : 1u;
    default:
        return 1u;
    }
}

static u32 current(int t, u64 cycles) {
    u64 ticks = (cycles - timers[t].base) / divisor(t);
    u32 target = timers[t].target ? timers[t].target : 0x10000u;
    if (timers[t].mode & 0x8) { /* reset on target */
        return (u32)(ticks % (target + 1));
    }
    return (u32)(ticks & 0xFFFFu);
}

u32 timers_read(u32 reg, u64 cycles) {
    int t = (int)((reg >> 4) & 3);
    if (t > 2) {
        return 0;
    }
    switch (reg & 0xF) {
    case 0x0:
        return current(t, cycles);
    case 0x4: {
        u32 m = timers[t].mode;
        timers[t].mode &= ~0x1800u; /* reached-target/overflow flags clear on read */
        return m | 0x400u;          /* IRQ line idle (high) */
    }
    case 0x8:
        return timers[t].target;
    default:
        return 0;
    }
}

void timers_write(u32 reg, u32 value, u64 cycles) {
    int t = (int)((reg >> 4) & 3);
    if (t > 2) {
        return;
    }
    switch (reg & 0xF) {
    case 0x0:
        timers[t].base = cycles - (u64)(value & 0xFFFF) * divisor(t);
        break;
    case 0x4:
        timers[t].mode = value & 0x3FFu;
        timers[t].base = cycles;
        break;
    case 0x8:
        timers[t].target = value & 0xFFFFu;
        break;
    default:
        break;
    }
}

void timers_serialize(StateIO *io) {
    STATE_VAR(io, timers);
}
