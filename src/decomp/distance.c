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

/* Clear the global placement record, then install defaults on the record
 * supplied by the caller. The guest memset runs between these two blocks. */
bool nc_begin_placement_defaults(CPUState *c) {
    if (c->budget <= 9)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(4);
    R(4) = 0x800d7630;
    R(5) = 0;
    MEM_W32(R(29) + 0x14, R(31));
    R(31) = 0x8002ac94;
    R(6) = 0x17c;
    c->budget -= 9;
    return true;
}

bool nc_finish_placement_defaults(CPUState *c) {
    if (c->budget <= 32)
        return false;
    const u32 record = R(16);
    MEM_W16(record + 4, 250);
    MEM_W32(record + 0x148, 5);
    MEM_W32(record + 0x14c, 2);
    R(3) = 725;
    R(2) = 180;
    MEM_W16(record, 0);
    MEM_W16(record + 2, 0);
    for (u32 axis = 0; axis < 6; axis += 2)
        MEM_W16(record + 8 + axis, 0);
    for (u32 axis = 0; axis < 6; axis += 2)
        MEM_W16(record + 0x20 + axis, 0);
    MEM_W32(record + 0x128, 0);
    MEM_W32(record + 0x12c, 0);
    MEM_W32(record + 0x134, R(3));
    MEM_W32(record + 0x130, R(3));
    MEM_W32(record + 0x13c, R(2));
    MEM_W32(record + 0x138, R(2));
    MEM_W32(record + 0x140, R(3));
    R(31) = MEM_R32(R(29) + 0x14);
    R(16) = MEM_R32(R(29) + 0x10);
    R(29) += 0x18;
    c->budget -= 32;
    return true;
}

/* Select the two placement angles in 65536-unit turns. The four guest
 * lookups return signed Q14 values; the stores use their Q8 components. */
bool nc_sample_placement_axes(CPUState *c) {
    if (c->budget <= 71)
        return false;
    const u32 flags = MEM_R32(R(28) + 0x244);
    R(29) -= 0x28;
    MEM_W32(R(29) + 0x1c, R(19));
    R(19) = R(4);
    MEM_W32(R(29) + 0x10, R(16));
    MEM_W32(R(29) + 0x20, R(20));
    MEM_W32(R(29) + 0x24, R(31));
    MEM_W32(R(29) + 0x18, R(18));
    MEM_W32(R(29) + 0x14, R(17));
    R(3) = 0x20000000;
    const bool wide = (MEM_R32(flags) & R(3)) != 0;
    R(16) = wide ? 0x2aaa : 0x1555;
    R(20) = wide ? 0x2e38 : 0x18e3;
    c->budget -= wide ? 35 : 33; /* The four lookups charge nine cycles each. */

    R(4) = R(16);
    nc_heading_cosine(c);
    R(17) = (s16)R(2) >> 6;
    R(4) = R(16);
    nc_heading_sine(c);
    R(18) = (s16)R(2) >> 6;
    R(4) = R(20);
    nc_heading_cosine(c);
    R(16) = (s16)R(2) >> 6;
    R(4) = R(20);
    R(31) = 0x8002ae1c;
    nc_heading_sine(c);
    return true;
}

/* Each angle supplies two X/Z vectors with opposite X components.
 * The first three signed table samples have already been shifted by six. */
bool nc_store_placement_axes(CPUState *c) {
    if (c->budget <= 35)
        return false;
    const u32 record = R(19);
    R(17) = 0u - R(17);
    R(3) = 0u - R(18);
    R(16) = 0u - R(16);
    R(2) = (s16)R(2) >> 6;
    MEM_W16(record + 0xc8, (u16)R(3));
    R(3) = 0u - R(2);
    MEM_W16(record + 0xd4, (u16)R(17));
    MEM_W16(record + 0xcc, (u16)R(17));
    MEM_W16(record + 0xd0, (u16)R(18));
    MEM_W16(record + 0x104, (u16)R(16));
    MEM_W16(record + 0xfc, (u16)R(16));
    MEM_W16(record + 0xf8, (u16)R(3));
    MEM_W16(record + 0x100, (u16)R(2));

    R(31) = MEM_R32(R(29) + 0x24);
    for (unsigned reg = 20; reg >= 16; --reg)
        R(reg) = MEM_R32(R(29) + 0x10 + 4 * (reg - 16));
    R(29) += 0x28;
    c->budget -= 35;
    return true;
}

/* The callers pass angles in 4096-unit turns and whole-world X, Y, -Z.
 * Both placement poses start at the supplied position and orientation. */
bool nc_init_placement_pose(CPUState *c) {
    const u32 record = 0x800d7630;
    if (c->budget <= 29)
        return false;
    R(3) = MEM_R16(R(29) + 0x10);
    R(8) = MEM_R16(R(29) + 0x14);
    R(2) = record + 8;
    R(1) = 0x800d0000;
    for (u32 offset = 8; offset <= 0x10; offset += 8) {
        MEM_W16(record + offset, (u16)R(4));
        MEM_W16(record + offset + 2, (u16)R(5));
        MEM_W16(record + offset + 4, (u16)R(6));
    }
    MEM_W16(record, (u16)R(7));
    MEM_W16(record + 0x18, (u16)R(7));
    MEM_W16(record + 0x16c, 0);
    MEM_W16(record + 2, (u16)R(3));
    MEM_W16(record + 4, (u16)R(8));
    MEM_W16(record + 0x1a, (u16)R(3));
    MEM_W16(record + 0x1c, (u16)R(8));
    c->budget -= 29;
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

bool nc_finish_placement_matrices(CPUState *c) {
    if (c->budget <= 21)
        return false;
    R(31) = MEM_R32(R(29) + 0x44);
    for (int reg = 20; reg >= 16; --reg)
        R(reg) = MEM_R32(R(29) + 0x30 + 4 * (reg - 16));
    R(29) += 0x48;
    c->budget -= 21;
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

/* Build the matrix at record + 0x38 from the three angles at +0x08. */
bool nc_prepare_placement_rotation(CPUState *c) {
    if (c->budget <= 5)
        return false;
    R(18) = R(19) + 8;
    R(4) = R(18);
    R(16) = R(19) + 0x38;
    R(31) = 0x8002b160;
    R(5) = R(16);
    c->budget -= 5;
    return true;
}

bool nc_prepare_placement_scale(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(4) = R(16);
    R(31) = 0x8002b16c;
    R(5) = R(20);
    c->budget -= 3;
    return true;
}

/* Transform the signed XYZ at the start of the record into the three-word
 * vector at +0x98, then install that vector as the matrix translation. */
bool nc_prepare_placement_position(CPUState *c) {
    if (c->budget <= 5)
        return false;
    R(4) = R(16);
    R(5) = R(19);
    R(17) = R(19) + 0x98;
    R(31) = 0x8002b180;
    R(6) = R(17);
    c->budget -= 5;
    return true;
}

bool nc_prepare_placement_translation(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(4) = R(16);
    R(31) = 0x8002b18c;
    R(5) = R(17);
    c->budget -= 3;
    return true;
}

bool nc_prepare_placement_light_rotation(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(4) = R(18);
    R(17) = R(19) + 0x78;
    R(31) = 0x8002b19c;
    R(5) = R(17);
    c->budget -= 4;
    return true;
}

bool nc_prepare_placement_light_scale(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(4) = R(17);
    R(31) = 0x8002b1a8;
    R(5) = R(20);
    c->budget -= 3;
    return true;
}

/* The matrix setters and final vector update pass their argument in the delay slot. */
bool nc_prepare_placement_call(CPUState *c, u32 argument, u32 resume) {
    if (c->budget <= 2)
        return false;
    R(31) = resume;
    R(4) = argument;
    c->budget -= 2;
    return true;
}

bool nc_prepare_placement_transpose(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(4) = R(16);
    R(16) = R(19) + 0x58;
    R(31) = 0x8002b1d0;
    R(5) = R(16);
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

bool nc_begin_actor_placement_target(CPUState *c, bool *has_pose) {
    if (c->budget <= 17)
        return false;
    R(29) -= 0x28;
    MEM_W32(R(29) + 0x1c, R(17));
    R(17) = R(4);
    MEM_W32(R(29) + 0x20, R(31));
    MEM_W32(R(29) + 0x18, R(16));
    R(4) = MEM_R32(R(17) + 0x164);
    *has_pose = MEM_R32(R(4) + 0x20) != 0;
    R(2) = 0;
    if (*has_pose)
        R(31) = 0x8002b260;
    c->budget -= *has_pose ? 17 : 15;
    return true;
}

bool nc_finish_actor_placement_target(CPUState *c, bool has_pose) {
    if (c->budget <= (has_pose ? 114 : 12))
        return false;
    if (has_pose) {
        R(3) = MEM_R32(R(17) + 0x164);
        MEM_W16(R(17) + 0x0a, R(2));
        R(4) = MEM_R32(R(3) + 0x50);
        R(31) = 0x8002b318;
        R(5) = R(29) + 0x10;
        nc_transform_part_position(c, true);
        R(3) = MEM_R16(R(29) + 0x12);
        R(2) = 1;
        MEM_W16(R(17) + 8, 0);
        R(3) = 40u - R(3);
        MEM_W16(R(17) + 2, R(3));
    }
    R(31) = MEM_R32(R(29) + 0x20);
    R(17) = MEM_R32(R(29) + 0x1c);
    R(16) = MEM_R32(R(29) + 0x18);
    R(29) += 0x28;
    c->budget -= has_pose ? 29 : 12; /* The part transform charges 85 cycles. */
    return true;
}

/* Offset the actor's whole-unit X/Z by 100 units along its 16-bit heading.
 * The Q14 components are multiplied by 25 and shifted by 12 in the game. */
bool nc_prepare_actor_placement_target(CPUState *c) {
    if (c->budget <= 71)
        return false;
    const u32 record = R(17);
    R(16) = (s32)(s16)R(2);
    R(31) = 0x8002b270;
    R(4) = R(16);
    nc_heading_sine(c);
    R(4) = R(16);
    const u32 x_actor = MEM_R32(record + 0x164);
    const s32 x = (s32)MEM_R32(x_actor + 0x27c) >> 8;
    R(5) = x + ((25 * (s16)R(2)) >> 12);
    R(31) = 0x8002b2a8;
    MEM_W32(record + 0x150, R(5));
    nc_heading_cosine(c);

    R(4) = 300;
    R(5) = 0x8000u - R(16);
    const u32 z_actor = MEM_R32(record + 0x164);
    const s32 z = (s32)MEM_R32(z_actor + 0x284) >> 8;
    const s32 offset_z = (25 * (s16)R(2)) >> 12;
    R(2) = MEM_R32(record + 0x150) << 8;
    MEM_W32(record + 0x158, z + offset_z);
    R(3) = MEM_R32(record + 0x158) << 8;
    R(6) = record;
    MEM_W32(record + 0x150, R(2));
    R(31) = 0x8002b304;
    MEM_W32(record + 0x158, R(3));
    c->budget -= 53; /* Both table lookups have charged their own nine cycles. */
    return true;
}

/* Bit 5 latches the current tick in the global placement record. Repeated
 * requests keep the original tick until the placement flags are reset. */
bool nc_latch_placement_tick(CPUState *c) {
    const u32 record = 0x800d7630;
    const u16 flags = MEM_R16(record + 0x16c);
    const bool latched = (flags & 0x20) != 0;
    const int cycles = latched ? 11 : 18;
    if (c->budget <= cycles)
        return false;
    R(5) = record + 0x16c;
    R(4) = flags;
    R(2) = flags | 0x20;
    if (!latched) {
        R(3) = MEM_R32(0x8008ef10);
        MEM_W16(R(5), (u16)R(2));
        R(1) = 0x800d0000;
        MEM_W32(record + 0x168, R(3));
    }
    c->budget -= cycles;
    return true;
}

/* The branch tests bit 2. Its delay slot saves bits 4 and 6 for the
 * alternate path, even when the paired-angle update is selected. */
bool nc_begin_placement_update(CPUState *c) {
    if (c->budget <= 17)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(4);
    R(1) = 0x800d0000;
    MEM_W32(0x800d7c14, 0xb00);
    MEM_W32(R(29) + 0x14, R(31));
    MEM_W32(0x800d7c10, 0x790000);
    R(3) = MEM_R16(R(16) + 0x16c);
    R(2) = R(3) & 0x50;
    c->budget -= 17;
    return true;
}

/* The entry delay slot leaves flags 0x50 in v0. While either is active,
 * advance four saved timestamps by the elapsed game ticks, modulo 2^32. */
bool nc_update_placement_clock(CPUState *c) {
    if (!R(2)) {
        if (c->budget <= 2)
            return false;
        c->budget -= 2;
        return true;
    }
    if (c->budget <= 41)
        return false;
    R(7) = 0x800d77a8;
    R(5) = 0x800cc044;
    R(6) = MEM_R32(0x8008ef10);
    const u32 previous = MEM_R32(R(7));
    R(2) = MEM_R32(R(5));
    R(3) = MEM_R32(0x800cc04c);
    R(4) = R(6) - previous;
    MEM_W32(R(5), R(2) + R(4));
    R(2) = MEM_R32(0x800cc048);
    MEM_W32(0x800cc04c, R(3) + R(4));
    R(3) = MEM_R32(0x800cc050);
    R(2) += R(4);
    R(3) += R(4);
    R(1) = 0x800d0000;
    MEM_W32(0x800cc048, R(2));
    MEM_W32(0x800cc050, R(3));
    MEM_W32(R(7), R(6));
    c->budget -= 41;
    return true;
}

bool nc_call_placement_reset(CPUState *c) {
    if (c->budget <= 2)
        return false;
    R(31) = 0x8002b5c0;
    c->budget -= 2;
    return true;
}

bool nc_finish_placement_reset(CPUState *c) {
    if (c->budget <= 8)
        return false;
    R(2) = (MEM_R16(R(16) + 0x16c) | 0x300u) & 0xff2fu;
    MEM_W16(R(16) + 0x16c, R(2));
    c->budget -= 8;
    return true;
}

bool nc_begin_placement_refresh(CPUState *c, bool *refresh) {
    if (c->budget <= 9)
        return false;
    R(2) = MEM_R16(R(16) + 0x16c) & 0x40;
    *refresh = R(2) != 0;
    if (*refresh) {
        R(31) = 0x8002b588;
        R(4) = R(16);
    }
    c->budget -= *refresh ? 9 : 7;
    return true;
}

/* A failed target update clears the preset and target flags. Either result
 * restarts a zero counter at one before the shared matrix update. */
bool nc_finish_placement_refresh(CPUState *c) {
    if (c->budget <= 16)
        return false;
    const u32 record = R(16);
    int cycles = 8;
    if (!R(2)) {
        MEM_W16(record + 0x16c, MEM_R16(record + 0x16c) & 0xffaf);
        cycles += 6;
    }
    const u32 counter = MEM_R32(record + 0x170);
    R(2) = 1;
    if (!counter) {
        MEM_W32(record + 0x170, R(2));
        cycles += 2;
    }
    c->budget -= cycles;
    return true;
}

NcPlacementCounterPath nc_check_placement_counter(CPUState *c) {
    if (c->budget <= 20)
        return NC_PLACEMENT_COUNTER_FALLBACK;
    const u32 record = R(16);
    R(3) = MEM_R32(record + 0x170);
    c->budget -= 6;
    if (!R(3))
        return NC_PLACEMENT_COUNTER_ZERO;

    /* The branch delay slot decrements v1 even when the flag skips the store. */
    R(2) = MEM_R16(record + 0x16c) & 0x10;
    R(3) -= 1;
    c->budget -= 7;
    if (!R(2))
        return NC_PLACEMENT_CHECK_TARGET;

    R(2) = (s32)MEM_R32(record + 0x174) < (s32)R(3);
    MEM_W32(record + 0x170, R(3));
    c->budget -= 7;
    return R(2) ? NC_PLACEMENT_UPDATE_MODE : NC_PLACEMENT_APPLY_PRESET;
}

/* Each 20-byte preset starts with two packed eight-byte groups: position
 * and angles. Load both words of each group before writing the record. */
bool nc_apply_placement_preset(CPUState *c) {
    if (c->budget <= 55)
        return false;
    const u32 record = R(16);
    for (unsigned offset = 0; offset <= 8; offset += 8) {
        const s16 index = (s16)MEM_R16(record + 0x16e);
        const u32 source = 0x800d00c0u + 20 * index + offset;
        const u32 dest = record + offset;
        R(3) = nc_lwr(c, source, nc_lwl(c, source + 3, (u32)(s32)index));
        const u32 second = nc_lwr(c, source + 4, nc_lwl(c, source + 7, 0));
        nc_swl(c, dest + 3, R(3));
        nc_swr(c, dest, R(3));
        nc_swl(c, dest + 7, second);
        nc_swr(c, dest + 4, second);
        if (offset == 0)
            R(5) = second;
        else
            R(4) = second;
    }
    R(2) = MEM_R16(record + 0x16c) | 8;
    MEM_W16(record + 0x16c, R(2));
    c->budget -= 55;
    return true;
}

/* Mode 1 passes the placement record and the linked actor's unsigned
 * byte at 0x398 to the update at 0x80047948. */
bool nc_begin_placement_mode(CPUState *c, bool *update_actor) {
    if (c->budget <= 21)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(4);
    R(3) = (s32)(s16)MEM_R16(0x800d7c28);
    R(2) = 1;
    MEM_W32(R(29) + 0x14, R(31));
    *update_actor = false;
    int cycles = 10;
    if (R(3) == 1) {
        R(2) = MEM_R32(R(16) + 0x160);
        cycles += 6;
        if (R(2)) {
            R(5) = MEM_R8(R(2) + 0x398);
            R(31) = 0x8002b384;
            *update_actor = true;
            cycles += 5;
        }
    }
    c->budget -= cycles;
    return true;
}

/* The dispatcher reads the current mode only when flag bit 1 is set.
 * Mode 3 advances both the signed XYZ coordinates and the record angles. */
bool nc_select_placement_step(CPUState *c, bool *step) {
    if (c->budget <= 20)
        return false;
    const u32 flags = MEM_R32(R(28) + 0x244);
    const bool enabled = (MEM_R32(flags) & 2) != 0;
    R(2) = 3;
    *step = false;
    int cycles = 11;
    if (enabled) {
        R(3) = (s32)(s16)MEM_R16(0x800d7c28);
        cycles += 7;
        if (R(3) == 3) {
            R(31) = 0x8002b3bc;
            R(4) = R(16);
            *step = true;
            cycles += 2;
        }
    }
    c->budget -= cycles;
    return true;
}

bool nc_restore_placement_frame(CPUState *c) {
    if (c->budget <= 9)
        return false;
    R(31) = MEM_R32(R(29) + 0x14);
    R(16) = MEM_R32(R(29) + 0x10);
    R(29) += 0x18;
    c->budget -= 9;
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

bool nc_begin_part_pose_heading(CPUState *c) {
    if (c->budget <= 18)
        return false;
    R(29) -= 0x40;
    MEM_W32(R(29) + 0x24, R(17));
    R(17) = R(4);
    MEM_W32(R(29) + 0x20, R(16));
    R(16) = R(5);
    R(2) = (s32)(s16)MEM_R16(0x800d4c9e);
    R(5) = R(6);
    MEM_W32(R(29) + 0x28, R(18));
    R(18) = 0;
    MEM_W32(R(29) + 0x3c, R(31));
    for (unsigned reg = 22; reg >= 19; --reg)
        MEM_W32(R(29) + 0x20 + 4 * (reg - 16), R(reg));
    c->budget -= 18;
    return true;
}

/* Compare the signed low word of the accumulated heading with one turn.
 * The original retains both halves of the signed product in HI and LO. */
bool nc_check_part_heading_turn(CPUState *c) {
    if (c->budget <= 17)
        return false;
    R(3) = (s32)(s16)MEM_R16(0x800d4c9c);
    nc_mult(c, R(3), 364u * R(16));
    R(8) = c->lo;
    R(2) = (s32)R(8) > 0xffff;
    R(5) = 0x10;
    c->budget -= 17;
    return true;
}

bool nc_reset_part_pose_heading(CPUState *c) {
    if (c->budget <= 5)
        return false;
    R(2) = MEM_R32(R(17) + 0x20);
    R(31) = 0x8001bd90;
    MEM_W32(R(2) + 0x48, 0);
    c->budget -= 5;
    return true;
}

/* Each of the five part tests takes 17 cycles. The first branch sets s2
 * in its delay slot; the other four leave twice the step in v0. */
NcPartFlagPath nc_check_turn_parts(CPUState *c) {
    if (c->budget <= 85)
        return NC_PART_FLAGS_FALLBACK;
    for (unsigned i = 0; i < 5; ++i) {
        const u32 index = MEM_R8(R(28) + 0x108 + i);
        const u32 part = MEM_R32(R(17) + 0x4c + 4 * index);
        const u32 flag = MEM_R8(part + 0x4d) & 8;
        R(18) = 1;
        R(2) = i == 0 ? flag : 2 * R(16);
        c->budget -= 17;
        if (flag)
            return i == 0 ? NC_PART_FLAGS_FIRST : NC_PART_FLAGS_LATER;
    }
    return NC_PART_FLAGS_CLEAR;
}

/* With all five parts clear, return one through the shared epilogue.
 * The actor flag word is written in the jump delay slot. */
bool nc_finish_part_turn(CPUState *c) {
    if (c->budget <= 13)
        return false;
    const u32 actor = R(17);
    R(2) = 1;
    MEM_W8(actor + 0x39c, 0x7f);
    R(3) = MEM_R32(actor + 0x330);
    R(4) = ~0x18u;
    MEM_W8(actor + 0x39b, 0);
    MEM_W8(actor + 0x38e, 0);
    MEM_W8(actor + 0x38a, 0);
    R(3) &= R(4);
    MEM_W32(actor + 0x330, R(3));
    c->budget -= 13;
    return true;
}

bool nc_reset_part_turn_counter(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(1) = 0x800d0000;
    MEM_W16(0x800d4c9c, 0);
    R(31) = 0x8001bea8;
    R(4) = R(17);
    c->budget -= 4;
    return true;
}

/* Every entry at 0x8001BEAC has v0 = 2*s0. The shift/add sequence
 * advances the pose heading by 364 units per step in a 65536-unit turn. */
bool nc_advance_part_pose_heading(CPUState *c, bool double_step, bool *search) {
    const bool start_search = R(18) == 0;
    const int cycles = (start_search ? 33 : 25) + double_step;
    if (c->budget <= cycles)
        return false;
    R(4) = MEM_R32(R(17) + 0x20);
    MEM_W32(R(4) + 0x48, MEM_R32(R(4) + 0x48) + 364u * R(16));
    R(3) = 0x800d4c9c;
    MEM_W16(R(3), (u16)(MEM_R16(R(3)) + 1));
    R(2) = 0;
    if (start_search) {
        R(19) = 0;
        R(21) = 0xb60b60b7;
        R(18) = 0x8008eea8;
        R(22) = 0x80081a58;
        R(20) = R(22);
    }
    *search = start_search;
    c->budget -= cycles;
    return true;
}

/* The loop sets s5 to the signed reciprocal multiplier for division by 360.
 * Keep the multiply results as well as the angle truncated toward zero. */
static u32 part_degrees_to_heading(CPUState *c, u32 address) {
    const u32 scaled = (u32)MEM_R16(address) << 16;
    nc_mult(c, scaled, R(21));
    R(8) = c->hi;
    R(3) = (s32)scaled >> 31;
    return (u32)((s32)(scaled + R(8)) >> 8) - R(3);
}

bool nc_begin_part_heading_search(CPUState *c) {
    if (c->budget <= 21)
        return false;
    const u32 heading = part_degrees_to_heading(c, R(20));
    const u32 pose = MEM_R32(R(17) + 0x20);
    R(4) = (heading - MEM_R32(pose + 0x48)) & 0xffff;
    R(6) = 0;
    R(2) = 0;
    c->budget -= 21;
    return true;
}

bool nc_test_part_heading(CPUState *c) {
    if (c->budget <= 19)
        return false;
    const u32 candidate = part_degrees_to_heading(c, R(22) + R(2));
    const u32 gap = (R(4) - candidate) & 0xffff;
    /* Accept offsets strictly within 182 units of zero across the wrap. */
    R(2) = gap - 182;
    R(3) = R(2) > 0xfe94;
    R(5) = R(29) + 0x18;
    c->budget -= 19;
    return true;
}

/* Each of five parts tries up to five headings. On a match the inner
 * loop exits immediately, leaving its candidate index in a2 untouched. */
NcPartSearchStep nc_advance_part_heading_search(CPUState *c, bool matched) {
    const u32 next_heading = R(6) + 1;
    if (!matched && (s32)next_heading < 5) {
        if (c->budget <= 4)
            return NC_PART_SEARCH_FALLBACK;
        R(6) = next_heading;
        R(2) = next_heading * 2;
        c->budget -= 4;
        return NC_PART_SEARCH_NEXT_HEADING;
    }

    const u32 next_part = R(19) + 1;
    const bool done = (s32)next_part >= 5;
    const int cycles = (matched ? 6 : 9) + done;
    if (c->budget <= cycles)
        return NC_PART_SEARCH_FALLBACK;
    if (!matched)
        R(6) = next_heading;
    R(18) += 1;
    R(19) = next_part;
    R(20) += 2;
    R(2) = !done;
    c->budget -= cycles;
    return done ? NC_PART_SEARCH_DONE : NC_PART_SEARCH_NEXT_PART;
}

bool nc_restore_part_search_frame(CPUState *c) {
    if (c->budget <= 27)
        return false;
    R(31) = MEM_R32(R(29) + 0x3c);
    for (unsigned reg = 22; reg >= 16; --reg)
        R(reg) = MEM_R32(R(29) + 0x20 + 4 * (reg - 16));
    R(29) += 0x40;
    c->budget -= 27;
    return true;
}

/* Select the part to transform and its point record. The preceding branch
 * supplies the output vector at sp+0x18 in a1. */
bool nc_select_part_bearing_point(CPUState *c) {
    if (c->budget <= 22)
        return false;
    const u32 part_index = MEM_R8(R(18));
    R(1) = 0x80090000u + R(6);
    R(3) = MEM_R8(0x8008eeb0u + R(6));
    const u32 points = MEM_R32(0x8008f0c8);
    R(4) = MEM_R32(R(17) + 0x4c + 4 * part_index);
    R(2) = R(3) * 36;
    R(16) = points + R(2);
    R(31) = 0x8001bfd0;
    c->budget -= 22;
    return true;
}

/* Both vectors are signed halfwords in whole units. The heading uses
 * transformed X minus point X, and point Z minus transformed Z. */
bool nc_prepare_part_point_bearing(CPUState *c) {
    if (c->budget <= 15)
        return false;
    R(3) = (s32)(s16)MEM_R16(R(16) + 0xa);
    R(4) = (s32)(s16)MEM_R16(R(29) + 0x18);
    R(2) = (s32)(s16)MEM_R16(R(16) + 0xc);
    R(5) = (s32)(s16)MEM_R16(R(29) + 0x1c);
    R(4) -= R(3);
    R(5) = R(2) - R(5);
    R(31) = 0x8001bfec;
    c->budget -= 15;
    return true;
}

/* Negate the vector bearing and expand it from 4096 to 65536 units per
 * turn. The fifth argument is stored before the selected part is loaded. */
bool nc_call_part_with_bearing(CPUState *c) {
    if (c->budget <= 16)
        return false;
    R(4) = R(17);
    R(2) = (0u - R(2)) << 4;
    const u32 part_index = MEM_R8(R(18));
    R(6) = R(2) & 0xfff0;
    MEM_W32(R(29) + 0x10, 1);
    R(3) = R(17) + 4 * part_index;
    R(5) = MEM_R32(R(3) + 0x4c);
    R(31) = 0x8001c01c;
    R(7) = R(19);
    c->budget -= 16;
    return true;
}

/* The cursor uses a signed remainder. A negative remainder selects a
 * halfword immediately before the four point indices copied to the frame. */
bool nc_select_bearing_point(CPUState *c) {
    if (c->budget <= 52)
        return false;
    R(29) -= 0x28;
    MEM_W32(R(29) + 0x18, R(16));
    R(16) = R(4);
    MEM_W32(R(29) + 0x20, R(18));
    R(18) = R(5);
    MEM_W32(R(29) + 0x1c, R(17));
    R(17) = 0x800d4c9a;
    MEM_W32(R(29) + 0x24, R(31));
    const s16 cursor = (s16)MEM_R16(R(17));
    R(6) = (s32)cursor;
    R(5) = 0x8008eeb8;
    const u32 first = MEM_R32(R(5));
    const u32 second = MEM_R32(R(5) + 4);
    const u32 indices = R(29) + 0x10;
    nc_swl(c, indices + 3, first);
    nc_swr(c, indices, first);
    nc_swl(c, indices + 7, second);
    nc_swr(c, indices + 4, second);

    const s16 point = (s16)MEM_R16(indices + 2 * (cursor % 4));
    R(2) = point * 36;
    R(3) = MEM_R32(0x8008f0c8) + R(2);
    c->budget -= cursor < 0 ? 52 : 51;
    return true;
}

/* The selected record stores -X and Z in whole units. Publish the target
 * halfwords, then reread the record before subtracting the actor position. */
bool nc_prepare_selected_point_bearing(CPUState *c) {
    if (c->budget <= 30)
        return false;
    const u32 point = R(3);
    R(1) = 0x800d0000;
    MEM_W16(0x800d4ca0, (u16)-MEM_R16(point + 0xa));
    MEM_W16(0x800d4ca2, MEM_R16(point + 0xc));
    R(2) = -(s32)(s16)MEM_R16(point + 0xa);
    const s32 actor_x = (s32)MEM_R32(R(16) + 0x27c) >> 8;
    const s32 actor_z = (s32)MEM_R32(R(16) + 0x284) >> 8;
    R(3) = (s32)(s16)MEM_R16(point + 0xc);
    R(4) = R(2) - actor_x;
    R(5) = R(3) - actor_z;
    R(31) = 0x8001c27c;
    c->budget -= 30;
    return true;
}

/* Expand the 12-bit vector heading to a 16-bit turn. The two actor
 * flags independently suppress the caller's signed step through the points. */
bool nc_finish_selected_point_bearing(CPUState *c) {
    if (c->budget <= 35)
        return false;
    R(4) = (R(2) << 4) & 0xffff;
    R(3) = MEM_R8(R(16) + 0x38a);
    int cycles = 23;
    if (!R(3)) {
        cycles = 28;
        if (!MEM_R8(R(16) + 0x39b)) {
            MEM_W16(R(17), (u16)(MEM_R16(R(17)) + R(18)));
            cycles = 35;
        }
    }
    R(2) = R(4);
    R(31) = MEM_R32(R(29) + 0x24);
    R(18) = MEM_R32(R(29) + 0x20);
    R(17) = MEM_R32(R(29) + 0x1c);
    R(16) = MEM_R32(R(29) + 0x18);
    R(29) += 0x28;
    c->budget -= cycles;
    return true;
}

/* Bearing from the supplied actor to the reference actor. Subtract both
 * 24.8 positions before shifting so negative fractional deltas round down. */
bool nc_prepare_reference_bearing(CPUState *c) {
    if (c->budget <= 24)
        return false;
    const u32 reference = MEM_R32(0x800ce9c4);
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x14, R(31));
    MEM_W32(R(29) + 0x10, R(16));
    const u32 x = MEM_R32(R(4) + 0x27c);
    R(3) = MEM_R32(reference + 0x27c) - x;
    const u32 z = MEM_R32(reference + 0x284);
    R(2) = MEM_R32(R(4) + 0x284);
    R(4) = (s32)R(3) >> 8;
    R(5) = (s32)(z - R(2)) >> 8;
    R(31) = 0x8001a754;
    c->budget -= 24;
    return true;
}

bool nc_call_reference_facing(CPUState *c) {
    if (c->budget <= 6)
        return false;
    R(4) = MEM_R32(0x800ce9c4);
    R(16) = R(2) << 4;
    R(31) = 0x8001a764;
    c->budget -= 6;
    return true;
}

/* Reverse the bearing to compare both angles from the reference actor.
 * The original wraps 0x7fff itself to -32769. */
bool nc_finish_reference_bearing(CPUState *c) {
    const u32 gap = (R(2) + 0x8000u - R(16)) & 0xffff;
    const bool wrap = gap >= 0x7fff;
    const int cycles = wrap ? 25 : 22;
    if (c->budget <= cycles)
        return false;
    R(3) = wrap ? gap - 0x10000u : gap;
    R(2) = R(3);
    R(31) = MEM_R32(R(29) + 0x14);
    R(16) = MEM_R32(R(29) + 0x10);
    R(29) += 0x18;
    c->budget -= cycles;
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

/* The null-target branch still saves ra in its delay slot. */
bool nc_begin_actor_distance(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    c->budget -= 3;
    return true;
}

bool nc_call_actor_distance_root(CPUState *c) {
    if (c->budget <= 2)
        return false;
    R(31) = 0x8004ce60;
    R(4) = R(6) + R(3);
    c->budget -= 2;
    return true;
}

bool nc_finish_actor_distance(CPUState *c, bool missing_target) {
    const int cycles = missing_target ? 7 : 8;
    if (c->budget <= cycles)
        return false;
    if (missing_target)
        R(2) = 0;
    R(31) = MEM_R32(R(29) + 0x10);
    R(29) += 0x18;
    c->budget -= cycles;
    return true;
}

bool nc_save_point_update_frame(CPUState *c) {
    if (c->budget <= 9)
        return false;
    R(29) -= 0x48;
    MEM_W32(R(29) + 0x34, R(19));
    R(19) = R(4);
    MEM_W32(R(29) + 0x40, R(31));
    MEM_W32(R(29) + 0x3c, R(21));
    MEM_W32(R(29) + 0x38, R(20));
    MEM_W32(R(29) + 0x30, R(18));
    MEM_W32(R(29) + 0x2c, R(17));
    MEM_W32(R(29) + 0x28, R(16));
    c->budget -= 9;
    return true;
}

bool nc_select_point_update_row(CPUState *c) {
    if (c->budget <= 9)
        return false;
    const u16 state = MEM_R16(R(19) + 0x386);
    u32 row, comparison;
    int cycles;
    switch (state) {
    case 2:
        row = 3;
        comparison = 2;
        cycles = 11;
        break;
    case 3:
        row = 1;
        comparison = 1;
        cycles = 9;
        break;
    case 4:
        row = 2;
        comparison = 5;
        cycles = 13;
        break;
    case 5:
        row = 0;
        comparison = 5;
        cycles = 15;
        break;
    default:
        row = state < 2 ? 3 : 0;
        comparison = state < 2 ? 2 : 5;
        cycles = state < 2 ? 12 : 13;
        break;
    }
    if (c->budget <= cycles)
        return false;
    R(3) = state;
    R(2) = comparison;
    R(20) = row;
    if (state >= 2 && state <= 5)
        R(18) = row;
    c->budget -= cycles;
    return true;
}

/* For zero or one placement point, either flag sends the record straight
 * to the pose stores. Otherwise the caller checks the X/Z projection. */
bool nc_check_projection_bypass(CPUState *c) {
    if (c->budget <= 11)
        return false;
    const u32 settings = MEM_R32(R(28) + 0x244);
    R(3) = 0x00800020;
    R(2) = MEM_R32(settings + 4) & R(3);
    c->budget -= 11;
    return true;
}

/* The point stores reflected Z. The branch tests the signed low word of
 * the dot product; HI and LO retain the final Z multiplication. */
bool nc_project_record_point(CPUState *c) {
    if (c->budget <= 30)
        return false;
    const u32 point = R(19), record = R(16);
    const s32 dx = (s16)MEM_R16(point) - (s16)MEM_R16(record + 0x18);
    const u32 x_term = (u32)dx * MEM_R32(0x800d7748);
    const s32 dz = -(s32)(s16)MEM_R16(point + 4) - (s16)MEM_R16(record + 0x1c);
    R(4) = MEM_R32(0x800d7750);
    R(5) = x_term;
    R(2) = dz;
    nc_mult(c, R(2), R(4));
    R(3) = x_term + c->lo;
    c->budget -= 30;
    return true;
}

bool nc_check_record_projection_limit(CPUState *c) {
    if (c->budget <= 9)
        return false;
    const u32 limit = MEM_R32(0x800d7c14) << 9;
    R(2) = (s32)R(3) > (s32)limit;
    c->budget -= 9;
    return true;
}

bool nc_begin_placement_point_scan(CPUState *c) {
    if (c->budget <= 7)
        return false;
    const u16 count = MEM_R16(R(16) + 0x38);
    const int cycles = count < 2 ? 7 : 14;
    if (c->budget <= cycles)
        return false;
    R(6) = count;
    R(2) = count < 2;
    if (count >= 2) {
        R(5) = 0;
        R(7) = MEM_R32(0x8008f0c8);
        R(4) = R(16);
    }
    c->budget -= cycles;
    return true;
}

bool nc_test_placement_point_index(CPUState *c) {
    if (c->budget <= 15)
        return false;
    const u16 index = MEM_R16(R(4) + 0x3a);
    R(3) = R(7) + 36u * index;
    R(2) = MEM_R32(R(3)) & 0x10;
    c->budget -= 15;
    return true;
}

bool nc_advance_placement_point_index(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(5) += 1;
    R(2) = (s32)R(5) < (s32)R(6);
    R(4) += 2;
    c->budget -= 4;
    return true;
}

bool nc_finish_placement_point_scan(CPUState *c) {
    if (c->budget <= 6)
        return false;
    const u16 count = MEM_R16(R(16) + 0x38);
    const bool exhausted = R(5) == count;
    const int cycles = exhausted ? 8 : 6;
    if (c->budget <= cycles)
        return false;
    R(2) = count;
    if (exhausted)
        R(20) += 1;
    c->budget -= cycles;
    return true;
}

/* A zero point count bypasses the table and leaves v0 and v1 intact.
 * Otherwise, the first unsigned index selects a 36-byte point record. */
bool nc_check_placement_point(CPUState *c) {
    const int cycles = R(6) ? 21 : 2;
    if (c->budget <= cycles)
        return false;
    if (R(6)) {
        const u16 index = MEM_R16(R(16) + 0x3a);
        R(3) = MEM_R32(0x8008f0c8);
        R(2) = MEM_R32(R(3) + 36u * index) & 0x10;
    }
    c->budget -= cycles;
    return true;
}

bool nc_call_placement_node_traversal(CPUState *c) {
    if (c->budget <= 6)
        return false;
    const u32 record = R(16);
    const u32 nodes = MEM_R32(record + 8);
    const int cycles = nodes ? 9 : 6;
    if (c->budget <= cycles)
        return false;
    R(2) = nodes;
    R(4) = record;
    if (nodes) {
        R(5) = R(19) + 0x38;
        R(6) = R(19) + 0x78;
        R(31) = 0x80029380;
    }
    c->budget -= cycles;
    return true;
}

bool nc_call_placement_angle_update(CPUState *c) {
    if (c->budget <= 7)
        return false;
    const u32 record = R(16);
    const u32 enabled = MEM_R32(record + 0x2c) & 0x40000000;
    const int cycles = enabled ? 14 : 7;
    if (c->budget <= cycles)
        return false;
    R(3) = 0x40000000;
    R(2) = enabled;
    R(5) = record + 0x20;
    if (enabled) {
        R(6) = record + 0x28;
        R(17) = 0x20000000;
        R(7) = MEM_R32(record + 0x30) & R(17);
        R(31) = 0x800293b0;
    }
    c->budget -= cycles;
    return true;
}

bool nc_store_placement_angle_result(CPUState *c) {
    if (c->budget <= 8)
        return false;
    const u32 address = R(16) + 0x30;
    const u32 flags = MEM_R32(address);
    if (R(2)) {
        R(3) = 0xdfff0000;
        R(2) = flags | R(17);
    } else {
        R(3) = 0xdfffffff;
        R(2) = flags & R(3);
    }
    MEM_W32(address, R(2));
    c->budget -= 8;
    return true;
}

bool nc_call_placement_part_traversal(CPUState *c) {
    if (c->budget <= 6)
        return false;
    R(4) = MEM_R32(R(16) + 4);
    R(5) = R(19) + 0x38;
    R(6) = R(19) + 0x78;
    R(31) = 0x800293e4;
    c->budget -= 6;
    return true;
}

/* The pose stores (-X, Y, Z) as halfwords. Reload the pose pointer
 * between stores in the same order as the guest. */
bool nc_place_record_pose(CPUState *c) {
    const u32 heading_flag = R(18) & 2;
    const int cycles = heading_flag ? 35 : 33;
    if (c->budget <= cycles)
        return false;
    const u32 record = R(16);
    MEM_W32(record + 0x2c, MEM_R32(record + 0x2c) | 0x80000000u);
    const u16 x = MEM_R16(record + 0x18);
    u32 pose = MEM_R32(record + 4);
    MEM_W16(pose + 0x10, (u16)-x);
    pose = MEM_R32(record + 4);
    MEM_W16(pose + 0x12, MEM_R16(record + 0x1a));
    R(3) = MEM_R32(record + 4);
    MEM_W16(R(3) + 0x14, MEM_R16(record + 0x1c));
    R(2) = heading_flag;
    R(4) = record;
    if (heading_flag) {
        R(31) = 0x80029364;
        R(5) = R(19);
    }
    c->budget -= cycles;
    return true;
}

static void measure_screen_winding(CPUState *c) {
    gte_write_data(c, 12, R(4));
    gte_write_data(c, 14, R(6));
    gte_write_data(c, 13, R(5));
    gte_command(c, 0x1400006); /* NCLIP */
    R(2) = gte_read_data(c, 24);
}

static void finish_placement_winding(CPUState *c, unsigned first_saved, u32 return_pc) {
    for (unsigned i = 0; i < 4; ++i)
        MEM_W32(R(29) + 0x30 + 4 * i, R(first_saved + i));
    R(31) = return_pc;
    measure_screen_winding(c);
    c->budget -= 38;
}

/* The face cursor is three bytes into its record. Signed halfword indices
 * select eight-byte projected vertices, in the winding order used by NormalClip. */
bool nc_measure_placement_triangle(CPUState *c) {
    if (c->budget <= 38)
        return false;
    const u32 face = R(20), vertices = R(8);
    const u32 first = (u32)(s32)(s16)MEM_R16(face + 0xd) * 8;
    R(3) = (u32)(s32)(s16)MEM_R16(face + 0xf) * 8;
    R(19) = vertices + first;
    R(17) = vertices + R(3);
    R(18) = vertices + (u32)(s32)(s16)MEM_R16(face + 0xb) * 8;
    R(4) = MEM_R32(R(19));
    R(5) = MEM_R32(R(17));
    R(6) = MEM_R32(R(18));
    finish_placement_winding(c, 7, 0x80027644);
    return true;
}

bool nc_begin_textured_faces(CPUState *c, bool quad) {
    if (c->budget <= 27)
        return false;
    const u32 frame = R(29) - 0x68;
    R(29) = frame;
    if (quad)
        MEM_W32(frame + 0x58, R(22));
    R(quad ? 22 : 8) = R(5);
    MEM_W32(frame + 0x54, R(21));
    R(21) = MEM_R32(R(28) + (quad ? 0x380 : 0x37c));
    R(5) = MEM_R32(frame + 0x78);
    if (quad) {
        R(9) = 0;
        MEM_W32(frame + 0x60, R(30));
        R(30) = MEM_R32(R(28) + 0x2a0);
        MEM_W32(frame + 0x64, R(31));
        MEM_W32(frame + 0x5c, R(23));
    } else {
        MEM_W32(frame + 0x18, R(7));
        R(7) = 0;
        MEM_W32(frame + 0x5c, R(23));
        R(23) = MEM_R32(R(28) + 0x29c);
        MEM_W32(frame + 0x64, R(31));
        MEM_W32(frame + 0x60, R(30));
        MEM_W32(frame + 0x58, R(22));
    }
    for (int reg = 20; reg >= 16; --reg)
        MEM_W32(frame + 0x40 + 4 * (reg - 16), R(reg));
    MEM_W32(frame + 0x10, R(6));
    if (quad)
        MEM_W32(frame + 0x18, R(7));
    R(3) = MEM_R32(R(4) + (quad ? 0x20 : 0x18));
    c->budget -= 27;
    return true;
}

bool nc_read_textured_face_count(CPUState *c, bool quad) {
    if (c->budget <= 6)
        return false;
    R(4) = MEM_R8(R(4) + (quad ? 3 : 1));
    R(quad ? 23 : 22) = 0;
    MEM_W32(R(29) + 0x20, R(4));
    c->budget -= 6;
    return true;
}

bool nc_init_textured_face_cursors(CPUState *c, bool quad) {
    if (c->budget <= 5)
        return false;
    R(quad ? 8 : 30) = 0x00ffffff;
    R(quad ? 11 : 10) = 0xff000000;
    R(quad ? 17 : 20) = R(3) + 3;
    R(16) = R(21) + 0x16;
    c->budget -= 5;
    return true;
}

bool nc_apply_textured_face_flags(CPUState *c, bool quad) {
    const u32 flags = R(5) & 7;
    const int cycles = 8 + 2 * !!(flags & 1) + 2 * !!(flags & 2) + !!(flags & 4);
    if (c->budget <= cycles)
        return false;
    // The second flag overrides the first texture-page setting.
    R(quad ? 10 : 9) = (flags & 2) ? 0x40 : (flags & 1) ? 0x20 : 0;
    if (flags)
        R(quad ? 9 : 7) = 2;
    R(2) = flags & 4;
    c->budget -= cycles;
    return true;
}

bool nc_begin_untextured_triangles(CPUState *c) {
    if (c->budget <= 34)
        return false;
    const u32 frame = R(29) - 0x68;
    R(29) = frame;
    R(8) = R(5);
    R(9) = R(7);
    MEM_W32(frame + 0x60, R(30));
    R(30) = 0;
    MEM_W32(frame + 0x64, R(31));
    for (int reg = 23; reg >= 16; --reg)
        MEM_W32(frame + 0x40 + 4 * (reg - 16), R(reg));
    MEM_W32(frame + 0x10, R(6));
    R(23) = MEM_R32(R(4) + 0x14);
    R(21) = MEM_R32(R(28) + 0x354);
    const u32 flags = MEM_R32(frame + 0x78);
    R(22) = MEM_R32(R(28) + 0x3d0);
    R(4) = MEM_R8(R(4));
    R(2) = (flags & 7) ? 2 : 0;
    MEM_W32(frame + 0x18, R(4));
    c->budget -= 34;
    return true;
}

bool nc_begin_untextured_quads(CPUState *c) {
    if (c->budget <= 34)
        return false;
    const u32 frame = R(29) - 0x68;
    R(29) = frame;
    MEM_W32(frame + 0x58, R(22));
    R(22) = R(5);
    R(10) = R(7);
    R(8) = 0;
    MEM_W32(frame + 0x64, R(31));
    MEM_W32(frame + 0x60, R(30));
    MEM_W32(frame + 0x5c, R(23));
    for (int reg = 21; reg >= 16; --reg)
        MEM_W32(frame + 0x40 + 4 * (reg - 16), R(reg));
    MEM_W32(frame + 0x10, R(6));
    R(30) = MEM_R32(R(4) + 0x1c);
    R(21) = MEM_R32(R(28) + 0x358);
    const u32 flags = MEM_R32(frame + 0x78);
    R(23) = MEM_R32(R(28) + 0x3d8);
    R(4) = MEM_R8(R(4) + 2);
    R(2) = (flags & 7) ? 2 : 0;
    MEM_W32(frame + 0x18, R(4));
    c->budget -= 34;
    return true;
}

bool nc_select_untextured_faces(CPUState *c, bool quad) {
    const int cycles = R(4) ? 7 : 2;
    if (c->budget <= cycles)
        return false;
    MEM_W8(R(29) + 0x20, R(2));
    if (R(4)) {
        R(quad ? 9 : 7) = 0xffffff;
        R(quad ? 11 : 10) = 0xff000000;
        R(20) = quad ? R(30) + 12 : R(23) + 10;
        R(19) = R(21) + 7;
    }
    c->budget -= cycles;
    return true;
}

bool nc_measure_untextured_triangle(CPUState *c) {
    if (c->budget <= 38)
        return false;
    const u32 face = R(20), vertices = R(8);
    const s32 first = (s16)MEM_R16(face - 6);
    R(3) = (u32)(s32)(s16)MEM_R16(face - 4) * 8;
    const s32 third = (s16)MEM_R16(face - 2);
    R(18) = vertices + 8 * first;
    R(16) = vertices + R(3);
    R(17) = vertices + 8 * third;
    R(4) = MEM_R32(R(18));
    R(5) = MEM_R32(R(16));
    R(6) = MEM_R32(R(17));
    finish_placement_winding(c, 7, 0x80027824);
    return true;
}

bool nc_measure_placement_quad(CPUState *c, bool textured) {
    if (c->budget <= 38)
        return false;
    // Both quad paths use vertices 1, 2 and 3 in packet order for NormalClip.
    const u32 indices = textured ? R(17) + 0xb : R(20) - 8;
    const unsigned first_reg = textured ? 18 : 16;
    const u32 vertices = R(22);
    const s32 first = (s16)MEM_R16(indices);
    R(3) = (u32)(s32)(s16)MEM_R16(indices + (textured ? 4 : 2)) * 8;
    const s32 third = (s16)MEM_R16(indices + (textured ? 8 : 6));
    R(first_reg) = vertices + 8 * first;
    R(first_reg + 1) = vertices + R(3);
    R(first_reg + 2) = vertices + 8 * third;
    R(4) = MEM_R32(R(first_reg));
    R(5) = MEM_R32(R(first_reg + 1));
    R(6) = MEM_R32(R(first_reg + 2));
    finish_placement_winding(c, 8, textured ? 0x80027a3c : 0x80027c50);
    return true;
}

bool nc_check_placement_face(CPUState *c, unsigned first_reg, u32 count, u32 limit, bool *accept) {
    const bool front_facing = (s32)R(2) > 0;
    const int cycles = front_facing ? 16 : 14;
    if (c->budget <= cycles)
        return false;
    for (unsigned i = 0; i < 4; ++i)
        R(first_reg + i) = MEM_R32(R(29) + 0x30 + 4 * i);
    R(2) = limit;
    *accept = front_facing && count != limit;
    c->budget -= cycles;
    return true;
}

bool nc_write_textured_triangle(CPUState *c) {
    if (c->budget <= 42)
        return false;
    const u32 first = R(19), second = R(17), third = R(18);
    const u32 packet = R(16), face = R(20);
    const u32 first_xy = MEM_R32(first);
    R(5) = (u32)(s32)(s16)MEM_R16(third + 4);
    const s32 second_z = (s16)MEM_R16(second + 4);
    R(6) = (u32)(s32)(s16)MEM_R16(first + 4);
    // Four times the weighted depth, masked to an aligned ordering-table offset.
    R(4) = (R(6) + (u32)second_z + 2 * R(5)) & 0x1ffc;
    MEM_W32(packet - 14, first_xy);
    const u32 second_xy = MEM_R32(second);
    ++R(23);
    MEM_W32(packet - 6, second_xy);
    const u32 third_xy = MEM_R32(third);
    R(11) = MEM_R32(R(29) + 0x18);
    MEM_W32(packet - 18, R(11));
    MEM_W32(packet + 2, third_xy);
    MEM_W32(packet - 10, MEM_R32(face + 1));
    MEM_W32(packet - 2, MEM_R32(face + 5));
    R(2) = MEM_R16(face + 9);
    MEM_W16(packet + 6, R(2));
    c->budget -= 42;
    return true;
}

bool nc_return_placement_faces(CPUState *c, u32 packet_offset, u32 count_offset, u32 face_count) {
    if (c->budget <= 35)
        return false;
    const u32 frame = R(29);
    MEM_W32(R(28) + packet_offset, R(21));
    MEM_W32(R(28) + count_offset, face_count);
    R(31) = MEM_R32(frame + 0x64);
    R(30) = MEM_R32(frame + 0x60);
    for (int reg = 23; reg >= 16; --reg)
        R(reg) = MEM_R32(frame + 0x40 + 4 * (reg - 16));
    R(29) = frame + 0x68;
    c->budget -= 35;
    return true;
}

bool nc_advance_placement_face(CPUState *c, bool textured, bool quad) {
    const int cycles = textured ? 8 : 9;
    if (c->budget <= cycles)
        return false;
    const unsigned count_reg = quad ? 12 : 11;
    if (textured) {
        const unsigned index_reg = quad ? 23 : 22;
        ++R(index_reg);
        R(count_reg) = MEM_R32(R(29) + 0x20);
        R(2) = (s32)R(index_reg) < (s32)R(count_reg);
        R(quad ? 17 : 20) += quad ? 28 : 24;
    } else {
        const unsigned index_reg = quad ? 8 : 30;
        const u32 stride = quad ? 16 : 12;
        ++R(index_reg);
        R(20) += stride;
        R(count_reg) = MEM_R32(R(29) + 0x18);
        R(2) = (s32)R(index_reg) < (s32)R(count_reg);
        R(quad ? 30 : 23) += stride;
    }
    c->budget -= cycles;
    return true;
}

bool nc_set_untextured_packet_color(CPUState *c, bool quad, bool indexed) {
    const int cycles = indexed ? 11 : 5;
    if (c->budget <= cycles)
        return false;
    const u32 color = indexed ? R(quad ? 10 : 9) + 4u * MEM_R16(R(20)) : R(quad ? 30 : 23);
    R(2) = MEM_R32(color);
    MEM_W32(R(19) - 3, R(2));
    c->budget -= cycles;
    return true;
}

bool nc_finish_untextured_packet(CPUState *c, bool quad) {
    if (c->budget <= 11)
        return false;
    ++R(quad ? 23 : 22);
    R(2) = MEM_R8(R(19));
    const unsigned flag_reg = quad ? 12 : 11;
    R(flag_reg) = MEM_R8(R(29) + 0x20);
    R(4) <<= 2;
    R(2) |= R(flag_reg);
    MEM_W8(R(19), R(2));
    R(19) += quad ? 24 : 20;
    c->budget -= 11;
    return true;
}

bool nc_finish_textured_packet(CPUState *c, bool quad) {
    if (c->budget <= 11)
        return false;
    const u32 packet = R(16);
    R(2) = MEM_R16(packet) | R(quad ? 10 : 9);
    R(3) = MEM_R8(R(quad ? 17 : 20)) | R(quad ? 9 : 7);
    MEM_W16(packet, R(2));
    MEM_W8(packet - 15, R(3));
    R(16) += quad ? 40 : 32;
    c->budget -= 11;
    return true;
}

static u32 insert_packet_in_bucket(u32 packet, u32 bucket, u32 tag_mask, u32 address_mask) {
    const u32 tag = MEM_R32(packet);
    const u32 old_head = MEM_R32(bucket);
    MEM_W32(packet, (tag & tag_mask) | (old_head & address_mask));
    // Read again after the packet store, including when both addresses coincide.
    const u32 new_head = (MEM_R32(bucket) & tag_mask) | (packet & address_mask);
    MEM_W32(bucket, new_head);
    return new_head;
}

bool nc_link_placement_packet(CPUState *c, unsigned table_reg, u32 tag_mask, u32 address_mask,
                              u32 packet_size) {
    if (c->budget <= 22)
        return false;
    R(table_reg) = MEM_R32(R(29) + 0x10);
    const u32 packet = R(21);
    R(4) += R(table_reg);
    R(2) = insert_packet_in_bucket(packet, R(4), tag_mask, address_mask);
    R(3) = packet & address_mask;
    R(21) += packet_size;
    c->budget -= 22;
    return true;
}

bool nc_begin_indexed_faces(CPUState *c) {
    if (c->budget <= 22)
        return false;
    const u32 frame = R(29) - 0x88;
    R(29) = frame;
    MEM_W32(frame + 0x6c, R(19));
    R(19) = R(4);
    MEM_W32(frame + 0x84, R(31));
    MEM_W32(frame + 0x80, R(30));
    for (int reg = 23; reg >= 16; --reg) {
        if (reg != 19)
            MEM_W32(frame + 0x60 + 4 * (reg - 16), R(reg));
    }
    MEM_W32(frame + 0x30, R(6));
    R(9) = MEM_R32(R(19) + 0x2e4);
    R(30) = MEM_R32(R(5) + 0x38);
    MEM_W32(frame + 0x38, R(9)); /* descriptor branch delay slot */
    c->budget -= 22;
    return true;
}

bool nc_set_indexed_face_flags(CPUState *c) {
    if (c->budget <= 19)
        return false;
    const u32 actor = R(19), flags = R(29) + 0x48;
    R(2) = MEM_R32(actor + 0x2c8) & 1;
    MEM_W8(flags, 0);
    c->budget -= 7;
    if (R(2)) {
        R(2) = (u32)(s32)(s16)MEM_R16(actor + 0x362);
        c->budget -= 6;
        if (R(2)) {
            R(2) = MEM_R16(actor + 0x2ce) & 2;
            MEM_W8(flags, (u8)R(2));
            c->budget -= 6;
        }
    }
    return true;
}

bool nc_begin_indexed_face_list(CPUState *c, bool textured) {
    if (c->budget <= (textured ? 22 : 28))
        return false;
    const u32 descriptor = R(30);
    R(3) = MEM_R32(descriptor + (textured ? 0x18 : 0x14));
    c->budget -= 6;
    if (!R(3))
        return true;
    if (!textured) {
        R(9) = MEM_R32(R(19) + 0x2b4);
        MEM_W32(R(29) + 0x40, R(9)); /* color-table branch delay slot */
        c->budget -= 6;
        if (!R(9))
            return true;
    }
    R(18) = MEM_R32(R(28) + (textured ? 0x37c : 0x354));
    R(2) = MEM_R8(descriptor + (textured ? 1 : 0));
    R(21) = MEM_R32(R(28) + (textured ? 0x29c : 0x3d0));
    R(20) = 0;
    c->budget -= 11;
    if (R(2)) {
        R(23) = R(29) + 0x28;
        R(22) = 0x00ffffff;
        R(17) = R(3) + 3;
        R(16) = R(18) + 7;
        c->budget -= 5;
    }
    return true;
}

bool nc_load_indexed_packet_limit(CPUState *c, bool textured) {
    if (c->budget <= 3)
        return false;
    R(2) = textured ? 0x400 : 0x80;
    c->budget -= 3;
    return true;
}

bool nc_return_indexed_faces(CPUState *c) {
    if (c->budget <= 33)
        return false;
    const u32 frame = R(29);
    R(31) = MEM_R32(frame + 0x84);
    R(30) = MEM_R32(frame + 0x80);
    for (int reg = 23; reg >= 16; --reg)
        R(reg) = MEM_R32(frame + 0x60 + 4 * (reg - 16));
    R(29) = frame + 0x88;
    c->budget -= 33;
    return true;
}

bool nc_advance_indexed_face(CPUState *c, bool textured) {
    if (c->budget <= 8)
        return false;
    ++R(20);
    const u32 count = MEM_R8(R(30) + (textured ? 1 : 0));
    R(2) = (s32)R(20) < (s32)count;
    R(17) += textured ? 24 : 12; /* branch delay slot */
    c->budget -= 8;
    return true;
}

/* The empty-list path also enters here with the original packet cursors. */
bool nc_save_indexed_face_cursors(CPUState *c, bool textured) {
    if (c->budget <= 2)
        return false;
    MEM_W32(R(28) + (textured ? 0x37c : 0x354), R(18));
    MEM_W32(R(28) + (textured ? 0x29c : 0x3d0), R(21));
    c->budget -= 2;
    return true;
}

bool nc_link_indexed_face_packet(CPUState *c, bool textured) {
    if (c->budget <= 32)
        return false;
    const u32 stride = textured ? 32 : 20;
    const unsigned bucket_reg = textured ? 4 : 3;
    const unsigned link_reg = textured ? 3 : 4;
    const u8 command = MEM_R8(R(17));
    const u8 flags = MEM_R8(R(29) + 0x48);
    R(5) = 0xff000000;
    MEM_W8(R(16), command | flags);
    R(16) += stride;
    R(9) = MEM_R32(R(29) + 0x30);
    R(bucket_reg) += R(9);
    const u32 packet = R(18);
    R(2) = insert_packet_in_bucket(packet, R(bucket_reg), R(5), R(22));
    R(link_reg) = packet & R(22);
    R(18) += stride;
    c->budget -= 32;
    return true;
}

bool nc_write_textured_quad(CPUState *c) {
    if (c->budget <= 65)
        return false;
    const u32 packet = R(16), face = R(17), vertices = R(22);
    const u32 first = R(18), second = R(19), third = R(20);
    const s32 fourth_index = (s16)MEM_R16(face + 0x11);
    R(3) = MEM_R32(first);
    R(5) = (u32)(s32)(s16)MEM_R16(second + 4);
    R(6) = (u32)(s32)(s16)MEM_R16(first + 4);
    R(7) = (u32)(s32)(s16)MEM_R16(third + 4);
    const s32 fourth_z = (s16)MEM_R16(vertices + 8 * fourth_index + 4);
    R(4) = ((u32)fourth_z + R(5) + R(6) + R(7)) & 0x1ffc;
    MEM_W32(packet - 14, R(3));
    MEM_W32(packet - 6, MEM_R32(second));
    MEM_W32(packet + 2, MEM_R32(third));
    // Packet writes can change the fourth index after its depth was read.
    const s32 packet_index = (s16)MEM_R16(face + 0x11);
    R(12) = MEM_R32(R(29) + 0x18);
    const u32 fourth_xy = MEM_R32(vertices + 8 * packet_index);
    ++R(30);
    MEM_W32(packet - 18, R(12));
    MEM_W32(packet + 10, fourth_xy);
    MEM_W32(packet - 10, MEM_R32(face + 1));
    MEM_W32(packet - 2, MEM_R32(face + 5));
    MEM_W16(packet + 6, MEM_R16(face + 9));
    R(2) = MEM_R16(face + 13);
    MEM_W16(packet + 14, R(2));
    c->budget -= 65;
    return true;
}

bool nc_write_projected_triangle(CPUState *c) {
    if (c->budget <= 27)
        return false;
    const u32 first = R(18), second = R(16), third = R(17), packet = R(19);
    const u32 first_xy = MEM_R32(first);
    const s32 third_z = (s16)MEM_R16(third + 4);
    const s32 second_z = (s16)MEM_R16(second + 4);
    const s32 first_z = (s16)MEM_R16(first + 4);
    R(5) = (u32)first_z;
    R(3) = (u32)((first_z + second_z + 2 * third_z) >> 2);
    R(4) = R(3) & 0x7ff;
    MEM_W32(packet + 1, first_xy);
    MEM_W32(packet + 5, MEM_R32(second));
    R(2) = MEM_R32(third);
    MEM_W32(packet + 9, R(2));
    c->budget -= 27;
    return true;
}

bool nc_write_projected_quad(CPUState *c) {
    if (c->budget <= 44)
        return false;
    const u32 first = R(16), second = R(17), third = R(18), packet = R(19);
    const u32 index_addr = R(20) - 4, vertices = R(22);
    const s32 fourth_index = (s16)MEM_R16(index_addr);
    const u32 first_xy = MEM_R32(first);
    R(5) = (u32)(s32)(s16)MEM_R16(second + 4);
    R(6) = (u32)(s32)(s16)MEM_R16(first + 4);
    R(7) = (u32)(s32)(s16)MEM_R16(third + 4);
    const s32 fourth_z = (s16)MEM_R16(vertices + 8 * fourth_index + 4);
    R(3) = (u32)((fourth_z + (s32)R(5) + (s32)R(6) + (s32)R(7)) >> 2);
    MEM_W32(packet + 1, first_xy);
    MEM_W32(packet + 5, MEM_R32(second));
    MEM_W32(packet + 9, MEM_R32(third));
    // The preceding packet writes can overlap the face's fourth index.
    const s32 packet_index = (s16)MEM_R16(index_addr);
    R(2) = MEM_R32(vertices + 8 * packet_index);
    R(4) = R(3) & 0x7ff;
    MEM_W32(packet + 13, R(2));
    c->budget -= 44;
    return true;
}

/* Both traversals start with the root pose and an empty pending-pose stack.
 * Their delay slots copy the placement color even when projection is skipped. */
bool nc_begin_pose_traversal(CPUState *c, bool small_frame) {
    const int cycles = small_frame ? 15 : 17;
    if (c->budget <= cycles)
        return false;
    const u32 frame = R(29) - (small_frame ? 0x180 : 0x300);
    const u32 saved = frame + (small_frame ? 0x168 : 0x2e0);
    R(29) = frame;
    if (!small_frame) {
        MEM_W32(saved + 20, R(21));
        R(21) = R(4);
    }
    MEM_W32(saved + 8, R(18));
    R(18) = R(5);
    MEM_W32(saved + 16, R(20));
    R(20) = R(6);
    MEM_W32(saved + (small_frame ? 20 : 24), R(31));
    MEM_W32(saved + 12, R(19));
    MEM_W32(saved + 4, R(17));
    MEM_W32(saved, R(16));
    const u32 color = MEM_R32(R(4) + 0x28);
    R(small_frame ? 4 : 2) = color;
    R(19) = 0;
    MEM_W32(frame + (small_frame ? 0x164 : 0x2dc), color);
    c->budget -= cycles;
    return true;
}

static void load_placement_light_matrix(CPUState *c) {
    const u32 light = 0x800d76a8;
    for (unsigned i = 0; i < 5; ++i)
        gte_write_ctrl(c, 8 + i, MEM_R32(light + 4 * i));
}

bool nc_begin_placement_lighting(CPUState *c) {
    if (c->budget <= 59)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0xc, R(6));
    MEM_W32(R(7), 0);
    load_placement_light_matrix(c);
    R(15) = 0x800d7668;
    load_part_transform(c, R(15));
    c->budget -= 59;
    return true;
}

/* A zero sum bypasses the color adjustment. The signed upper bound
 * leaves negative offsets intact, including after a wrapping addition. */
bool nc_placement_lighting_offset(CPUState *c) {
    if (c->budget <= 15)
        return false;
    const u32 settings = MEM_R32(R(28) + 0x244);
    const u32 local = (u32)(s32)(s16)MEM_R16(settings + 0xc);
    const u32 sum = MEM_R32(0x800d7c08) + local;
    const bool clamp = (s32)sum >= 256;
    const int cycles = !sum ? 15 : clamp ? 18 : 17;
    if (c->budget <= cycles)
        return false;
    R(2) = !clamp;
    R(3) = clamp ? 255 : sum;
    c->budget -= cycles;
    return true;
}

/* The original byte stores cap each signed sum at 255. Negative sums
 * keep their low byte, and the fourth byte passes through untouched. */
bool nc_adjust_placement_color(CPUState *c, bool linked_pose) {
    int cycles = linked_pose ? 33 : 37;
    if (c->budget <= cycles)
        return false;
    const u32 frame = R(29) + (linked_pose ? 0x158 : 0);
    const u32 color = linked_pose ? R(4) : MEM_R32(frame + 0xc);
    u32 adjusted = color & 0xff000000;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const u32 sum = ((color >> shift) & 0xff) + R(3);
        const bool clamp = (s32)sum >= 256;
        adjusted |= (clamp ? 255u : sum & 0xff) << shift;
        cycles += clamp;
    }
    if (c->budget <= cycles)
        return false;
    R(linked_pose ? 4 : 6) = (color >> 16) & 0xff;
    R(2) = adjusted;
    MEM_W32(frame, adjusted);
    MEM_W32(frame + 0xc, adjusted);
    c->budget -= cycles;
    return true;
}

/* Store the final lighting color even when the actor has no color records.
 * The loop uses the channel masks in their original byte positions. */
bool nc_begin_placement_color_records(CPUState *c, bool *has_records) {
    if (c->budget <= 9)
        return false;
    const u32 actor = R(4);
    const u32 color = MEM_R32(R(29) + 0xc);
    const u32 count = MEM_R8(actor + 0x38d);
    const int cycles = count ? 21 : 9;
    if (c->budget <= cycles)
        return false;
    R(3) = color;
    R(5) = count;
    MEM_W32(actor + 0x2e4, color);
    if (count) {
        const u32 records = MEM_R32(actor + 0x2b4);
        R(6) = records;
        R(8) = 0;
        R(12) = color & 0xff;
        R(11) = color & 0xff00;
        R(2) = 0xff0000;
        R(10) = color & R(2);
        R(9) = records + 4;
        R(5) = records + 3;
        R(3) = records + 7;
    }
    *has_records = count != 0;
    c->budget -= cycles;
    return true;
}

/* Each eight-byte record holds the source color followed by its lit copy.
 * The three channel masks still occupy their packed RGB bit positions. */
bool nc_light_placement_color_record(CPUState *c) {
    if (c->budget <= 39)
        return false;
    const u32 red = MEM_R8(R(6)) * R(12);
    MEM_W8(R(9), (u8)(red >> 7));
    const u32 green = MEM_R8(R(5) - 2) * R(11);
    MEM_W8(R(3) - 2, (u8)(green >> 15));
    nc_mult(c, MEM_R8(R(5) - 1), R(10));
    R(6) += 8;
    R(8)++;
    R(9) += 8;
    R(15) = c->lo;
    MEM_W8(R(3) - 1, (u8)(R(15) >> 23));
    const u8 flags = MEM_R8(R(5));
    R(5) += 8;
    MEM_W8(R(3), flags);
    R(2) = (s32)R(8) < (s32)MEM_R8(R(4) + 0x38d);
    R(3) += 8;
    c->budget -= 39;
    return true;
}

bool nc_return_placement_lighting(CPUState *c, bool has_records) {
    const int cycles = has_records ? 11 : 6;
    if (c->budget <= cycles)
        return false;
    if (has_records)
        MEM_W32(R(7), MEM_R32(R(4) + 0x2b4));
    R(2) = MEM_R32(R(29) + 0xc);
    R(29) += 0x18;
    c->budget -= cycles;
    return true;
}

/* The traversals install the global light matrix, then the rotation and
 * translation stored 0x40 bytes before it. The three SDK calls take
 * 22, 22, and 14 cycles in addition to the nine caller instructions. */
bool nc_install_placement_matrices(CPUState *c, u32 continuation) {
    if (c->budget <= 67)
        return false;
    load_placement_light_matrix(c);
    R(16) = 0x800d7668;
    install_node_matrix(c, R(16), continuation);
    c->budget -= 67;
    return true;
}

/* Project the signed pose coordinates and keep the latest GTE depth (SZ3).
 * The branch compares depth and threshold before the delay-slot subtraction. */
bool nc_project_placement_depth(CPUState *c, u32 pose, u32 depth_offset, unsigned depth_reg) {
    if (c->budget <= 23)
        return false;
    const u32 point = pose + 0x10;
    gte_write_data(c, 0, MEM_R32(point));
    gte_write_data(c, 1, MEM_R32(point + 4));
    gte_command(c, 0x0180001); /* RTPS */
    const u32 depth_slot = R(29) + depth_offset;
    MEM_W32(depth_slot, gte_read_data(c, 19));
    R(depth_reg) = MEM_R32(depth_slot);
    R(3) = MEM_R32(0x8008f108);
    R(2) = R(depth_reg) - R(3);
    c->budget -= 23;
    return true;
}

/* Beyond the depth threshold, four times the excess depth supplies IR0
 * for the GTE interpolation of the placement color toward the far color. */
bool nc_apply_placement_depth_cue(CPUState *c, u32 depth_offset, unsigned factor_reg) {
    if (c->budget <= 15)
        return false;
    const u32 factor_slot = R(29) + depth_offset;
    const u32 color_slot = factor_slot + 4;
    MEM_W32(factor_slot, R(2) << 2);
    R(2) = color_slot;
    gte_write_data(c, 6, MEM_R32(color_slot));
    R(factor_reg) = MEM_R32(factor_slot);
    gte_write_data(c, 8, R(factor_reg));
    gte_command(c, 0x0780010); /* DPCS */
    MEM_W32(color_slot, gte_read_data(c, 22));
    c->budget -= 15;
    return true;
}

/* On the loop back-edge the branch delay slot has already decremented the
 * count. The alternate entry reloads that index without decrementing again. */
static bool select_pose_from_stack(CPUState *c, bool index_ready, unsigned count_reg,
                                   unsigned output_reg, u32 output_offset) {
    const bool reload = index_ready || R(count_reg) != 0;
    const int cycles = index_ready ? 6 : reload ? 9 : 2;
    if (c->budget <= cycles)
        return false;
    if (reload) {
        if (!index_ready)
            R(count_reg)--;
        R(2) = R(29) + (R(count_reg) << 2);
        R(18) = MEM_R32(R(2) + 0x18);
    }
    R(output_reg) = R(29) + output_offset;
    c->budget -= cycles;
    return true;
}

bool nc_select_placement_pose(CPUState *c, bool index_ready, u32 output_offset) {
    return select_pose_from_stack(c, index_ready, 19, 17, output_offset);
}

/* The traversal keeps its actor, pose and projection argument in s5, s2
 * and fp while it walks the pose links. */
bool nc_begin_projection_traversal(CPUState *c) {
    if (c->budget <= 23)
        return false;
    const u32 frame = R(29) - 0x320;
    R(29) = frame;
    for (int reg = 16; reg <= 23; ++reg)
        MEM_W32(frame + 0x2f8 + 4 * (reg - 16), R(reg));
    MEM_W32(frame + 0x318, R(30));
    MEM_W32(frame + 0x31c, R(31));
    R(21) = R(4);
    R(18) = R(5);
    R(30) = R(6);
    MEM_W32(frame + 0x2e8, 0);
    R(2) = MEM_R32(R(21) + 0x2c8) & 1;
    R(22) = 0;
    MEM_W32(frame + 0x2f0, 0);
    MEM_W32(frame + 0x2e0, 0);
    c->budget -= 23;
    return true;
}

/* A nonzero actor halfword enables the second test. Its signed half
 * remains in v0 from the branch delay slot even when the flag is clear. */
bool nc_check_projection_effect(CPUState *c, bool *active) {
    if (c->budget <= 7)
        return false;
    const u32 actor = R(21);
    const u32 value = MEM_R16(actor + 0x362);
    const int cycles = value ? 14 : 7;
    if (c->budget <= cycles)
        return false;
    R(4) = value << 16;
    R(3) = 0x20000;
    *active = value && (MEM_R32(actor + 0x2cc) & 0x20000);
    R(2) = (u32)((s32)R(4) >> 17);
    c->budget -= cycles;
    return true;
}

/* Move the shared intensity halfway toward black or white. The MIPS
 * subtraction wraps before the signed shift, which rounds down. */
bool nc_apply_projection_intensity(CPUState *c) {
    if (c->budget <= 22)
        return false;
    const u32 target = (R(2) & 1) ? 0 : 255;
    const u32 slot = R(29) + 0x2e8;
    R(4) = R(21);
    R(7) = 0x800ce9cc;
    MEM_W32(slot, target);
    const u32 previous = MEM_R32(R(7));
    R(3) = previous + (u32)((s32)(target - previous) >> 1);
    R(5) = 1;
    R(6) = R(3) << 8;
    R(2) = (R(3) << 16) + R(6) + R(3);
    MEM_W32(slot, R(2));
    R(31) = 0x800280d0;
    MEM_W32(R(7), R(3));
    c->budget -= 22;
    return true;
}

bool nc_disable_projection_effect(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(4) = R(21);
    R(31) = 0x800280e4;
    R(5) = 0;
    c->budget -= 3;
    return true;
}

/* The active effect jumps over the flag clear. A traversal that bypassed
 * the effect starts at the actor argument, just before the shared call. */
bool nc_call_projection_lighting(CPUState *c, u32 entry) {
    int cycles = 10;
    switch (entry) {
    case 0x800280d0:
        cycles += 2;
        break;
    case 0x800280e4:
        cycles += 8;
        break;
    case 0x800280f8:
        cycles += 1;
        break;
    }
    if (c->budget <= cycles)
        return false;
    const u32 actor = R(21);
    if (entry == 0x800280e4) {
        R(3) = 0xfffdffff;
        R(2) = MEM_R32(actor + 0x2cc) & R(3);
        MEM_W32(actor + 0x2cc, R(2));
    }
    if (entry != 0x800280fc)
        R(4) = actor;
    R(5) = R(18);
    R(6) = MEM_R32(actor + 0x2e0);
    R(8) = MEM_R32(R(29) + 0x2e8);
    R(7) = R(29) + 0x2e0;
    R(31) = 0x80028114;
    R(6) += R(8);
    c->budget -= cycles;
    return true;
}

/* A non-root starting pose selects the traversal mask from the actor's flag table. */
bool nc_set_projection_pose_mask(CPUState *c) {
    if (c->budget <= 6)
        return false;
    const u32 root = MEM_R32(R(21) + 0x20);
    const int cycles = root == R(18) ? 6 : 18;
    if (c->budget <= cycles)
        return false;
    R(3) = root;
    MEM_W32(R(29) + 0x2e8, R(2));
    if (root != R(18)) {
        const u32 index = MEM_R8(R(18) + 0x4c);
        R(2) = MEM_R16(R(21) + 0x10a + 2 * index) & 0x1f00;
        MEM_W32(R(29) + 0x2f0, R(2));
    }
    c->budget -= cycles;
    return true;
}

bool nc_load_projection_pose_descriptor(CPUState *c) {
    if (c->budget <= 8)
        return false;
    R(2) = MEM_R32(R(21) + 0x108);
    R(19) = MEM_R32(R(18) + 0x38);
    c->budget -= 8;
    return true;
}

bool nc_load_projection_pose_flags(CPUState *c) {
    if (c->budget <= 7)
        return false;
    const u32 index = MEM_R8(R(18) + 0x4c);
    const int cycles = index ? 12 : 7;
    if (c->budget <= cycles)
        return false;
    R(2) = index << 1;
    if (index) {
        R(2) += R(21);
        R(17) = (u32)(s32)(s16)MEM_R16(R(2) + 0x10a);
    } else {
        R(17) = 0x81;
    }
    c->budget -= cycles;
    return true;
}

/* With no traversal mask, only ungrouped poses pass. Otherwise at least
 * one selected group must match before the per-pose flags are tested. */
bool nc_filter_projection_pose(CPUState *c, bool *project) {
    if (c->budget <= 8)
        return false;
    const u32 flags = R(17);
    const u32 mask = MEM_R32(R(29) + 0x2f0);
    const bool group_matches = mask ? (flags & mask) != 0 : (flags & 0x1f00) == 0;
    const bool disabled = (flags & 0x20) != 0;
    int cycles = mask ? 8 : group_matches ? 9 : 11;
    if (group_matches)
        cycles += disabled ? 2 : 5;
    if (c->budget <= cycles)
        return false;

    R(8) = mask;
    R(2) = flags & 0x20;
    if (group_matches) {
        R(3) = flags & 0x81;
        if (!disabled)
            R(2) = flags & 0x10;
    }
    *project = group_matches && !disabled && R(3) == 0x81;
    c->budget -= cycles;
    return true;
}

bool nc_select_projection_pose(CPUState *c, bool index_ready) {
    return select_pose_from_stack(c, index_ready, 22, 23, 0x58);
}

bool nc_set_projection_pose_cursor(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(3) = R(29) + 0x18;
    R(2) = R(22) << 2;
    R(20) = R(3) + R(2);
    c->budget -= 3;
    return true;
}

bool nc_save_linked_placement_pose(CPUState *c) {
    if (c->budget <= 6)
        return false;
    const u32 linked_pose = MEM_R32(R(18) + 0xc);
    const int cycles = linked_pose ? 10 : 6;
    if (c->budget <= cycles)
        return false;
    R(4) = linked_pose;
    R(2) = R(19) << 2;
    if (linked_pose) {
        R(3) = R(29) + 0x18;
        R(2) += R(3);
        MEM_W32(R(2), linked_pose);
        R(19)++;
    }
    c->budget -= cycles;
    return true;
}

/* The projection traversal keeps a cursor into its stack of deferred poses. */
bool nc_push_projection_pose(CPUState *c) {
    if (c->budget <= 6)
        return false;
    const u32 linked = MEM_R32(R(18) + 0xc);
    const int cycles = linked ? 9 : 6;
    if (c->budget <= cycles)
        return false;
    R(2) = linked;
    if (linked) {
        MEM_W32(R(20), linked);
        R(20) += 4;
        ++R(22);
    }
    c->budget -= cycles;
    return true;
}

/* Follow the direct link first. When it is null, the second branch uses
 * the old pending count and decrements it in the delay slot on both exits. */
static bool advance_pose_link(CPUState *c, unsigned pending_reg) {
    if (c->budget <= 6)
        return false;
    const u32 next_pose = MEM_R32(R(18) + 4);
    const int cycles = next_pose ? 6 : 8;
    if (c->budget <= cycles)
        return false;
    R(18) = next_pose;
    if (!next_pose)
        R(pending_reg)--;
    c->budget -= cycles;
    return true;
}

bool nc_advance_placement_pose(CPUState *c) {
    return advance_pose_link(c, 19);
}

bool nc_advance_projection_pose(CPUState *c) {
    return advance_pose_link(c, 22);
}

/* Grouped poses always use mode zero. For ungrouped poses the actor's
 * mode-four flag takes precedence over the pose and actor defaults. */
bool nc_select_projection_packet_mode(CPUState *c) {
    if (c->budget <= 18)
        return false;
    const u32 actor = R(21);
    const u32 flags = MEM_R32(actor + 0x2cc);
    const u32 group = R(17) & 0x1f00;
    u32 mode = (flags >> 29) & 1;
    int cycles = 18;
    if (R(17) & 0x8000) {
        cycles += 7;
        if (!(MEM_R32(actor + 0x2c8) & 0x40000000)) {
            mode = 2;
            ++cycles;
        }
    }
    if (flags & 0x10000) {
        mode = 4;
        ++cycles;
    }
    if (group) {
        mode = 0;
        ++cycles;
    }
    if (c->budget <= cycles)
        return false;
    R(2) = group;
    R(3) = 0x10000;
    R(4) = R(19);
    R(16) = mode;
    c->budget -= cycles;
    return true;
}

/* The triangle call inherits a0 from mode selection. The quad call reloads
 * the descriptor after the triangle routine has used the argument registers. */
bool nc_call_projection_textured_faces(CPUState *c, bool quad) {
    const int cycles = quad ? 8 : 7;
    if (c->budget <= cycles)
        return false;
    if (quad)
        R(4) = R(19);
    R(5) = R(23);
    R(7) = MEM_R32(R(29) + 0x2e8);
    R(6) = R(30);
    R(31) = quad ? 0x80028298 : 0x80028280;
    MEM_W32(R(29) + 0x10, R(16));
    c->budget -= cycles;
    return true;
}

/* Only packet modes zero and four emit the untextured faces. The first
 * branch leaves four in v0 even when mode zero bypasses the second test. */
bool nc_check_untextured_projection_mode(CPUState *c) {
    const int cycles = R(16) ? 4 : 2;
    if (c->budget <= cycles)
        return false;
    R(2) = 4;
    c->budget -= cycles;
    return true;
}

bool nc_call_projection_untextured_faces(CPUState *c, bool quad) {
    if (c->budget <= 8)
        return false;
    R(4) = R(19);
    R(5) = R(23);
    MEM_W32(R(29) + 0x10, R(16));
    R(7) = MEM_R32(R(29) + 0x2e0);
    R(31) = quad ? 0x800282d8 : 0x800282c0;
    R(6) = R(30);
    c->budget -= 8;
    return true;
}

bool nc_return_projection_traversal(CPUState *c) {
    if (c->budget <= 33)
        return false;
    const u32 frame = R(29);
    R(31) = MEM_R32(frame + 0x31c);
    R(30) = MEM_R32(frame + 0x318);
    for (int reg = 23; reg >= 16; --reg)
        R(reg) = MEM_R32(frame + 0x2f8 + 4 * (reg - 16));
    R(29) = frame + 0x320;
    c->budget -= 33;
    return true;
}

/* The traversal at 0x80028C58 saves s0 through s4 and ra in a 0x180-byte frame. */
bool nc_return_linked_pose_traversal(CPUState *c) {
    if (c->budget <= 21)
        return false;
    const u32 frame = R(29);
    R(31) = MEM_R32(frame + 0x17c);
    for (int reg = 20; reg >= 16; --reg)
        R(reg) = MEM_R32(frame + 0x168 + 4 * (reg - 16));
    R(29) = frame + 0x180;
    c->budget -= 21;
    return true;
}

bool nc_return_placement_projection(CPUState *c) {
    if (c->budget <= 24)
        return false;
    const u32 frame = R(29);
    R(31) = MEM_R32(frame + 0x2f8);
    for (int reg = 21; reg >= 16; --reg)
        R(reg) = MEM_R32(frame + 0x2e0 + 4 * (reg - 16));
    R(29) = frame + 0x300;
    c->budget -= 24;
    return true;
}

/* Placement projection takes the vertices from the descriptor and the
 * rotation and translation from the matrix embedded in the pose. */
bool nc_begin_placement_vertex_projection(CPUState *c) {
    if (c->budget <= 38)
        return false;
    R(29) -= 0x18;
    R(7) = R(6);
    R(5) += 0x18;
    R(8) = MEM_R32(R(4) + 8);
    load_part_transform(c, R(5));
    c->budget -= 38;
    return true;
}

/* The lookup supplies the loop limit for groups of three vertices. */
bool nc_prepare_placement_vertex_groups(CPUState *c) {
    if (c->budget <= 11)
        return false;
    const u32 count = MEM_R8(R(4) + 4);
    const u32 limit = MEM_R8(0x8009e0d8 + count);
    const int cycles = limit ? 14 : 11;
    if (c->budget <= cycles)
        return false;
    R(1) = 0x800a0000 + count;
    R(2) = count;
    R(9) = limit;
    R(5) = 0;
    if (limit) {
        R(11) = R(29) + 4;
        R(10) = R(29) + 8;
        R(6) += 20;
    }
    c->budget -= cycles;
    return true;
}

bool nc_check_placement_vertex_remainder(CPUState *c) {
    if (c->budget <= 7)
        return false;
    R(9) = MEM_R8(R(4) + 4);
    R(2) = (s32)R(5) < (s32)R(9);
    c->budget -= 7;
    return true;
}

bool nc_return_placement_vertices(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(29) += 0x18;
    c->budget -= 3;
    return true;
}

/* RTPT projects three vertices into the GTE's screen and depth FIFOs.
 * The caller keeps a separate pointer to the third output depth field. */
bool nc_project_placement_vertex_triple(CPUState *c, unsigned depth_shift) {
    if (c->budget <= 52)
        return false;
    const u32 vertices = R(8), output = R(7), depth_field = R(6);
    for (unsigned word = 0; word < 6; ++word)
        gte_write_data(c, word, MEM_R32(vertices + 4 * word));
    gte_command(c, 0x0280030); /* RTPT */
    R(5) += 3;
    R(8) += 24;
    for (unsigned i = 0; i < 3; ++i)
        MEM_W32(output + 8 * i, gte_read_data(c, 12 + i));
    MEM_W32(R(29), gte_read_data(c, 17));
    MEM_W32(R(11), gte_read_data(c, 18));
    MEM_W32(R(10), gte_read_data(c, 19));
    const s32 first_depth = (s32)MEM_R32(R(29)) >> depth_shift;
    R(7) += 24;
    MEM_W16(depth_field - 16, (u16)first_depth);
    const s32 second_depth = (s32)MEM_R32(R(29) + 4) >> depth_shift;
    R(3) = (u32)((s32)MEM_R32(R(29) + 8) >> depth_shift);
    MEM_W16(depth_field - 8, (u16)second_depth);
    MEM_W16(depth_field, (u16)R(3));
    R(2) = (s32)R(5) < (s32)R(9);
    R(6) += 24;
    c->budget -= 52;
    return true;
}

/* Each unsigned selector packs a pose slot above an eight-bit vertex index.
 * A missing pose skips the face before any vertex is projected. */
bool nc_select_indexed_face_poses(CPUState *c, bool textured) {
    if (c->budget <= 37)
        return false;
    const u32 face = R(17), actor = R(19);
    const u32 first = MEM_R16(face + (textured ? 13 : 1));
    const u32 second = MEM_R16(face + (textured ? 15 : 3));
    const u32 third = MEM_R16(face + (textured ? 11 : 5));
    R(6) = first & 0xff;
    R(7) = second & 0xff;
    R(8) = third & 0xff;
    R(5) = second >> 8;
    R(2) = actor + 4 * (first >> 8);
    R(3) = MEM_R32(R(2) + 0x4c);
    R(4) = third >> 8;
    c->budget -= 22;
    if (!R(3))
        return true;
    R(5) = MEM_R32(actor + 4 * R(5) + 0x4c);
    R(2) = R(4) << 2;
    c->budget -= 8;
    if (!R(5))
        return true;
    R(4) = MEM_R32(actor + R(2) + 0x4c);
    R(2) = R(3) + 0x18;
    c->budget -= 7;
    return true;
}

/* The face selects a pose and a vertex within that pose. The incoming v0
 * points at its rotation and translation, prepared before this block. */
bool nc_prepare_indexed_vertex(CPUState *c, u32 pose, u32 vertex_index) {
    if (c->budget <= 41)
        return false;
    load_part_transform(c, R(2));
    const u32 descriptor = MEM_R32(pose + 0x38);
    const u32 vertices = MEM_R32(descriptor + 8);
    R(3) = vertex_index << 3;
    R(2) = vertices + R(3);
    c->budget -= 41;
    return true;
}

/* The projection left SZ3 / 4 at sp+0x28. Store another two-bit
 * reduction beside the packed screen coordinates, then select the next matrix. */
bool nc_store_indexed_vertex_depth(CPUState *c, u32 depth_offset, u32 next_pose) {
    if (c->budget <= 7)
        return false;
    const s32 depth = (s32)MEM_R32(R(29) + 0x28);
    MEM_W16(R(29) + depth_offset, (u16)(depth >> 2));
    R(2) = next_pose + 0x18;
    c->budget -= 7;
    return true;
}

/* NormalClip measures the signed area of the three packed screen points.
 * Its call delay slot saves the third vertex's depth beside its coordinates. */
bool nc_measure_indexed_face(CPUState *c, u32 return_pc) {
    if (c->budget <= 26)
        return false;
    const u32 frame = R(29);
    R(4) = MEM_R32(frame + 0x10);
    R(5) = MEM_R32(frame + 0x18);
    const s32 depth = (s32)MEM_R32(frame + 0x28);
    R(6) = MEM_R32(frame + 0x20);
    MEM_W16(frame + 0x24, (u16)(depth >> 2));
    R(31) = return_pc;
    measure_screen_winding(c);
    c->budget -= 26;
    return true;
}

/* Accepted faces weight the first depth twice. Masking the sum gives a
 * word-aligned byte offset into the ordering table. */
bool nc_write_indexed_face_packet(CPUState *c, bool textured) {
    const int cycles = textured ? 42 : 38;
    if (c->budget <= cycles)
        return false;
    const u32 frame = R(29), packet = R(16), face = R(17);
    ++R(21);
    const u32 first_xy = MEM_R32(frame + 0x10);
    R(5) = (u32)(s32)(s16)MEM_R16(frame + 0x14);
    const s32 second_z = (s16)MEM_R16(frame + 0x1c);
    R(6) = (u32)(s32)(s16)MEM_R16(frame + 0x24);
    const u32 bucket = (2 * R(5) + (u32)second_z + R(6)) & 0x1ffc;
    MEM_W32(packet + 1, first_xy);
    MEM_W32(packet + (textured ? 9 : 5), MEM_R32(frame + 0x18));
    const u32 third_xy = MEM_R32(frame + 0x20);
    if (textured) {
        R(9) = MEM_R32(frame + 0x38);
        MEM_W32(packet - 3, R(9));
        MEM_W32(packet + 17, third_xy);
        MEM_W32(packet + 5, MEM_R32(face + 1));
        MEM_W32(packet + 13, MEM_R32(face + 5));
        R(2) = MEM_R16(face + 9);
        R(4) = bucket;
        MEM_W16(packet + 21, (u16)R(2));
    } else {
        MEM_W32(packet + 9, third_xy);
        const u32 color_index = MEM_R16(face + 7);
        R(9) = MEM_R32(frame + 0x40);
        R(2) = MEM_R32(R(9) + 4 * color_index);
        R(3) = bucket;
        MEM_W32(packet - 3, R(2));
    }
    c->budget -= cycles;
    return true;
}

/* Each of the three indexed vertices has its own screen-coordinate slot.
 * The depth output is shared and receives SZ3 in quarter-depth units. */
bool nc_project_indexed_vertex(CPUState *c, u32 screen_offset) {
    if (c->budget <= 15)
        return false;
    const u32 vertex = R(2);
    gte_write_data(c, 0, MEM_R32(vertex));
    gte_write_data(c, 1, MEM_R32(vertex + 4));
    gte_command(c, 0x0180001); /* RTPS */
    R(2) = R(29) + screen_offset;
    MEM_W32(R(2), gte_read_data(c, 14));
    R(12) = gte_read_data(c, 19) >> 2;
    MEM_W32(R(23), R(12));
    c->budget -= 15;
    return true;
}

/* Project one vertex left after the three-at-a-time loop. Each eight-byte
 * output holds packed screen coordinates and a scaled depth halfword. */
bool nc_project_remaining_placement_vertex(CPUState *c, bool sixteenth_depth) {
    const int cycles = sixteenth_depth ? 25 : 24;
    if (c->budget <= cycles)
        return false;
    const u32 vertex = R(8), output = R(7);
    gte_write_data(c, 0, MEM_R32(vertex));
    gte_write_data(c, 1, MEM_R32(vertex + 4));
    gte_command(c, 0x0180001); /* RTPS */
    R(5)++;
    R(8) = vertex + 8;
    MEM_W32(output, gte_read_data(c, 14));
    R(12) = (u32)((s32)gte_read_data(c, 19) >> 2);
    MEM_W32(R(29), R(12));
    const s32 stack_depth = (s32)MEM_R32(R(29));
    MEM_W16(output + 4, (u16)(sixteenth_depth ? stack_depth >> 2 : stack_depth));
    R(2) = (s32)R(5) < (s32)R(9);
    R(7) = output + 8;
    c->budget -= cycles;
    return true;
}

/* Dispatch the projection that stores one-sixteenth depth. The caller's
 * 0x10 pose flag requests a halfword update before the vertex transform. */
bool nc_call_indexed_pose_projection(CPUState *c) {
    const bool update_flags = R(2) != 0;
    const int cycles = update_flags ? 17 : 5;
    if (c->budget <= cycles)
        return false;
    R(4) = R(19);
    if (update_flags) {
        const u32 pose_index = MEM_R8(R(18) + 0x4c);
        R(3) = R(21) + 2 * pose_index;
        R(2) = MEM_R16(R(3) + 0x10a) & 0xff6f;
        MEM_W16(R(3) + 0x10a, (u16)R(2));
    }
    R(5) = R(18);
    R(31) = 0x80028214;
    R(6) = R(29) + 0x58;
    c->budget -= cycles;
    return true;
}

/* Some paths have already advanced the slot. Both loop entries leave its
 * byte offset in v0, including the final iteration. */
bool nc_advance_placement_scan(CPUState *c, bool increment) {
    const int cycles = increment ? 4 : 3;
    if (c->budget <= cycles)
        return false;
    R(20) += increment;
    R(2) = R(20) << 2;
    c->budget -= cycles;
    return true;
}

/* Reload the transform pointer for each coordinate, as the stores can
 * overlap the record containing that pointer. */
bool nc_copy_placement_coordinates(CPUState *c) {
    if (c->budget <= 24)
        return false;
    const u32 record = R(16);
    for (unsigned axis = 0; axis < 3; ++axis) {
        R(3) = MEM_R32(record + 4);
        R(2) = (u32)(s32)(s16)MEM_R16(record + 0x20 + axis * 2);
        MEM_W32(R(3) + 0x40 + axis * 4, R(2));
    }
    c->budget -= 24;
    return true;
}

/* Each axis uses an unsigned record coordinate and signed comparisons
 * against the wrapping 32-bit bounds. The lower branch leaves center+5 in v0. */
bool nc_check_placement_axis(CPUState *c, bool second, bool *inside) {
    if (c->budget <= 11)
        return false;
    const u32 center = MEM_R32(second ? 0x8008f180 : 0x8008f174);
    const u32 coordinate = MEM_R16(R(16) + (second ? 0x36 : 0x34));
    const bool below = (s32)coordinate < (s32)(center - 5);
    const int cycles = below ? 11 : 14;
    if (c->budget <= cycles)
        return false;
    const u32 upper = center + 5;
    const bool above = (s32)upper < (s32)coordinate;
    R(4) = center;
    R(3) = coordinate;
    R(2) = below ? upper : (u32)above;
    *inside = !below && !above;
    c->budget -= cycles;
    return true;
}

bool nc_begin_placement_scan(CPUState *c) {
    if (c->budget <= 15)
        return false;
    R(29) -= 0x38;
    const u32 frame = R(29);
    MEM_W32(frame + 0x2c, R(21));
    R(21) = MEM_R32(0x800ce9c4);
    for (unsigned reg = 16; reg <= 20; ++reg)
        MEM_W32(frame + 0x18 + 4 * (reg - 16), R(reg));
    MEM_W32(frame + 0x30, R(31));
    R(19) = R(4);
    R(20) = 0;
    R(2) = 0;
    c->budget -= 15;
    return true;
}

/* Every occupied slot clears the high flag bit, including records skipped
 * because bit 2 is clear. A vacant slot leaves the prior flags untouched. */
bool nc_check_placement_slot(CPUState *c) {
    if (c->budget <= 8)
        return false;
    const u32 index_bytes = R(2);
    const u32 record = MEM_R32(0x800cf184 + index_bytes);
    const int cycles = record ? 16 : 8;
    if (c->budget <= cycles)
        return false;
    R(1) = 0x800d0000 + index_bytes;
    R(16) = record;
    R(2) = 0x7fff0000;
    if (record) {
        R(3) = MEM_R32(record + 0x2c);
        R(4) = R(3) & 0x7fffffff;
        R(2) = R(3) & 4;
        MEM_W32(record + 0x2c, R(4));
    }
    c->budget -= cycles;
    return true;
}

bool nc_check_placement_pose_callback(CPUState *c) {
    if (c->budget <= 7)
        return false;
    const u32 flags = MEM_R32(R(21) + 0x30) & 0x40200000;
    const int cycles = flags ? 9 : 7;
    if (c->budget <= cycles)
        return false;
    R(3) = 0x40200000;
    R(2) = flags;
    R(4) = R(21);
    if (flags) {
        R(31) = 0x80028b98;
        R(5) = R(18);
    }
    c->budget -= cycles;
    return true;
}

bool nc_call_placement_faces(CPUState *c, u32 light_offset, u32 return_pc) {
    const int cycles = light_offset ? 8 : 6;
    if (c->budget <= cycles)
        return false;
    R(4) = R(16);
    R(5) = R(17);
    MEM_W32(R(29) + 0x10, 0);
    R(7) = light_offset ? MEM_R32(R(29) + light_offset) : 0;
    R(6) = R(20);
    R(31) = return_pc;
    c->budget -= cycles;
    return true;
}

/* Both placement traversals project vertices when descriptor flag 0x02
 * is set. The branch delay slot supplies the descriptor on either path. */
bool nc_call_pose_vertex_projection(CPUState *c, u32 return_pc) {
    if (c->budget <= 11)
        return false;
    const u32 descriptor = MEM_R32(R(18) + 0x38);
    const u32 project = MEM_R8(descriptor + 5) & 2;
    const int cycles = project ? 14 : 11;
    if (c->budget <= cycles)
        return false;
    R(16) = descriptor;
    R(4) = descriptor;
    R(2) = project;
    if (project) {
        R(5) = R(18);
        R(6) = R(17);
        R(31) = return_pc;
    }
    c->budget -= cycles;
    return true;
}

/* After projecting a placement, the loop tests two flags before passing
 * the record to the additional transform at 0x8002A674. */
bool nc_check_placement_transform(CPUState *c) {
    if (c->budget <= 7)
        return false;
    const u32 record = R(16);
    u32 transform = 0;
    int cycles = 7;
    if (MEM_R32(record + 0x2c) & 0x40) {
        if (c->budget <= 22)
            return false;
        const u32 owner = MEM_R32(record);
        const u32 descriptor = MEM_R32(owner + 0x14);
        transform = MEM_R32(descriptor + 0x10) & 0x10000;
        cycles = transform ? 24 : 22;
        if (c->budget <= cycles)
            return false;
    }
    R(2) = transform;
    R(3) = 0x10000;
    if (transform) {
        R(4) = record;
        R(31) = 0x80029460;
    }
    c->budget -= cycles;
    return true;
}

/* Select the pose and parameter block passed to the projection at 0x80028A70. */
bool nc_select_placement_projection_args(CPUState *c) {
    if (c->budget <= 14)
        return false;
    const u32 record = R(16);
    const u32 flag = MEM_R32(record + 0x30) & 0x100000;
    const int cycles = flag ? 16 : 14;
    if (c->budget <= cycles)
        return false;
    R(2) = flag;
    R(3) = 0x100000;
    R(4) = record;
    R(5) = MEM_R32(record + 4);
    R(6) = flag ? MEM_R32(R(28) + 0x364) + 0xa0 : MEM_R32(R(28) + 0x2bc);
    R(7) = 1;
    c->budget -= cycles;
    return true;
}

/* The caller passes a direction flag and a packed word. The upper turn
 * uses exact equality; the lower turn uses a signed comparison. */
bool nc_begin_placement_angle_update(CPUState *c) {
    if (c->budget <= 13)
        return false;
    const u32 pose = MEM_R32(R(4) + 4);
    const u32 value = MEM_R32(R(6));
    const bool rising = R(7) != 0;
    const bool reverse = rising ? value == 0x808080 : (s32)value < 0x202020;
    const int cycles = rising ? (reverse ? 13 : 15) : (reverse ? 15 : 16);
    if (c->budget <= cycles)
        return false;
    R(8) = pose;
    R(3) = value;
    if (reverse) {
        R(2) = rising ? 0x00040000 : 0xfffb0000;
        R(7) = !rising;
    } else {
        R(2) = rising ? value + 0x40404 : value - 0x40404;
        MEM_W32(R(6), R(2));
    }
    c->budget -= cycles;
    return true;
}

bool nc_set_type7_pose_angles(CPUState *c) {
    if (c->budget <= 6)
        return false;
    const u16 type = MEM_R16(R(4) + 0x5a);
    const int cycles = type == 7 ? 19 : 6;
    if (c->budget <= cycles)
        return false;
    R(3) = type;
    R(2) = (u32)-0x4000;
    if (type == 7) {
        const u32 pose = R(8);
        MEM_W32(pose + 0x40, R(2));
        MEM_W32(pose + 0x44, R(2));
        R(2) = (s16)MEM_R16(R(5) + 4);
        MEM_W32(pose + 0x48, R(2));
        R(8) = MEM_R32(pose + 4);
        R(4) = 0x8000;
    }
    c->budget -= cycles;
    return true;
}

bool nc_set_type7_linked_heading(CPUState *c) {
    if (c->budget <= 12)
        return false;
    const s16 global_angle = (s16)MEM_R16(0x800d763a);
    R(3) = (s16)MEM_R16(R(5) + 4) + R(4);
    R(2) = global_angle * 16 + R(3);
    MEM_W32(R(8) + 0x40, R(2));
    c->budget -= 12;
    return true;
}

bool nc_set_other_placement_angles(CPUState *c) {
    if (c->budget <= 6)
        return false;
    const u32 linked = MEM_R32(R(8) + 4);
    const bool write_angles = linked && R(3) != 5;
    const int cycles = write_angles ? 24 : linked ? 9 : 6;
    if (c->budget <= cycles)
        return false;
    R(8) = linked;
    if (linked) {
        R(2) = 5;
        R(3) = 0x8000;
    }
    if (write_angles) {
        MEM_W32(linked + 0x40, (s32)(s16)MEM_R16(R(5) + 4));
        const s16 global_angle = (s16)MEM_R16(0x800d763a);
        R(4) = MEM_R32(R(4) + 4);
        R(2) = global_angle * 16 + R(3);
        MEM_W32(R(4) + 0x48, R(2));
    }
    c->budget -= cycles;
    return true;
}

bool nc_finish_placement_angle_step(CPUState *c) {
    if (c->budget <= 8)
        return false;
    const u32 angle = R(5) + 4;
    MEM_W16(angle, (u16)(MEM_R16(angle) + 0x200));
    R(2) = R(7);
    c->budget -= 8;
    return true;
}

bool nc_begin_record_point_heading(CPUState *c) {
    if (c->budget <= 12)
        return false;
    R(29) -= 0x20;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(4);
    MEM_W32(R(29) + 0x14, R(17));
    MEM_W32(R(29) + 0x18, R(31));
    R(3) = 0x40000000;
    R(2) = MEM_R32(R(16) + 0x2c) & R(3);
    R(17) = R(5);
    c->budget -= 12;
    return true;
}

bool nc_return_record_point_heading(CPUState *c) {
    if (c->budget <= 12)
        return false;
    R(31) = MEM_R32(R(29) + 0x18);
    R(17) = MEM_R32(R(29) + 0x14);
    R(16) = MEM_R32(R(29) + 0x10);
    R(29) += 0x20;
    c->budget -= 12;
    return true;
}

/* X uses record minus point. The point's Z has the opposite sign convention,
 * so the vector-heading call receives the sum of the signed Z halfwords. */
bool nc_call_record_point_bearing(CPUState *c) {
    if (c->budget <= 15)
        return false;
    const u32 record = R(16), point = R(17);
    R(3) = (s16)MEM_R16(record + 0x18);
    const s16 point_x = (s16)MEM_R16(point);
    R(2) = (s16)MEM_R16(record + 0x1c);
    const s16 point_z = (s16)MEM_R16(point + 4);
    R(4) = R(3) - point_x;
    R(5) = R(2) + point_z;
    R(31) = 0x800295e0;
    c->budget -= 15;
    return true;
}

bool nc_expand_record_point_bearing(CPUState *c) {
    if (c->budget <= 10)
        return false;
    R(3) = MEM_R32(R(16) + 4);
    R(5) = MEM_R32(R(3) + 4);
    R(6) = R(2) << 4;
    c->budget -= 10;
    return true;
}

/* Convert the signed point angle to 16-bit turn units, add half a turn,
 * then subtract the record angle. The linked node stores the full word. */
bool nc_set_linked_heading_offset(CPUState *c) {
    if (c->budget <= 16)
        return false;
    const u32 node = R(5);
    MEM_W8(node + 0x4d, MEM_R8(node + 0x4d) | 1);
    const s16 point_angle = (s16)MEM_R16(R(17) + 0xa);
    R(4) = (s16)MEM_R16(R(16) + 0x24);
    R(2) = point_angle * 16 + 0x8000;
    R(3) = R(2) - R(4);
    MEM_W32(node + 0x40, R(3));
    c->budget -= 16;
    return true;
}

bool nc_apply_linked_heading_offset(CPUState *c) {
    if (c->budget <= 7)
        return false;
    const u32 flag = MEM_R32(R(16) + 0x2c) & 0x200;
    const int cycles = flag ? 8 : 7;
    if (c->budget <= cycles)
        return false;
    R(2) = flag;
    R(6) += R(3);
    if (flag)
        MEM_W32(R(5) + 0x44, R(6));
    c->budget -= cycles;
    return true;
}

/* The first link falls through to the node body. Subsequent non-null links
 * execute a jump and its delay slot before processing the next node. */
bool nc_follow_heading_link(CPUState *c, bool first) {
    if (c->budget <= 6)
        return false;
    const u32 next = MEM_R32(R(5) + 0x0c);
    const int cycles = next && !first ? 8 : 6;
    if (c->budget <= cycles)
        return false;
    R(5) = next;
    c->budget -= cycles;
    return true;
}

bool nc_copy_linked_heading_offset(CPUState *c) {
    if (c->budget <= 6)
        return false;
    const u32 node = R(5);
    R(2) = MEM_R8(node + 0x4d);
    MEM_W32(node + 0x40, R(3));
    R(2) |= 1;
    MEM_W8(node + 0x4d, R(2));
    c->budget -= 6;
    return true;
}

bool nc_copy_linked_bearing(CPUState *c) {
    if (c->budget <= 7)
        return false;
    const u32 flag = MEM_R32(R(16) + 0x2c) & 0x200;
    const int cycles = flag ? 8 : 7;
    if (c->budget <= cycles)
        return false;
    R(2) = flag;
    if (flag)
        MEM_W32(R(5) + 0x44, R(6));
    c->budget -= cycles;
    return true;
}

/* Without a linked node, the pose takes the signed point angle plus half
 * a turn directly. Both heading fields retain the full 32-bit result. */
bool nc_set_unlinked_pose_heading(CPUState *c) {
    if (c->budget <= 16)
        return false;
    const u32 pose = R(3);
    MEM_W8(pose + 0x4d, MEM_R8(pose + 0x4d) | 1);
    const s16 angle = (s16)MEM_R16(R(17) + 0xa);
    R(4) = MEM_R32(R(16) + 4);
    R(2) = angle * 16;
    R(3) = R(2) + 0x8000;
    MEM_W32(R(4) + 0x48, R(3));
    c->budget -= 16;
    return true;
}

bool nc_apply_unlinked_pose_bearing(CPUState *c) {
    if (c->budget <= 7)
        return false;
    const u32 flag = MEM_R32(R(16) + 0x2c) & 0x200;
    const int cycles = flag ? 12 : 7;
    if (c->budget <= cycles)
        return false;
    R(2) = flag;
    R(6) += R(3);
    if (flag) {
        R(2) = MEM_R32(R(16) + 4);
        MEM_W32(R(2) + 0x44, R(6));
    }
    c->budget -= cycles;
    return true;
}

/* Each 20-byte row holds signed point identifiers. Its signed cursor
 * selects an identifier, which indexes the 36-byte coordinate records. */
bool nc_lookup_point_row_record(CPUState *c) {
    if (c->budget <= 26)
        return false;
    const u32 row = R(18);
    R(4) = 0x80081a08;
    R(18) = 2 * row;
    R(1) = 0x800d0000 + R(18);
    const s16 cursor = (s16)MEM_R16(0x800d4ca2 + R(18));
    const u32 entry = R(4) + 20 * row + 2 * cursor;
    const s16 point = (s16)MEM_R16(entry);
    R(2) = point * 36;
    R(3) = MEM_R32(0x8008f0c8) + R(2);
    c->budget -= 26;
    return true;
}

/* The point record stores -X and Z in whole units. Convert each actor
 * coordinate before subtraction and pass the low-word sum to the root. */
bool nc_prepare_point_distance_root(CPUState *c) {
    if (c->budget <= 23)
        return false;
    const u32 point = R(3);
    const s32 actor_x = (s32)MEM_R32(R(19) + 0x27c) >> 8;
    R(16) = -(s32)(s16)MEM_R16(point + 0xa) - actor_x;
    const s32 point_z = (s16)MEM_R16(point + 0xc);
    R(2) = (s32)MEM_R32(R(19) + 0x284) >> 8;
    R(17) = point_z - R(2);
    nc_mult(c, R(17), R(17));
    R(3) = c->lo;
    R(4) = R(16) * R(16) + R(3);
    R(31) = 0x8001ad2c;
    c->budget -= 23;
    return true;
}

bool nc_call_point_distance_bearing(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(4) = R(16);
    R(5) = R(17);
    R(31) = 0x8001ad3c;
    R(21) = R(2);
    c->budget -= 4;
    return true;
}

/* The vector bearing is a 12-bit turn. The stored heading uses 16 bits,
 * and the original wraps a gap of exactly 0x7fff in the negative direction. */
bool nc_turn_toward_point(CPUState *c) {
    const u32 headings = 0x800d4c9a;
    if (c->budget <= 29)
        return false;
    const u16 target = (u16)(R(2) << 4);
    const u16 gap = target - MEM_R16(headings + R(18));
    const bool wrap = gap >= 0x7fff;
    const int cycles = 29 + wrap;
    if (c->budget <= cycles)
        return false;
    const s32 turn = wrap ? (s32)gap - 0x10000 : gap;
    R(1) = 0x800d0000 + R(18);
    R(17) = headings + (R(20) << 1);
    R(3) = MEM_R16(R(17)) + (turn >> 3);
    R(2) = R(3) << 16;
    R(16) = (s16)R(3);
    R(4) = R(16);
    R(31) = 0x8001ada4;
    MEM_W16(R(17), (u16)R(3));
    c->budget -= cycles;
    return true;
}

/* Sine and cosine use 16384 for one unit. Scaling by 5/16 advances the
 * actor twenty whole units along the heading in its 24.8 coordinate system. */
static s32 point_stride_component(u32 sample) {
    return ((s16)sample * 5) >> 4;
}

bool nc_step_point_x(CPUState *c) {
    if (c->budget <= 12)
        return false;
    R(4) = R(16);
    R(3) = point_stride_component(R(2));
    R(2) = MEM_R32(R(19) + 0x27c) + R(3);
    R(31) = 0x8001adcc;
    MEM_W32(R(19) + 0x27c, R(2));
    c->budget -= 12;
    return true;
}

bool nc_step_point_z(CPUState *c) {
    if (c->budget <= 16)
        return false;
    R(5) = MEM_R32(R(19) + 0x284) + point_stride_component(R(2));
    R(3) = 0x40000;
    R(2) = MEM_R32(R(19) + 0x2c8) & R(3);
    MEM_W32(R(19) + 0x284, R(5));
    c->budget -= 16;
    return true;
}

bool nc_store_point_pose_heading(CPUState *c) {
    if (c->budget <= 8)
        return false;
    R(3) = MEM_R32(R(19) + 0x20);
    R(2) = (s16)MEM_R16(R(17));
    MEM_W32(R(3) + 0x48, R(2));
    c->budget -= 8;
    return true;
}

bool nc_prepare_point_coordinate_query(CPUState *c) {
    if (c->budget <= 7)
        return false;
    R(4) = (s32)MEM_R32(R(19) + 0x27c) >> 8;
    R(5) = (s32)R(5) >> 8;
    R(6) = R(29) + 0x10;
    R(31) = 0x8001ae20;
    c->budget -= 7;
    return true;
}

/* The pose-heading path joins at the distance test, bypassing the query result. */
bool nc_check_point_proximity(CPUState *c, bool from_query) {
    const bool copy_result = from_query && R(2) != 0;
    const int cycles = from_query ? (copy_result ? 10 : 4) : 3;
    if (c->budget <= cycles)
        return false;
    if (copy_result)
        MEM_W16(R(19) + 0x360, MEM_R16(R(29) + 0x14));
    R(2) = (s32)R(21) < 150;
    R(4) = R(20);
    c->budget -= cycles;
    return true;
}

/* The four actor states select 20-byte rows of signed point identifiers.
 * A zero entry restarts the row at index zero. */
bool nc_advance_point_index(CPUState *c) {
    const u32 row = R(4);
    if (c->budget <= 24 || row >= 4)
        return false;
    const u32 index_addr = 0x800d4ca2 + 2 * row;
    const u16 next = MEM_R16(index_addr) + 1;
    const u32 table = 0x80081a08;
    const u32 entry = table + 20 * row + 2 * (s16)next;
    const s16 point = MEM_R16(entry);
    const int cycles = point ? 24 : 25;
    if (c->budget <= cycles)
        return false;
    R(6) = index_addr;
    R(5) = table;
    R(3) = entry;
    R(2) = point;
    MEM_W16(index_addr, next);
    if (!point)
        MEM_W16(index_addr, 0);
    c->budget -= cycles;
    return true;
}

bool nc_finish_point_update(CPUState *c) {
    if (c->budget <= 6)
        return false;
    const s32 state = MEM_R32(R(19) + 0x2cc);
    const int cycles = state < 0 ? 6 : 8;
    if (c->budget <= cycles)
        return false;
    R(2) = state;
    R(4) = R(19);
    if (state >= 0) {
        R(5) = 5;
        R(31) = 0x8001aeac;
    }
    c->budget -= cycles;
    return true;
}

static bool restore_s0_to_s5_frame(CPUState *c, u32 size) {
    if (c->budget <= 24)
        return false;
    R(31) = MEM_R32(R(29) + size - 8);
    for (unsigned reg = 21; reg >= 16; --reg)
        R(reg) = MEM_R32(R(29) + size - 0x20 + 4 * (reg - 16));
    R(29) += size;
    c->budget -= 24;
    return true;
}

bool nc_restore_point_update_frame(CPUState *c) {
    return restore_s0_to_s5_frame(c, 0x48);
}

bool nc_restore_placement_scan_frame(CPUState *c) {
    return restore_s0_to_s5_frame(c, 0x38);
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
