#include "decomp/damage.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include <stdio.h>

static const u32 target = 0x80120000, attacker = 0x80121000, rules = 0x80122000;
static const u32 table = 0x800D42C8, output = SCRATCH_BASE + 0x100;

typedef struct Hit {
    bool target_flag;
    int attacker_flag; /* -1 for no attacker */
    u32 kind, target_value, attacker_value;
    u32 flags, flags4, attacker_status;
    u16 tier;
} Hit;

static bool check(Hit h, bool use_disc, int want_power, int want_damage) {
    CPUState initial = {0};
    for (unsigned i = 1; i < 32; i++)
        initial.r[i] = i * 0x12345u;
    initial.r[4] = target;
    initial.r[5] = h.attacker_flag < 0 ? 0 : attacker;
    initial.r[6] = h.kind;
    initial.r[7] = output;
    initial.r[28] = 0x80130000;
    initial.r[31] = 0x80010000;
    initial.budget = 95;
    MEM_W32(initial.r[28] + 0x244, rules);
    MEM_W32(rules, h.flags);
    MEM_W32(rules + 4, h.flags4);
    MEM_W32(target + 0x2C8, h.target_flag);
    MEM_W32(attacker + 0x2C8, h.attacker_flag == 1);
    MEM_W32(attacker + 0x2D0, h.attacker_status);
    MEM_W8(target + 0x388, 2);
    MEM_W8(attacker + 0x388, 5);
    MEM_W8(table + 2 * 52 + h.kind, h.target_value);
    MEM_W8(table + 5 * 52 + h.kind, h.attacker_value);
    MEM_W16(0x800CBFF6, h.tier);
    MEM_W32(output, 0xdeadbeefu);

    CPUState native = initial;
    native.r[8] = target; /* first guest instruction precedes the native entry */
    native.budget = 93;
    CPUState before = native;
    if (nc_select_base_damage(&native) || memcmp(&before, &native, sizeof native) ||
        MEM_R32(output) != 0xdeadbeefu)
        return false;
    native.budget = initial.budget - 1;
    if (!nc_select_base_damage(&native))
        return false;
    u32 power = MEM_R32(output);
    if (want_power >= 0 && (power != (u32)want_power || native.r[2] != (u32)want_damage))
        return false;

    if (use_disc) {
        CPUState guest = initial;
        MEM_W32(output, 0xdeadbeefu);
        interp_call(&guest, 0x8004E6DC);
        if (memcmp(&guest, &native, sizeof native) || MEM_R32(output) != power) {
            fprintf(stderr, "kind %u, actors %u/%d, tier %u: cycles left %d/%d, power %u/%u\n",
                    h.kind, h.target_flag, h.attacker_flag, h.tier, guest.budget, native.budget,
                    MEM_R32(output), power);
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    bool use_disc = argc == 2;
    if (argc > 2)
        return 1;
    if (use_disc) {
        FILE *file = fopen(argv[1], "rb");
        if (!file)
            return 77;
        fclose(file);
        ExecInfo exe;
        if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX2.EXE;1", &exe))
            return 1;
    }

    /* First-combat table entries: the incoming hit and the two outgoing attack kinds. */
    if (!check((Hit){true, 0, 38, 60, 20}, use_disc, 20, 321) ||
        !check((Hit){false, 1, 1, 100, 15}, use_disc, 15, 240) ||
        !check((Hit){false, 1, 15, 100, 25}, use_disc, 25, 401))
        return 1;

    if (use_disc) {
        const unsigned kinds[] = {0, 1, 23, 24, 32, 33, 51};
        const u16 tiers[] = {0, 1, 2, 3, 4, 0xffff};
        for (unsigned t = 0; t < 2; t++)
            for (int a = -1; a <= 1; a++)
                for (unsigned mask = 0; mask < 16; mask++)
                    for (unsigned k = 0; k < sizeof kinds / sizeof kinds[0]; k++)
                        for (unsigned n = 0; n < sizeof tiers / sizeof tiers[0]; n++) {
                            Hit h = {t,
                                     a,
                                     kinds[k],
                                     (k * 43 + n) & 255,
                                     (255 - k * 37 - n) & 255,
                                     ((mask & 1) ? 0x00800000u : 0) |
                                         ((mask & 2) ? 0x01000000u : 0),
                                     (mask & 4) ? 0x80000000u : 0,
                                     (mask & 8) ? 0x00100000u : 0,
                                     tiers[n]};
                            if (!check(h, true, -1, -1))
                                return 1;
                        }
        disc_close();
    }
    return 0;
}
