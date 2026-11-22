#include "decomp/distance.h"

/* Horizontal displacement is measured after subtracting the 24.8 positions.
 * The caller adds the low words of the squares in the root call's delay slot. */
bool nc_actor_distance_terms(CPUState *c) {
    if (c->budget <= 21)
        return false;
    const u32 from = R(4), to = R(5);
    u32 dx = (u32)((s32)(MEM_R32(to + 0x27C) - MEM_R32(from + 0x27C)) >> 8);
    u32 dz = (u32)((s32)(MEM_R32(to + 0x284) - MEM_R32(from + 0x284)) >> 8);
    R(6) = dx * dx;
    nc_mult(c, dz, dz);
    R(3) = c->lo;
    R(2) = dz;
    c->budget -= 21;
    return true;
}

/* PSX2.EXE 800475E4. The digit-by-digit integer root consumes two input
 * bits per round. Compute into locals so an interrupt deadline can send the
 * entire call back to the instruction-timed path with the CPU untouched. */
bool nc_distance_root(CPUState *c) {
    u32 value = R(4);
    if ((s32)value < 3) {
        if (c->budget <= 8)
            return false;
        R(2) = value;
        c->budget -= 8;
        return true;
    }

    unsigned skipped = 0;
    while (!(value & 0xB0000000u)) {
        value <<= 2;
        if (!value)
            return false; /* original normalization loop never exits for this input */
        skipped++;
    }
    unsigned rounds = 16 - skipped;
    int cycles = 18 + (skipped ? 1 + 4 * (int)skipped : 0) + 13 * (int)rounds;
    if (c->budget <= cycles)
        return false;
    u32 root = 0, remainder = 0, trial = 0;
    for (unsigned i = 0; i < rounds; i++) {
        root <<= 1;
        remainder = (remainder << 2) | (value >> 30);
        value <<= 2;
        trial = 2 * root + 1;
        if (remainder >= trial) {
            remainder -= trial;
            root++;
            cycles += 2;
        }
    }
    if (c->budget <= cycles)
        return false;
    R(2) = R(7) = root;
    R(3) = remainder;
    R(4) = value;
    R(5) = trial;
    R(6) = R(8) = rounds;
    c->budget -= cycles;
    return true;
}
