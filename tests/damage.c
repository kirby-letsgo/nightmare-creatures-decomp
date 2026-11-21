#include "decomp/damage.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/game_vars.h"
#include "port/interp.h"
#include <stdio.h>

static const u32 target = 0x80120000, rules = 0x80130000;
static u8 before[RAM_SIZE];

static const struct Mode {
    bool check_mode;
    u32 flags, flags4;
    unsigned multiplier;
    int cycles;
} modes[] = {
    {false, 0, 0, 1, 5},
    {true, 0, 0, 2, 23},
    {true, 0x40, 8, 2, 23},
    {true, 0x10, 0, 1, 23},
    {true, 0x20, 0, 1, 23},
    {true, 0, 0x08000000, 1, 16},
    {true, 0x30, 0x08000008, 1, 16},
};

static bool check(const struct Mode *mode, u16 health, u32 damage, bool compare_disc) {
    CPUState initial = {0};
    for (unsigned i = 1; i < 32; i++)
        initial.r[i] = 0x12345u * i;
    initial.r[17] = target;
    initial.r[20] = damage;
    initial.r[28] = 0x80140000;
    initial.r[31] = 0x8004EB78; /* stop immediately before the health store */
    MEM_W32(initial.r[28] + 0x244, rules);
    MEM_W32(rules, mode->flags);
    MEM_W32(rules + 4, mode->flags4);
    MEM_W16(target + ACTOR_HEALTH_OFFSET, health);
    memcpy(before, nc_ram, sizeof before);

    initial.budget = mode->check_mode ? 23 : 5;
    CPUState native = initial;
    if (nc_calculate_hit_health(&native, mode->check_mode) ||
        memcmp(&native, &initial, sizeof native) || memcmp(before, nc_ram, sizeof before))
        return false;

    initial.budget++;
    native = initial;
    CPUState expected = initial;
    expected.budget -= mode->cycles;
    expected.r[2] = (u32)health - damage * mode->multiplier;
    if (mode->check_mode) {
        expected.r[3] = mode->cycles == 16 ? 0x08000000u : damage * 2;
        expected.r[4] = rules;
    }
    if (!nc_calculate_hit_health(&native, mode->check_mode) ||
        memcmp(&native, &expected, sizeof native) || memcmp(before, nc_ram, sizeof before))
        return false;

    if (compare_disc) {
        CPUState guest = initial;
        interp_call(&guest, mode->check_mode ? 0x8004EB30 : 0x8004EB6C);
        if (memcmp(&guest, &native, sizeof native) || memcmp(before, nc_ram, sizeof before))
            return false;
    }
    return true;
}

int main(int argc, char **argv) {
    bool compare_disc = argc == 2;
    if (argc > 2)
        return 1;
    if (compare_disc) {
        FILE *disc = fopen(argv[1], "rb");
        if (!disc)
            return 77;
        fclose(disc);
        ExecInfo exe;
        if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX2.EXE;1", &exe))
            return 1;
    }
    static const struct {
        u16 health;
        u32 damage;
    } hits[] = {{4096, 321}, {1, 1}, {0, 1}, {0xffff, 0}, {0x8000, 0x80000001u}, {7, ~0u}};
    for (unsigned m = 0; m < sizeof modes / sizeof modes[0]; m++) {
        for (unsigned h = 0; h < sizeof hits / sizeof hits[0]; h++) {
            if (!check(&modes[m], hits[h].health, hits[h].damage, compare_disc)) {
                fprintf(stderr, "damage mismatch for mode %u, hit %u\n", m, h);
                return 1;
            }
        }
    }
    if (compare_disc)
        disc_close();
    return 0;
}
