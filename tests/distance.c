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
    bool ok = roots(disc) && displacements(disc);
    if (disc)
        disc_close();
    return ok ? 0 : 1;
}
