#include "credits_stubs.h"
#include "decomp/credits.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include <stdio.h>

typedef struct Call {
    u32 function, saved_ra;
    CPUState cpu;
} Call;
static Call calls[5];
static unsigned count;

static void capture(CPUState *c, u32 address) {
    if (count < sizeof calls / sizeof calls[0]) {
        calls[count] = (Call){address, MEM_R32(R(29) + 0x10), *c};
    }
    count++;
    /* Make the next call observe distinct return and argument registers. */
    for (unsigned i = 2; i <= 7; i++)
        R(i) = address + i;
}

#define PAD_STUB(name, addr)                                                                       \
    static void name(CPUState *c) {                                                                \
        capture(c, addr);                                                                          \
    }
PAD_STUB(read_input, 0x80018c8c)
PAD_STUB(stop_pad, 0x8001a28c)
PAD_STUB(change_clear_pad, 0x8001a2ac)
PAD_STUB(init_pad, 0x8001a324)
PAD_STUB(start_pad, 0x8001a38c)
PAD_STUB(vsync, 0x8001a454)
const NcFuncEntry credits_exe_table[] = {{0x80018c8c, read_input},       {0x8001a28c, stop_pad},
                                         {0x8001a2ac, change_clear_pad}, {0x8001a324, init_pad},
                                         {0x8001a38c, start_pad},        {0x8001a454, vsync}};
const unsigned credits_exe_table_len = sizeof credits_exe_table / sizeof credits_exe_table[0];

static u8 before[RAM_SIZE], after[RAM_SIZE];
static bool compare(u32 entry, bool (*routine)(CPUState *), int threshold,
                    unsigned expected_calls) {
    count = 0;
    memset(calls, 0, sizeof calls);
    CPUState initial = {0};
    for (unsigned i = 1; i < 32; i++)
        initial.r[i] = i * 0x13579u;
    initial.r[29] = 0x801fef00;
    initial.r[31] = 0x80010000;
    initial.budget = threshold;
    memcpy(before, nc_ram, sizeof before);
    CPUState native = initial;
    if (routine(&native) || memcmp(&initial, &native, sizeof native) ||
        memcmp(before, nc_ram, sizeof before))
        return false;
    native.budget = initial.budget = 4096;
    if (!routine(&native) || count != expected_calls)
        return false;
    Call expected[5];
    memcpy(expected, calls, sizeof expected);
    memcpy(after, nc_ram, sizeof after);
    memcpy(nc_ram, before, sizeof before);
    count = 0;
    memset(calls, 0, sizeof calls);
    CPUState guest = initial;
    interp_call(&guest, entry);
    return count == expected_calls && !memcmp(expected, calls, sizeof expected) &&
           !memcmp(&native, &guest, sizeof guest) && !memcmp(nc_ram, after, sizeof after);
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
    bool ok = compare(0x80018cfc, nc_credits_init_pad, 8, 5) &&
              compare(0x80018d4c, nc_credits_stop_pad, 4, 1);
    disc_close();
    return ok ? 0 : 1;
}
