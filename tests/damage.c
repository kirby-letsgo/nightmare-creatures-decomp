#include "decomp/damage.h"
#include "credits_compare.h"
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

static bool lethal_hits(bool disc) {
    const u16 values[] = {0, 1, 83, 0x7fff, 0x8000, (u16)-318, 0xffff};
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; ++i)
        for (unsigned player = 0; player < 2; ++player) {
            bool alive = (s16)values[i] > 0;
            CPUState cpu = {0};
            cpu.r[17] = target;
            cpu.r[31] = alive ? 0x8004ecd0 : player ? 0x8004ec8c : 0x8004ecac;
            MEM_W16(target + ACTOR_HEALTH_OFFSET, values[i]);
            MEM_W32(target + 0x2c8, 0x84002000 | player);
            cpu.budget = 13;
            CPUState native = cpu;
            if (nc_check_lethal_hit(&native) != NC_HIT_TIMED || memcmp(&native, &cpu, sizeof cpu) ||
                MEM_R16(target + ACTOR_HEALTH_OFFSET) != values[i])
                return false;
            cpu.budget = native.budget = 14;
            NcHitOutcome want = alive    ? NC_HIT_SURVIVED
                                : player ? NC_HIT_PLAYER_DEATH
                                         : NC_HIT_OTHER_DEATH;
            u16 final_health = alive ? values[i] : 0;
            if (nc_check_lethal_hit(&native) != want || native.r[2] != (alive ? 40u : player) ||
                native.budget != (alive ? 8 : 1) ||
                MEM_R16(target + ACTOR_HEALTH_OFFSET) != final_health)
                return false;
            if (disc) {
                MEM_W16(target + ACTOR_HEALTH_OFFSET, values[i]);
                interp_call(&cpu, 0x8004ec68);
                if (memcmp(&cpu, &native, sizeof cpu) ||
                    MEM_R16(target + ACTOR_HEALTH_OFFSET) != final_health)
                    return false;
            }
        }
    return true;
}

/* Each effect call records its four arguments in the actor's test buffer.
 * The original caller and the C caller both execute this guest test stub. */
static bool death_transitions(void) {
    const u32 recorder[] = {
        0x00074100, /* sll t0,a3,4 */
        0x01044021, /* addu t0,t0,a0 */
        0xad040400, /* sw a0,0x400(t0) */
        0xad050404, /* sw a1,0x404(t0) */
        0xad060408, /* sw a2,0x408(t0) */
        0xad07040c, /* sw a3,0x40c(t0) */
        0x03e00008, /* jr ra */
        0x34041234, /* ori a0,zero,0x1234 (clobber the first argument) */
    };
    for (unsigned i = 0; i < sizeof recorder / sizeof recorder[0]; ++i)
        MEM_W32(0x80058b9c + 4 * i, recorder[i]);

    CPUState c = {0};
    for (unsigned r = 1; r < 32; ++r)
        c.r[r] = 0x12345 * r;
    c.r[4] = target;
    c.r[29] = 0x801fe000;
    c.r[31] = 0x80010000;
    MEM_W32(0x8008f0c8, rules);
    const u32 flags[] = {0, 1, 0x00200001};
    const u32 masks[] = {0, 0x08000000, 0x40000000, 0x48000000, 0x88000000, 0x50000000};
    const unsigned budgets[] = {63, 99, 513, 4096};

    for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; ++i)
        for (unsigned j = 0; j < sizeof masks / sizeof masks[0]; ++j)
            for (unsigned b = 0; b < sizeof budgets / sizeof budgets[0]; ++b) {
                const int index = (int)b - 2;
                memset(nc_ram + (target & (RAM_SIZE - 1)), 0xa5, 0x800);
                MEM_W32(target + 0x2c8, flags[i]);
                MEM_W16(target + 0x360, (u16)index);
                MEM_W32(rules + (u32)index * 36, masks[j]);
                c.r[5] = b & 1 ? 15 : 0x1234;
                c.r[6] = b & 1 ? 0x80000 : 0x100000;
                c.budget = budgets[b];
                if (!compare(c, 0x8004d0cc, nc_enter_death_state, 62) ||
                    MEM_R16(target + ACTOR_HEALTH_OFFSET) || MEM_R8(target + 0x39b) != (u8)c.r[5])
                    return false;
                bool effects = flags[i] == 1 && (masks[j] & 0x48000000) && !(masks[j] & 0x90000000);
                for (unsigned slot = 0; slot < 48; ++slot) {
                    u32 saved = target + 0x400 + 16 * slot;
                    if (effects && slot >= 2) {
                        if (MEM_R32(saved) != target || MEM_R32(saved + 4) != 17 ||
                            MEM_R32(saved + 8) != 0x808080 || MEM_R32(saved + 12) != slot)
                            return false;
                    } else if (MEM_R32(saved) != 0xa5a5a5a5) {
                        return false;
                    }
                }
            }

    c.budget = 15;
    for (unsigned state = 0; state < 256; ++state) {
        memset(nc_ram + (target & (RAM_SIZE - 1)), 0xa5, 0x400);
        c.r[5] = state | 0x12340000u;
        MEM_W32(target + 0x330, 0xffffff00u | state);
        if (!compare(c, 0x8004d1a0, nc_set_actor_state, 14))
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
    if (!lethal_hits(compare_disc))
        return 1;
    if (compare_disc && !death_transitions()) {
        fprintf(stderr, "death transition comparison failed\n");
        return 1;
    }
    if (compare_disc)
        disc_close();
    return 0;
}
