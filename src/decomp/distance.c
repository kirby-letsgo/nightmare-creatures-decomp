#include "decomp/distance.h"
#include <limits.h>

/* Initialize the two coordinate triples in the actor-linked record. The actor
 * height is read after X and Z are stored, which matters for overlapping inputs. */
bool nc_init_actor_coordinates(CPUState *c) {
    if (c->budget <= 27)
        return false;
    const u32 actor = R(4), record = R(5);
    const u32 z = 0u - MEM_R16(0x8008f0c0);
    const u16 x = MEM_R16(0x8008f0bc);
    R(6) = MEM_R32(record + 0x138);
    MEM_W16(record + 0x18, x);
    MEM_W16(record, x);
    MEM_W16(record + 0x1c, z);
    MEM_W16(record + 4, z);
    R(3) = MEM_R32(actor + 0x2dc) + R(6);
    R(2) = 45;
    MEM_W16(record + 0x10, R(2));
    MEM_W16(record + 8, R(2));
    MEM_W32(record + 0x160, actor);
    MEM_W16(record + 0x1a, R(3));
    MEM_W16(record + 2, R(3));
    c->budget -= 27;
    return true;
}

/* Both coordinates and the reference point are signed whole units. */
bool nc_prepare_local_point_heading(CPUState *c) {
    if (c->budget <= 19)
        return false;
    const u32 point = R(4);
    R(29) -= 24;
    MEM_W32(R(29) + 16, R(31));
    const s16 point_x = (s16)MEM_R16(point);
    const s16 origin_x = (s16)MEM_R16(0x800D7630);
    const s16 origin_z = (s16)MEM_R16(0x800D7634);
    const s16 point_z = (s16)MEM_R16(point + 4);
    R(6) = point_x;
    R(2) = origin_x;
    R(3) = origin_z;
    R(4) = point_x - origin_x;
    R(5) = point_z - origin_z;
    R(31) = 0x8004720C;
    c->budget -= 19;
    return true;
}

/* Actor-linked points store X and negated Z as signed world units.
 * The two origin words use eight fractional bits. Save ra before reading
 * the point, as the guest stack may overlap it. */
bool nc_prepare_point_heading(CPUState *c) {
    if (c->budget <= 21)
        return false;
    const s32 origin_x = (s32)MEM_R32(0x800D7780) >> 8;
    const s32 origin_z = (s32)MEM_R32(0x800D7788) >> 8;
    const u32 point = R(4);
    R(29) -= 24;
    MEM_W32(R(29) + 16, R(31));
    const s16 point_x = (s16)MEM_R16(point);
    const s16 point_z = (s16)MEM_R16(point + 4);
    R(2) = origin_x;
    R(3) = point_x;
    R(6) = point_z;
    R(4) = origin_x - point_x;
    R(5) = origin_z + point_z;
    R(31) = 0x80047254;
    c->budget -= 21;
    return true;
}

bool nc_return_point_heading(CPUState *c) {
    if (c->budget <= 7)
        return false;
    R(2) = (s32)(s16)R(2);
    R(31) = MEM_R32(R(29) + 16);
    R(29) += 24;
    c->budget -= 7;
    return true;
}

/* Actor facing combines two word-sized angles in 65536 units per turn.
 * The return delay slot sign-extends the wrapped sum. */
bool nc_actor_heading(CPUState *c) {
    if (c->budget <= 18)
        return false;
    const u32 pose = MEM_R32(R(4) + 0x20);
    const u32 linked = MEM_R32(pose + 4);
    const u32 heading = MEM_R32(pose + 0x48);
    R(3) = MEM_R32(linked + 0x40);
    R(2) = (s32)(s16)(heading + R(3));
    c->budget -= 18;
    return true;
}

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

/* Project a signed radius along a 16-bit heading. Reserve enough time for
 * both Q14 lookups, the coordinate stores and the vector-heading call slot. */
bool nc_prepare_heading_projection(CPUState *c) {
    if (c->budget <= 57)
        return false;
    const u32 radius = R(4), angle = R(5), point = R(6);
    R(29) -= 32;
    MEM_W32(R(29) + 20, R(17));
    MEM_W32(R(29) + 16, R(16));
    MEM_W32(R(29) + 24, R(18));
    MEM_W32(R(29) + 28, R(31));
    R(17) = radius;
    R(18) = point;

    R(4) = angle;
    nc_heading_cosine(c);
    nc_mult(c, R(2), radius);
    R(16) = (u32)((s32)c->lo >> 14);
    R(4) = angle;
    nc_heading_sine(c);
    nc_project_heading_offset(c);
    MEM_W16(point + 4, R(2));
    R(31) = 0x800471B4;
    c->budget -= 19; /* wrapper instructions outside the three helpers */
    return true;
}

bool nc_finish_heading_projection(CPUState *c) {
    if (c->budget <= 19)
        return false;
    R(3) = (4096u - R(2)) << 16;
    R(2) = (u32)((s32)R(3) >> 16);
    R(31) = MEM_R32(R(29) + 28);
    R(18) = MEM_R32(R(29) + 24);
    R(17) = MEM_R32(R(29) + 20);
    R(16) = MEM_R32(R(29) + 16);
    R(29) += 32;
    c->budget -= 19;
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

/* Transform the part's three coordinates by the shared GTE rotation matrix.
 * The two entry points differ only in how they scale the input vector. */
bool nc_transform_part_position(CPUState *c, bool quarter_scale) {
    const int cycles = quarter_scale ? 85 : 82;
    if (c->budget <= cycles)
        return false;
    const u32 frame = R(29) - 24;
    for (unsigned axis = 0; axis < 3; ++axis) {
        s32 value = (s32)MEM_R32(R(4) + 0x2c + 4 * axis);
        MEM_W16(frame + 2 * axis, (u16)(quarter_scale ? value >> 2 : value));
    }
    R(6) = 0x800d7688;
    for (unsigned reg = 0; reg < 5; ++reg) {
        u32 value = MEM_R32(R(6) + 4 * reg);
        gte_write_ctrl(c, reg, value);
        if (reg >= 2)
            R(10 + reg) = value;
    }
    gte_write_data(c, 0, MEM_R32(frame));
    gte_write_data(c, 1, MEM_R32(frame + 4));
    gte_command(c, 0x0486012); /* MVMVA, rotation matrix, V0, no translation */
    for (unsigned axis = 0; axis < 3; ++axis)
        MEM_W32(frame + 8 + 4 * axis, gte_read_data(c, 25 + axis));
    for (unsigned axis = 0; axis < 3; ++axis) {
        s32 coord = (s32)MEM_R32(frame + 8 + 4 * axis);
        R(3) = MEM_R16(0x800d7630 + 2 * axis);
        R(2) = (u32)(coord >> 1) - R(3);
        MEM_W16(R(5) + 2 * axis, (u16)R(2));
    }
    c->budget -= cycles;
    return true;
}
