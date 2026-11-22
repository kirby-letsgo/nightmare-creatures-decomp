#include "credits_stubs.h"
#include "decomp/credits.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include "port/runtime.h"
#include "port/savestate.h"
#include <stdio.h>
#include <stdlib.h>

static CPUState at_play;
static unsigned play_calls;
static void play(CPUState *c) {
    at_play = *c;
    play_calls++;
    R(2) = 0xfedcba98;
}

static CPUState at_volume;
static unsigned volume_calls;
static void volume(CPUState *c) {
    at_volume = *c;
    volume_calls++;
    R(2) = 0x369c;
}
const NcFuncEntry credits_exe_table[] = {{0x800192e4, volume}, {0x80022ccc, play}};
const unsigned credits_exe_table_len = 2;
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

/* Move gp and retain separate sentinels to distinguish the gp-relative
 * first entry from the absolute second entry. */
static bool check_reset(u32 gp) {
    CPUState initial = {0};
    for (unsigned i = 1; i < 32; i++)
        initial.r[i] = 0x16bd5u * i;
    initial.r[28] = gp;
    initial.r[29] = 0x801fef00;
    initial.r[31] = 0x80010000;
    initial.budget = 5;
    MEM_W32(gp + 0x34, 0x7dbf8u);
    MEM_W32(0x80024ca0, 0x31ed);
    memcpy(before, nc_ram, sizeof before);
    CPUState native = initial;
    if (nc_credits_reset_music(&native) || memcmp(&initial, &native, sizeof native) ||
        memcmp(before, nc_ram, sizeof before))
        return false;
    native.budget = initial.budget = 4096;
    volume_calls = 0;
    if (!nc_credits_reset_music(&native) || volume_calls != 1 || at_volume.r[4] || at_volume.r[5] ||
        MEM_R32(gp + 0x34) || MEM_R32(0x80024ca0))
        return false;
    CPUState args = at_volume;
    memcpy(after, nc_ram, sizeof after);
    memcpy(nc_ram, before, sizeof before);
    CPUState guest = initial;
    volume_calls = 0;
    interp_call(&guest, 0x8001932c);
    return volume_calls == 1 && !memcmp(&args, &at_volume, sizeof args) &&
           !memcmp(&guest, &native, sizeof native) && !memcmp(after, nc_ram, sizeof after);
}

/* Save the scheduler before each run so budgets 1 and 2 exercise the
 * return-delay-slot event at exactly the same emulated time. */
static bool check_empty(u32 entry, int budget) {
    nc_cpu = (CPUState){0};
    for (unsigned i = 1; i < 32; i++)
        nc_cpu.r[i] = 0x13ed5u * i;
    nc_cpu.r[29] = 0x801fef00;
    nc_cpu.r[31] = 0x80010000;
    nc_cpu.budget = budget;
    StateIO initial = {0}, native = {0}, guest = {0};
    cpu_serialize(&initial);
    bool ok = nc_credits_empty_hook(&nc_cpu);
    cpu_serialize(&native);
    initial.loading = true;
    cpu_serialize(&initial);
    interp_call(&nc_cpu, entry);
    cpu_serialize(&guest);
    ok = ok && !initial.error && !native.error && !guest.error && native.len == guest.len &&
         !memcmp(native.buf, guest.buf, guest.len);
    free(initial.buf);
    free(native.buf);
    free(guest.buf);
    return ok;
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
    if (!check_reset(0x80024c68) || !check_reset(0x80034000) || !check_reset(0x80024c6c))
        return 1;
    for (int budget = 1; budget <= 4; budget++)
        if (!check_empty(0x8001935c, budget) || !check_empty(0x8002335c, budget))
            return 1;
    disc_close();
    return 0;
}
