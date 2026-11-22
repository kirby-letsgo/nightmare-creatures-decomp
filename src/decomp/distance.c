#include "decomp/distance.h"
#include <limits.h>

/* The low 16 angle bits represent a full turn. The game disc supplies
 * 4096 signed Q14 samples per table, indexed by the upper 12 angle bits. */
static bool heading_sample(CPUState *c, u32 table) {
    if (c->budget <= 9)
        return false;
    const u32 offset = (R(4) >> 3) & 0x1ffe;
    R(4) = offset;
    R(1) = 0x80080000u + offset;
    R(2) = (u32)(s32)(s16)MEM_R16(table + offset);
    c->budget -= 9;
    return true;
}

bool nc_heading_cosine(CPUState *c) {
    return heading_sample(c, 0x80081BE4);
}

bool nc_heading_sine(CPUState *c) {
    return heading_sample(c, 0x80083BE4);
}

/* After the two angle lookups, s0 holds the scaled cosine component.
 * Store projected X and prepare Z for the store in the heading call's delay slot. */
bool nc_project_heading_offset(CPUState *c) {
    if (c->budget <= 20)
        return false;
    nc_mult(c, (u32)(s32)(s16)R(2), R(17));
    R(7) = c->lo;
    R(4) = (u32)((s32)c->lo >> 14);
    const s32 origin_x = (s32)MEM_R32(0x800D7780) >> 8;
    MEM_W16(R(18), (u32)origin_x + R(4));
    const s32 origin_z = (s32)MEM_R32(0x800D7788) >> 8;
    R(5) = R(16);
    R(2) = R(16) - (u32)origin_z;
    c->budget -= 20;
    return true;
}

/* Convert an X/Z displacement to the game's 4096-unit turn. The table
 * supplies one octant; the sign and dominant axis determine its reflection. */
bool nc_vector_heading(CPUState *c) {
    static const struct {
        u16 base;
        bool reflect;
        u8 cycles;
    } octants[] = {
        {0, false, 29},    {1024, true, 29},  /* +Z, +X */
        {4096, true, 32},  {3072, false, 31}, /* +Z, -X */
        {2048, true, 31},  {1024, false, 30}, /* -Z, +X */
        {2048, false, 30}, {3072, true, 31},  /* -Z, -X */
    };
    s32 x = (s32)R(4), z = (s32)R(5);
    if (c->budget <= 29 || x == INT32_MIN || z == INT32_MIN)
        return false;
    int zero_axes = (x == 0) + (z == 0);
    u32 ax = x < 0 ? (u32)-x : (u32)x;
    u32 az = z < 0 ? (u32)-z : (u32)z;
    /* Original axis handling substitutes +1, even for the zero vector. */
    if (!ax)
        ax = 1;
    if (!az)
        az = 1;
    bool x_major = ax > az;
    unsigned octant = 4 * (z < 0) + 2 * (x < 0) + x_major;
    int cycles = octants[octant].cycles + zero_axes;
    u32 minor = x_major ? az : ax, major = x_major ? ax : az;
    /* A wrapped signed numerator can index outside the normal angle table. */
    if (c->budget <= cycles || minor > INT32_MAX / 1024u)
        return false;
    u32 numerator = minor * 1024;
    u32 index = numerator / major;
    u32 angle = (u32)(s32)(s16)MEM_R16(0x80085C4Cu + index * 2);
    c->lo = R(4) = index;
    c->hi = numerator % major;
    R(1) = 0x80080000u + index * 2;
    R(5) = az;
    if (octants[octant].reflect) {
        R(3) = angle;
        R(2) = octants[octant].base - angle;
    } else {
        R(2) = octants[octant].base + angle;
    }
    c->budget -= cycles;
    return true;
}

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
