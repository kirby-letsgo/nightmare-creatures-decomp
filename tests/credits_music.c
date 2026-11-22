#include "credits_stubs.h"
#include "decomp/credits.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include <stdio.h>

static CPUState at_play;
static unsigned play_calls;
static void play(CPUState *c) {
    at_play = *c;
    play_calls++;
    R(2) = 0xfedcba98;
}

const NcFuncEntry credits_exe_table[] = {{0x80022ccc, play}};
const unsigned credits_exe_table_len = 1;
static u8 before[RAM_SIZE], after[RAM_SIZE];

static bool check_play(u32 track) {
    CPUState start = {0};
    for (unsigned i = 1; i < 32; i++)
        start.r[i] = 0x1856du * i;
    start.r[4] = track;
    start.r[28] = 0x80024c68;
    start.r[29] = 0x801fef00;
    start.r[31] = 0x80010000;
    start.budget = 5;
    MEM_W32(0x80024c9c, 0x70000000);
    memcpy(before, nc_ram, sizeof before);
    CPUState native = start;
    if (nc_credits_play_track(&native) || memcmp(&start, &native, sizeof native) ||
        memcmp(before, nc_ram, sizeof before))
        return false;
    native.budget = start.budget = 4096;
    play_calls = 0;
    if (!nc_credits_play_track(&native) || play_calls != 1 ||
        at_play.r[4] != (track >= 11 && track <= 25 ? 1u : 2u) || at_play.r[5] != 0x80024c9c ||
        at_play.r[6] || MEM_R32(0x80024c9c) != track)
        return false;
    CPUState args = at_play;
    memcpy(after, nc_ram, sizeof after);
    memcpy(nc_ram, before, sizeof before);
    play_calls = 0;
    CPUState guest = start;
    interp_call(&guest, 0x80019364);
    return play_calls == 1 && !memcmp(&args, &at_play, sizeof args) &&
           !memcmp(&guest, &native, sizeof native) && !memcmp(after, nc_ram, sizeof after);
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
    const u32 tracks[] = {0, 1, 7, 10, 11, 18, 25, 26, 0x80000000, 0xffffffff};
    for (unsigned i = 0; i < sizeof tracks / sizeof tracks[0]; i++)
        if (!check_play(tracks[i]))
            return 1;
    disc_close();
    return 0;
}
