#include "credits_compare.h"
#include "decomp/credits.h"
#include "port/disc.h"
#include "port/exe.h"
#include <stdio.h>

static CPUState arguments(u32 destination, u32 source, u32 count, int budget) {
    CPUState state = {0};
    for (unsigned i = 1; i < 32; i++)
        state.r[i] = 0x468b7u * i;
    state.r[4] = destination;
    state.r[5] = source;
    state.r[6] = count;
    state.r[28] = 0x80024c68;
    state.r[29] = 0x801fef00;
    state.r[31] = 0x80010000;
    state.budget = budget;
    return state;
}

static bool check_copy(void) {
    const u32 base = 0x80060000;
    const u32 destinations[] = {0, base, base + 5, base + 9, base + 0x40, SCRATCH_BASE, 0xa0060008};
    const u32 counts[] = {0, 1, 4, 19, 0x80000000, 0xffffffff};
    for (unsigned i = 0; i < sizeof destinations / sizeof destinations[0]; i++) {
        for (unsigned j = 0; j < sizeof counts / sizeof counts[0]; j++) {
            for (unsigned byte = 0; byte < 128; byte++)
                MEM_W8(base + byte, (u8)(13 * byte + 3));
            CPUState state = arguments(destinations[i], base + 5, counts[j], 4096);
            if (!compare(state, 0x80019928, nc_credits_copy_bytes, 2))
                return false;
        }
    }
    for (int budget = 3; budget <= 65; budget++)
        if (!compare(arguments(base + 2, base, 7, budget), 0x80019928, nc_credits_copy_bytes, 2))
            return false;
    return true;
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
    bool pass = check_copy();
    disc_close();
    return pass ? 0 : 1;
}
