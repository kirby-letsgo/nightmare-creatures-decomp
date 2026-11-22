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
              projected_points(disc);
    if (disc)
        disc_close();
    return ok ? 0 : 1;
}
