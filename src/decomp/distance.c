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

/* Distance to the signed reference point, whose Z axis is reflected.
 * SquareRoot0 receives the wrapped sum of the low product words. */
bool nc_prepare_reference_distance(CPUState *c) {
    if (c->budget <= 28)
        return false;
    const s32 x = (s32)MEM_R32(0x800D7780) >> 8;
    const s32 dx = x - (s16)MEM_R16(0x800D7648);
    const s32 z = (s32)MEM_R32(0x800D7788) >> 8;
    const s32 dz = z + (s16)MEM_R16(0x800D764C);
    nc_mult(c, (u32)dz, (u32)dz);
    R(2) = dz;
    R(3) = c->lo;
    R(4) = (u32)dx * (u32)dx + c->lo;
    R(29) -= 24;
    MEM_W32(R(29) + 16, R(31));
    R(31) = 0x800472B8;
    c->budget -= 28;
    return true;
}

bool nc_store_reference_distance(CPUState *c) {
    if (c->budget <= 10)
        return false;
    R(1) = 0x800D0000;
    MEM_W32(0x800D7764, R(2));
    MEM_W32(0x800D7760, R(2));
    R(31) = MEM_R32(R(29) + 16);
    R(29) += 24;
    c->budget -= 10;
    return true;
}

/* Keep the linked pose across the point-heading call. The actor and its
 * current pose are read again when the call returns. */
bool nc_prepare_relative_heading(CPUState *c) {
    if (c->budget <= 16)
        return false;
    const u32 actor = MEM_R32(0x800D7790);
    R(29) -= 24;
    MEM_W32(R(29) + 20, R(31));
    MEM_W32(R(29) + 16, R(16));
    R(2) = MEM_R32(actor + 0x20);
    R(16) = MEM_R32(R(2) + 4);
    R(31) = 0x80047328;
    c->budget -= 16;
    return true;
}

bool nc_finish_relative_heading(CPUState *c) {
    if (c->budget <= 33)
        return false;
    const u32 actor = MEM_R32(0x800D7790);
    const u32 pose = MEM_R32(actor + 0x20);
    const u32 heading = MEM_R32(pose + 0x48);
    R(4) = MEM_R32(R(16) + 0x40);
    const u32 facing = (u32)((s32)(heading + R(4)) >> 4);
    R(3) = ((u32)(s32)(s16)R(2) - facing) & 0xfff;
    R(2) = 4096 - R(3); /* aligned headings return a full turn */
    R(31) = MEM_R32(R(29) + 20);
    R(16) = MEM_R32(R(29) + 16);
    R(29) += 24;
    c->budget -= 33;
    return true;
}

/* Snapshot the actor coordinates and project into the second point in the
 * record. X is stored before Z is read, so the two may alias. */
bool nc_prepare_record_projection(CPUState *c) {
    if (c->budget <= 29)
        return false;
    const u32 record = R(5);
    R(29) -= 32;
    MEM_W32(R(29) + 16, R(16));
    MEM_W32(R(29) + 20, R(17));
    MEM_W32(R(29) + 24, R(31));
    R(16) = record;
    R(17) = record + 0x18;
    const u32 actor = MEM_R32(record + 0x160);
    R(4) = MEM_R32(record + 0x130);
    R(3) = MEM_R32(record + 0x160);
    R(5) = MEM_R32(record + 0x128) << 4;
    R(6) = R(17);
    MEM_W32(record + 0x150, MEM_R32(actor + 0x27c));
    R(2) = MEM_R32(R(3) + 0x284);
    MEM_W32(record + 0x158, R(2));
    R(31) = 0x800473c0;
    c->budget -= 29;
    return true;
}

bool nc_store_projected_heading(CPUState *c) {
    if (c->budget <= 14)
        return false;
    MEM_W16(R(16) + 0x12, R(2));
    MEM_W16(R(16) + 0x0a, R(2));
    R(31) = MEM_R32(R(29) + 24);
    R(17) = MEM_R32(R(29) + 20);
    R(16) = MEM_R32(R(29) + 16);
    R(29) += 32;
    c->budget -= 14;
    return true;
}

/* Compare the new bearing with the record's previous bearing. The guest
 * reflects a difference over half a turn once, even for out-of-range inputs. */
bool nc_projected_heading_gap(CPUState *c) {
    if (c->budget <= 15)
        return false;
    const s32 projected = (s16)R(2);
    const s32 previous = (s16)MEM_R16(R(16) + 0x0a);
    s32 gap = projected - previous;
    int cycles = 11;
    if (gap <= 0) {
        gap = -gap;
        cycles = 12;
    }
    if (gap > 2048) {
        gap = 4096 - gap;
        cycles = 15;
    }
    R(17) = projected;
    R(4) = previous;
    R(3) = gap;
    R(2) = gap < 569;
    c->budget -= cycles;
    return true;
}

/* A gap of 569 sets bit zero. Once set, gaps down to 456 keep
 * the projected bearing. Smaller gaps resume projection from the old bearing. */
bool nc_select_projected_heading(CPUState *c, bool *reproject) {
    if (c->budget <= 13)
        return false;
    const u32 record = R(16);
    const u16 flags = MEM_R16(record + 0x16c);
    if (!R(2)) {
        MEM_W16(record + 0x12, R(17));
        R(2) = flags | 1u;
        MEM_W16(record + 0x16c, R(2));
        *reproject = false;
        c->budget -= 9;
        return true;
    }

    R(2) = (s32)R(3) < 456;
    R(6) = record + 0x18;
    int cycles = (flags & 1) ? 11 : 10;
    *reproject = !(flags & 1) || R(2);
    if (!*reproject) {
        MEM_W16(record + 0x12, R(17));
        cycles += 2;
    }
    c->budget -= cycles;
    return true;
}

/* Convert the saved 4096-unit bearing back to the reflected 16-bit
 * projection angle. Clear only bit zero in the call's delay slot. */
bool nc_prepare_saved_bearing_projection(CPUState *c) {
    if (c->budget <= 15)
        return false;
    const u32 record = R(16);
    const s32 previous = (s16)MEM_R16(record + 0x0a);
    R(4) = MEM_R32(record + 0x130);
    R(5) = (u32)(4096 - previous) << 4;
    R(2) = MEM_R16(record + 0x16c) & 0xfffe;
    R(31) = 0x800475cc;
    MEM_W16(record + 0x16c, R(2));
    c->budget -= 15;
    return true;
}

bool nc_finish_projected_point_update(CPUState *c) {
    if (c->budget <= 12)
        return false;
    R(31) = MEM_R32(R(29) + 32);
    R(17) = MEM_R32(R(29) + 28);
    R(16) = MEM_R32(R(29) + 24);
    R(29) += 40;
    c->budget -= 12;
    return true;
}

/* Offset the actor by 130 world units along its facing. The original
 * multiplies each Q14 sample by 65 and shifts right 13, rounding down.
 * X is visible in whole units before the Z coordinate is read. */
bool nc_prepare_facing_offset(CPUState *c) {
    if (c->budget <= 70)
        return false;
    const u32 record = R(16), facing = R(17);
    R(4) = facing;
    nc_heading_sine(c);
    const u32 actor_x = MEM_R32(record + 0x160);
    const s32 x = (s32)MEM_R32(actor_x + 0x27c) >> 8;
    const s32 dx = ((s16)R(2) * 65) >> 13;
    MEM_W32(record + 0x150, (u32)(x + dx));

    R(4) = facing;
    nc_heading_cosine(c);
    const s32 dz = ((s16)R(2) * 65) >> 13;
    R(6) = R(29) + 16;
    R(4) = MEM_R32(record + 0x130);
    R(5) = (MEM_R32(record + 0x128) << 4) - facing;
    const u32 actor_z = MEM_R32(record + 0x160);
    const s32 z = (s32)MEM_R32(actor_z + 0x284) >> 8;
    R(2) = MEM_R32(record + 0x150) << 8;
    R(7) = z + dz;
    MEM_W32(record + 0x158, R(7));
    R(3) = MEM_R32(record + 0x158) << 8;
    MEM_W32(record + 0x150, R(2));
    MEM_W32(record + 0x158, R(3));
    R(31) = 0x80047518;
    c->budget -= 52; /* the two table lookups charged nine cycles each */
    return true;
}

/* Select the record's existing point when the linked actor's state is
 * below two and bit 0x40 is set. Otherwise project along the combined facing. */
bool nc_begin_projected_point_update(CPUState *c, bool *use_point) {
    if (c->budget <= 40)
        return false;
    R(29) -= 40;
    MEM_W32(R(29) + 24, R(16));
    R(16) = R(5);
    MEM_W32(R(29) + 32, R(31));
    MEM_W32(R(29) + 28, R(17));
    const u32 pose = MEM_R32(R(4) + 0x20);
    R(5) = MEM_R32(R(16) + 0x160);
    const u32 linked_pose = MEM_R32(pose + 4);
    R(4) = MEM_R32(pose + 0x48);
    R(2) = MEM_R8(R(5) + 0x388) < 2;
    R(3) = MEM_R32(linked_pose + 0x40);
    R(17) = R(4) + R(3);
    int cycles = 26;
    *use_point = false;
    if (R(2)) {
        R(2) = MEM_R32(R(5) + 0x330) & 0x40;
        R(4) = R(16) + 0x18;
        cycles += 7;
        if (R(2)) {
            R(2) = MEM_R16(R(16) + 0x16c) & 0xfffe;
            R(31) = 0x80047480;
            MEM_W16(R(16) + 0x16c, R(2));
            *use_point = true;
            cycles += 7;
        }
    }
    c->budget -= cycles;
    return true;
}

/* Rotate the local -Z or +X basis vector with the record's matrix.
 * Copy all eight bytes, including the fourth halfword, into the guest frame. */
bool nc_prepare_rotated_point_offset(CPUState *c, bool negative_z) {
    if (c->budget <= 28)
        return false;
    const u32 frame = R(29) - 56;
    R(29) = frame;
    MEM_W32(frame + 40, R(16));
    R(16) = R(4);
    MEM_W32(frame + 44, R(17));
    R(17) = R(5);
    MEM_W32(frame + 48, R(31));

    const u32 vector = negative_z ? 0x8008eefc : 0x8008ef04;
    R(2) = MEM_R32(vector);
    R(3) = MEM_R32(vector + 4);
    nc_swl(c, frame + 19, R(2));
    nc_swr(c, frame + 16, R(2));
    nc_swl(c, frame + 23, R(3));
    nc_swr(c, frame + 20, R(3));
    R(4) = R(17) + 0x38;
    R(5) = frame + 16;
    R(6) = frame + 24;
    R(31) = negative_z ? 0x80046e68 : 0x80046f2c;
    c->budget -= 28;
    return true;
}

/* ApplyMatrix left the rotated vector on the stack. Both entries scale
 * the low product words by 1/512, rounding toward zero. They move the
 * record in opposite X/Z directions. */
bool nc_apply_rotated_point_offset(CPUState *c, bool subtract_x) {
    if (c->budget <= 43)
        return false;
    const u32 frame = R(29), record = R(17);
    const s32 scale = (s16)R(16);
    const s32 x_product = (s32)((u32)scale * MEM_R32(frame + 0x18));
    const s32 dx = x_product / 512;
    const u32 x = MEM_R16(record + 0x18);
    MEM_W16(record + 0x18, subtract_x ? x - dx : x + dx);

    nc_mult(c, (u32)scale, MEM_R32(frame + 0x20));
    const s32 z_product = (s32)c->lo;
    const s32 dz = z_product / 512;
    const u32 z = MEM_R16(record + 0x1c);
    R(3) = dz;
    R(2) = subtract_x ? z + dz : z - dz;
    MEM_W16(record + 0x1c, R(2));
    R(31) = MEM_R32(frame + 0x30);
    R(17) = MEM_R32(frame + 0x2c);
    R(16) = MEM_R32(frame + 0x28);
    R(29) += 0x38;
    c->budget -= 41 + (x_product < 0) + (z_product < 0);
    return true;
}

/* The fifth argument supplies the point height. Convert actor X/Z from
 * 24.8 and the requested bearing from a 4096-unit to a 65536-unit turn. */
bool nc_begin_actor_point_placement(CPUState *c) {
    if (c->budget <= 49)
        return false;
    const u32 frame = R(29) - 64;
    R(29) = frame;
    MEM_W32(frame + 32, R(18));
    R(18) = R(4);
    MEM_W32(frame + 28, R(17));
    R(17) = R(5);
    R(6) <<= 4;
    MEM_W32(frame + 60, R(31));
    MEM_W32(frame + 56, R(30));
    for (int reg = 23; reg >= 19; --reg)
        MEM_W32(frame + 24 + 4 * (reg - 16), R(reg));
    MEM_W32(frame + 24, R(16));

    const u32 pose = MEM_R32(R(18) + 0x20);
    R(3) = MEM_R32(R(18) + 0x20);
    R(19) = MEM_R32(frame + 80);
    const u32 linked_pose = MEM_R32(pose + 4);
    R(22) = MEM_R32(R(3) + 0x48);
    R(2) = MEM_R32(linked_pose + 0x40);
    R(23) = R(7);
    MEM_W32(frame + 16, R(6));
    R(20) = (u32)((s32)MEM_R32(R(18) + 0x27c) >> 8);
    R(16) = (u32)((s32)MEM_R32(R(18) + 0x284) >> 8);
    R(30) = MEM_R32(R(18) + 0x280);
    R(22) += R(2);
    R(4) = R(21) = R(22) - R(6);
    R(31) = 0x8004701c;
    c->budget -= 49;
    return true;
}

bool nc_finish_actor_point_placement(CPUState *c) {
    if (c->budget <= 34)
        return false;
    const u32 frame = R(29);
    MEM_W16(R(17) + 10, R(2));
    R(31) = MEM_R32(frame + 60);
    R(30) = MEM_R32(frame + 56);
    for (int reg = 23; reg >= 16; --reg)
        R(reg) = MEM_R32(frame + 24 + 4 * (reg - 16));
    R(29) += 64;
    c->budget -= 34;
    return true;
}

/* Cosine is already in v0. Build both signed coordinate triples, then
 * pass the opposite relative angle to the projection call. */
bool nc_place_actor_points(CPUState *c) {
    if (c->budget <= 51)
        return false;
    const u32 record = R(17), actor = R(18), radius = R(23);
    const u32 z_product = (u32)(s32)(s16)R(2) * radius;
    R(4) = R(21);
    R(21) = (u32)((s32)z_product >> 14);
    nc_heading_sine(c);
    nc_mult(c, (u32)(s32)(s16)R(2), radius);
    R(16) = 0u - (R(16) + R(21));
    MEM_W32(record + 0x160, actor);
    R(3) = MEM_R32(record + 0x160);
    R(19) -= R(30);
    MEM_W16(record + 0x1c, R(16));
    MEM_W16(record + 4, R(16));
    MEM_W16(record + 0x1a, R(19));
    MEM_W16(record + 2, R(19));
    R(20) += (u32)((s32)c->lo >> 14);
    MEM_W16(record + 0x18, R(20));
    MEM_W16(record, R(20));
    MEM_W32(record + 0x150, MEM_R32(actor + 0x27c));
    R(2) = MEM_R32(R(3) + 0x284);
    MEM_W32(record + 0x158, R(2));
    R(8) = MEM_R32(R(29) + 0x10);
    R(4) = radius;
    R(5) = R(8) - R(22);
    R(6) = record;
    R(31) = 0x800470a4;
    c->budget -= 42; /* sine lookup charged the other nine cycles */
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

static u32 part_rotation_angle(u32 source, unsigned axis, NcRotationInput input) {
    if (input == NC_ROTATION_PACKED)
        return (s32)(s16)MEM_R16(source + 2 * axis);
    return MEM_R32(source + 0x40 + 4 * axis) + (input == NC_ROTATION_PART ? 2 : 0);
}

/* All three entries use the same Q14 tables. Only the first adds two
 * angle units before the lookup. */
bool nc_sample_part_rotation(CPUState *c, NcRotationInput input) {
    const bool biased = input == NC_ROTATION_PART;
    const int cycles = biased ? 104 : 98;
    if (c->budget <= cycles)
        return false;
    const u32 part = R(4), frame = R(29) - 48;
    R(29) = frame;
    const unsigned part_reg = biased ? 16 : 17;
    const unsigned cz_reg = biased ? 17 : 16;
    MEM_W32(frame + 16 + 4 * (part_reg - 16), R(part_reg));
    MEM_W32(frame + 40, R(31));
    for (int reg = 21; reg >= 18; --reg)
        MEM_W32(frame + 16 + 4 * (reg - 16), R(reg));
    MEM_W32(frame + 16 + 4 * (cz_reg - 16), R(cz_reg));
    R(19) = R(5);

    const unsigned cosine_regs[] = {21, 20, cz_reg};
    for (unsigned axis = 0; axis < 3; ++axis) {
        R(4) = part_rotation_angle(part, axis, input);
        nc_heading_cosine(c);
        R(cosine_regs[axis]) = (s32)(s16)R(2) >> 2;
    }
    R(4) = part_rotation_angle(part, 0, input);
    nc_heading_sine(c);
    R(18) = (s32)(s16)R(2) >> 2;
    R(4) = part_rotation_angle(part, 1, input);
    nc_heading_sine(c);
    R(part_reg) = (s32)(s16)R(2) >> 2;
    R(4) = part_rotation_angle(part, 2, input);
    nc_heading_sine(c);
    static const u32 continuation[] = {0x8002607c, 0x80026248, 0x80026414};
    R(31) = continuation[input];
    c->budget -= cycles - 6 * 9; /* lookup helpers charged nine cycles each */
    return true;
}

/* The original takes the low product word before each Q12 shift. */
static u32 shift_part_product(u32 product) {
    return (s32)product >> 12;
}

bool nc_finish_part_rotation(CPUState *c, NcRotationInput input) {
    if (c->budget <= 97)
        return false;
    const bool transposed = input == NC_ROTATION_TRANSPOSED;
    const u32 cx = R(21), cy = R(20), cz = R(input == NC_ROTATION_PART ? 17 : 16);
    const u32 sx = R(18), sy = R(input == NC_ROTATION_PART ? 16 : 17), sz = (s32)(s16)R(2) >> 2;
    const u32 matrix = R(19), frame = R(29);
    const u32 cy_cz = cy * cz;
    R(11) = cz * sy;
    R(13) = cx * cz;
    R(14) = sx * cz;
    R(6) = shift_part_product(cx * cy);
    R(12) = R(6) * sz;
    R(4) = shift_part_product(sx * sy);
    R(7) = shift_part_product(sx * cy);
    R(9) = R(7) * sz;
    R(5) = shift_part_product(cx * sy);
    R(10) = R(5) * sz;
    nc_mult(c, R(4), sz);

    MEM_W32(matrix + 20, 0);
    MEM_W32(matrix + 24, 0);
    MEM_W32(matrix + 28, 0);
    MEM_W16(matrix + (transposed ? 6 : 2), -sz);
    MEM_W16(matrix, shift_part_product(cy_cz));
    MEM_W16(matrix + (transposed ? 12 : 4), shift_part_product(-R(11)));
    MEM_W16(matrix + 8, shift_part_product(R(13)));
    MEM_W16(matrix + (transposed ? 10 : 14), shift_part_product(R(14)));
    R(4) = shift_part_product(R(12)) - R(4);
    R(5) += shift_part_product(R(9));
    R(2) = -shift_part_product(R(10)) - R(7);
    MEM_W16(matrix + (transposed ? 2 : 6), R(4));
    MEM_W16(matrix + (transposed ? 4 : 12), R(5));
    MEM_W16(matrix + (transposed ? 14 : 10), R(2));
    R(8) = c->lo;
    R(3) = shift_part_product(R(8));
    R(6) -= R(3);
    MEM_W16(matrix + 16, R(6));

    R(31) = MEM_R32(frame + 40);
    for (int reg = 21; reg >= 16; --reg)
        R(reg) = MEM_R32(frame + 16 + 4 * (reg - 16));
    R(29) += 48;
    c->budget -= 97;
    return true;
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

/* Load the GTE rotation and translation. The three translation words also
 * remain in the guest temporaries after the final control-register writes. */
static void load_part_transform(CPUState *c, u32 matrix) {
    for (unsigned reg = 0; reg < 8; ++reg) {
        const u32 word = MEM_R32(matrix + 4 * reg);
        gte_write_ctrl(c, reg, word);
        if (reg >= 5)
            R(12 + reg - 5) = word;
    }
}

/* Start the part chain with an identity rotation and zero translation. */
bool nc_init_part_transform(CPUState *c) {
    if (c->budget <= 55)
        return false;
    const u32 frame = R(29) - 0xf8, matrix = frame + 0x98;
    R(29) = frame;
    R(7) = R(4);
    MEM_W32(frame + 0xf0, R(20));
    R(20) = R(6);
    MEM_W32(frame + 0xe4, R(17));
    R(17) = matrix;
    R(2) = 4096;
    MEM_W32(frame + 0xf4, R(31));
    MEM_W32(frame + 0xec, R(19));
    MEM_W32(frame + 0xe8, R(18));
    MEM_W32(frame + 0xe0, R(16));
    for (unsigned i = 0; i < 9; ++i)
        MEM_W16(matrix + 2 * i, i % 4 == 0 ? 4096 : 0);
    MEM_W32(matrix + 0x1c, 0);
    MEM_W32(matrix + 0x18, 0);
    MEM_W32(matrix + 0x14, 0);
    load_part_transform(c, matrix);
    c->budget -= 55;
    return true;
}

/* Collect the part and its parents in eight-byte stack slots. Each link
 * gets a separate budget check so a long chain can resume mid-traversal. */
bool nc_begin_part_chain(CPUState *c) {
    if (c->budget <= 9)
        return false;
    R(4) = R(5);
    MEM_W32(R(29) + 0x1c, R(4));
    R(2) = MEM_R32(R(4));
    R(16) = 1;
    if (R(2))
        R(3) = R(29) + 0x20;
    c->budget -= R(2) ? 9 : 8;
    return true;
}

bool nc_append_part_parent(CPUState *c) {
    if (c->budget <= 11)
        return false;
    R(4) = MEM_R32(R(4));
    ++R(16);
    MEM_W32(R(3) + 4, R(4));
    R(2) = MEM_R32(R(4));
    R(3) += 8;
    c->budget -= 11;
    return true;
}

bool nc_prepare_part_chain_rotation(CPUState *c) {
    const int cycles = R(16) ? 8 : 5;
    if (c->budget <= cycles)
        return false;
    R(4) = R(7);
    MEM_W16(R(4) + 0x14, 0);
    MEM_W16(R(4) + 0x12, 0);
    MEM_W16(R(4) + 0x10, 0);
    if (R(16)) {
        R(19) = R(29) + 0x98;
        R(18) = R(29) + 0xb8;
        --R(16);
    }
    c->budget -= cycles;
    return true;
}

/* Transform the selected part's signed local offset into the accumulated
 * matrix translation before composing its rotation. */
bool nc_transform_part_offset(CPUState *c) {
    if (c->budget <= 26)
        return false;
    const u32 frame = R(29);
    R(4) = MEM_R32(frame + 0x1c + 8 * R(16));
    gte_write_data(c, 0, MEM_R32(R(4) + 0x10));
    gte_write_data(c, 1, MEM_R32(R(4) + 0x14));
    gte_command(c, 0x0480012); /* MVMVA, rotation matrix, V0, translation */
    for (unsigned axis = 0; axis < 3; ++axis)
        MEM_W32(frame + 0xac + 4 * axis, gte_read_data(c, 25 + axis));
    R(2) = frame + 0xd8;
    R(12) = gte_read_ctrl(c, 31);
    MEM_W32(R(2), R(12));
    R(5) = R(18);
    R(31) = 0x80034480;
    c->budget -= 26;
    return true;
}

static void rotate_matrix_column(CPUState *c, u32 input, u32 output) {
    for (unsigned row = 0; row < 3; ++row)
        gte_write_data(c, 9 + row, MEM_R16(input + 6 * row));
    gte_command(c, 0x049e012); /* MVMVA, rotation matrix, IR vector, no translation */
    for (unsigned row = 0; row < 3; ++row) {
        R(12 + row) = gte_read_data(c, 9 + row);
        MEM_W16(output + 6 * row, R(12 + row));
    }
}

/* Compose this part's rotation, then install the resulting transform for
 * the next part. Only the first column uses the saved matrix pointers. */
bool nc_compose_part_matrix(CPUState *c) {
    const int cycles = R(16) ? 122 : 123;
    if (c->budget <= cycles)
        return false;
    const u32 frame = R(29);
    for (unsigned reg = 0; reg < 5; ++reg)
        gte_write_ctrl(c, reg, MEM_R32(R(17) + 4 * reg));
    rotate_matrix_column(c, R(18), R(19));
    rotate_matrix_column(c, frame + 0xba, frame + 0x9a);
    rotate_matrix_column(c, frame + 0xbc, frame + 0x9c);
    R(2) = frame + 0x9c;
    R(17) = R(19);
    load_part_transform(c, R(17));
    if (R(16))
        --R(16);
    c->budget -= cycles;
    return true;
}

/* Both transform paths return the matrix translation in quarter units. */
bool nc_finish_part_translation(CPUState *c) {
    if (c->budget <= 39)
        return false;
    for (unsigned offset = 0; offset < 12; offset += 4) {
        R(2) = (s32)MEM_R32(R(17) + 0x14 + offset) >> 2;
        MEM_W32(R(20) + offset, R(2));
    }
    const u32 frame = R(29);
    R(31) = MEM_R32(frame + 0xf4);
    for (int reg = 20; reg >= 16; --reg)
        R(reg) = MEM_R32(frame + 0xe0 + 4 * (reg - 16));
    R(29) += 0xf8;
    c->budget -= 39;
    return true;
}

/* Rotate each column of the input matrix with the shared GTE rotation.
 * The input may overlap the frame, so each column is stored before the
 * next is read. The scaled result feeds the three-heading extraction. */
bool nc_prepare_heading_matrix(CPUState *c) {
    if (c->budget <= 119)
        return false;
    const u32 input = R(4), frame = R(29) - 88;
    R(29) = frame;
    MEM_W32(frame + 76, R(19));
    R(19) = R(5);
    MEM_W32(frame + 80, R(31));
    for (int reg = 18; reg >= 16; --reg)
        MEM_W32(frame + 64 + 4 * (reg - 16), R(reg));

    const u32 scale = 0x80018538;
    const u32 sx = MEM_R32(scale), sy = MEM_R32(scale + 4), sz = MEM_R32(scale + 8);
    MEM_W32(frame + 48, sx);
    MEM_W32(frame + 52, sy);
    MEM_W32(frame + 56, sz);
    MEM_W32(frame + 60, MEM_R32(scale + 12));
    R(5) = frame + 48;
    R(6) = 0x800d7688;
    for (unsigned reg = 0; reg < 5; ++reg)
        gte_write_ctrl(c, reg, MEM_R32(R(6) + 4 * reg));

    const u32 matrix = frame + 16;
    for (unsigned column = 0; column < 3; ++column)
        rotate_matrix_column(c, input + 2 * column, matrix + 2 * column);
    R(2) = matrix + 4;
    R(3) = R(4) = matrix;
    R(31) = 0x8003475c;
    c->budget -= 119;
    return true;
}

/* The planar length uses the last two entries of the matrix's middle row.
 * Shift the wrapped 32-bit sum as a signed word in the root call's delay slot. */
bool nc_prepare_matrix_planar_length(CPUState *c) {
    if (c->budget <= 15)
        return false;
    const s32 y = (s16)MEM_R16(R(29) + 0x18);
    const s32 z = (s16)MEM_R16(R(29) + 0x1a);
    R(3) = (u32)(y * y);
    R(2) = z;
    nc_mult(c, R(2), R(2));
    R(7) = c->lo;
    R(4) = (u32)((s32)(R(3) + R(7)) >> 12);
    R(31) = 0x80034788;
    c->budget -= 15;
    return true;
}

/* Extract three bearings from the scaled matrix at sp+16. When the
 * planar length is zero the first bearing uses the last row and the
 * second bearing is zero. Each 4096-unit result becomes a 16-bit turn. */
bool nc_finish_matrix_headings(CPUState *c) {
    const u32 length = R(2);
    const int cycles = length ? 28 : 18;
    /* Each heading lookup takes at most 34 cycles. Its first component
     * fits a signed halfword (or its negation), keeping the ratio in range. */
    if (length == 0x80000000u || c->budget <= cycles + 25 + (length ? 3 : 2) * 34)
        return false;
    const u32 frame = R(29);
    R(16) = length;
    R(18) = 0;
    if (length) {
        R(4) = (s32)(s16)MEM_R16(frame + 0x1a);
        R(5) = (s32)(s16)MEM_R16(frame + 0x18);
        nc_vector_heading(c);
        R(17) = R(2);
        R(4) = -(s32)(s16)MEM_R16(frame + 0x1c);
        R(5) = (s32)(s16)MEM_R16(frame + 0x10);
        nc_vector_heading(c);
        R(18) = R(2);
    } else {
        R(4) = -(s32)(s16)MEM_R16(frame + 0x1e);
        R(5) = (s32)(s16)MEM_R16(frame + 0x20);
        nc_vector_heading(c);
        R(17) = R(2);
    }
    R(4) = -(s32)(s16)MEM_R16(frame + 0x16);
    R(5) = length;
    nc_vector_heading(c);
    R(3) = R(2);
    MEM_W16(R(19), R(17) << 4);
    MEM_W16(R(19) + 2, R(18) << 4);
    R(2) = R(3) << 4;
    MEM_W16(R(19) + 4, R(2));
    R(31) = MEM_R32(frame + 0x50);
    for (int reg = 19; reg >= 16; --reg)
        R(reg) = MEM_R32(frame + 0x40 + 4 * (reg - 16));
    R(29) += 0x58;
    c->budget -= cycles + 25;
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
