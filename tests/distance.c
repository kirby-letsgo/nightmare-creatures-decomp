#include "decomp/distance.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include <stdio.h>

static CPUState initial(void) {
    CPUState cpu = {0};
    for (unsigned r = 1; r < 32; r++)
        cpu.r[r] = r * 0x12345;
    cpu.budget = 512;
    cpu.r[31] = 0x80010000;
    return cpu;
}

/* Run the disc routine up to its return (or the caller's next instruction).
 * Check all registers, timing, and the transition to the timed path. */
static bool compare(CPUState input, u32 pc, bool (*native_fn)(CPUState *), bool disc,
                    CPUState *result) {
    *result = input;
    if (!native_fn(result))
        return false;
    if (disc) {
        CPUState guest = input;
        interp_call(&guest, pc);
        if (memcmp(&guest, result, sizeof guest)) {
            fprintf(stderr, "%08x input %08x: guest/native result %u/%u, budget %d/%d\n", pc,
                    input.r[4], guest.r[2], result->r[2], guest.budget, result->budget);
            return false;
        }
    }
    input.budget -= result->budget;
    CPUState cutoff = input;
    if (native_fn(&cutoff) || memcmp(&input, &cutoff, sizeof input))
        return false;
    input.budget++;
    return native_fn(&input) && input.budget == 1;
}

static bool roots(bool disc) {
    CPUState c = initial(), result;
    const u32 cases[][2] = {{0, 0},
                            {1, 1},
                            {2, 2},
                            {3, 1},
                            {4, 2},
                            {15, 3},
                            {16, 4},
                            {17, 4},
                            {65535, 255},
                            {65536, 256},
                            {0xffffffffu, 0xffffffffu}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        c.r[4] = cases[i][0];
        if (!compare(c, 0x800475E4, nc_distance_root, disc, &result) || result.r[2] != cases[i][1])
            return false;
    }
    c.r[4] = 0x40000000;
    CPUState before = c;
    if (nc_distance_root(&c) || memcmp(&before, &c, sizeof c))
        return false;
    u32 sample = 0;
    for (unsigned i = 0; i < 4096; i++) {
        sample = sample * 1664525u + 1013904223u;
        c.r[4] = sample & 0x7fffffffu;
        if (!compare(c, 0x800475E4, nc_distance_root, disc, &result))
            return false;
    }
    return true;
}

static bool displacements(bool disc) {
    CPUState c = initial(), result;
    c.r[4] = 0x80120000;
    c.r[5] = 0x80121000;
    c.r[31] = 0x8004CE58;
    const u32 coords[] = {0, 1, 255, 256, 257, 0x7fffffff, 0x80000000, 0xffffffff};
    for (unsigned i = 0; i < 8; i++)
        for (unsigned j = 0; j < 8; j++) {
            MEM_W32(c.r[4] + 0x27C, coords[i]);
            MEM_W32(c.r[5] + 0x27C, coords[j]);
            MEM_W32(c.r[4] + 0x284, coords[j]);
            MEM_W32(c.r[5] + 0x284, coords[i]);
            if (!compare(c, 0x8004CE24, nc_actor_distance_terms, disc, &result))
                return false;
        }
    return true;
}

static bool headings(bool disc) {
    CPUState c = initial(), result;
    const s32 axes[] = {-2147483647, -2097151, -1024, -17, -1, 0, 1, 17, 1024, 2097151, 2147483647};
    /* Non-disc runs use a synthetic ramp. Disc runs read the game's table. */
    if (!disc)
        for (unsigned i = 0; i <= 1024; i++)
            MEM_W16(0x80085C4C + i * 2, i / 2);
    for (unsigned i = 0; i < sizeof axes / sizeof axes[0]; i++)
        for (unsigned j = 0; j < sizeof axes / sizeof axes[0]; j++) {
            c.r[4] = (u32)axes[i];
            c.r[5] = (u32)axes[j];
            bool wrapped = (axes[i] > 2097151 || axes[i] < -2097151) &&
                           (axes[j] > 2097151 || axes[j] < -2097151);
            if (wrapped) {
                CPUState before = c;
                if (nc_vector_heading(&c) || memcmp(&before, &c, sizeof c))
                    return false;
            } else if (!compare(c, 0x80047684, nc_vector_heading, disc, &result))
                return false;
        }
    c.r[4] = c.r[5] = 0;
    return compare(c, 0x80047684, nc_vector_heading, disc, &result) && result.r[2] == 512;
}

static bool heading_samples(bool disc) {
    const s16 cardinal[] = {0, 16384, 0, -16384};
    CPUState c = initial(), result;
    for (unsigned table = 0; table < 2; table++) {
        bool (*sample)(CPUState *) = table ? nc_heading_sine : nc_heading_cosine;
        u32 address = table ? 0x80083be4 : 0x80081be4;
        u32 pc = table ? 0x80035338 : 0x8003531c;
        for (unsigned i = 0; i < 4096; i++) {
            if (!disc) {
                s16 value = i % 1024 ? (s16)(i * 31) : cardinal[(i / 1024 + !table) % 4];
                MEM_W16(address + 2 * i, value);
            }
            c.r[4] = 0xfffe0000u + i * 16 + i % 16;
            if (!compare(c, pc, sample, disc, &result) ||
                result.r[2] != (u32)(s32)(s16)MEM_R16(address + 2 * i))
                return false;
            if (i % 1024 == 0 && (s32)result.r[2] != cardinal[(i / 1024 + !table) % 4])
                return false;
        }
    }
    return true;
}

static bool projected_points(bool disc) {
    const u32 samples[] = {0, 1, 0x4000, 0xffffc000, 0x7fff, 0x8000, 0xffffffff};
    for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; i++)
        for (unsigned j = 0; j < sizeof samples / sizeof samples[0]; j++) {
            CPUState c = initial();
            c.r[31] = 0x800471AC;
            c.r[2] = samples[i];
            c.r[16] = samples[i] * 0x10001u;
            c.r[17] = samples[j] * 0x12345u;
            c.r[18] = SCRATCH_BASE + 0x140;
            MEM_W32(0x800D7780, 0xfffffcff);
            MEM_W32(0x800D7788, 0x000154ff);
            MEM_W32(c.r[18], 0x11002200);
            CPUState native = c;
            if (!nc_project_heading_offset(&native))
                return false;
            u32 output = MEM_R32(c.r[18]);
            u32 product = (u32)(s32)(s16)samples[i] * c.r[17];
            u16 x = (u16)(((s32)product >> 14) - 4);
            if (output != (0x11000000u | x) || native.r[2] != c.r[16] - 0x154 ||
                native.budget != c.budget - 20)
                return false;
            MEM_W32(c.r[18], 0x11002200);
            CPUState cutoff = c;
            cutoff.budget = 20;
            CPUState before = cutoff;
            if (nc_project_heading_offset(&cutoff) || memcmp(&cutoff, &before, sizeof before) ||
                MEM_R32(c.r[18]) != 0x11002200)
                return false;
            if (disc) {
                CPUState guest = c;
                interp_call(&guest, 0x8004716C);
                if (memcmp(&guest, &native, sizeof guest) || MEM_R32(c.r[18]) != output)
                    return false;
            }
        }
    return true;
}

static bool actor_coordinates(bool disc) {
    const u32 record = SCRATCH_BASE + 0x80;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    const u32 offsets[] = {0, 0xffffffff, 0x80001234};
    for (unsigned i = 0; i < 3; ++i) {
        CPUState input = initial();
        input.r[4] = i == 2 ? record + 0x18 - 0x2dc : 0x80120000;
        input.r[5] = record;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(input.r[4] + 0x2dc, 0xfffeffff);
        MEM_W32(record + 0x138, offsets[i]);
        MEM_W16(0x8008f0c0, 0x8123);
        MEM_W16(0x8008f0bc, 0x3456);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        native.budget = 27;
        CPUState cutoff = native;
        if (nc_init_actor_coordinates(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native = input;
        native.budget = input.budget = 28;
        u32 y = (i == 2 ? 0xfffe3456u : 0xfffeffffu) + offsets[i];
        if (!nc_init_actor_coordinates(&native) || native.r[3] != y || native.budget != 1 ||
            MEM_R16(record + 2) != (u16)y || MEM_R32(record + 0x160) != input.r[4])
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x800470dc);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool heading_projections(bool disc) {
    const u32 angles[] = {0, 0x3fff, 0x4000, 0x8000, 0xc000, 0xffff};
    const s32 radii[] = {0, 1, -257, 32767, -32768};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i)
        for (unsigned j = 0; j < sizeof radii / sizeof radii[0]; ++j) {
            CPUState input = initial();
            input.r[4] = (u32)radii[j];
            input.r[5] = angles[i];
            input.r[29] = SCRATCH_BASE + 0x100;
            /* Also project onto the stack's saved s0/s1 words. */
            input.r[6] = j & 1 ? input.r[29] - 16 : SCRATCH_BASE;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(0x800D7780, 0xfffffeff);
            MEM_W32(0x800D7788, 0x7ff);
            memcpy(before, nc_scratch, sizeof before);
            CPUState cutoff = input;
            cutoff.budget = 57;
            CPUState native = cutoff;
            if (nc_prepare_heading_projection(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            u32 offset = (angles[i] >> 4) * 2;
            s32 sine = (s16)MEM_R16(0x80083be4 + offset);
            s32 cosine = (s16)MEM_R16(0x80081be4 + offset);
            s32 dx = (sine * radii[j]) >> 14;
            s32 dz = (cosine * radii[j]) >> 14;
            native.budget++;
            if (!nc_prepare_heading_projection(&native) || native.budget != 1 ||
                native.r[31] != 0x800471b4 || native.r[4] != (u32)dx || native.r[5] != (u32)dz ||
                MEM_R16(input.r[6]) != (u16)(dx - 2) || MEM_R16(input.r[6] + 4) != (u16)(dz - 7))
                return false;
            native.budget = input.budget - 57;
            if (!nc_vector_heading(&native))
                return false;
            memcpy(after, nc_scratch, sizeof after);
            cutoff = native;
            cutoff.budget = 19;
            CPUState epilogue = cutoff;
            if (nc_finish_heading_projection(&epilogue) ||
                memcmp(&epilogue, &cutoff, sizeof cutoff) ||
                memcmp(after, nc_scratch, sizeof after))
                return false;
            epilogue.budget++;
            if (!nc_finish_heading_projection(&epilogue) || epilogue.budget != 1 ||
                !nc_finish_heading_projection(&native))
                return false;
            if (disc) {
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80047128);
                if (memcmp(&input, &native, sizeof input) ||
                    memcmp(nc_scratch, after, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool actor_headings(bool disc) {
    const u32 angles[] = {0,      1,      0x3fff,     0x4000,     0x7fff,    0x8000,
                          0xc000, 0xffff, 0xffff8000, 0xffffffff, 0x80000000};
    const u32 actor = SCRATCH_BASE, pose = actor + 128, linked = pose + 128;
    CPUState input = initial(), result;
    input.r[4] = actor;
    MEM_W32(actor + 0x20, pose);
    MEM_W32(pose + 4, linked);
    u8 before[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i)
        for (unsigned j = 0; j < sizeof angles / sizeof angles[0]; ++j) {
            MEM_W32(pose + 0x48, angles[i]);
            MEM_W32(linked + 0x40, angles[j]);
            memcpy(before, nc_scratch, sizeof before);
            if (!compare(input, 0x800472D8, nc_actor_heading, disc, &result) ||
                result.r[2] != (u32)(s32)(s16)(angles[i] + angles[j]) || result.r[3] != angles[j] ||
                result.budget != input.budget - 18 || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    return true;
}

static bool record_projections(bool disc) {
    const u32 angles[] = {0, 1024, 2048, 3072, 4095};
    const s32 radii[] = {0, 257, -257};
    const u32 record = SCRATCH_BASE + 128;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i)
        for (unsigned j = 0; j < sizeof radii / sizeof radii[0]; ++j) {
            CPUState input = initial();
            input.r[5] = record;
            input.r[29] = SCRATCH_BASE + 896;
            const u32 actor = j == 2 ? record + 0x150 - 0x284 : 0x80120000;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(0x800d7780, (u32)-769);
            MEM_W32(0x800d7788, 1023);
            MEM_W32(record + 0x160, actor);
            MEM_W32(record + 0x128, angles[i]);
            MEM_W32(record + 0x130, radii[j]);
            MEM_W32(actor + 0x27c, 0xffff89ab);
            MEM_W32(actor + 0x284, 0x123456);
            memcpy(before, nc_scratch, sizeof before);
            CPUState cutoff = input;
            cutoff.budget = 29;
            CPUState native = cutoff;
            if (nc_prepare_record_projection(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget++;
            if (!nc_prepare_record_projection(&native) || native.budget != 1 ||
                native.r[4] != (u32)radii[j] || native.r[5] != angles[i] * 16 ||
                native.r[6] != record + 0x18 || native.r[31] != 0x800473c0 ||
                MEM_R32(record + 0x150) != 0xffff89ab ||
                MEM_R32(record + 0x158) != (j == 2 ? 0xffff89abu : 0x123456u))
                return false;
            native.budget = input.budget - 29;
            if (disc) {
                interp_call(&native, 0x80047128);
                native.r[31] = 0x800473c8;
                native.r[4] = native.r[17];
                native.budget -= 2;
                interp_call(&native, 0x80047220);
            } else {
                native.r[2] = angles[i];
            }
            cutoff = native;
            cutoff.budget = 14;
            CPUState finish = cutoff;
            memcpy(after, nc_scratch, sizeof after);
            if (nc_store_projected_heading(&finish) || memcmp(&finish, &cutoff, sizeof cutoff) ||
                memcmp(after, nc_scratch, sizeof after))
                return false;
            finish.budget++;
            if (!nc_store_projected_heading(&finish) || finish.budget != 1 ||
                !nc_store_projected_heading(&native) ||
                MEM_R16(record + 0x12) != (u16)native.r[2] ||
                MEM_R16(record + 0x0a) != (u16)native.r[2] || native.r[31] != input.r[31] ||
                native.r[29] != input.r[29])
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x8004737c);
                if (memcmp(&input, &native, sizeof input) ||
                    memcmp(nc_scratch, after, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool projection_entries(bool disc) {
    const u32 record = SCRATCH_BASE, actor = 0x80121000, linked_actor = 0x80122000;
    const u32 pose = 0x80123000, linked_pose = 0x80124000;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned state = 0; state <= 2; ++state)
        for (unsigned flag = 0; flag <= 1; ++flag) {
            const bool expected_point = state < 2 && flag;
            CPUState input = initial();
            input.r[4] = actor;
            input.r[5] = record;
            input.r[29] = SCRATCH_BASE + 896;
            input.r[31] = expected_point ? 0x80047220 : 0x80047488;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(actor + 0x20, pose);
            MEM_W32(record + 0x160, linked_actor);
            MEM_W32(pose + 4, linked_pose);
            MEM_W32(pose + 0x48, 0xffff8000);
            MEM_W32(linked_pose + 0x40, 0x4000);
            MEM_W8(linked_actor + 0x388, state);
            MEM_W32(linked_actor + 0x330, flag ? 0xc0 : 0x80);
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 40;
            CPUState native = input;
            bool use_point = !expected_point;
            if (nc_begin_projected_point_update(&native, &use_point) ||
                memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_begin_projected_point_update(&native, &use_point) ||
                use_point != expected_point || native.r[17] != 0xffffc000 ||
                native.r[16] != record ||
                native.budget != 41 - (state >= 2 ? 26 : (flag ? 40 : 33)) ||
                MEM_R16(record + 0x16c) != (expected_point ? 0xa5a4 : 0xa5a5))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                /* Start the oracle with the post-prologue stack pointer so it
                 * stops at either continuation. Undo that adjustment in the slot. */
                const u32 stub = 0x80120000;
                MEM_W32(stub, 0x08000000u | ((0x80047420u >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, 0x27bd0028); /* addiu sp, sp, 40 */
                input.r[29] -= 40;
                input.budget += 2;
                interp_call(&input, stub);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool rotated_vector_setup(bool disc) {
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned negative_z = 0; negative_z < 2; ++negative_z) {
        const u32 source = negative_z ? 0x8008eefc : 0x8008ef04;
        if (!disc) {
            MEM_W32(source, 0x80017fff + negative_z);
            MEM_W32(source + 4, 0x1234abcd + negative_z);
        }
        for (unsigned alignment = 0; alignment < 4; ++alignment) {
            CPUState input = initial();
            input.r[4] = (u32)-513;
            input.r[5] = SCRATCH_BASE;
            input.r[29] = SCRATCH_BASE + 512 + alignment;
            input.r[31] = 0x8007a320; /* stop at ApplyMatrix */
            memset(nc_scratch, 0xa5, sizeof before);
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 28;
            CPUState native = input;
            if (nc_prepare_rotated_point_offset(&native, negative_z) ||
                memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            const u32 frame = input.r[29] - 56;
            const u32 first = MEM_R32(source), second = MEM_R32(source + 4);
            if (!nc_prepare_rotated_point_offset(&native, negative_z) || native.budget != 1 ||
                native.r[29] != frame || native.r[4] != input.r[5] + 0x38 ||
                native.r[5] != frame + 16 || native.r[6] != frame + 24 || native.r[2] != first ||
                native.r[3] != second)
                return false;
            for (unsigned b = 0; b < 8; ++b)
                if (MEM_R8(frame + 16 + b) != MEM_R8(source + b))
                    return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                /* Match interp_call's expected stack at the nested call. */
                const u32 stub = 0x80120000, pc = negative_z ? 0x80046e18 : 0x80046edc;
                MEM_W32(stub, 0x08000000u | ((pc >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, 0x27bd0038); /* addiu sp, sp, 56 */
                input.r[29] = frame;
                input.budget += 2;
                interp_call(&input, stub);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool rotated_point_offsets(bool disc) {
    const s32 components[][2] = {
        {0, 0}, {511, -511}, {-512, 513}, {-513, -1025}, {0x40000001, -0x40000001}};
    const s16 scales[] = {1, -1, 32767};
    const u32 frame = SCRATCH_BASE + 128;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned dir = 0; dir < 2; ++dir)
        for (unsigned i = 0; i < sizeof components / sizeof components[0]; ++i)
            for (unsigned j = 0; j < sizeof scales / sizeof scales[0]; ++j) {
                CPUState input = initial();
                const u32 record = j == 2 ? frame + 8 : SCRATCH_BASE + 512;
                input.r[29] = frame;
                input.r[16] = 0xabcd0000u | (u16)scales[j];
                input.r[17] = record;
                memset(nc_scratch, 0xa5, sizeof before);
                MEM_W16(record + 0x18, 1);
                MEM_W16(record + 0x1c, 0xffff);
                MEM_W32(frame + 0x18, components[i][0]);
                MEM_W32(frame + 0x20, components[i][1]);
                MEM_W32(frame + 0x30, input.r[31]);
                MEM_W32(frame + 0x2c, 0x12345678);
                MEM_W32(frame + 0x28, 0x87654321);
                memcpy(before, nc_scratch, sizeof before);
                input.budget = 43;
                CPUState native = input;
                if (nc_apply_rotated_point_offset(&native, dir) ||
                    memcmp(&native, &input, sizeof input) ||
                    memcmp(before, nc_scratch, sizeof before))
                    return false;
                native.budget = ++input.budget;
                const s32 dx = (s32)((u32)components[i][0] * (u32)scales[j]) / 512;
                const u16 x = MEM_R16(record + 0x18);
                if (!nc_apply_rotated_point_offset(&native, dir) ||
                    MEM_R16(record + 0x18) != (u16)(dir ? x - dx : x + dx) ||
                    native.r[29] != frame + 56 || native.r[31] != input.r[31])
                    return false;
                const s32 dz = (s32)(MEM_R32(frame + 0x20) * (u32)scales[j]) / 512;
                if (native.r[3] != (u32)dz ||
                    MEM_R16(record + 0x1c) != (u16)(dir ? 65535 + dz : 65535 - dz))
                    return false;
                if (disc) {
                    memcpy(after, nc_scratch, sizeof after);
                    memcpy(nc_scratch, before, sizeof before);
                    /* Enter with the caller's sp so the oracle recognizes the return. */
                    const u32 stub = 0x80120000, pc = dir ? 0x80046e68 : 0x80046f2c;
                    MEM_W32(stub, 0x08000000u | ((pc >> 2) & 0x03ffffffu));
                    MEM_W32(stub + 4, 0x27bdffc8); /* addiu sp, sp, -56 */
                    input.r[29] += 56;
                    input.budget += 2;
                    interp_call(&input, stub);
                    if (memcmp(&native, &input, sizeof input) ||
                        memcmp(after, nc_scratch, sizeof after))
                        return false;
                }
            }
    return true;
}

static bool actor_point_placements(bool disc) {
    const u32 angles[] = {0, 1024, 2048, 3072, 4095, 0xffffffff};
    const u32 actor = 0x80121000, pose = 0x80122000, linked = 0x80123000;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i) {
        CPUState input = initial();
        input.r[4] = actor;
        input.r[5] = SCRATCH_BASE;
        input.r[6] = angles[i];
        input.r[7] = i & 1 ? (u32)-257 : 257;
        input.r[29] = SCRATCH_BASE + 896;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(actor + 0x20, pose);
        MEM_W32(pose + 4, linked);
        MEM_W32(pose + 0x48, 0xffff8000);
        MEM_W32(linked + 0x40, 0x4000);
        MEM_W32(actor + 0x27c, (u32)-769);
        MEM_W32(actor + 0x280, (u32)-7);
        MEM_W32(actor + 0x284, 1023);
        MEM_W32(input.r[29] + 16, 33);
        memcpy(before, nc_scratch, sizeof before);
        CPUState cutoff = input;
        cutoff.budget = 49;
        CPUState native = cutoff;
        if (nc_begin_actor_point_placement(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget++;
        if (!nc_begin_actor_point_placement(&native) || native.budget != 1 || native.r[19] != 33 ||
            native.r[20] != (u32)-4 || native.r[16] != 3 ||
            native.r[4] != 0xffffc000u - (angles[i] << 4) || native.r[31] != 0x8004701c)
            return false;
        native.budget = input.budget - 49;
        if (!nc_heading_cosine(&native) || !nc_place_actor_points(&native))
            return false;
        if (disc)
            interp_call(&native, 0x80047128);
        cutoff = native;
        cutoff.budget = 34;
        CPUState finish = cutoff;
        memcpy(after, nc_scratch, sizeof after);
        if (nc_finish_actor_point_placement(&finish) || memcmp(&finish, &cutoff, sizeof cutoff) ||
            memcmp(after, nc_scratch, sizeof after))
            return false;
        finish.budget++;
        if (!nc_finish_actor_point_placement(&finish) || finish.budget != 1 ||
            !nc_finish_actor_point_placement(&native) || native.r[29] != input.r[29] ||
            native.r[31] != input.r[31] || MEM_R16(SCRATCH_BASE + 10) != (u16)native.r[2])
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x80046fa0);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placed_actor_points(bool disc) {
    const u32 record = SCRATCH_BASE + 128;
    const u32 angles[] = {0, 0x3fff, 0x4000, 0x8000, 0xc000, 0xffff};
    const s32 radii[] = {0, 257, -257, 0x40000001};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i)
        for (unsigned j = 0; j < sizeof radii / sizeof radii[0]; ++j) {
            CPUState input = initial();
            const u32 offset = (angles[i] >> 4) * 2;
            const u32 cosine = (u32)(s32)(s16)MEM_R16(0x80081be4 + offset);
            const u32 sine = (u32)(s32)(s16)MEM_R16(0x80083be4 + offset);
            const u32 actor = j == 2 ? record + 0x150 - 0x284 : 0x80120000;
            input.r[2] = cosine | 0xaaaa0000;
            input.r[16] = (u32)-31;
            input.r[17] = record;
            input.r[18] = actor;
            input.r[19] = 16;
            input.r[20] = (u32)-4;
            input.r[21] = angles[i];
            input.r[22] = 0xffff8000;
            input.r[23] = radii[j];
            input.r[29] = SCRATCH_BASE + 768;
            input.r[30] = 37;
            input.r[31] = 0x80047128;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(actor + 0x27c, (u32)-769);
            MEM_W32(actor + 0x284, 1023);
            MEM_W32(input.r[29] + 16, 0x1230);
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 51;
            CPUState native = input;
            if (nc_place_actor_points(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            const s32 dx = (s32)(sine * (u32)radii[j]) >> 14;
            const s32 dz = (s32)(cosine * (u32)radii[j]) >> 14;
            if (!nc_place_actor_points(&native) || native.budget != 1 ||
                native.r[31] != 0x800470a4 || native.r[5] != 0x9230 ||
                MEM_R16(record) != (u16)(-4 + dx) || MEM_R16(record + 4) != (u16)(31 - dz) ||
                MEM_R16(record + 2) != (u16)-21 || MEM_R32(record + 0x160) != actor ||
                memcmp(nc_scratch + 128, nc_scratch + 128 + 24, 6) ||
                MEM_R32(record + 0x158) != (j == 2 ? (u32)-769 : 1023))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x8004701c);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool part_transform_setup(bool disc) {
    const u32 frame = SCRATCH_BASE + 256, matrix = frame + 0x98;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    CPUState input = initial();
    input.r[29] = frame + 0xf8;
    input.r[31] = 0x800343d8;
    for (unsigned reg = 0; reg < 8; ++reg)
        gte_write_ctrl(&input, reg, 0x8000ffffu + reg);
    memset(nc_scratch, 0xa5, sizeof before);
    memcpy(before, nc_scratch, sizeof before);
    input.budget = 55;
    CPUState native = input;
    if (nc_init_part_transform(&native) || memcmp(&input, &native, sizeof input) ||
        memcmp(before, nc_scratch, sizeof before))
        return false;
    native.budget = ++input.budget;
    if (!nc_init_part_transform(&native) || native.budget != 1 || native.r[29] != frame ||
        native.r[17] != matrix || MEM_R16(matrix + 18) != 0xa5a5)
        return false;
    for (unsigned i = 0; i < 9; ++i)
        if (MEM_R16(matrix + 2 * i) != (i % 4 == 0 ? 4096 : 0))
            return false;
    if (disc) {
        memcpy(after, nc_scratch, sizeof after);
        memcpy(nc_scratch, before, sizeof before);
        /* The oracle stops at the continuation with the new frame active. */
        const u32 stub = 0x80120000;
        MEM_W32(stub, 0x08000000u | ((0x8003433cu >> 2) & 0x03ffffffu));
        MEM_W32(stub + 4, 0x27bd00f8); /* addiu sp, sp, 248 */
        input.r[29] = frame;
        input.budget += 2;
        interp_call(&input, stub);
        if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
            return false;
    }
    return true;
}

static bool part_chain_traversal(bool disc) {
    const u32 frame = SCRATCH_BASE, part = frame + 512;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned count = 0; count <= 3; ++count) {
        CPUState input = initial();
        input.r[29] = frame;
        input.r[5] = part;
        input.r[7] = frame + 384;
        input.r[16] = 0;
        input.r[31] = count ? 0x80034430 : 0x800345bc;
        memset(nc_scratch, 0xa5, sizeof before);
        for (unsigned i = 0; i < count; ++i)
            MEM_W32(part + 32 * i, i + 1 < count ? part + 32 * (i + 1) : 0);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        if (count) {
            if (!nc_begin_part_chain(&native))
                return false;
            for (unsigned i = 1; i < count; ++i)
                if (!native.r[2] || !nc_append_part_parent(&native))
                    return false;
            if (native.r[2] || native.r[16] != count)
                return false;
        }
        if (!nc_prepare_part_chain_rotation(&native) || native.r[16] != (count ? count - 1 : 0))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, count ? 0x800343d8 : 0x80034410);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    CPUState input = initial();
    input.r[5] = input.r[4] = part;
    input.r[29] = input.r[3] = frame;
    input.r[7] = frame + 384;
    memcpy(before, nc_scratch, sizeof before);
    bool (*const stages[])(CPUState *) = {nc_begin_part_chain, nc_append_part_parent,
                                          nc_prepare_part_chain_rotation,
                                          nc_prepare_part_chain_rotation};
    const int cutoffs[] = {9, 11, 8, 5};
    for (unsigned i = 0; i < 4; ++i) {
        input.r[16] = i == 3 ? 0 : 1;
        input.budget = cutoffs[i];
        CPUState native = input;
        if (stages[i](&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
    }
    return true;
}

static const u32 rotation_entry_pc[] = {0x80025fe4, 0x800261c8, 0x80026394};
static const u32 rotation_matrix_pc[] = {0x8002607c, 0x80026248, 0x80026414};

static bool part_rotation_samples(bool disc, NcRotationInput variant) {
    const u32 frame = SCRATCH_BASE + 256, part = SCRATCH_BASE;
    const u32 angles[][3] = {{0, 0x4000, 0x8000}, {-3, -2, -1}, {0x3ffd, 0x7ffe, 0xbfff}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i) {
        CPUState input = initial();
        input.r[4] = part;
        input.r[5] = SCRATCH_BASE + 512;
        input.r[29] = frame + 48;
        input.r[31] = rotation_matrix_pc[variant];
        memset(nc_scratch, 0xa5, sizeof before);
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (variant == NC_ROTATION_PACKED)
                MEM_W16(part + 2 * axis, angles[i][axis]);
            else
                MEM_W32(part + 0x40 + 4 * axis, angles[i][axis]);
        }
        memcpy(before, nc_scratch, sizeof before);
        input.budget = variant == NC_ROTATION_PART ? 104 : 98;
        CPUState native = input;
        if (nc_sample_part_rotation(&native, variant) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_sample_part_rotation(&native, variant) || native.budget != 1 ||
            native.r[29] != frame || native.r[19] != input.r[5] || native.r[31] != input.r[31])
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            /* Match the oracle's stack at the continuation after the sixth lookup. */
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((rotation_entry_pc[variant] >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bd0030); /* addiu sp, sp, 48 */
            input.r[29] = frame;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool part_rotation_matrices(bool disc, NcRotationInput variant) {
    const u32 frame = SCRATCH_BASE + 256;
    const s32 samples[][6] = {{4096, 4096, 4096, 0, 0, 0},
                              {0, 4096, 0, -4096, 0, 16384},
                              {-1, 2371, -3241, 4095, -3340, -10003}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
        CPUState input = initial();
        input.r[29] = frame;
        input.r[19] = i == 2 ? frame : SCRATCH_BASE;
        input.r[21] = samples[i][0];
        input.r[20] = samples[i][1];
        input.r[variant == NC_ROTATION_PART ? 17 : 16] = samples[i][2];
        input.r[18] = samples[i][3];
        input.r[variant == NC_ROTATION_PART ? 16 : 17] = samples[i][4];
        input.r[2] = samples[i][5];
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(frame + 40, input.r[31]);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 97;
        CPUState native = input;
        if (nc_finish_part_rotation(&native, variant) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_finish_part_rotation(&native, variant) || native.budget != 1 ||
            native.r[29] != frame + 48)
            return false;
        if (!i)
            for (unsigned k = 0; k < 9; ++k)
                if (MEM_R16(input.r[19] + 2 * k) != (k % 4 == 0 ? 4096 : 0))
                    return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((rotation_matrix_pc[variant] >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bdffd0); /* addiu sp, sp, -48 */
            input.r[29] += 48;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool part_transform_calls(bool disc) {
    const u32 node = SCRATCH_BASE, descriptor = node + 128;
    static const struct {
        bool translation, branch, child, flagged;
        u32 stop;
        int cycles;
    } cases[] = {
        {true, true, false, false, 0x8007a530, 14},  {true, false, false, false, 0x8007a530, 10},
        {false, true, false, false, 0x80025fe4, 8},  {false, false, true, false, 0x80025fe4, 14},
        {false, false, false, true, 0x80025fe4, 25}, {false, false, false, false, 0x800267ec, 23}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[17] = node;
        input.r[18] = node + 256;
        input.r[31] = cases[i].stop;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(node + 12, cases[i].branch ? node + 64 : 0);
        MEM_W32(node + 4, cases[i].child ? node + 96 : 0);
        MEM_W32(node + 0x38, descriptor);
        MEM_W8(descriptor + 5, cases[i].flagged ? 0xff : 0xfd);
        MEM_W16(node + 0x10, -32768);
        MEM_W16(node + 0x12, 0);
        MEM_W16(node + 0x14, 32767);
        memcpy(before, nc_scratch, sizeof before);
        bool (*run)(CPUState *) =
            cases[i].translation ? nc_prepare_part_translation : nc_select_part_rotation;
        input.budget = cases[i].translation ? 14 : 25;
        CPUState native = input;
        if (run(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!run(&native) || native.budget != input.budget - cases[i].cycles ||
            (!cases[i].translation && (native.r[2] != 0) != (cases[i].stop == 0x80025fe4)))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, cases[i].translation ? 0x80026754 : 0x80026784);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool node_transform_selection(bool disc) {
    const u32 node = SCRATCH_BASE, descriptor = node + 256;
    static const struct {
        bool branch, child, flagged, overlap;
        int cycles;
    } cases[] = {{true, false, false, false, 19}, {false, true, false, false, 16},
                 {false, false, true, false, 27}, {false, false, false, false, 23},
                 {true, false, true, true, 36},   {true, false, false, true, 32}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[16] = node;
        input.r[18] = 0;
        input.r[19] = cases[i].overlap ? node + 12 : node + 128;
        const bool transform =
            cases[i].child || cases[i].flagged || (cases[i].branch && !cases[i].overlap);
        input.r[31] = transform ? 0x8007a530 : 0x800266b4;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(node + 4, cases[i].child ? node + 64 : 0);
        MEM_W32(node + 12, cases[i].branch ? node + 96 : 0);
        MEM_W32(node + 0x38, descriptor);
        MEM_W8(descriptor + 5, cases[i].flagged ? 0xff : 0xfd);
        MEM_W16(node + 0x10, -32768);
        MEM_W16(node + 0x12, 0);
        MEM_W16(node + 0x14, 32767);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 36;
        CPUState native = input;
        if (nc_select_node_transform(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_select_node_transform(&native) || native.budget != 37 - cases[i].cycles ||
            (native.r[2] != 0) != transform || (transform && native.r[4] != node + 0x10))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x800265cc);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool node_continuations(bool disc, bool part) {
    const u32 node = SCRATCH_BASE, frame = node + 256;
    static const struct {
        u32 link, pending, stop;
        NcNodeContinuation result;
        int cycles;
    } cases[] = {{SCRATCH_BASE + 64, 1, 0x800265cc, NC_NODE_NEXT, 6},
                 {0, 1, 0x800265ac, NC_NODE_POP, 8},
                 {0, 0, 0x80010000, NC_NODE_RETURN, 32}};
    u8 before[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[part ? 17 : 16] = node;
        input.r[part ? 18 : 19] = frame + 24;
        input.r[part ? 19 : 20] = cases[i].pending;
        input.r[29] = frame;
        input.r[31] = cases[i].stop;
        if (part && cases[i].result == NC_NODE_NEXT)
            input.r[31] = 0x80026754;
        if (part && cases[i].result == NC_NODE_POP)
            input.r[31] = 0x80026738;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(node + 4, cases[i].link);
        for (unsigned reg = 16; reg <= (part ? 20u : 21u); ++reg)
            MEM_W32(frame + (part ? 0xb8 : 0xc0) + 4 * (reg - 16), 0x80000000u + reg);
        MEM_W32(frame + (part ? 0xcc : 0xd8), input.r[31]);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = part ? 29 : 32;
        CPUState native = input;
        if (nc_advance_node_transform(&native, part) != NC_NODE_FALLBACK ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (nc_advance_node_transform(&native, part) != cases[i].result ||
            native.budget != input.budget - (part && i == 2 ? 29 : cases[i].cycles) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        if (disc) {
            u32 entry = part ? 0x800267ec : 0x800266b4;
            if (cases[i].result == NC_NODE_RETURN) {
                const u32 stub = 0x80120000;
                MEM_W32(stub, 0x08000000u | ((entry >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, part ? 0x27bdff30 : 0x27bdff20); /* allocate guest frame */
                input.r[29] += part ? 208 : 224;
                input.budget += 2;
                entry = stub;
            }
            interp_call(&input, entry);
            if (memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool part_composition(bool disc) {
    CPUState input = initial(), result;
    input.r[16] = SCRATCH_BASE;
    input.r[17] = SCRATCH_BASE + 128;
    input.r[20] = SCRATCH_BASE + 256;
    input.r[31] = 0x8007a0e0;
    return compare(input, 0x800267c8, nc_prepare_part_composition, disc, &result) &&
           result.r[4] == input.r[16] && result.r[5] == input.r[20] &&
           result.r[6] == input.r[17] + 24 && result.r[31] == 0x800267dc &&
           result.budget == input.budget - 5;
}

static bool node_matrix_installation(bool disc) {
    const u32 node = SCRATCH_BASE, matrix = node + 64, frame = node + 256;
    static const struct {
        bool (*run)(CPUState *);
        u32 entry, stop;
        int cycles;
        u32 frame_size;
    } paths[] = {{nc_start_node_traversal, 0x80026560, 0x800265cc, 60, 224},
                 {nc_restore_node_branch, 0x800265ac, 0x800265cc, 48, 0},
                 {nc_apply_node_matrix, 0x800266a4, 0x800266b4, 40, 0},
                 {nc_apply_part_matrix, 0x800267dc, 0x800267ec, 40, 0},
                 {nc_start_part_traversal, 0x800266f4, 0x80026754, 53, 208},
                 {nc_restore_part_branch, 0x80026738, 0x80026754, 47, 0}};
    const u32 words[] = {0x80001000, 0x7fff0000, 0x0001f000, 0xffff0800,
                         0x1000,     (u32)-17,   0,          32767};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        CPUState input = initial();
        input.r[4] = node;
        input.r[5] = matrix;
        if (paths[i].run == nc_apply_part_matrix)
            input.r[16] = matrix;
        else if (paths[i].run == nc_restore_part_branch)
            input.r[18] = frame + 16;
        else
            input.r[18] = matrix;
        input.r[19] = frame + 16;
        input.r[29] = frame + paths[i].frame_size;
        input.r[31] = paths[i].stop;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(node + 4, node + 128);
        MEM_W32(node + 8, node + 160);
        MEM_W32(frame + 16, matrix);
        MEM_W32(frame + 20, node + 128);
        for (unsigned reg = 0; reg < 8; ++reg)
            MEM_W32(matrix + 4 * reg, words[reg]);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = paths[i].cycles;
        CPUState native = input;
        if (paths[i].run(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!paths[i].run(&native) || native.budget != 1 || native.r[29] != frame)
            return false;
        for (unsigned reg = 0; reg < 8; ++reg)
            if (gte_read_ctrl(&native, reg) != words[reg])
                return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            u32 entry = paths[i].entry;
            if (paths[i].frame_size) {
                const u32 stub = 0x80120000;
                MEM_W32(stub, 0x08000000u | ((entry >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, 0x27bd0000u | paths[i].frame_size); /* addiu sp, sp, frame_size */
                input.r[29] = frame;
                input.budget += 2;
                entry = stub;
            }
            interp_call(&input, entry);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool node_rotations(bool disc) {
    const u32 source = SCRATCH_BASE + 64;
    const s16 values[] = {-32768, 0, 32767, -4096, 4096, 0, 1, -1, -13};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < 3; ++i) {
        const u32 output = i == 1 ? source + 2 : SCRATCH_BASE + 256;
        CPUState input = initial();
        input.r[16] = output - 0x18;
        input.r[17] = source;
        input.r[18] = SCRATCH_BASE + 512;
        input.r[21] = i == 2 ? source + 4 : source;
        input.r[31] = 0x8007a0e0;
        memset(nc_scratch, 0xa5, sizeof before);
        for (unsigned k = 0; k < 9; ++k)
            MEM_W16(source + 2 * k, values[k]);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 46;
        CPUState native = input;
        if (nc_prepare_node_rotation(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_prepare_node_rotation(&native) || native.budget != 1 ||
            native.r[4] != input.r[18] || native.r[5] != source || native.r[6] != output ||
            native.r[31] != 0x800266a4)
            return false;
        for (unsigned k = 0; k < 9; ++k) {
            const unsigned index = i == 1 ? 0 : (i == 2 && k == 0 ? 2 : k);
            if (MEM_R16(output + 2 * k) != (u16)values[index])
                return false;
        }
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x80026634);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool scaled_part_walk(bool disc) {
    const u32 frame = SCRATCH_BASE, actor = frame + 256, part = frame + 768;
    const u32 matrix = frame + 64;
    const u32 words[] = {0x80001000, 0x7fff0000, 0x0001f000, 0xffff0800,
                         0x1000,     (u32)-17,   0,          32767};
    u8 before[SCRATCH_SIZE];
    for (unsigned path = 0; path < 3; ++path) {
        const bool resume = path == 2;
        CPUState input = initial();
        input.r[10] = part;
        input.r[17] = frame + 16;
        input.r[19] = resume ? 3 : 0;
        input.r[22] = 0;
        input.r[29] = frame;
        input.r[31] = 0x800269b4;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(frame + 0xb0, actor);
        MEM_W32(actor + 0x20, path == 0 ? part : part + 80);
        MEM_W8(part + 0x4c, 7);
        MEM_W16(actor + 0x10a + 14, 0xf5ff);
        MEM_W32(frame + 16, matrix);
        MEM_W32(frame + 20, part + 80);
        for (unsigned reg = 0; reg < 8; ++reg)
            MEM_W32(matrix + 4 * reg, words[reg]);
        memcpy(before, nc_scratch, sizeof before);
        bool (*run)(CPUState *) = resume ? nc_resume_scaled_part_walk : nc_begin_scaled_part_walk;
        input.budget = resume ? 42 : 31;
        CPUState native = input;
        if (run(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!run(&native) || native.budget != (path == 0 ? 12 : 1) ||
            native.r[22] != (path == 1 ? 0x1500u : 0) || native.r[18] != frame + (resume ? 16 : 0))
            return false;
        if (disc) {
            interp_call(&input, resume ? 0x8002695c : 0x80026908);
            if (memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool scaled_part_positions(bool disc) {
    const u32 frame = SCRATCH_BASE, matrix = frame + 512;
    const s16 points[][3] = {{-32768, 0, 32767}, {0, -1, 0}, {32767, -32768, 0}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof points / sizeof points[0]; ++i) {
        CPUState input = initial();
        const u32 part = i == 2 ? frame + 0xa8 - 0x2c : frame + 320;
        input.r[5] = part;
        input.r[6] = matrix;
        input.r[29] = frame + 240;
        input.r[31] = 0x80026908;
        memset(nc_scratch, 0xa5, sizeof before);
        for (unsigned k = 0; k < 9; ++k)
            MEM_W16(matrix + 2 * k, k % 4 == 0 ? 4096 : 0);
        for (unsigned axis = 0; axis < 3; ++axis) {
            MEM_W16(part + 0x10 + 2 * axis, points[i][axis]);
            MEM_W32(matrix + 20 + 4 * axis, axis - 1);
        }
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 82;
        CPUState native = input;
        if (nc_start_scaled_part_position(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_start_scaled_part_position(&native) || native.budget != 1)
            return false;
        for (unsigned axis = 0; axis < 3; ++axis) {
            u32 value = points[i][axis] + axis - 1;
            if (i == 2 && axis == 0)
                value = native.r[12];
            if (MEM_R32(part + 0x2c + 4 * axis) != value * 4)
                return false;
        }
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((0x80026828u >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bd00f0); /* enter with the caller's stack */
            input.r[29] = frame;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool part_offsets(bool disc) {
    const u32 frame = SCRATCH_BASE, part = frame + 512;
    const s16 points[][3] = {{-32768, 0, 0}, {0, 32767, 0}, {0, 0, -1}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof points / sizeof points[0]; ++i) {
        CPUState input = initial();
        input.r[16] = i;
        input.r[29] = frame;
        input.r[31] = 0x80025fe4;
        for (unsigned reg = 0; reg < 5; ++reg)
            gte_write_ctrl(&input, reg, (reg % 2 == 0) ? 4096 : 0);
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(frame + 0x1c + 8 * i, part);
        for (unsigned axis = 0; axis < 3; ++axis) {
            gte_write_ctrl(&input, 5 + axis, axis + 1);
            MEM_W16(part + 0x10 + 2 * axis, points[i][axis]);
        }
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 26;
        CPUState native = input;
        if (nc_transform_part_offset(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_transform_part_offset(&native) || native.budget != 1 || native.r[4] != part ||
            native.r[5] != input.r[18] || native.r[31] != 0x80034480)
            return false;
        for (unsigned axis = 0; axis < 3; ++axis)
            if (MEM_R32(frame + 0xac + 4 * axis) != (u32)points[i][axis] + axis + 1)
                return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x80034430);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool part_matrix_composition(bool disc) {
    const u32 frame = SCRATCH_BASE, parent = frame + 512;
    const s16 rotations[][9] = {{4096, 0, 0, 0, 4096, 0, 0, 0, 4096},
                                {0, 0, -4096, 0, 4096, 0, 4096, 0, 0}};
    const s16 part[] = {-32768, 0, 32767, 0, 4096, 0, -1, 0, 1};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < 2; ++i)
        for (unsigned overlap = 0; overlap < 2; ++overlap) {
            CPUState input = initial();
            input.r[16] = i;
            input.r[17] = parent;
            input.r[18] = frame + 0xb8;
            input.r[19] = overlap ? frame + 0xbc : frame + 0x98;
            input.r[29] = frame;
            input.r[31] = i ? 0x80034430 : 0x800345bc;
            memset(nc_scratch, 0xa5, sizeof before);
            for (unsigned k = 0; k < 9; ++k) {
                MEM_W16(parent + 2 * k, rotations[i][k]);
                MEM_W16(frame + 0xb8 + 2 * k, part[k]);
            }
            memcpy(before, nc_scratch, sizeof before);
            input.budget = i ? 122 : 123;
            CPUState native = input;
            if (nc_compose_part_matrix(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_compose_part_matrix(&native) || native.budget != 1 ||
                native.r[17] != input.r[19] || native.r[16] != 0)
                return false;
            if (!i && !overlap)
                for (unsigned k = 0; k < 9; ++k)
                    if (MEM_R16(frame + 0x98 + 2 * k) != (u16)part[k])
                        return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80034480);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool part_translation(bool disc) {
    const u32 matrix = SCRATCH_BASE, frame = SCRATCH_BASE + 256;
    const u32 outputs[] = {SCRATCH_BASE + 64, matrix + 0x18, frame + 0xe0};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof outputs / sizeof outputs[0]; ++i) {
        CPUState input = initial();
        input.r[17] = matrix;
        input.r[20] = outputs[i];
        input.r[29] = frame;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(matrix + 0x14, -5);
        MEM_W32(matrix + 0x18, 0);
        MEM_W32(matrix + 0x1c, INT32_MIN);
        MEM_W32(frame + 0xf4, input.r[31]);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 39;
        CPUState native = input;
        if (nc_finish_part_translation(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_finish_part_translation(&native) || native.budget != 1 ||
            MEM_R32(outputs[i]) != (u32)-2 || MEM_R32(outputs[i] + 4) != (i == 1 ? (u32)-1 : 0) ||
            MEM_R32(outputs[i] + 8) != (i == 1 ? (u32)-1 : (u32)(INT32_MIN / 4)))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            /* Enter with caller sp so the oracle recognizes the return. */
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((0x800345bcu >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bdff08); /* addiu sp, sp, -248 */
            input.r[29] += 248;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool heading_matrix_setup(bool disc) {
    const s16 rotations[][9] = {{4096, 0, 0, 0, 4096, 0, 0, 0, 4096},
                                {0, 0, -4096, 0, 4096, 0, 4096, 0, 0},
                                {32767, -32768, 32767, -32768, 32767, -32768, 0, 1, -1}};
    const s16 values[] = {4096, -32768, 32767, 1, -1, 0, 2048, -2048, 4096};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof rotations / sizeof rotations[0]; ++i)
        for (unsigned alias = 0; alias < 2; ++alias) {
            CPUState input = initial();
            input.r[29] = SCRATCH_BASE + 512;
            const u32 frame = input.r[29] - 88;
            input.r[4] = alias ? frame + 14 : SCRATCH_BASE;
            input.r[5] = SCRATCH_BASE + 704;
            input.r[31] = 0x8007a1f0; /* stop at ScaleMatrixL */
            memset(nc_scratch, 0xa5, sizeof before);
            for (unsigned k = 0; k < 9; ++k) {
                MEM_W16(0x800d7688 + 2 * k, rotations[i][k]);
                MEM_W16(input.r[4] + 2 * k, values[k]);
            }
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 119;
            CPUState native = input;
            if (nc_prepare_heading_matrix(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_prepare_heading_matrix(&native) || native.budget != 1 ||
                native.r[4] != frame + 16 || native.r[5] != frame + 48 ||
                native.r[19] != input.r[5] || native.r[31] != 0x8003475c)
                return false;
            if (!i && !alias)
                for (unsigned k = 0; k < 9; ++k)
                    if (MEM_R16(frame + 16 + 2 * k) != (u16)values[k])
                        return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                /* Match the oracle's stack at the nested call. */
                const u32 stub = 0x80120000;
                MEM_W32(stub, 0x08000000u | ((0x80034610u >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, 0x27bd0058); /* addiu sp, sp, 88 */
                input.r[29] = frame;
                input.budget += 2;
                interp_call(&input, stub);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool matrix_planar_lengths(bool disc) {
    static const struct {
        s16 y, z;
        s32 root_input;
    } cases[] = {
        {0, 0, 0},
        {63, -1, 0},
        {-64, 0, 1},
        {0, 4096, 4096},
        {4096, -4096, 8192},
        {-32768, 32767, 524272},
        {-32768, -32768, -524288},
    };
    CPUState input = initial(), native;
    input.r[29] = SCRATCH_BASE + 64;
    input.r[31] = 0x800475e4; /* stop at the root call */
    u8 before[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        MEM_W16(input.r[29] + 0x18, cases[i].y);
        MEM_W16(input.r[29] + 0x1a, cases[i].z);
        memcpy(before, nc_scratch, sizeof before);
        if (!compare(input, 0x8003475c, nc_prepare_matrix_planar_length, disc, &native) ||
            native.r[4] != (u32)cases[i].root_input || native.r[31] != 0x80034788 ||
            native.budget != input.budget - 15 || memcmp(before, nc_scratch, sizeof before))
            return false;
    }
    return true;
}

static bool matrix_headings(bool disc) {
    const s16 matrices[][9] = {
        {4096, 0, 0, 0, 4096, 0, 0, 0, 4096},
        {4096, 0, 0, -4096, 0, 0, 0, 4096, 0},
        {-32768, 0, 0, 32767, -1, -32768, 32767, -32768, -1},
    };
    const u32 lengths[] = {0, 1, 64, 0x7fffffff};
    const u32 frame = SCRATCH_BASE + 128;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof matrices / sizeof matrices[0]; ++i)
        for (unsigned j = 0; j < sizeof lengths / sizeof lengths[0]; ++j) {
            CPUState input = initial();
            input.r[2] = lengths[j];
            input.r[29] = frame;
            /* Include output overlapping the saved registers. */
            input.r[19] = j == 2 ? frame + 0x40 : SCRATCH_BASE;
            memset(nc_scratch, 0xa5, sizeof before);
            for (unsigned k = 0; k < 9; ++k)
                MEM_W16(frame + 16 + k * 2, matrices[i][k]);
            MEM_W32(frame + 0x50, input.r[31]);
            memcpy(before, nc_scratch, sizeof before);
            input.budget = lengths[j] ? 155 : 111;
            CPUState native = input;
            if (nc_finish_matrix_headings(&native) || memcmp(&input, &native, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_finish_matrix_headings(&native) || native.r[29] != frame + 88 ||
                native.r[31] != input.r[31] || native.budget <= 0 ||
                (!lengths[j] && MEM_R16(input.r[19] + 2) != 0))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                /* Enter with the caller's stack pointer for interp_call's return check. */
                const u32 stub = 0x80120000;
                MEM_W32(stub, 0x08000000u | ((0x80034788u >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, 0x27bdffa8); /* addiu sp, sp, -88 */
                input.r[29] += 88;
                input.budget += 2;
                interp_call(&input, stub);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    CPUState exceptional = initial();
    exceptional.r[2] = 0x80000000;
    CPUState unchanged = exceptional;
    return !nc_finish_matrix_headings(&exceptional) &&
           !memcmp(&exceptional, &unchanged, sizeof unchanged);
}

static bool facing_offsets(bool disc) {
    const u32 record = SCRATCH_BASE + 0x80;
    const u32 angles[] = {0, 0x3fff, 0x4000, 0x8000, 0xc000, 0xffff};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i)
        for (unsigned alias = 0; alias < 2; ++alias) {
            CPUState input = initial();
            input.r[16] = record;
            input.r[17] = angles[i];
            input.r[29] = SCRATCH_BASE + 0x300;
            input.r[31] = 0x80047128; /* compare up to entry into the projection call */
            const u32 actor = alias ? record + 0x150 - 0x284 : 0x80120000;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(record + 0x160, actor);
            MEM_W32(record + 0x130, (u32)-257);
            MEM_W32(record + 0x128, 0xffff0123);
            MEM_W32(actor + 0x27c, (u32)-769);
            MEM_W32(actor + 0x284, 1023);
            memcpy(before, nc_scratch, sizeof before);
            const u32 offset = (angles[i] >> 4) * 2;
            const s32 x = -4 + (((s16)MEM_R16(0x80083be4 + offset) * 130) >> 14);
            const s32 z = (alias ? x >> 8 : 3) + (((s16)MEM_R16(0x80081be4 + offset) * 130) >> 14);
            input.budget = 70;
            CPUState native = input;
            if (nc_prepare_facing_offset(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_prepare_facing_offset(&native) || native.budget != 1 ||
                native.r[31] != 0x80047518 || native.r[4] != (u32)-257 ||
                native.r[5] != 0xfff01230u - angles[i] || native.r[6] != input.r[29] + 16 ||
                native.r[7] != (u32)z || MEM_R32(record + 0x150) != ((u32)x << 8) ||
                MEM_R32(record + 0x158) != ((u32)z << 8))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80047488);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool saved_bearing_projections(bool disc) {
    const s16 bearings[] = {-32768, -1, 0, 1024, 2048, 3072, 4095, 4096, 32767};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof bearings / sizeof bearings[0]; ++i) {
        CPUState input = initial();
        input.r[16] = SCRATCH_BASE;
        input.r[6] = SCRATCH_BASE + 24;
        input.r[31] = 0x80047128; /* stop the interpreter at the projection call */
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W16(SCRATCH_BASE + 10, bearings[i]);
        MEM_W32(SCRATCH_BASE + 0x130, (u32)-257);
        MEM_W16(SCRATCH_BASE + 0x16c, 0xabd0 | (i & 1));
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 15;
        CPUState native = input;
        if (nc_prepare_saved_bearing_projection(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_prepare_saved_bearing_projection(&native) || native.budget != 1 ||
            native.r[4] != (u32)-257 || native.r[5] != (u32)(4096 - bearings[i]) * 16 ||
            native.r[31] != 0x800475cc || MEM_R16(SCRATCH_BASE + 0x16c) != 0xabd0)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x800475a8);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    CPUState input = initial();
    input.r[29] = SCRATCH_BASE + 512;
    MEM_W32(input.r[29] + 32, input.r[31]);
    MEM_W32(input.r[29] + 28, 0x12345678);
    MEM_W32(input.r[29] + 24, 0x87654321);
    input.budget = 12;
    CPUState native = input;
    if (nc_finish_projected_point_update(&native) || memcmp(&native, &input, sizeof input))
        return false;
    native.budget = ++input.budget;
    if (!nc_finish_projected_point_update(&native) || native.budget != 1 ||
        native.r[16] != 0x87654321 || native.r[17] != 0x12345678 ||
        native.r[29] != input.r[29] + 40)
        return false;
    if (disc) {
        /* Enter with the caller's stack so interp_call recognizes the return.
         * The jump and its stack-allocation delay slot cost two cycles. */
        const u32 stub = 0x80120000;
        MEM_W32(stub, 0x08000000u | ((0x800475ccu >> 2) & 0x03ffffffu));
        MEM_W32(stub + 4, 0x27bdffd8); /* addiu sp, sp, -40 */
        input.r[29] += 40;
        input.budget += 2;
        interp_call(&input, stub);
        if (memcmp(&native, &input, sizeof input))
            return false;
    }
    return true;
}

static bool projected_heading_selection(bool disc) {
    static const struct {
        s32 gap;
        u16 flags;
        bool reproject;
        int cycles;
    } cases[] = {
        {569, 0xa500, false, 9},  {4095, 0xa501, false, 9}, {568, 0xa500, true, 10},
        {568, 0xa501, false, 13}, {456, 0xa501, false, 13}, {455, 0xa501, true, 11},
        {-1, 0xa501, true, 11},   {0, 0xa500, true, 10},
    };
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[2] = cases[i].gap < 569;
        input.r[3] = cases[i].gap;
        input.r[16] = SCRATCH_BASE;
        input.r[17] = 0xffff8123;
        input.r[31] = cases[i].reproject ? 0x800475a8 : 0x800475cc;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W16(SCRATCH_BASE + 0x16c, cases[i].flags);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 13;
        CPUState native = input;
        bool reproject = !cases[i].reproject;
        if (nc_select_projected_heading(&native, &reproject) ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_select_projected_heading(&native, &reproject) || reproject != cases[i].reproject ||
            native.budget != input.budget - cases[i].cycles ||
            MEM_R16(SCRATCH_BASE + 0x12) != (reproject ? 0xa5a5 : 0x8123) ||
            MEM_R16(SCRATCH_BASE + 0x16c) != (cases[i].flags | (cases[i].gap >= 569)))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x80047564);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool projected_heading_gaps(bool disc) {
    static const struct {
        s16 previous, projected;
        s32 gap, cycles;
    } cases[] = {
        {0, 0, 0, 12},
        {0, 568, 568, 11},
        {569, 0, 569, 12},
        {0, 2048, 2048, 11},
        {2048, 0, 2048, 12},
        {0, 2049, 2047, 15},
        {4095, 0, 1, 15},
        {-1, 4095, 0, 15},
        {4096, 0, 0, 15},
        {-32768, 32767, -61439, 15},
        {32767, -32768, -61439, 15},
    };
    u8 before[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[16] = SCRATCH_BASE;
        input.r[2] = 0xdead0000u | (u16)cases[i].projected;
        input.r[31] = 0x80047564;
        MEM_W16(SCRATCH_BASE + 10, cases[i].previous);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 15;
        CPUState native = input;
        if (nc_projected_heading_gap(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_projected_heading_gap(&native) || native.r[2] != (u32)(cases[i].gap < 569) ||
            native.r[3] != (u32)cases[i].gap || native.r[4] != (u32)(s32)cases[i].previous ||
            native.r[17] != (u32)(s32)cases[i].projected ||
            native.budget != input.budget - cases[i].cycles ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        if (disc) {
            interp_call(&input, 0x80047518);
            if (memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool relative_headings(bool disc) {
    const u32 actor = SCRATCH_BASE, pose = actor + 128, linked = actor + 256;
    const u32 point = actor + 384;
    const s16 points[][2] = {{-4, -3}, {-5, -3}, {-3, -3}, {-4, -4}, {-4, -2}, {-32768, 32767}};
    const u32 offsets[] = {0x2000, 0x1fff, 0x2010, 0x80000000, 0xfffffff0};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof points / sizeof points[0]; ++i)
        for (unsigned j = 0; j < sizeof offsets / sizeof offsets[0]; ++j) {
            CPUState input = initial();
            input.r[4] = point;
            input.r[29] = SCRATCH_BASE + 768;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(0x800D7790, actor);
            MEM_W32(0x800D7780, (u32)-769);
            MEM_W32(0x800D7788, 1023);
            MEM_W32(actor + 0x20, pose);
            MEM_W32(pose + 4, linked);
            MEM_W32(pose + 0x48, 0xfffffff0);
            MEM_W32(linked + 0x40, offsets[j] + 16);
            MEM_W16(point, points[i][0]);
            MEM_W16(point + 4, points[i][1]);
            memcpy(before, nc_scratch, sizeof before);
            CPUState cutoff = input;
            cutoff.budget = 16;
            CPUState native = cutoff;
            if (nc_prepare_relative_heading(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget++;
            if (!nc_prepare_relative_heading(&native) || native.budget != 1 ||
                native.r[2] != pose || native.r[16] != linked || native.r[31] != 0x80047328)
                return false;
            native.budget = input.budget - 16;
            if (!nc_prepare_point_heading(&native) || !nc_vector_heading(&native) ||
                !nc_return_point_heading(&native))
                return false;
            u32 relative = ((offsets[j] >> 4) - native.r[2]) & 4095;
            cutoff = native;
            cutoff.budget = 33;
            CPUState finish = cutoff;
            memcpy(after, nc_scratch, sizeof after);
            if (nc_finish_relative_heading(&finish) || memcmp(&finish, &cutoff, sizeof cutoff) ||
                memcmp(after, nc_scratch, sizeof after))
                return false;
            finish.budget++;
            if (!nc_finish_relative_heading(&finish) || finish.budget != 1 ||
                !nc_finish_relative_heading(&native) ||
                native.r[2] != (relative ? relative : 4096) || native.r[4] != offsets[j] + 16 ||
                native.r[31] != input.r[31] || native.r[16] != input.r[16] ||
                native.r[29] != input.r[29])
                return false;
            if (disc) {
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80047300);
                if (memcmp(&input, &native, sizeof input) ||
                    memcmp(nc_scratch, after, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool reference_distances(bool disc) {
    static const struct {
        s32 x, z;
        s16 ref_x, ref_z;
        s32 dx, dz;
    } cases[] = {
        {-1, -1, -1, 1, 0, 0},
        {-257, 255, -5, 4, 3, 4},
        {-257, 255, -2, -1, 0, -1},
        {255, -257, 1, 2, -1, 0},
        {0x800000, 0x800100, -32768, 32767, 65536, 65536},
        {0, -0x800100, -32768, -32768, 32768, -65537},
    };
    const u32 sentinel = 0xa55a5aa5;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[29] = SCRATCH_BASE + 128;
        MEM_W32(0x800D7780, cases[i].x);
        MEM_W32(0x800D7788, cases[i].z);
        MEM_W16(0x800D7648, cases[i].ref_x);
        MEM_W16(0x800D764C, cases[i].ref_z);
        MEM_W32(0x800D7760, sentinel);
        MEM_W32(0x800D7764, sentinel);
        memset(nc_scratch, 0xa5, sizeof before);
        memcpy(before, nc_scratch, sizeof before);
        CPUState cutoff = input;
        cutoff.budget = 28;
        CPUState native = cutoff;
        if (nc_prepare_reference_distance(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget++;
        u32 dx = (u32)cases[i].dx, dz = (u32)cases[i].dz;
        if (!nc_prepare_reference_distance(&native) || native.budget != 1 || native.r[2] != dz ||
            native.r[3] != dz * dz || native.r[4] != dx * dx + dz * dz ||
            native.r[31] != 0x800472b8 || MEM_R32(input.r[29] - 8) != input.r[31])
            return false;
        native.budget = input.budget - 28;
        if (disc)
            interp_call(&native, 0x8007a050);
        else
            native.r[2] = i;
        cutoff = native;
        cutoff.budget = 10;
        CPUState finish = cutoff;
        if (nc_store_reference_distance(&finish) || memcmp(&finish, &cutoff, sizeof cutoff) ||
            MEM_R32(0x800D7760) != sentinel || MEM_R32(0x800D7764) != sentinel)
            return false;
        finish.budget++;
        if (!nc_store_reference_distance(&finish) || finish.budget != 1 ||
            !nc_store_reference_distance(&native) || native.r[31] != input.r[31] ||
            native.r[29] != input.r[29] || MEM_R32(0x800D7760) != native.r[2] ||
            MEM_R32(0x800D7764) != native.r[2])
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            MEM_W32(0x800D7760, sentinel);
            MEM_W32(0x800D7764, sentinel);
            interp_call(&input, 0x80047268);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after) ||
                MEM_R32(0x800D7760) != native.r[2] || MEM_R32(0x800D7764) != native.r[2])
                return false;
        }
    }
    return true;
}

static bool point_headings(bool disc, bool local) {
    const int cycles = local ? 19 : 21;
    const u32 entry = local ? 0x800471e0 : 0x80047220;
    const u32 resume = local ? 0x8004720c : 0x80047254;
    bool (*prepare)(CPUState *) = local ? nc_prepare_local_point_heading : nc_prepare_point_heading;
    const s16 points[][2] = {{-4, -3}, {-4, -4},        {-5, -3},       {-3, -3},
                             {-4, -2}, {-32768, 32767}, {32767, -32768}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof points / sizeof points[0]; ++i) {
        CPUState input = initial();
        input.r[29] = SCRATCH_BASE + 128;
        /* Last point overlaps the return-address save. */
        u32 save = input.r[29] - 8;
        u32 point = i + 1 == sizeof points / sizeof points[0] ? save : SCRATCH_BASE;
        input.r[4] = point;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(0x800D7780, (u32)-769); /* -4 after arithmetic shift */
        MEM_W32(0x800D7788, 1023);      /* +3 */
        MEM_W16(0x800D7630, (u16)-4);
        MEM_W16(0x800D7634, (u16)-3);
        MEM_W16(point, points[i][0]);
        MEM_W16(point + 4, points[i][1]);
        memcpy(before, nc_scratch, sizeof before);
        CPUState cutoff = input;
        cutoff.budget = cycles;
        CPUState native = cutoff;
        if (prepare(&native) || memcmp(&native, &cutoff, sizeof native) ||
            memcmp(nc_scratch, before, sizeof before))
            return false;
        native = input;
        native.budget = cycles + 1;
        s32 x = point == save ? (s16)input.r[31] : points[i][0];
        s32 dx = local ? x + 4 : -4 - x;
        s32 v1 = local ? -3 : x;
        s32 a2 = local ? x : points[i][1];
        if (!prepare(&native) || native.budget != 1 || native.r[4] != (u32)dx ||
            native.r[5] != (u32)(3 + points[i][1]) || native.r[3] != (u32)v1 ||
            native.r[6] != (u32)a2 || native.r[2] != (u32)-4 || native.r[31] != resume ||
            native.r[29] != input.r[29] - 24 || MEM_R32(save) != input.r[31])
            return false;
        native.budget = input.budget - cycles;
        if (!nc_vector_heading(&native))
            return false;
        cutoff = native;
        cutoff.budget = 7;
        CPUState epilogue = cutoff;
        memcpy(after, nc_scratch, sizeof after);
        if (nc_return_point_heading(&epilogue) || memcmp(&cutoff, &epilogue, sizeof cutoff) ||
            memcmp(nc_scratch, after, sizeof after))
            return false;
        epilogue.budget++;
        epilogue.r[2] = 0x12348001;
        if (!nc_return_point_heading(&epilogue) || epilogue.budget != 1 ||
            epilogue.r[2] != 0xffff8001 || !nc_return_point_heading(&native) ||
            native.r[31] != input.r[31] || native.r[29] != input.r[29])
            return false;
        if (disc) {
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, entry);
            if (memcmp(&input, &native, sizeof input) || memcmp(nc_scratch, after, sizeof after))
                return false;
        }
    }
    return true;
}

static bool part_positions(bool disc) {
    const u32 matrices[][5] = {
        {0x1000, 0, 0x1000, 0, 0x1000}, /* identity */
        {0x10000000, 0xf0000000, 0, 0, 0x1000},
        {0xf8ab1234, 0x7fff4321, 0x8000fffb, 0x2468aaa0, 0xabcd1357},
    };
    const u32 coords[] = {0, 1, 0xfffffff7, 0x7fff, 0xffff8000, 0x7fffffff, 0x80000000};
    const unsigned count = sizeof coords / sizeof coords[0];
    u8 original[SCRATCH_SIZE], expected[SCRATCH_SIZE];
    for (unsigned m = 0; m < sizeof matrices / sizeof matrices[0]; ++m) {
        for (unsigned i = 0; i < 5; ++i)
            MEM_W32(0x800d7688 + i * 4, matrices[m][i]);
        for (unsigned axis = 0; axis < 3; ++axis)
            MEM_W16(0x800d7630 + axis * 2, 0xfffd + axis);
        for (unsigned quarter = 0; quarter < 2; ++quarter)
            for (unsigned sample = 0; sample < count; ++sample)
                for (unsigned alias = 0; alias < 2; ++alias) {
                    CPUState c = initial();
                    c.r[4] = SCRATCH_BASE + 0x40;
                    c.r[29] = SCRATCH_BASE + 0x180;
                    c.r[5] = alias ? c.r[29] - 12 : SCRATCH_BASE + 0x240;
                    memset(nc_scratch, 0xa5, SCRATCH_SIZE);
                    for (unsigned axis = 0; axis < 3; ++axis)
                        MEM_W32(c.r[4] + 0x2c + 4 * axis, coords[(sample + axis) % count]);
                    memcpy(original, nc_scratch, SCRATCH_SIZE);
                    c.budget = quarter ? 85 : 82;
                    CPUState native = c;
                    if (nc_transform_part_position(&native, quarter) ||
                        memcmp(&native, &c, sizeof c) || memcmp(original, nc_scratch, SCRATCH_SIZE))
                        return false;
                    native.budget = ++c.budget;
                    if (!nc_transform_part_position(&native, quarter) || native.budget != 1)
                        return false;
                    if (!m && !alias) {
                        for (unsigned axis = 0; axis < 3; ++axis) {
                            s32 v = (s32)coords[(sample + axis) % count];
                            s16 narrowed = (s16)(quarter ? v >> 2 : v);
                            if (MEM_R16(c.r[5] + axis * 2) !=
                                (u16)((narrowed >> 1) - (0xfffd + axis)))
                                return false;
                        }
                    }
                    if (disc) {
                        memcpy(expected, nc_scratch, SCRATCH_SIZE);
                        memcpy(nc_scratch, original, SCRATCH_SIZE);
                        interp_call(&c, quarter ? 0x80034828 : 0x800348fc);
                        if (memcmp(&c, &native, sizeof c) ||
                            memcmp(expected, nc_scratch, SCRATCH_SIZE)) {
                            fprintf(stderr,
                                    "part transform mismatch: matrix %u, scale %u, sample %u, "
                                    "alias %u\n",
                                    m, quarter, sample, alias);
                            return false;
                        }
                    }
                }
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc > 2)
        return 1;
    bool disc = argc == 2;
    if (disc) {
        FILE *f = fopen(argv[1], "rb");
        if (!f)
            return 77;
        fclose(f);
        ExecInfo exe;
        if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX2.EXE;1", &exe))
            return 1;
    }
    bool ok =
        roots(disc) && displacements(disc) && headings(disc) && heading_samples(disc) &&
        projected_points(disc) && part_positions(disc) && actor_coordinates(disc) &&
        point_headings(disc, false) && point_headings(disc, true) && actor_headings(disc) &&
        heading_projections(disc) && reference_distances(disc) && relative_headings(disc) &&
        record_projections(disc) && projected_heading_gaps(disc) &&
        projected_heading_selection(disc) && saved_bearing_projections(disc) &&
        facing_offsets(disc) && projection_entries(disc) && rotated_point_offsets(disc) &&
        rotated_vector_setup(disc) && placed_actor_points(disc) && actor_point_placements(disc) &&
        matrix_headings(disc) && matrix_planar_lengths(disc) && heading_matrix_setup(disc) &&
        part_translation(disc) && part_transform_calls(disc) && node_transform_selection(disc) &&
        node_matrix_installation(disc) && part_composition(disc) &&
        node_continuations(disc, false) && node_continuations(disc, true) && node_rotations(disc) &&
        scaled_part_walk(disc) && scaled_part_positions(disc) && part_offsets(disc) &&
        part_matrix_composition(disc) && part_transform_setup(disc) && part_chain_traversal(disc);
    for (NcRotationInput variant = NC_ROTATION_PART; ok && variant <= NC_ROTATION_PACKED; ++variant)
        ok = part_rotation_samples(disc, variant) && part_rotation_matrices(disc, variant);
    if (disc)
        disc_close();
    return ok ? 0 : 1;
}
