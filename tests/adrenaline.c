/* Compare the hand-written update against the same region from the user's disc.
 * Vary the meter independently of the HUD so the empty-gauge paths run immediately. */
#include "decomp/adrenaline.h"
#include "credits_compare.h"
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

static bool hit_refills(void) {
    const u32 actor = 0x80120000;
    const u32 kinds[] = {0, 15, 39, 40, 41, 42, 0x10028, ~0u};
    CPUState cpu = {0};
    for (unsigned r = 1; r < 32; ++r)
        cpu.r[r] = r * 0x10203u;
    cpu.r[29] = 0x801fe000;
    cpu.r[31] = 0x80010000;
    for (unsigned role = 0; role < 3; ++role) {
        cpu.r[4] = role ? actor : 0;
        MEM_W32(actor + 0x2c8, 0x84002000 | (role == 2));
        for (unsigned k = 0; k < sizeof kinds / sizeof kinds[0]; ++k) {
            cpu.r[5] = kinds[k];
            cpu.budget = k & 1 ? 38 : 4096;
            const u16 flags = 0x81e3 ^ (k << 4);
            MEM_W16(0x800cbff8, flags);
            MEM_W16(0x800cbffe, 777);
            MEM_W16(ADDR_ADRENALINE, 9);
            bool refill = role != 2 && kinds[k] != 40 && kinds[k] != 41;
            if (!compare(cpu, 0x80052b80, nc_refill_hit_adrenaline, 37) ||
                MEM_R16(ADDR_ADRENALINE) != (refill ? 256 : 9) ||
                MEM_R16(0x800cbff8) != (refill ? flags & ~0x60u : flags) ||
                MEM_R16(0x800cbffe) != (refill ? 0 : 777))
                return false;
        }
    }
    cpu.budget = 12;
    MEM_W16(0x800cbffe, 0x1234);
    MEM_W16(0x800cbff8, 0xffff);
    return compare(cpu, 0x800562a0, nc_clear_adrenaline_hud, 11);
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
    if (!hit_refills()) {
        fprintf(stderr, "hit adrenaline refill comparison failed\n");
        return 1;
    }
    disc_close();
    return 0;
}
