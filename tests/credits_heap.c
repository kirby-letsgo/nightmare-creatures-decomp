#include "decomp/credits.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include "port/runtime.h"
#include "port/savestate.h"
#include <stdio.h>
#include <stdlib.h>

/* CPU serialization includes all registers, RAM, scratchpad, the scheduler's
 * budget and global clock. Restore the same clock before interpreting. */
static bool compare(CPUState start, u32 entry, bool (*routine)(CPUState *), int threshold) {
    StateIO initial = {0}, native = {0}, guest = {0};
    nc_cpu = start;
    nc_cpu.budget = threshold;
    cpu_serialize(&initial);
    bool ok = !initial.error && !routine(&nc_cpu);
    cpu_serialize(&native);
    ok = ok && !native.error && initial.len == native.len &&
         !memcmp(initial.buf, native.buf, initial.len);
    native.len = 0;
    nc_cpu.budget = start.budget;
    ok = ok && routine(&nc_cpu);
    cpu_serialize(&native);

    initial.loading = true;
    cpu_serialize(&initial);
    nc_cpu.budget = start.budget;
    interp_call(&nc_cpu, entry);
    cpu_serialize(&guest);
    ok = ok && !initial.error && !guest.error && !native.error && native.len == guest.len &&
         !memcmp(native.buf, guest.buf, guest.len);
    free(initial.buf);
    free(native.buf);
    free(guest.buf);
    return ok;
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
    start.budget = 19;
    return compare(start, 0x80019454, nc_credits_init_heap, 18);
}

static bool check_lookup(unsigned count, unsigned match, int budget) {
    CPUState start = {0};
    for (unsigned i = 1; i < 32; i++)
        start.r[i] = i * 0x13579u;
    const u32 head = 0x80048000;
    start.r[28] = 0x80024d00;
    start.r[31] = 0x80010000;
    start.r[4] = match ? head + 0x40 * match + 0x1c : 0;
    start.budget = budget;
    MEM_W32(start.r[28] + 0x2c, head);
    for (unsigned i = 0; i < count; i++) {
        u32 record = head + i * 0x40;
        MEM_W32(record + 4, i ? record + 0x1c : 0);
        MEM_W32(record + 0x18, i + 1 == count ? 0 : record + 0x40);
    }
    return compare(start, 0x800198b0, nc_credits_find_allocation, 20);
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
    /* A miss traverses every link. Move the event through all instruction
     * boundaries on the second and subsequent iterations. */
    for (int budget = 21; budget <= 95; budget++)
        for (unsigned match = 0; match <= 5; match++)
            if (!check_lookup(5, match, budget)) {
                fprintf(stderr, "heap lookup mismatch for index %u and budget %d\n", match, budget);
                return 1;
            }
    disc_close();
    return 0;
}
