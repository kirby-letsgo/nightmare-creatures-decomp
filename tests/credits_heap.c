#include "decomp/credits.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include <stdio.h>

static u8 initial_ram[RAM_SIZE], native_ram[RAM_SIZE];
static u8 initial_scratch[SCRATCH_SIZE], native_scratch[SCRATCH_SIZE];

static bool compare(CPUState start, u32 entry, bool (*routine)(CPUState *), int threshold) {
    memcpy(initial_ram, nc_ram, sizeof initial_ram);
    memcpy(initial_scratch, nc_scratch, sizeof initial_scratch);
    start.budget = threshold;
    CPUState native = start;
    if (routine(&native) || memcmp(&start, &native, sizeof start) ||
        memcmp(initial_ram, nc_ram, sizeof initial_ram) ||
        memcmp(initial_scratch, nc_scratch, sizeof initial_scratch))
        return false;
    start.budget++;
    native = start;
    if (!routine(&native))
        return false;
    memcpy(native_ram, nc_ram, sizeof native_ram);
    memcpy(native_scratch, nc_scratch, sizeof native_scratch);
    memcpy(nc_ram, initial_ram, sizeof initial_ram);
    memcpy(nc_scratch, initial_scratch, sizeof initial_scratch);
    CPUState guest = start;
    interp_call(&guest, entry);
    return !memcmp(&guest, &native, sizeof guest) &&
           !memcmp(native_ram, nc_ram, sizeof native_ram) &&
           !memcmp(native_scratch, nc_scratch, sizeof native_scratch);
}

static bool check_init(u32 base, u32 size, u32 globals) {
    CPUState start = {0};
    for (unsigned i = 1; i < 32; i++)
        start.r[i] = i * 0x12345u;
    start.r[4] = base;
    start.r[5] = size;
    start.r[28] = globals;
    start.r[31] = 0x80010000;
    memset(nc_fast_ptr(base & ~3u), 0xc5, 0x40);
    memset(nc_fast_ptr(globals), 0x5c, 0x40);
    return compare(start, 0x80019454, nc_credits_init_heap, 18);
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 77;
    fclose(file);
    ExecInfo exe;
    if (!disc_open(argv[1]) || !exe_load("cdrom:\\CREDITS.EXE;1", &exe))
        return 1;
    const u32 sizes[] = {0, 0x1b, 0x1c, 0x1000, 0xffffffff};
    for (unsigned i = 0; i < 4; i++)
        for (unsigned j = 0; j < sizeof sizes / sizeof sizes[0]; j++)
            if (!check_init(0x80048000 + i, sizes[j], 0x80024d00) ||
                !check_init(SCRATCH_BASE + i, sizes[j], SCRATCH_BASE) ||
                !check_init(0xa0048000 + i, sizes[j], 0x80048010)) {
                fprintf(stderr, "heap init mismatch for alignment %u and size %08x\n", i, sizes[j]);
                return 1;
            }
    disc_close();
    return 0;
}
