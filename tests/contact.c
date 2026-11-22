#include "decomp/contact.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include <limits.h>
#include <stdio.h>

typedef struct Region {
    u32 start, passed, rejected;
    int max_cycles;
    NcContactResult (*native)(CPUState *);
} Region;

static const Region incoming = {0x800422DC, 0x80042360, 0x800424AC, 49, nc_attack_bounds};
static const Region outgoing = {0x80042AA0, 0x80042B24, 0x80042D10, 49, nc_attack_bounds};
static const Region state_check = {0x8004E510, 0x8004E570, 0x8004E6AC, 25,
                                   nc_target_accepts_contact};

static CPUState initial_cpu(void) {
    CPUState c = {0};
    for (unsigned i = 1; i < 32; ++i)
        c.r[i] = i * 0x12345;
    c.r[31] = 0x80010000;
    return c;
}

static void mark_exit(u32 pc, NcContactResult decision) {
    /* Test-only return stubs outside the region. Record which edge the disc code took. */
    MEM_W32(pc, 0x03e00008u);                /* jr ra */
    MEM_W32(pc + 4, 0x341a0000u | decision); /* ori k0, zero, decision */
}

static bool check(const Region *region, CPUState initial, NcContactResult want, bool disc) {
    initial.budget = region->max_cycles;
    CPUState native = initial;
    if (region->native(&native) != NC_CONTACT_TIMED || memcmp(&initial, &native, sizeof native))
        return false;
    initial.budget = region->max_cycles + 3; /* leave room for the test return */
    native = initial;
    if (region->native(&native) != want)
        return false;
    if (disc) {
        CPUState guest = initial;
        interp_call(&guest, region->start);
        if (guest.r[26] != (u32)want)
            return false;
        guest.r[26] = initial.r[26];
        guest.budget += 2;
        if (memcmp(&guest, &native, sizeof guest))
            return false;
    }
    return true;
}

static bool check_preparation(bool disc) {
    const u32 entries[] = {0x800420B4, 0x800421A4, 0x8004281C, 0x80042904};
    const u32 extents[] = {0, 1, 0x7fff, 0xffff};
    const u32 center = SCRATCH_BASE + 0x100;
    u8 initial_mem[SCRATCH_SIZE], native_mem[SCRATCH_SIZE];
    for (unsigned i = 0; i < 4; i++)
        for (unsigned layout = 0; layout < 3; layout++)
            for (unsigned e = 0; e < 4; e++) {
                bool outgoing = i >= 2;
                u32 lower = layout == 1 ? center + 12 : SCRATCH_BASE + 0x200;
                u32 upper = layout == 2 ? lower + 2 : SCRATCH_BASE + 0x300;
                CPUState initial = initial_cpu();
                initial.r[4] = center + 12;
                initial.r[5] = outgoing ? lower : center;
                initial.r[6] = outgoing ? upper : lower;
                initial.r[outgoing ? 23 : 7] = outgoing ? center : upper;
                initial.r[31] = entries[i] + 0x78;
                memset(nc_scratch, 0xcd, SCRATCH_SIZE);
                const u32 coords[] = {0xffffffff, 0x80000000, 0x12347fff};
                for (unsigned axis = 0; axis < 3; axis++)
                    MEM_W32(center + axis * 4, coords[axis]);
                MEM_W16(center + 12, extents[e]);
                memcpy(initial_mem, nc_scratch, SCRATCH_SIZE);
                initial.budget = 54;
                CPUState native = initial;
                if (nc_prepare_attack_box(&native, outgoing) ||
                    memcmp(&native, &initial, sizeof native) ||
                    memcmp(initial_mem, nc_scratch, SCRATCH_SIZE))
                    return false;
                initial.budget = 55;
                native = initial;
                if (!nc_prepare_attack_box(&native, outgoing) || native.budget != 1)
                    return false;
                if (!layout) {
                    for (unsigned axis = 0; axis < 3; axis++)
                        if (MEM_R16(lower + axis * 2) != (u16)(coords[axis] - extents[e]) ||
                            MEM_R16(upper + axis * 2) != (u16)(coords[axis] + extents[e]))
                            return false;
                }
                if (disc) {
                    memcpy(native_mem, nc_scratch, SCRATCH_SIZE);
                    memcpy(nc_scratch, initial_mem, SCRATCH_SIZE);
                    CPUState guest = initial;
                    interp_call(&guest, entries[i]);
                    if (memcmp(&native, &guest, sizeof guest) ||
                        memcmp(native_mem, nc_scratch, SCRATCH_SIZE)) {
                        fprintf(stderr, "box preparation mismatch: copy %u layout %u extent %u\n",
                                i, layout, extents[e]);
                        return false;
                    }
                }
            }
    return true;
}

static bool check_boxes(bool disc) {
    const struct {
        s32 center;
        s16 extent, low, high;
        NcContactResult result;
    } cases[] = {
        {10, 5, 15, 20, NC_CONTACT_PASS},
        {10, 5, 16, 20, NC_CONTACT_REJECT},
        {-10, 5, -20, -15, NC_CONTACT_PASS},
        {-10, 5, -20, -16, NC_CONTACT_REJECT},
        {0, 0, 0, 0, NC_CONTACT_PASS},
        {32768, 1, 32767, 32767, NC_CONTACT_PASS},
        {-32769, 1, -32768, -32768, NC_CONTACT_PASS},
        {INT32_MAX, 1, -10, 10, NC_CONTACT_REJECT},
        {INT32_MIN, 1, -10, 10, NC_CONTACT_REJECT},
        {0, -1, 0, 0, NC_CONTACT_REJECT},
    };
    for (unsigned axis = 0; axis < 3; ++axis) {
        for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            CPUState c = initial_cpu();
            c.r[29] = 0x80110000;
            c.r[6] = 16; /* third box */
            c.r[7] = c.r[29] + 0x60 + c.r[6];
            c.r[17] = 0x80112008;
            c.r[5] = (u32)(s32)cases[i].extent;
            for (unsigned j = 0; j < 3; ++j) {
                s32 center = axis == j ? cases[i].center : 0;
                MEM_W32(c.r[17] - 8 + j * 4, (u32)center);
                MEM_W16(c.r[29] + 0x20 + c.r[6] + 2 * j, axis == j ? cases[i].low : -100);
                MEM_W16(c.r[7] + 2 * j, axis == j ? cases[i].high : 100);
            }
            c.r[4] = MEM_R32(c.r[17] - 8);
            if (!check(&incoming, c, cases[i].result, disc) ||
                !check(&outgoing, c, cases[i].result, disc)) {
                fprintf(stderr, "bounds mismatch on axis %u, case %u\n", axis, i);
                return false;
            }
        }
    }
    return true;
}

static bool check_states(bool disc) {
    const u32 target = 0x80120000;
    const u32 flags[] = {0, 0x80000, 0x200000, 0x280000, 1};
    for (unsigned state = 0; state < 256; ++state)
        for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; ++i)
            for (unsigned options = 0; options < 4; ++options) {
                CPUState c = initial_cpu();
                c.r[16] = target;
                c.r[2] = options & 1;
                MEM_W8(target + 0x39B, state);
                MEM_W32(target + 0x2C8, flags[i]);
                MEM_W32(target + 0x330, options & 2 ? 0x8000 : 0);
                bool pass = state == 15 ? (options & 2) != 0
                                        : state != 16 && state != 23 && !(flags[i] & 0x280000);
                if (!check(&state_check, c, pass && !c.r[2] ? NC_CONTACT_PASS : NC_CONTACT_REJECT,
                           disc)) {
                    fprintf(stderr, "target state %u, flags %x, options %u\n", state, flags[i],
                            options);
                    return false;
                }
            }
    return true;
}

static bool check_attack_kinds(bool disc) {
    const u32 actor = 0x80120000, table = SCRATCH_BASE + 0x100;
    for (unsigned i = 0; i < 24; ++i) {
        MEM_W16(table + i * 4, i + 1);
        MEM_W16(table + i * 4 + 2, i + 100);
    }
    MEM_W16(table + 96, 50);
    MEM_W16(table + 98, 0xfffe);
    MEM_W16(table + 100, 51);
    MEM_W16(table + 102, 250);
    const unsigned secondary[] = {0, 50, 51, 255};
    for (unsigned primary = 0; primary <= 25; primary++)
        for (unsigned j = 0; j < 4; j++)
            for (unsigned flags = 0; flags < 8; flags++) {
                CPUState c = initial_cpu();
                c.r[8] = actor;
                c.r[29] = table;
                c.r[31] = 0; /* interp_resume_exe stops when the guest returns to zero */
                MEM_W8(actor + 0x38A, primary);
                MEM_W8(actor + 0x38B, secondary[j]);
                MEM_W8(actor + 0x39B, (flags & 4) ? 19 : 0);
                MEM_W32(actor + 0x2C8, (flags & 2) ? 0x8000 : 0);
                MEM_W32(actor + 0x44, flags & 1);
                c.budget = 271;
                CPUState native = c;
                if (nc_player_attack_kind(&native) || memcmp(&native, &c, sizeof c))
                    return false;
                native.budget = c.budget = 272;
                u32 want = (flags & 6) == 6;
                if ((flags & 1) && j == 1)
                    want = (u32)-2;
                if ((flags & 1) && j == 2)
                    want = 250;
                if (primary >= 1 && primary <= 24)
                    want = primary + 99;
                if (!nc_player_attack_kind(&native) || native.r[2] != want)
                    return false;
                if (disc) {
                    /* At this mid-function entry the original epilogue pops the stack. */
                    interp_resume_exe(&c, 0x8004260C);
                    if (memcmp(&c, &native, sizeof c)) {
                        fprintf(stderr, "attack kind mismatch: primary %u secondary %u flags %u\n",
                                primary, secondary[j], flags);
                        return false;
                    }
                }
            }
    return true;
}

int main(int argc, char **argv) {
    if (argc > 2)
        return 1;
    bool disc = argc == 2;
    if (disc) {
        FILE *f = fopen(argv[1], "rb");
        if (!f)
            return 77;
        fclose(f);
        ExecInfo exe;
        if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX2.EXE;1", &exe))
            return 1;
        const Region *regions[] = {&incoming, &outgoing, &state_check};
        for (unsigned i = 0; i < sizeof regions / sizeof regions[0]; ++i) {
            mark_exit(regions[i]->passed, NC_CONTACT_PASS);
            mark_exit(regions[i]->rejected, NC_CONTACT_REJECT);
        }
    }
    bool ok = check_preparation(disc) && check_boxes(disc) && check_states(disc) &&
              check_attack_kinds(disc);
    if (disc)
        disc_close();
    return ok ? 0 : 1;
}
