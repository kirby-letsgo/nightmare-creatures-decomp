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
    bool ok = roots(disc) && displacements(disc) && headings(disc) && heading_samples(disc) &&
              projected_points(disc) && part_positions(disc) && actor_coordinates(disc) &&
              point_headings(disc, false) && point_headings(disc, true) && actor_headings(disc) &&
              heading_projections(disc) && reference_distances(disc) && relative_headings(disc) &&
              record_projections(disc) && projected_heading_gaps(disc);
    if (disc)
        disc_close();
    return ok ? 0 : 1;
}
