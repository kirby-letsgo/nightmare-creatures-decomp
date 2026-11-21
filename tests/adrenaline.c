/* Compare the hand-written update against the same region from the user's disc.
 * Vary the meter independently of the HUD so the empty-gauge paths run immediately. */
#include "decomp/adrenaline.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/game_vars.h"
#include "port/interp.h"
#include "port/patches.h"
#include "port/runtime.h"
#include <stdio.h>

static u8 before[RAM_SIZE], expected[RAM_SIZE];

static bool check(unsigned i) {
    u32 actor = 0x80120000, rules = 0x80130000;
    CPUState initial = {0};
    for (unsigned r = 1; r < 32; r++)
        initial.r[r] = r * 0x10203u;
    initial.r[16] = i == 0 ? 0 : actor;
    initial.r[28] = 0x80140000;
    initial.r[31] = 0x80055F48;
    initial.budget = 4096;
    MEM_W32(initial.r[28] + 0x244, rules);
    MEM_W32(actor, i == 1);
    MEM_W16(0x800D779C, i == 2 ? 0x10 : 0);
    MEM_W32(rules, i & 1 ? 0x30 : 0);
    MEM_W32(rules + 4, ((i & 2) ? 0x08000000 : 0) | ((i & 4) ? 8 : 0));
    MEM_W32(0x800D4C10, 20);
    MEM_W32(0x8008EF10, 100);
    MEM_W32(0x800CC038, (i & 8) ? 70 : 20);
    MEM_W16(ADDR_ADRENALINE, (i >> 4) % 3);
    MEM_W16(0x800CC004, 100);
    MEM_W16(actor + ACTOR_HEALTH_OFFSET, (i >> 3) % 9);
    nc_flag_no_adrenaline = (i & 64) != 0;
    memcpy(before, nc_ram, RAM_SIZE);
    CPUState reference = initial, native = initial;
    interp_call(&reference, 0x80055DD4);
    memcpy(expected, nc_ram, RAM_SIZE);
    memcpy(nc_ram, before, RAM_SIZE);
    if (!nc_update_adrenaline(&native) || memcmp(&reference, &native, sizeof native) ||
        memcmp(expected, nc_ram, RAM_SIZE)) {
        fprintf(stderr, "adrenaline case %u: reference budget %d, native %d\n", i, reference.budget,
                native.budget);
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    FILE *disc = fopen(argv[1], "rb");
    if (!disc)
        return 77;
    fclose(disc);
    ExecInfo exe;
    if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX2.EXE;1", &exe))
        return 1;
    for (unsigned i = 0; i < 192; i++)
        if (!check(i))
            return 1;
    CPUState cpu = {0};
    cpu.r[16] = 0x80120000;
    MEM_W16(ADDR_ADRENALINE, 1);
    MEM_W16(0x800CC004, 0);
    for (int budget = 100; budget <= 4096; budget += 3996) {
        cpu.budget = budget;
        CPUState original = cpu;
        memcpy(before, nc_ram, RAM_SIZE);
        if (nc_update_adrenaline(&cpu) || memcmp(&cpu, &original, sizeof cpu) ||
            memcmp(before, nc_ram, RAM_SIZE))
            return 1;
    }
    disc_close();
    return 0;
}
