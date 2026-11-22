#include "credits_stubs.h"
#include "decomp/credits.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include <stdio.h>

/* The retail credits image has no CLUT. Supply both TIM layouts at the file
 * boundary and record the upload arguments. GsGetTimInfo still runs from the
 * disc; only file ownership and the GPU transfer are test doubles. */
enum { TIM = 0x80080000 };
typedef struct Transfer {
    CPUState cpu;
    u16 rect[4];
} Transfer;
typedef struct Trace {
    CPUState load, release;
    Transfer uploads[2];
    unsigned count;
} Trace;
static Trace trace;
static u8 before[RAM_SIZE], after[RAM_SIZE];

static void load_file(CPUState *c) {
    trace.load = *c;
    R(2) = TIM;
}
static void upload(CPUState *c) {
    if (trace.count < 2) {
        Transfer *t = &trace.uploads[trace.count];
        t->cpu = *c;
        for (unsigned i = 0; i < 4; i++)
            t->rect[i] = MEM_R16(R(4) + 2 * i);
    }
    trace.count++;
    R(2) = 0xdeadbeef;
}
static void release_file(CPUState *c) {
    trace.release = *c;
}

/* Only these three external calls resolve to native code in the oracle. */
const NcFuncEntry credits_exe_table[] = {
    {0x80018e28, load_file}, {0x800197e0, release_file}, {0x8001c5a4, upload}};
const unsigned credits_exe_table_len = 3;

static bool check(bool palette) {
    CPUState initial = {0};
    for (unsigned i = 1; i < 32; i++)
        initial.r[i] = i * 0x12345u;
    initial.r[29] = 0x801fef00;
    initial.r[31] = 0x80010000;
    initial.budget = 4096;
    MEM_W32(TIM, 0x10);
    MEM_W32(TIM + 4, palette ? 8 : 2);
    u32 block = TIM + 8;
    if (palette) {
        MEM_W32(block, 44);
        MEM_W32(block + 4, (99u << 16) | 32);
        MEM_W32(block + 8, (1u << 16) | 16);
        block += 44;
    }
    MEM_W32(block, 28);
    MEM_W32(block + 4, (6u << 16) | 5);
    MEM_W32(block + 8, (2u << 16) | 4);
    memset(nc_fast_ptr(initial.r[29] - 0x48), 0xa5, 0x48);
    memcpy(before, nc_ram, sizeof before);
    memset(&trace, 0, sizeof trace);
    CPUState native = initial;
    native.budget = 5;
    CPUState at_boundary = native;
    if (nc_credits_load_tim(&native) || memcmp(&native, &at_boundary, sizeof native) ||
        memcmp(before, nc_ram, sizeof before))
        return false;
    native = initial;
    if (!nc_credits_load_tim(&native) || trace.count != (palette ? 2u : 1u) ||
        trace.release.r[4] != TIM || trace.uploads[0].cpu.r[5] != block + 12 ||
        (palette && trace.uploads[1].cpu.r[5] != TIM + 20))
        return false;
    Trace expected = trace;
    memcpy(after, nc_ram, sizeof after);
    memcpy(nc_ram, before, sizeof before);
    memset(&trace, 0, sizeof trace);
    CPUState guest = initial;
    interp_call(&guest, 0x80018ee4);
    return !memcmp(&expected, &trace, sizeof trace) && !memcmp(&guest, &native, sizeof guest) &&
           !memcmp(after, nc_ram, sizeof after);
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
    bool ok = check(false) && check(true);
    disc_close();
    return ok ? 0 : 1;
}
