#include "decomp/distance.h"
#include <limits.h>

/* The record stores angles in 4096 units per turn. The original switches
 * direction at 2047, then adds the shifted step to the full current angle. */
bool nc_step_record_angle(CPUState *c) {
    const u32 offset = (R(4) - R(5)) & 0xfff;
    const bool reverse = offset >= 2047;
    const int cycles = reverse ? 12 : 11;
    if (c->budget <= cycles)
        return false;
    R(4) = reverse ? offset - 4096 : offset;
    const s32 step = (s32)R(4) >> (R(6) & 31);
    R(2) = R(5) + (u32)step;
    c->budget -= cycles;
    return true;
}

/* Both placement routines stage XYZ before writing it, then copy the fourth word. */
static void copy_placement_scales(CPUState *c, u32 destination) {
    for (u32 offset = 0; offset < 0x20; offset += 0x10) {
        R(5) = 0x800184d8 + offset;
        const u32 dst = destination + offset;
        R(2) = MEM_R32(R(5));
        R(3) = MEM_R32(R(5) + 4);
        R(4) = MEM_R32(R(5) + 8);
        MEM_W32(dst, R(2));
        MEM_W32(dst + 4, R(3));
        MEM_W32(dst + 8, R(4));
        R(2) = MEM_R32(R(5) + 12);
        MEM_W32(dst + 12, R(2));
    }
}

bool nc_begin_placement_matrices(CPUState *c) {
    if (c->budget <= 59)
        return false;
    R(29) -= 0x48;
    MEM_W32(R(29) + 0x3c, R(19));
    R(19) = R(4);
    MEM_W32(R(29) + 0x44, R(31));
    MEM_W32(R(29) + 0x40, R(20));
    MEM_W32(R(29) + 0x38, R(18));
    MEM_W32(R(29) + 0x34, R(17));
    MEM_W32(R(29) + 0x30, R(16));
    copy_placement_scales(c, R(29) + 0x10);

    R(20) = R(29) + 0x10;
    const u32 flags = MEM_R32(R(28) + 0x244);
    R(3) = 0x20000000;
    R(2) = MEM_R32(flags) & R(3);
    R(4) = R(29) + 0x20;
    if (R(2))
        R(20) = R(4); /* The second vector doubles the X scale. */
    c->budget -= R(2) ? 59 : 58;
    return true;
}

bool nc_begin_placement_vectors(CPUState *c) {
    if (c->budget <= 42)
        return false;
    R(29) -= 0x68;
    MEM_W32(R(29) + 0x60, R(16));
    R(16) = R(4);
    MEM_W32(R(29) + 0x64, R(31));
    copy_placement_scales(c, R(29) + 0x40);
    c->budget -= 42;
    return true;
}

bool nc_finish_placement_vectors(CPUState *c) {
    if (c->budget <= 9)
        return false;
    R(31) = MEM_R32(R(29) + 0x64);
    R(16) = MEM_R32(R(29) + 0x60);
    R(29) += 0x68;
    c->budget -= 9;
    return true;
}

/* The placement heading is the Y rotation in a 4096-unit turn. */
bool nc_prepare_placement_yaw(CPUState *c) {
    if (c->budget <= 9)
        return false;
    R(4) = R(29) + 0x30;
    R(2) = MEM_R16(R(16) + 0x0a);
    R(5) = R(29) + 0x10;
    MEM_W16(R(4) + 4, 0);
    MEM_W16(R(4), 0);
    R(31) = 0x8002aefc;
    MEM_W16(R(4) + 2, R(2));
    c->budget -= 9;
    return true;
}

bool nc_prepare_placement_yaw_scale(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(4) = R(29) + 0x10;
    R(31) = 0x8002af08;
    R(5) = R(29) + 0x40;
    c->budget -= 3;
    return true;
}

/* Apply the scaled yaw matrix to the local negative-Z axis. */
bool nc_prepare_placement_heading_vector(CPUState *c) {
    if (c->budget <= 8)
        return false;
    R(4) = R(29) + 0x10;
    R(5) = R(29) + 0x38;
    R(6) = R(16) + 0x118;
    R(2) = (u32)-256;
    MEM_W16(R(5) + 4, R(2));
    MEM_W16(R(5), 0);
    R(31) = 0x8002af28;
    MEM_W16(R(5) + 2, 0);
    c->budget -= 8;
    return true;
}

/* Each packed pair of signed XYZ vectors is transformed into a pair of
 * word-sized vectors with the same scaled yaw matrix. */
bool nc_prepare_placement_vector(CPUState *c, u32 source_offset, u32 result_offset, u32 resume) {
    if (c->budget <= 4)
        return false;
    R(4) = R(29) + 0x10;
    R(5) = R(16) + source_offset;
    R(31) = resume;
    R(6) = R(16) + result_offset;
    c->budget -= 4;
    return true;
}

/* After TransposeMatrix, quarter the selected XYZ scale in place. The
 * ScaleMatrixL call stores Y in its delay slot, after the X and Z writes. */
bool nc_prepare_transposed_placement_scale(CPUState *c) {
    if (c->budget <= 18)
        return false;
    R(4) = R(16);
    R(5) = R(20);
    const s32 x = (s32)MEM_R32(R(5));
    const s32 z = (s32)MEM_R32(R(5) + 8);
    MEM_W32(R(5), x >> 2);
    const s32 y = (s32)MEM_R32(R(5) + 4);
    R(3) = z >> 2;
    MEM_W32(R(5) + 8, R(3));
    R(2) = y >> 2;
    R(31) = 0x8002b200;
    MEM_W32(R(5) + 4, R(2));
    c->budget -= 18;
    return true;
}

/* Mode 3 moves each signed coordinate toward its target by a power-of-two
 * fraction. The shift at 0x148 independently controls the three angle steps. */
bool nc_step_placement_record(CPUState *c) {
    const int body_cycles = 63 * NC_INSN_CYCLES + 26 * (NC_LOAD_CYCLES - NC_INSN_CYCLES);
    if (c->budget <= body_cycles + 3 * 12)
        return false;

    const u32 record = R(4);
    R(29) -= 0x30;
    MEM_W32(R(29) + 0x28, R(16));
    R(16) = record;
    MEM_W32(R(29) + 0x2c, R(31));

    R(5) = (s16)MEM_R16(record + 0x0a);
    const u16 target_angle = MEM_R16(record + 0x12) & 0xfff;
    R(6) = MEM_R32(record + 0x148);
    s32 current = (s16)MEM_R16(record);
    R(7) = MEM_R32(record + 0x14c);
    MEM_W16(record + 0x12, target_angle);
    s32 target = (s16)MEM_R16(record + 0x18);
    R(4) = (s16)MEM_R16(record + 0x12);
    /* The next target is read before storing each updated coordinate. */
    for (unsigned axis = 0; axis < 2; ++axis) {
        const s32 next = current + ((target - current) >> (R(7) & 31));
        target = (s16)MEM_R16(record + 0x1a + 2 * axis);
        MEM_W16(record + 2 * axis, next);
        current = (s16)MEM_R16(record + 2 * axis + 2);
        R(7) = MEM_R32(record + 0x14c);
    }
    const s32 next_z = current + ((target - current) >> (R(7) & 31));
    R(2) = MEM_R16(record + 0x10) & 0xfff;
    MEM_W16(record + 4, next_z);
    R(3) = MEM_R16(record + 0x14) & 0xfff;
    MEM_W16(record + 0x10, R(2));
    R(31) = 0x8002b04c;
    MEM_W16(record + 0x14, R(3));
    nc_step_record_angle(c);

    R(4) = (s16)MEM_R16(record + 0x10);
    R(5) = (s16)MEM_R16(record + 8);
    R(6) = MEM_R32(record + 0x148);
    R(31) = 0x8002b060;
    MEM_W16(record + 0x0a, R(2));
    nc_step_record_angle(c);

    R(4) = MEM_R32(record + 0x12c);
    R(5) = MEM_R32(record + 0x128);
    R(6) = MEM_R32(record + 0x148);
    R(31) = 0x8002b074;
    MEM_W16(record + 8, R(2));
    nc_step_record_angle(c);

    R(3) = MEM_R16(record + 8) & 0xfff;
    MEM_W32(record + 0x128, R(2));
    R(2) = MEM_R16(record + 0x0a) & 0xfff;
    MEM_W16(record + 8, R(3));
    R(3) = MEM_R16(record + 0x0c) & 0xfff;
    MEM_W16(record + 0x0a, R(2));
    MEM_W16(record + 0x0c, R(3));
    R(31) = MEM_R32(R(29) + 0x2c);
    R(16) = MEM_R32(R(29) + 0x28);
    R(29) += 0x30;
    c->budget -= body_cycles;
    return true;
}

/* Bit 2 of the record flags selects this pair of angle updates. The
 * second call's delay slot stores the first result; the jump stores the second. */
bool nc_step_record_angle_pair(CPUState *c) {
    if (c->budget <= 48)
        return false;
    const u32 record = R(16);
    R(4) = (s32)(s16)MEM_R16(record + 0x12);
    R(5) = (s32)(s16)MEM_R16(record + 0x0a);
    R(6) = MEM_R32(record + 0x148);
    R(31) = 0x8002b420;
    nc_step_record_angle(c);
    R(4) = (s32)(s16)MEM_R16(record + 0x10);
    R(5) = (s32)(s16)MEM_R16(record + 0x08);
    R(6) = MEM_R32(record + 0x148);
    R(31) = 0x8002b434;
    MEM_W16(record + 0x0a, R(2));
    nc_step_record_angle(c);
    MEM_W16(record + 0x08, R(2));
    c->budget -= 24; /* Each angle step already charged its own cycles. */
    return true;
}

/* The paired points use unsigned halfwords. Their differences are full
 * words, including negative displacements across the signed-halfword edge. */
bool nc_prepare_paired_point_bearing(CPUState *c) {
    if (c->budget <= 19)
        return false;
    R(3) = MEM_R16(0x8008f078);
    R(4) = MEM_R16(0x8008f0bc);
    R(2) = MEM_R16(0x8008f07c);
    R(5) = MEM_R16(0x8008f0c0);
    R(4) = R(3) - R(4);
    R(5) = R(2) - R(5);
    R(31) = 0x8006e014;
    c->budget -= 19;
    return true;
}

bool nc_place_paired_point_actor(CPUState *c, bool store_heading) {
    const int cycles = store_heading ? 20 : 14;
    if (c->budget <= cycles)
        return false;
    const u32 actor = R(16);
    if (store_heading) {
        const u32 pose = MEM_R32(actor + 0x20);
        MEM_W32(pose + 0x48, R(2) << 4);
        R(4) = actor;
    }
    R(5) = 0;
    R(2) = (u32)MEM_R16(0x8008f078) << 8;
    R(3) = (u32)MEM_R16(0x8008f07c) << 8;
    MEM_W32(actor + 0x27c, R(2));
    MEM_W32(actor + 0x284, R(3));
    R(31) = 0x8006e04c;
    c->budget -= cycles;
    return true;
}

bool nc_call_actor_coordinate_init(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(5) = 0x800d7630;
    R(4) = R(16);
    R(31) = 0x8006e05c;
    c->budget -= 4;
    return true;
}

/* The null-actor branch also reaches this call with the shared record. */
bool nc_call_placement_record_update(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(4) = 0x800d7630;
    R(31) = 0x8006e06c;
    c->budget -= 4;
    return true;
}

bool nc_finish_paired_point_placement(CPUState *c) {
    if (c->budget <= 13)
        return false;
    R(2) = 0;
    R(31) = MEM_R32(R(29) + 48);
    R(17) = MEM_R32(R(29) + 44);
    R(16) = MEM_R32(R(29) + 40);
    R(29) += 56;
    c->budget -= 13;
    return true;
}

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

/* After projection, s1 points to the signed X/Z point in the record. */
bool nc_call_projected_point_heading(CPUState *c) {
    if (c->budget <= 2)
        return false;
    R(31) = 0x800473c8;
    R(4) = R(17);
    c->budget -= 2;
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

bool nc_finish_projected_point_update(CPUState *c, bool store_heading) {
    const int cycles = store_heading ? 14 : 12;
    if (c->budget <= cycles)
        return false;
    if (store_heading)
        MEM_W16(R(16) + 0x12, R(2));
    R(31) = MEM_R32(R(29) + 32);
    R(17) = MEM_R32(R(29) + 28);
    R(16) = MEM_R32(R(29) + 24);
    R(29) += 40;
    c->budget -= cycles;
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

bool nc_call_spatial_attenuation(CPUState *c, bool actor) {
    if (c->budget <= 9)
        return false;
    R(3) = MEM_R32(R(actor ? 18 : 19));
    R(17) = R(2);
    R(4) = MEM_R16(R(3) + 8);
    R(5) = R(17);
    R(31) = actor ? 0x8006c7dc : 0x8006c9c4;
    c->budget -= 9;
    return true;
}

/* Scale the level by the bearing gap. The actor path uses a signed
 * halfword level and a logical shift; the point path uses signed words. */
bool nc_scale_spatial_level(CPUState *c, bool actor) {
    const u32 level = actor ? (u32)(s32)(s16)R(2) : R(2);
    const bool negative = (s32)(R(16) * level) < 0;
    const int cycles = (actor ? 17 : 16) + negative;
    if (c->budget <= cycles)
        return false;
    R(6) = R(2);
    nc_mult(c, R(16), level);
    const u32 adjusted = c->lo + (negative ? 2047 : 0);
    const u32 descriptor = MEM_R32(R(actor ? 18 : 19));
    const u32 flags = MEM_R16(descriptor + 8);
    R(2) = flags & 2;
    if (actor) {
        R(3) = adjusted;
        R(7) = adjusted >> 11;
    } else {
        R(3) = flags;
        R(5) = adjusted;
        R(7) = (s32)adjusted >> 11;
    }
    c->budget -= cycles;
    return true;
}

/* The signed cutoff precedes all actor reads. Either of the first two
 * actor flags can admit a nearby source to the remaining checks. */
NcActorLevelPath nc_check_actor_level_distance(CPUState *c) {
    if (c->budget <= 17)
        return NC_ACTOR_LEVEL_FALLBACK;
    R(2) = (s32)R(17) <= 2400;
    R(3) = 0x40000;
    c->budget -= 3;
    if (!R(2))
        return NC_ACTOR_LEVEL_DISTANT;
    R(2) = MEM_R32(R(20) + 0x2c8) & R(3);
    c->budget -= 7;
    if (R(2))
        return NC_ACTOR_LEVEL_CHECK_FLAGS;
    R(2) = MEM_R32(R(20) + 0x2cc) & 0x800;
    R(4) = R(18);
    c->budget -= 7;
    return R(2) ? NC_ACTOR_LEVEL_CHECK_FLAGS : NC_ACTOR_LEVEL_DISTANT_RECORD_READY;
}

NcActorLevelPath nc_check_actor_level_flags(CPUState *c) {
    if (c->budget <= 18)
        return NC_ACTOR_LEVEL_FALLBACK;
    R(3) = MEM_R32(R(20) + 0x2c8);
    R(2) = R(3) & 0x10000000;
    R(4) = R(18);
    c->budget -= 7;
    if (R(2))
        return NC_ACTOR_LEVEL_DISTANT_RECORD_READY;
    R(2) = R(3) & 0x80000;
    R(3) = 0x40000000;
    c->budget -= 4;
    if (!R(2))
        return NC_ACTOR_LEVEL_SET;
    R(3) |= 0x10;
    R(2) = MEM_R32(R(20) + 0x330) & R(3);
    R(4) = R(21) << 16;
    c->budget -= 7;
    return R(2) ? NC_ACTOR_LEVEL_DISTANT : NC_ACTOR_LEVEL_CHANNEL_READY;
}

/* Some incoming branches supply a0 in their delay slot and enter one
 * instruction later. Keep the argument supplied by that path. */
bool nc_call_distant_actor(CPUState *c, bool load_record) {
    const int cycles = load_record ? 5 : 4;
    if (c->budget <= cycles)
        return false;
    if (load_record)
        R(4) = R(18);
    R(5) = 0;
    R(6) = (s32)(s16)R(21);
    R(31) = 0x8006c894;
    c->budget -= cycles;
    return true;
}

bool nc_set_actor_levels(CPUState *c, bool load_channel) {
    const int cycles = load_channel ? 8 : 7;
    if (c->budget <= cycles)
        return false;
    R(4) = load_channel ? (s32)(s16)R(21) : (s32)R(4) >> 16;
    R(5) = (s32)(s16)R(7);
    R(6) = (s32)(s16)(R(6) - R(7));
    R(31) = 0x8006c8bc;
    c->budget -= cycles;
    return true;
}

/* The flagged entry either forces full levels or applies a signed 2400-unit
 * cutoff. The ordinary entry always splits the level using the bearing. */
NcPointLevelAction nc_select_point_levels(CPUState *c, bool flagged) {
    const bool full = flagged && (R(3) & 4);
    const bool distant = flagged && !full && (s32)R(17) > 2400;
    const int cycles = distant ? 11 : flagged && !full ? 13 : 9;
    if (c->budget <= cycles)
        return NC_POINT_LEVEL_FALLBACK;
    const u32 channel = (s32)(s16)R(20);
    c->budget -= cycles;
    if (distant) {
        R(2) = 0;
        R(4) = R(19);
        R(5) = 0;
        R(6) = channel;
        R(31) = 0x8006ca34;
        return NC_POINT_LEVEL_DISTANT;
    }
    R(4) = channel;
    if (full) {
        R(2) = 4;
        R(5) = R(6) = 0x2fff;
    } else {
        if (flagged)
            R(2) = 1;
        R(5) = (s32)(s16)R(7);
        R(6) = (s32)(s16)(R(6) - R(7));
    }
    R(31) = 0x8006ca60;
    return NC_POINT_LEVEL_SET;
}

/* Both spatial callers scale their planar distance against 2400. Bit 1
 * squares the remaining distance first, with a separate truncated divide.
 * Keep each low-word product and the final reciprocal multiply's HI/LO. */
bool nc_spatial_attenuation(CPUState *c) {
    if (c->budget <= 31)
        return false;
    const u32 maximum = 0x2fff, reciprocal = 0x1b4e81b5;
    const bool quadratic = (R(4) & 2) != 0;
    u32 distance = R(5);
    if (quadratic) {
        const u32 remaining = 2400 - distance;
        distance = (s32)(remaining * remaining) / 2400;
    }
    const u32 numerator = distance * maximum;
    nc_mult(c, numerator, reciprocal);
    R(6) = c->hi;
    R(4) = quadratic ? reciprocal : 0;
    const s32 quotient = (s32)numerator / 2400;
    const s32 level = quadratic ? quotient : (s32)maximum - quotient;
    int cycles = quadratic ? 29 : 22;
    if (level < 0) {
        R(3) = 0;
        cycles += 2;
    } else if (level > (s32)maximum) {
        R(3) = maximum;
        ++cycles;
    } else {
        R(3) = level;
    }
    R(2) = R(3);
    c->budget -= cycles;
    return true;
}

/* Both spatial bearing calls subtract reference X and add reference Z.
 * An actor supplies fixed-point words; a point supplies signed halfwords. */
bool nc_prepare_spatial_actor_bearing(CPUState *c) {
    if (c->budget <= 25)
        return false;
    R(20) = MEM_R32(R(18) + 4);
    R(21) = R(5);
    const s32 x = (s32)MEM_R32(R(20) + 0x27c) >> 8;
    R(19) = x - (s16)MEM_R16(0x800d7630);
    R(4) = R(19);
    R(2) = (s32)MEM_R32(R(20) + 0x284) >> 8;
    R(3) = (s32)(s16)MEM_R16(0x800d7634);
    R(17) = R(2) + R(3);
    R(5) = R(17);
    R(31) = 0x8006c750;
    c->budget -= 25;
    return true;
}

bool nc_begin_spatial_actor(CPUState *c) {
    if (c->budget <= 9)
        return false;
    R(29) -= 48;
    MEM_W32(R(29) + 24, R(18));
    R(18) = R(4);
    MEM_W32(R(29) + 40, R(31));
    for (int reg = 21; reg >= 19; --reg)
        MEM_W32(R(29) + 16 + 4 * (reg - 16), R(reg));
    MEM_W32(R(29) + 20, R(17));
    MEM_W32(R(29) + 16, R(16));
    c->budget -= 9;
    return true;
}

bool nc_begin_spatial_point(CPUState *c) {
    if (c->budget <= 8)
        return false;
    R(29) -= 40;
    MEM_W32(R(29) + 28, R(19));
    R(19) = R(4);
    MEM_W32(R(29) + 36, R(31));
    MEM_W32(R(29) + 32, R(20));
    for (int reg = 18; reg >= 16; --reg)
        MEM_W32(R(29) + 16 + 4 * (reg - 16), R(reg));
    c->budget -= 8;
    return true;
}

/* The distant call returns to a jump and delay slot before the shared epilogue. */
bool nc_finish_spatial_frame(CPUState *c, bool actor, bool distant) {
    const int cycles = (actor ? 24 : 21) + (distant ? 2 : 0);
    if (c->budget <= cycles)
        return false;
    R(31) = MEM_R32(R(29) + (actor ? 40 : 36));
    for (int reg = actor ? 21 : 20; reg >= 16; --reg)
        R(reg) = MEM_R32(R(29) + 16 + 4 * (reg - 16));
    R(29) += actor ? 48 : 40;
    c->budget -= cycles;
    return true;
}

bool nc_prepare_spatial_point_bearing(CPUState *c) {
    if (c->budget <= 23)
        return false;
    R(6) = MEM_R32(R(19) + 4);
    const s32 reference_x = (s16)MEM_R16(0x800d7630);
    R(20) = R(5);
    R(18) = (s16)MEM_R16(R(6) + 0x18) - reference_x;
    R(3) = (s32)(s16)MEM_R16(R(6) + 0x1c);
    R(2) = (s32)(s16)MEM_R16(0x800d7634);
    R(4) = R(18);
    R(17) = R(3) + R(2);
    R(5) = R(17);
    R(31) = 0x8006c938;
    c->budget -= 23;
    return true;
}

/* Fold the quarter-turn-adjusted bearing before computing planar distance.
 * The guest starts reflecting at 2047, so that input yields a gap of 2049. */
bool nc_prepare_spatial_distance(CPUState *c, bool actor) {
    if (c->budget <= 24)
        return false;
    const s32 reference = (s16)MEM_R16(0x800d763a) - 1024;
    const u32 offset = (R(2) - (u32)reference) & 4095;
    const bool reflect = offset >= 2047;
    const int cycles = reflect ? 28 : 24;
    if (c->budget <= cycles)
        return false;
    R(16) = reflect ? 4096 - offset : offset;
    const u32 x = R(actor ? 19 : 18);
    R(2) = x * x;
    nc_mult(c, R(17), R(17));
    R(3) = c->lo;
    R(4) = R(2) + R(3);
    R(31) = actor ? 0x8006c7c8 : 0x8006c9b0;
    c->budget -= cycles;
    return true;
}

/* The target is a signed whole-unit X/Z pair. Convert each actor coordinate
 * from eight fractional bits before subtracting it from the target. */
bool nc_prepare_target_bearing(CPUState *c) {
    if (c->budget <= 21)
        return false;
    const s32 actor_x = (s32)MEM_R32(R(19) + 0x27c) >> 8;
    const s32 target_x = (s16)MEM_R16(0x800d4ca0);
    const s32 target_z = (s16)MEM_R16(0x800d4ca2);
    R(17) = target_x - actor_x;
    R(2) = (s32)MEM_R32(R(19) + 0x284) >> 8;
    R(16) = (u32)target_z - R(2);
    R(4) = R(17);
    R(5) = R(16);
    R(31) = 0x800587a0;
    c->budget -= 21;
    return true;
}

/* Store the reflected 16-bit turn and pass the low-word sum of squares
 * to the distance root. HI/LO retain the signed Z square. */
bool nc_prepare_target_distance(CPUState *c) {
    if (c->budget <= 11)
        return false;
    R(3) = R(17) * R(17);
    nc_mult(c, R(16), R(16));
    R(2) = -(R(2) << 4);
    MEM_W16(R(18) + 0x34, R(2));
    R(9) = c->lo;
    R(4) = R(3) + R(9);
    R(31) = 0x800587cc;
    c->budget -= 11;
    return true;
}

/* The vertical component uses the actor Y word directly and biases the
 * signed record height by -300. Keep the guest's wrapped numerator and
 * signed quotient/remainder. The two break paths use the original code. */
bool nc_store_target_vertical_velocity(CPUState *c) {
    const u32 divisor = R(2) + 140;
    const int cycles = divisor == UINT32_MAX ? 29 : 27;
    if (c->budget <= cycles || !divisor)
        return false;
    const u32 actor = MEM_R32(0x800ce9c4);
    const s32 height = (s16)MEM_R16(R(18) + 0x12);
    const u32 delta = MEM_R32(actor + 0x280) - (u32)(height - 300);
    const u32 numerator = delta * 40;
    if (divisor == UINT32_MAX && numerator == 0x80000000u)
        return false;
    R(2) = divisor;
    R(4) = delta;
    nc_div(c, numerator, divisor);
    R(1) = 0x80000000u;
    R(3) = c->lo;
    R(5) = 0x2fff;
    R(6) = 2;
    MEM_W16(R(18) + 0x38, 0);
    MEM_W16(R(18) + 0x22, R(3));
    c->budget -= cycles;
    return true;
}

/* The record stores signed whole-unit X/Z, while the actor has eight
 * fractional bits. The bearing adds record X and subtracts record Z. */
bool nc_prepare_planar_velocity(CPUState *c) {
    if (c->budget <= 28)
        return false;
    R(3) = MEM_R32(0x800ce9c4);
    R(29) -= 32;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(4);
    MEM_W32(R(29) + 24, R(18));
    R(18) = R(5);
    MEM_W32(R(29) + 28, R(31));
    MEM_W32(R(29) + 16, R(16));
    const s32 x = (s16)MEM_R16(R(17) + 0x10);
    R(4) = ((s32)MEM_R32(R(3) + 0x27c) >> 8) + x;
    R(5) = (s32)MEM_R32(R(3) + 0x284) >> 8;
    R(2) = (s32)(s16)MEM_R16(R(17) + 0x14);
    R(5) -= R(2);
    R(31) = 0x800588b0;
    c->budget -= 28;
    return true;
}

/* Store the reflected bearing in 16-bit turn units. Project the speed
 * with a negative quarter-turn offset and signed Q14 table samples. */
bool nc_finish_planar_velocity(CPUState *c) {
    if (c->budget <= 53)
        return false;
    const u32 angle = R(2) << 4;
    R(16) = angle - 0x4000;
    R(4) = R(16);
    MEM_W16(R(17) + 0x34, -angle);
    nc_heading_cosine(c);
    nc_mult(c, (u32)(s32)(s16)R(2), R(18));
    R(6) = c->lo;
    MEM_W16(R(17) + 0x20, (s32)R(6) >> 14);
    R(4) = R(16);
    nc_heading_sine(c);
    nc_mult(c, (u32)(s32)(s16)R(2), R(18));
    R(6) = c->lo;
    R(2) = (s32)R(6) >> 14;
    MEM_W16(R(17) + 0x24, R(2));
    R(31) = MEM_R32(R(29) + 28);
    R(18) = MEM_R32(R(29) + 24);
    R(17) = MEM_R32(R(29) + 20);
    R(16) = MEM_R32(R(29) + 16);
    R(29) += 32;
    c->budget -= 35; /* The two lookups charged nine cycles each. */
    return true;
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

static void transform_part_position(CPUState *c, u32 part) {
    gte_write_data(c, 0, MEM_R32(part + 0x10));
    gte_write_data(c, 1, MEM_R32(part + 0x14));
    gte_command(c, 0x0480012); /* MVMVA, rotation matrix, V0, translation */
    const u32 output = part + 0x2c;
    for (unsigned axis = 0; axis < 3; ++axis)
        MEM_W32(output + 4 * axis, gte_read_data(c, 25 + axis));
    R(12) = gte_read_ctrl(c, 31);
    R(2) = R(29) + 0xa8;
    MEM_W32(R(2), R(12));
}

/* Install the caller's rotation and translation, then transform the
 * part's signed local XYZ. The stored position is four times the MAC
 * result. The stack flag word is written before the positions are reread. */
bool nc_start_scaled_part_position(CPUState *c) {
    if (c->budget <= 82)
        return false;
    const u32 frame = R(29) - 0xf0, part = R(5);
    R(29) = frame;
    R(10) = part;
    MEM_W32(frame + 0xe8, R(30));
    R(30) = R(6);
    MEM_W32(frame + 0xd4, R(19));
    R(19) = 0;
    MEM_W32(frame + 0xe0, R(22));
    R(22) = 0;
    R(15) = R(30);
    const unsigned saved[] = {23, 21, 20, 18, 17, 16};
    for (unsigned i = 0; i < sizeof saved / sizeof saved[0]; ++i)
        MEM_W32(frame + 0xc8 + 4 * (saved[i] - 16), R(saved[i]));
    MEM_W32(frame + 0xb0, R(4));
    MEM_W32(frame + 0xb8, part);
    load_part_transform(c, R(30));

    transform_part_position(c, part);
    const u32 output = part + 0x2c;
    const u32 x = MEM_R32(output);
    R(3) = MEM_R32(output + 8) << 2;
    MEM_W32(output, x << 2);
    R(2) = MEM_R32(output + 4) << 2;
    MEM_W32(output + 8, R(3));
    MEM_W32(output + 4, R(2));
    c->budget -= 82;
    return true;
}

/* Angles have 65536 units per turn. Only the shared X cosine uses the
 * signed, unwrapped table index and the low twelve bits of the sample. */
bool nc_sample_traversal_angles(CPUState *c, bool shared) {
    const int cycles = shared ? 57 : 54;
    if (c->budget <= cycles)
        return false;
    const u32 source = shared ? R(23) : R(10) + 0x40;
    const u32 x = MEM_R32(source) + 2;
    const u32 y = MEM_R32(source + 4) + 2;
    const u32 z = MEM_R32(source + 8) + 2;
    const u32 x_offset = (x >> 3) & 0x1ffe;
    const u32 y_offset = (y >> 3) & 0x1ffe;
    const u32 z_offset = (z >> 3) & 0x1ffe;
    const u32 cosine_x_offset = shared ? (u32)((s32)x >> 4) * 2 : x_offset;
    const u32 cosine_x = MEM_R16(R(21) + cosine_x_offset);
    R(11) = shared ? (cosine_x & 0xfff) >> 2 : (u32)((s32)(s16)cosine_x >> 2);
    const u32 sine_x = MEM_R16(R(20) + x_offset);
    R(9) = (s32)(s16)sine_x >> 2;
    R(7) = (s32)(s16)MEM_R16(R(21) + y_offset) >> 2;
    R(6) = (s32)(s16)MEM_R16(R(21) + z_offset) >> 2;
    R(5) = shared ? R(20) + y_offset : sine_x << 16;
    R(4) = (s32)(s16)MEM_R16(R(20) + y_offset) >> 2;
    R(3) = R(20) + z_offset;
    R(2) = (u32)MEM_R16(R(3)) << 16;
    R(8) = (s32)R(2) >> 18;
    c->budget -= cycles;
    return true;
}

/* Build the Q12 rotation at sp+0x80 of the traversal frame. The guest
 * spills one intermediate and clears the matrix translation. */
bool nc_build_traversal_matrix(CPUState *c) {
    if (c->budget <= 75)
        return false;
    const u32 frame = R(29), matrix = frame + 0x80;
    const u32 cx = R(11), cy = R(7), cz = R(6);
    const u32 sx = R(9), sy = R(4), sz = R(8);
    R(12) = cy * cz;
    R(14) = cz * sy;
    R(24) = cx * cz;
    R(25) = sx * cz;
    R(5) = shift_part_product(cx * cy);
    R(16) = R(5) * sz;
    R(3) = shift_part_product(sx * sy);
    R(6) = shift_part_product(sx * cy);
    R(9) = R(6) * sz;
    R(4) = shift_part_product(cx * sy);
    R(11) = R(4) * sz;

    MEM_W32(matrix + 20, 0);
    MEM_W32(matrix + 24, 0);
    MEM_W32(matrix + 28, 0);
    MEM_W32(frame + 0xc0, R(16));
    R(16) = MEM_R32(frame + 0xc0);
    MEM_W16(matrix + 2, -sz);
    MEM_W16(matrix, shift_part_product(R(12)));
    MEM_W16(matrix + 4, shift_part_product(-R(14)));
    MEM_W16(matrix + 8, shift_part_product(R(24)));
    nc_mult(c, R(3), sz);
    MEM_W16(matrix + 14, shift_part_product(R(25)));
    R(3) = shift_part_product(R(16)) - R(3);
    R(4) += shift_part_product(R(9));
    R(2) = -shift_part_product(R(11)) - R(6);
    MEM_W16(matrix + 6, R(3));
    MEM_W16(matrix + 12, R(4));
    MEM_W16(matrix + 10, R(2));
    R(8) = c->lo;
    R(7) = shift_part_product(R(8));
    R(5) -= R(7);
    MEM_W16(matrix + 16, R(5));
    c->budget -= 75;
    return true;
}

/* Linked parts always sample angles. A leaf needs flag 0x80. The
 * delay slots leave bit 3 in v0 even when the leaf skips sampling. */
NcHeadingSource nc_select_part_heading_source(CPUState *c) {
    if (c->budget <= 17)
        return NC_HEADING_FALLBACK;
    const u32 part = R(10);
    R(2) = R(13) & 8;
    c->budget -= 6;
    if (!MEM_R32(part + 12)) {
        c->budget -= 6;
        if (!MEM_R32(part + 4)) {
            c->budget -= 3;
            if (!(R(13) & 0x80))
                return NC_HEADING_SKIP;
        }
    }
    c->budget -= 2;
    return R(2) ? NC_HEADING_FROM_SHARED : NC_HEADING_FROM_PART;
}

/* The starting part already holds its scaled position. Other parts use
 * the current GTE matrix and store the MAC coordinates directly. */
bool nc_update_child_position(CPUState *c) {
    if (c->budget <= 23)
        return false;
    const u32 part = R(10);
    R(16) = MEM_R32(R(29) + 0xb8);
    R(2) = part + 0x10;
    c->budget -= 6;
    if (part != R(16)) {
        transform_part_position(c, part);
        c->budget -= 17;
    }
    return true;
}

static void locate_scaled_part_stack_slot(CPUState *c) {
    R(2) = R(19) << 3;
    R(18) = R(29) + R(2);
}

/* On initial entry, non-root parts select a mask from the actor's table.
 * The branch-resume entry keeps the mask and restores the saved transform. */
bool nc_begin_scaled_part_walk(CPUState *c) {
    if (c->budget <= 31)
        return false;
    R(16) = MEM_R32(R(29) + 0xb0);
    if (MEM_R32(R(16) + 0x20) != R(10)) {
        const u32 index = MEM_R8(R(10) + 0x4c);
        R(22) = MEM_R16(R(16) + 0x10a + 2 * index) & 0x1f00;
        c->budget -= 11;
    }
    R(23) = 0x8009b2a8;
    R(21) = 0x80081be4;
    R(20) = 0x80083be4;
    R(17) = R(29);
    locate_scaled_part_stack_slot(c);
    c->budget -= 20;
    return true;
}

/* Follow the next part before popping a pending branch. The pop branch
 * decrements its cursor in the delay slot, including on the return path. */
NcNodeContinuation nc_advance_scaled_part_walk(CPUState *c) {
    if (c->budget <= 38)
        return NC_NODE_FALLBACK;
    R(10) = MEM_R32(R(10) + 4);
    c->budget -= 6;
    if (R(10))
        return NC_NODE_NEXT;
    R(17) -= 8;
    c->budget -= 2;
    if (R(19))
        return NC_NODE_POP;
    const u32 frame = R(29);
    R(30) = MEM_R32(frame + 0xe8);
    for (int reg = 23; reg >= 16; --reg)
        R(reg) = MEM_R32(frame + 0xc8 + 4 * (reg - 16));
    R(29) += 0xf0;
    c->budget -= 30;
    return NC_NODE_RETURN;
}

bool nc_resume_scaled_part_walk(CPUState *c) {
    if (c->budget <= 42)
        return false;
    --R(19);
    R(15) = MEM_R32(R(17));
    load_part_transform(c, R(15));
    R(10) = MEM_R32(R(17) + 4);
    locate_scaled_part_stack_slot(c);
    c->budget -= 42;
    return true;
}

/* Save pending branches before reading the part index. An active mask
 * selects matching parts; a zero mask selects parts with no group bits. */
bool nc_filter_scaled_part(CPUState *c) {
    if (c->budget <= 34)
        return false;
    const u32 part = R(10);
    const u32 branch = MEM_R32(part + 12);
    int cycles = 12;
    if (branch) {
        MEM_W32(R(18) + 4, branch);
        MEM_W32(R(18), R(15));
        R(18) += 8;
        R(17) += 8;
        ++R(19);
        cycles += 5;
    }
    const u32 index = MEM_R8(part + 0x4c);
    if (index) {
        R(16) = MEM_R32(R(29) + 0xb0);
        R(13) = (s32)(s16)MEM_R16(R(16) + 0x10a + 2 * index);
        cycles += 10;
    } else {
        R(13) = 0;
        ++cycles;
    }
    if (R(22)) {
        R(2) = R(13) & R(22);
        cycles += 4;
    } else {
        R(2) = R(13) & 0x1f00;
        cycles += R(2) ? 7 : 5;
    }
    c->budget -= cycles;
    return true;
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

/* The paired SetRotMatrix and SetTransMatrix calls also leave the
 * translation in t0-t2 and the last rotation words in t3-t4. */
static void install_node_matrix(CPUState *c, u32 matrix, u32 continuation) {
    R(4) = matrix;
    for (unsigned reg = 0; reg < 8; ++reg) {
        const u32 word = MEM_R32(R(4) + 4 * reg);
        R(8 + (reg % 5)) = word;
        gte_write_ctrl(c, reg, word);
    }
    R(31) = continuation;
}

/* The part traversal keeps its pending branches and temporary rotation
 * matrix at sp+16 and sp+144 in a 208-byte guest frame. */
bool nc_start_part_traversal(CPUState *c) {
    if (c->budget <= 53)
        return false;
    const u32 frame = R(29) - 208;
    R(29) = frame;
    MEM_W32(frame + 0xbc, R(17));
    R(17) = R(4);
    MEM_W32(frame + 0xc4, R(19));
    R(19) = 0;
    MEM_W32(frame + 0xb8, R(16));
    R(16) = R(5);
    MEM_W32(frame + 0xcc, R(31));
    MEM_W32(frame + 0xc8, R(20));
    MEM_W32(frame + 0xc0, R(18));
    install_node_matrix(c, R(16), 0x8002672c);
    R(18) = frame + 0x10;
    R(20) = frame + 0x90;
    c->budget -= 53;
    return true;
}

bool nc_restore_part_branch(CPUState *c) {
    if (c->budget <= 47)
        return false;
    R(16) = MEM_R32(R(18));
    --R(19);
    install_node_matrix(c, R(16), 0x80026750);
    R(17) = MEM_R32(R(18) + 4);
    c->budget -= 47;
    return true;
}

bool nc_start_node_traversal(CPUState *c) {
    if (c->budget <= 60)
        return false;
    const u32 frame = R(29) - 0xe0;
    R(29) = frame;
    R(2) = R(4);
    MEM_W32(frame + 0xd0, R(20));
    R(20) = 0;
    MEM_W32(frame + 0xc8, R(18));
    R(18) = R(5);
    MEM_W32(frame + 0xd8, R(31));
    MEM_W32(frame + 0xd4, R(21));
    MEM_W32(frame + 0xcc, R(19));
    MEM_W32(frame + 0xc4, R(17));
    MEM_W32(frame + 0xc0, R(16));
    R(16) = MEM_R32(R(2) + 4);
    R(21) = MEM_R32(R(2) + 8);
    install_node_matrix(c, R(18), 0x800265a4);
    R(19) = frame + 16;
    R(17) = R(21);
    c->budget -= 60;
    return true;
}

bool nc_restore_node_branch(CPUState *c) {
    if (c->budget <= 48)
        return false;
    R(18) = MEM_R32(R(19));
    --R(20);
    install_node_matrix(c, R(18), 0x800265c4);
    R(16) = MEM_R32(R(19) + 4);
    R(17) = R(21);
    c->budget -= 48;
    return true;
}

bool nc_apply_node_matrix(CPUState *c) {
    if (c->budget <= 40)
        return false;
    install_node_matrix(c, R(18), 0x800266b4);
    c->budget -= 40;
    return true;
}

bool nc_prepare_part_composition(CPUState *c) {
    if (c->budget <= 5)
        return false;
    R(4) = R(16);
    R(5) = R(20);
    R(6) = R(16) = R(17) + 0x18;
    R(31) = 0x800267dc;
    c->budget -= 5;
    return true;
}

bool nc_apply_part_matrix(CPUState *c) {
    if (c->budget <= 40)
        return false;
    install_node_matrix(c, R(16), 0x800267ec);
    c->budget -= 40;
    return true;
}

/* This traversal translates every part, including leaves without rotation. */
bool nc_prepare_part_translation(CPUState *c) {
    if (c->budget <= 14)
        return false;
    const u32 node = R(17);
    R(2) = MEM_R32(node + 12);
    R(4) = node + 0x10;
    if (R(2)) {
        MEM_W32(R(18) + 4, R(2));
        MEM_W32(R(18), R(16));
        R(18) += 8;
        ++R(19);
        c->budget -= 4;
    }
    R(5) = node + 0x2c;
    R(6) = R(29) + 0xb0;
    R(31) = 0x80026784;
    c->budget -= 10;
    return true;
}

bool nc_select_part_rotation(CPUState *c) {
    if (c->budget <= 25)
        return false;
    const u32 node = R(17);
    R(4) = node;
    R(2) = MEM_R32(node + 12);
    int cycles = 6;
    if (!R(2)) {
        R(2) = MEM_R32(node + 4);
        cycles += 6;
        if (!R(2)) {
            const u32 descriptor = MEM_R32(node + 0x38);
            R(2) = MEM_R8(descriptor + 5) & 2;
            cycles += 11;
        }
    }
    if (R(2)) {
        R(5) = R(20);
        R(31) = 0x800267c8;
        cycles += 2;
    }
    c->budget -= cycles;
    return true;
}

/* Save a pending traversal branch, then select nodes that have links or
 * a flagged leaf descriptor. v0 retains the value that selected RotTrans. */
bool nc_select_node_transform(CPUState *c) {
    if (c->budget <= 36)
        return false;
    const u32 node = R(16);
    int cycles = 6;
    R(2) = MEM_R32(node + 12);
    if (R(2)) {
        MEM_W32(R(19) + 4, R(2));
        MEM_W32(R(19), R(18));
        R(19) += 8;
        R(2) = MEM_R32(node + 12); /* stack writes may overlap the link */
        ++R(20);
        cycles += 9;
    }
    if (!R(2)) {
        R(2) = MEM_R32(node + 4);
        cycles += 6;
        if (!R(2)) {
            const u32 descriptor = MEM_R32(node + 0x38);
            R(2) = MEM_R8(descriptor + 5) & 2;
            cycles += 11;
        }
    }
    if (R(2)) {
        R(4) = node + 0x10;
        R(5) = node + 0x2c;
        R(6) = R(29) + 0xb8;
        R(31) = 0x80026634;
        cycles += 4;
    }
    c->budget -= cycles;
    return true;
}

/* Follow the next link, pop a saved branch, or return to the caller.
 * The stack cursor moves in the delay slot of the second branch. */
NcNodeContinuation nc_advance_node_transform(CPUState *c, bool part_rotation) {
    const int return_cycles = part_rotation ? 29 : 32;
    if (c->budget <= return_cycles)
        return NC_NODE_FALLBACK;
    const unsigned node = part_rotation ? 17 : 16, cursor = part_rotation ? 18 : 19;
    R(node) = MEM_R32(R(node) + 4);
    c->budget -= 6;
    if (R(node))
        return NC_NODE_NEXT;
    R(cursor) -= 8;
    c->budget -= 2;
    if (R(cursor + 1))
        return NC_NODE_POP;
    const u32 frame_size = part_rotation ? 0xd0 : 0xe0;
    const u32 caller_sp = R(29) + frame_size;
    const int last_saved = part_rotation ? 20 : 21;
    R(31) = MEM_R32(caller_sp - (part_rotation ? 4 : 8));
    const u32 saved = R(29) + (part_rotation ? 0xb8 : 0xc0);
    for (int reg = last_saved; reg >= 16; --reg)
        R(reg) = MEM_R32(saved + 4 * (reg - 16));
    R(29) = caller_sp;
    c->budget -= return_cycles - 8;
    return NC_NODE_RETURN;
}

/* RotTrans has already written the node translation. Copy the nine
 * rotation halfwords in guest order before composing the node matrix. */
bool nc_prepare_node_rotation(CPUState *c) {
    if (c->budget <= 46)
        return false;
    const u32 output = R(16) + 0x18, source = R(17);
    MEM_W16(output, MEM_R16(R(21)));
    for (unsigned offset = 2; offset <= 16; offset += 2) {
        R(2) = MEM_R16(source + offset);
        MEM_W16(output + offset, R(2));
    }
    R(4) = R(18);
    R(5) = source;
    R(6) = R(18) = output;
    R(17) += 32;
    R(21) += 32;
    R(31) = 0x800266a4;
    c->budget -= 46;
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

/* Bit 3 selects the traversal root rotation at fp. Otherwise the part
 * inherits the current rotation at t7. Both paths install the result. */
bool nc_compose_traversal_rotation(CPUState *c) {
    const bool use_root = (R(13) & 8) != 0;
    const int cycles = use_root ? 127 : 125;
    if (c->budget <= cycles)
        return false;
    const u32 parent = use_root ? R(30) : R(15);
    const u32 local = R(29) + 0x80, output = R(10) + 0x18;
    for (unsigned reg = 0; reg < 5; ++reg)
        gte_write_ctrl(c, reg, MEM_R32(parent + 4 * reg));
    for (unsigned col = 0; col < 3; ++col)
        rotate_matrix_column(c, local + 2 * col, output + 2 * col);
    R(2) = output + 4;
    R(3) = R(15) = output;
    load_part_transform(c, output);
    c->budget -= cycles;
    return true;
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
