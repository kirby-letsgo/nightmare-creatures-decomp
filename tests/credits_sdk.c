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

static bool check_fill(void) {
    const u32 dest[] = {0, 0x80060005, SCRATCH_BASE + 3, 0xa0060008};
    const u32 counts[] = {0, 1, 4, 19, 0x80000000, 0xffffffff};
    for (unsigned i = 0; i < sizeof dest / sizeof dest[0]; i++)
        for (unsigned j = 0; j < sizeof counts / sizeof counts[0]; j++) {
            if (dest[i])
                memset(nc_fast_ptr(dest[i]), 0x11, 24);
            if (!compare(arguments(dest[i], 0xb2e41289, counts[j], 4096), 0x8001995c,
                         nc_credits_fill_bytes, 2))
                return false;
        }
    for (int budget = 3; budget <= 37; budget++) {
        memset(nc_fast_ptr(dest[1]), 0x11, 24);
        if (!compare(arguments(dest[1], 0xed, 7, budget), 0x8001995c, nc_credits_fill_bytes, 2))
            return false;
    }
    return true;
}

static bool check_length(void) {
    const u32 locations[] = {0, 0x80060001, 0xa0060003, SCRATCH_BASE + 7};
    for (unsigned i = 0; i < sizeof locations / sizeof locations[0]; i++)
        for (unsigned length = 0; length < 10; length++) {
            if (locations[i]) {
                memset(nc_fast_ptr(locations[i]), 0xe1, length);
                MEM_W8(locations[i] + length, 0);
            }
            for (int budget = 3; budget < 32; budget++)
                if (!compare(arguments(locations[i], 0, 0, budget), 0x80019a30,
                             nc_credits_string_length, 2))
                    return false;
        }
    return true;
}

static bool check_append(void) {
    const u32 base = 0x80060010, other = SCRATCH_BASE + 32;
    const u32 pairs[][2] = {
        {0, other},        {base, 0},         {base, base}, {base, base + 1}, {base + 3, other + 4},
        {base, other + 4}, {base + 3, other}, {base, other}};
    for (unsigned i = 0; i < sizeof pairs / sizeof pairs[0]; i++)
        for (int budget = 9; budget <= 140; budget += 3) {
            memcpy(nc_fast_ptr(base), "abc", 4);
            memcpy(nc_fast_ptr(other), "xyzw", 5);
            if (!compare(arguments(pairs[i][0], pairs[i][1], 0, budget), 0x80019988,
                         nc_credits_append_string, 8))
                return false;
        }
    return true;
}

static bool check_printf(void) {
    const char *formats[] = {"", "NC", "%d %s %x", "%02x:%%:%c"};
    const u32 fmt = 0x80060000, string = 0x80060400;
    memcpy(nc_fast_ptr(string), "test", 5);
    for (unsigned i = 0; i < sizeof formats / sizeof formats[0]; i++) {
        strcpy((char *)nc_fast_ptr(fmt), formats[i]);
        for (int budget = 12; budget < 64; budget += 7) {
            CPUState state = arguments(fmt, (u32)-12, string, budget);
            state.r[7] = 0xcafe;
            if (!compare(state, 0x80019a60, nc_credits_printf, 11))
                return false;
        }
    }
    return true;
}

static bool upper(CPUState *c) {
    return nc_credits_change_case(c, true);
}
static bool lower(CPUState *c) {
    return nc_credits_change_case(c, false);
}

static bool check_case(void) {
    for (unsigned byte = 0; byte < 256; byte++) {
        CPUState state = arguments(0xabcdef00 | byte, 0, 0, 15);
        if (!compare(state, 0x8001a128, upper, 14) || !compare(state, 0x8001a158, lower, 14))
            return false;
    }
    u32 table_byte = 0x80023375 + '!';
    u8 original = MEM_R8(table_byte);
    for (unsigned flags = 0; flags < 4; flags++) {
        MEM_W8(table_byte, (u8)flags);
        CPUState state = arguments('!', 0, 0, 100);
        if (!compare(state, 0x8001a128, upper, 14) || !compare(state, 0x8001a158, lower, 14))
            return false;
    }
    MEM_W8(table_byte, original);
    return true;
}

static NcCreditsPrimitive packet_type;
static bool init_packet(CPUState *c) {
    return nc_credits_init_primitive(c, packet_type);
}

static bool check_primitives(void) {
    const struct {
        u32 entry;
        NcCreditsPrimitive type;
        bool polyline;
    } cases[] = {
        {0x8001b874, NC_POLY_F3, false}, {0x8001b888, NC_POLY_FT3, false},
        {0x8001b89c, NC_POLY_G3, false}, {0x8001b8b0, NC_POLY_GT3, false},
        {0x8001b8c4, NC_POLY_F4, false}, {0x8001b8d8, NC_POLY_FT4, false},
        {0x8001b8ec, NC_POLY_G4, false}, {0x8001b900, NC_POLY_GT4, false},
        {0x8001b914, NC_SPRT8, false},   {0x8001b928, NC_SPRT16, false},
        {0x8001b93c, NC_SPRT, false},    {0x8001b950, NC_TILE1, false},
        {0x8001b964, NC_TILE8, false},   {0x8001b978, NC_TILE16, false},
        {0x8001b98c, NC_TILE, false},    {0x8001b9a0, NC_LINE_F2, false},
        {0x8001b9b4, NC_LINE_G2, false}, {0x8001b9c8, NC_LINE_F3, true},
        {0x8001b9e8, NC_LINE_G3, true},  {0x8001ba08, NC_LINE_F4, true},
        {0x8001ba28, NC_LINE_G4, true},  {0x8001ba48, NC_BLOCK_FILL, false},
    };
    const u32 locations[] = {0x80060000, 0xa0060001, SCRATCH_BASE + 4};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        packet_type = cases[i].type;
        int cost = cases[i].polyline ? 8 : 5;
        for (unsigned j = 0; j < sizeof locations / sizeof locations[0]; j++) {
            memset(nc_fast_ptr(locations[j]), 0xcc, 64);
            if (!compare(arguments(locations[j], 0, 0, cost + 1), cases[i].entry, init_packet,
                         cost))
                return false;
        }
    }
    return true;
}

static bool semi_trans(CPUState *c) {
    return nc_credits_packet_flag(c, 2);
}
static bool shade_tex(CPUState *c) {
    return nc_credits_packet_flag(c, 1);
}

static bool check_packet_flags(void) {
    const u32 args[] = {0, 1, 0x80000000};
    for (unsigned i = 0; i < sizeof args / sizeof args[0]; i++)
        for (unsigned byte = 0; byte <= 255; byte++) {
            const u32 base = SCRATCH_BASE + 4;
            CPUState start = arguments(base, args[i], 0, 10);
            MEM_W8(base + 7, (u8)byte);
            if (!compare(start, 0x8001b824, semi_trans, 9))
                return false;
            MEM_W8(base + 7, (u8)byte);
            if (!compare(start, 0x8001b84c, shade_tex, 9))
                return false;
        }
    return true;
}

static bool add_one(CPUState *c) {
    return nc_credits_prepend_packets(c, false);
}
static bool add_range(CPUState *c) {
    return nc_credits_prepend_packets(c, true);
}
static bool check_prepend(void) {
    const u32 head = 0x80060300;
    const u32 nodes[] = {head, SCRATCH_BASE, 0xa0060300, head + 8};
    for (unsigned i = 0; i < 4; i++)
        for (unsigned j = 0; j < 4; j++)
            for (unsigned range = 0; range < 2; range++) {
                MEM_W32(head, 0xacffffff);
                MEM_W32(nodes[i], 0x55017abc);
                MEM_W32(nodes[j], 0x660c1234);
                CPUState state = arguments(head, nodes[i], nodes[j], 22);
                if (!compare(state, range ? 0x8001b7ac : 0x8001b770, range ? add_range : add_one,
                             21))
                    return false;
            }
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
    pass = pass && check_fill();
    pass = pass && check_length();
    pass = pass && check_append();
    pass = pass && check_printf();
    pass = pass && check_case();
    pass = pass && check_primitives();
    pass = pass && check_packet_flags();
    pass = pass && check_prepend();
    disc_close();
    return pass ? 0 : 1;
}
