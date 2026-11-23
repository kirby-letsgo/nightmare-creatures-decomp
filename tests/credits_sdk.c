#include "credits_compare.h"
#include "decomp/credits.h"
#include "port/bios.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/hw/hw.h"
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

static bool check_bios_a0_stub(u32 entry, u32 service, bool (*routine)(CPUState *)) {
    const int budgets[] = {4, 4096};
    for (unsigned i = 0; i < sizeof budgets / sizeof budgets[0]; i++) {
        CPUState state = arguments(0x80062000, 0x1000, 3, budgets[i]);
        if (!compare(state, entry, routine, 3) ||
            nc_cpu.r[10] != 0xa0 || nc_cpu.r[9] != service ||
            nc_cpu.r[4] != state.r[4] || nc_cpu.r[5] != state.r[5] ||
            nc_cpu.r[31] != state.r[31])
            return false;
    }
    return true;
}

static bool check_bios_write(void) {
    const u32 buffer = 0x80061001;
    const struct {
        u32 fd, length, expected;
    } cases[] = {{1, 0, 0}, {1, 3, 3}, {0, 3, 3}, {UINT32_MAX, 3, UINT32_MAX}};
    memcpy(nc_fast_ptr(buffer), "X\r\n", 3);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        CPUState state = arguments(cases[i].fd, buffer, cases[i].length, 4);
        if (!compare(state, 0x8001a29c, nc_credits_bios_write, 3) ||
            nc_cpu.r[2] != cases[i].expected || nc_cpu.r[9] != 0x35 || nc_cpu.r[10] != 0xb0)
            return false;
    }
    return true;
}

static bool check_put_char(void) {
    const u32 column = 0x80024ce8, saved = MEM_R32(column);
    const struct {
        u32 argument, initial_column, final_column;
    } cases[] = {
        {'A', UINT32_MAX, 0}, {0x123400ff, 3, 3}, {'\r', 6, 6}, {'\n', 5, 0},
        {'\t', 7, 8}, {'\t', 8, 16}, {0x10009, UINT32_MAX, 0},
    };
    bool ok = true;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0] && ok; i++)
        for (unsigned tight = 0; tight < 2 && ok; tight++) {
            MEM_W32(column, cases[i].initial_column);
            CPUState state = arguments(cases[i].argument, 2, 3, tight ? 12 : 4096);
            ok = compare(state, 0x8001a1d8, nc_credits_put_char, 11) &&
                 MEM_R32(column) == cases[i].final_column &&
                 MEM_R8(state.r[29] - 16) == (u8)cases[i].argument;
        }
    MEM_W32(column, saved);
    return ok;
}

static bool check_put_string(void) {
    const u32 ram = 0x80061001, column = 0x80024ce8;
    const struct {
        u32 address;
        const char *string;
        u32 final_column;
    } cases[] = {
        {0, NULL, 9}, {ram, "", 3}, {ram, "AB", 5},
        {SCRATCH_BASE + 1, "A\tB", 9}, {0xa0061001, "A\nB", 1},
    };
    u32 saved_column = MEM_R32(column);
    bool ok = true;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0] && ok; i++) {
        if (cases[i].address)
            memcpy(nc_fast_ptr(cases[i].address), cases[i].string, strlen(cases[i].string) + 1);
        for (unsigned tight = 0; tight < 2 && ok; tight++) {
            MEM_W32(column, 3);
            CPUState state = arguments(cases[i].address, 2, 3, tight ? 10 : 4096);
            ok = compare(state, 0x8001a638, nc_credits_put_string, 9) &&
                 MEM_R32(column) == cases[i].final_column;
        }
    }
    MEM_W32(column, saved_column);
    return ok;
}

static bool check_find_byte(void) {
    const u32 ram = 0x80061001;
    const u8 bytes[] = {0x12, 0, 0xfe, 0x12};
    const struct {
        u32 address, key, count, result;
    } cases[] = {
        {0, 0, 4, 0}, {ram, 0x12, 0, 0}, {ram, 0x12, 0x80000000, 0},
        {ram, 0xabcd0012, 4, ram}, {ram, 0, 2, ram + 1}, {ram, 0xfe, 2, 0},
        {0xa0061001, 0xffff00fe, 3, 0xa0061003},
        {SCRATCH_BASE + 1, 0xff, 4, 0},
    };
    memcpy(nc_fast_ptr(ram), bytes, sizeof bytes);
    memcpy(nc_fast_ptr(SCRATCH_BASE + 1), bytes, sizeof bytes);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++)
        for (unsigned tight = 0; tight < 2; tight++) {
            CPUState state = arguments(cases[i].address, cases[i].key, cases[i].count,
                                       tight ? 10 : 4096);
            if (!compare(state, 0x8001a188, nc_credits_find_byte, 9) ||
                nc_cpu.r[2] != cases[i].result)
                return false;
        }
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

static bool next_packet(CPUState *c) {
    return nc_credits_packet_link(c, false);
}
static bool end_packet(CPUState *c) {
    return nc_credits_packet_link(c, true);
}
static bool check_link(void) {
    const u32 tags[] = {0, 0xff000000, 0x12ffffff, 0x00fffffe, 0xff800000, 0xabcdef01};
    for (unsigned i = 0; i < sizeof tags / sizeof tags[0]; i++)
        for (unsigned j = 0; j < 2; j++) {
            u32 base = j ? SCRATCH_BASE : 0x80060300;
            MEM_W32(base, tags[i]);
            CPUState start = arguments(base, 0, 0, 10);
            if (!compare(start, 0x8001b738, next_packet, 9) ||
                !compare(start, 0x8001b754, end_packet, 9))
                return false;
        }
    return true;
}

static bool check_link_writer(void) {
    const u32 next[] = {0, 0xffffff, 0x80040001, 0xa0120300, 0xffffffff};
    for (unsigned i = 0; i < sizeof next / sizeof next[0]; i++)
        for (unsigned j = 0; j < 2; j++) {
            u32 base = j ? SCRATCH_BASE : 0xa0060300;
            MEM_W32(base, 0x6aacbdef);
            if (!compare(arguments(base, next[i], 0, 12), 0x8001b7e8, nc_credits_link_packets, 11))
                return false;
        }
    return true;
}

static bool check_draw_mode_words(void) {
    const u32 packets[] = {0x80060400, SCRATCH_BASE + 4, 0x801feee4};
    const u32 pages[] = {0, 0xffff, 0xa5a527ff, 0xffffd800};
    const u8 types[] = {0, 1, 2, 255};
    for (unsigned g = 0; g < sizeof types; g++)
        for (unsigned f = 0; f < 4; f++)
            for (unsigned p = 0; p < sizeof pages / sizeof pages[0]; p++) {
                u32 display = (f & 1) ? 0x80000000 : 0;
                u32 dither = f & 2;
                MEM_W8(0x80024558, types[g]);
                CPUState state = arguments(display, dither, pages[p], 20);
                if (!compare(state, 0x8001d470, nc_credits_draw_mode_word, 19))
                    return false;
                for (unsigned j = 0; j < sizeof packets / sizeof packets[0]; j++) {
                    memset(nc_fast_ptr(packets[j]), 0xab, 8);
                    state = arguments(packets[j], display, dither, 61);
                    state.r[7] = pages[p];
                    if (!compare(state, 0x8001ba5c, nc_credits_draw_tpage, 60))
                        return false;
                }
            }
    return true;
}

static bool top_left(CPUState *c) {
    return nc_credits_draw_corner(c, false);
}

static bool bottom_right(CPUState *c) {
    return nc_credits_draw_corner(c, true);
}

static bool check_draw_corners(void) {
    const u32 bounds[] = {0x02000400, 0x10001000, 0x00010001, 0, 0x8000ffff};
    const u32 coords[] = {0, 1, 511, 512, 1023, 1024, 4095, 4096, 0x1234ffff, 0x8000};
    const u8 types[] = {0, 1, 2, 255};
    for (unsigned g = 0; g < sizeof types; g++)
        for (unsigned b = 0; b < sizeof bounds / sizeof bounds[0]; b++)
            for (unsigned j = 0; j < sizeof coords / sizeof coords[0]; j++) {
                MEM_W32(0x8002455c, bounds[b]);
                MEM_W8(0x80024558, types[g]);
                CPUState state = arguments(coords[j], coords[(j + 3) % 10], 0x87654321, 56);
                if (!compare(state, 0x8001d4cc, top_left, 55) ||
                    !compare(state, 0x8001d598, bottom_right, 55))
                    return false;
            }
    return true;
}

static bool check_draw_area(void) {
    const u32 packet = 0x80060500;
    const u32 rects[] = {SCRATCH_BASE + 4, packet, 0xa0060504, packet + 8};
    const s16 fields[][4] = {{0, 0, 1, 1}, {12, 34, 0, 0}, {-3, -8, 6, 12},
                             {1024, 512, 16, 16}, {32767, 32767, 2, 2}, {-32768, 7, -1, -9}};
    for (unsigned graph = 0; graph < 4; graph++)
        for (unsigned i = 0; i < sizeof rects / sizeof rects[0]; i++)
            for (unsigned j = 0; j < sizeof fields / sizeof fields[0]; j++) {
                memset(nc_fast_ptr(packet), 0xa5, 16);
                for (unsigned k = 0; k < 4; k++)
                    MEM_W16(rects[i] + 2 * k, (u16)fields[j][k]);
                MEM_W8(0x80024558, graph);
                MEM_W32(0x8002455c, 0x02000400);
                CPUState state = arguments(packet, rects[i], 0xabcdef12, 162);
                if (!compare(state, 0x8001d084, nc_credits_draw_area_packet, 161))
                    return false;
            }
    return true;
}

static bool read_draw_env(CPUState *c) {
    return nc_credits_read_environment(c, false);
}
static bool read_disp_env(CPUState *c) {
    return nc_credits_read_environment(c, true);
}

static bool check_environment_reads(void) {
    for (unsigned display = 0; display < 2; display++) {
        u32 source = display ? 0x800245c4 : 0x80024568;
        u32 length = display ? 20 : 92;
        const u32 destinations[] = {0, 0x80060503, SCRATCH_BASE + 1, source, source - 3,
                                    source + 1, source + 0x20000004, 0x801feefc - length};
        const int budgets[] = {11, 13, 19, 100, 4096};
        for (unsigned i = 0; i < sizeof destinations / sizeof destinations[0]; i++)
            for (unsigned j = 0; j < sizeof budgets / sizeof budgets[0]; j++) {
                if (destinations[i])
                    memset(nc_fast_ptr(destinations[i]), 0x8c, length);
                for (u32 k = 0; k < length; k++)
                    MEM_W8(source + k, (u8)(7 * k + 1));
                CPUState state = arguments(destinations[i], 0, 0, budgets[j]);
                if (!compare(state, display ? 0x8001cfdc : 0x8001caf4,
                             display ? read_disp_env : read_draw_env, 10))
                    return false;
            }
    }
    return true;
}

static bool check_dump_texture_page(void) {
    const u8 types[] = {0, 1, 2, 255};
    const u32 pages[] = {0, 0x10, 0x60, 0x780, 0x800, 0x0123ffff, 0xfedc0123};
    const u32 callbacks[] = {0x8001935c, 0x80019a60};
    const int budgets[] = {45, 53, 4096};
    u32 previous = MEM_R32(0x80024554);
    for (unsigned g = 0; g < sizeof types; g++)
        for (unsigned p = 0; p < sizeof pages / sizeof pages[0]; p++)
            for (unsigned cb = 0; cb < sizeof callbacks / sizeof callbacks[0]; cb++)
                for (unsigned b = 0; b < sizeof budgets / sizeof budgets[0]; b++) {
                    MEM_W8(0x80024558, types[g]);
                    MEM_W32(0x80024554, callbacks[cb]);
                    CPUState state = arguments(pages[p], 0x1234, 0x5678, budgets[b]);
                    if (!compare(state, 0x8001b628, nc_credits_dump_texture_page, 44))
                        return false;
                }
    MEM_W32(0x80024554, previous);
    return true;
}

static bool check_dump_palette(void) {
    const u32 palettes[] = {0, 1, 0x3f, 0x40, 0xffc0, 0xffff, 0x12345678, 0xabcd0000};
    const u32 callbacks[] = {0x8001935c, 0x80019a60};
    const int budgets[] = {15, 18, 24, 4096};
    u32 previous = MEM_R32(0x80024554);
    for (unsigned i = 0; i < sizeof palettes / sizeof palettes[0]; i++)
        for (unsigned j = 0; j < sizeof callbacks / sizeof callbacks[0]; j++)
            for (unsigned k = 0; k < sizeof budgets / sizeof budgets[0]; k++) {
                MEM_W32(0x80024554, callbacks[j]);
                CPUState state = arguments(palettes[i], 0x1234, 0x5678, budgets[k]);
                if (i & 1)
                    state.r[29] = SCRATCH_BASE + 0x100;
                if (!compare(state, 0x8001b6f8, nc_credits_dump_palette, 14))
                    return false;
            }
    MEM_W32(0x80024554, previous);
    return true;
}

static bool check_dump_environment(bool draw, u8 graph) {
    const u32 recorder = 0x80060900, trace = 0x80060a00;
    /* Guest callback: record a0..a3 and the stack argument, then advance t1. */
    const u32 code[] = {0xad240000, 0xad250004, 0xad260008, 0xad27000c,
                        0x8fa80010, 0, 0xad280010, 0x03e00008, 0x25290014};
    const u32 callbacks[] = {recorder, 0x80019a60};
    const u32 envs[] = {0xa0060502, SCRATCH_BASE + 4, 0x801feee0};
    const int budgets[] = {5, 17, 32, 4096};
    u32 previous = MEM_R32(0x80024554);
    for (unsigned i = 0; i < sizeof code / sizeof code[0]; i++)
        MEM_W32(recorder + i * 4, code[i]);
    for (unsigned cb = 0; cb < sizeof callbacks / sizeof callbacks[0]; cb++)
        for (unsigned e = 0; e < sizeof envs / sizeof envs[0]; e++)
            for (unsigned b = 0; b < sizeof budgets / sizeof budgets[0]; b++) {
                MEM_W32(0x80024554, callbacks[cb]);
                MEM_W8(0x80024558, graph);
                for (unsigned j = 0; j < (draw ? 24u : 18u); j++)
                    MEM_W8(envs[e] + j, (u8)(j * 53 + 11));
                memset(nc_fast_ptr(trace), 0xcc, 120);
                CPUState state = arguments(envs[e], 0x1234, 0x5678, budgets[b]);
                state.r[9] = trace;
                if (!compare(state, draw ? 0x8001bc14 : 0x8001bd94,
                             draw ? nc_credits_dump_draw : nc_credits_dump_display, 4))
                    return false;
            }
    MEM_W32(0x80024554, previous);
    return true;
}

static bool check_display_mask(void) {
    const u32 env = 0x800245c4, gp1 = 0x1f801814;
    const u32 masks[] = {0, 1, UINT32_MAX};
    StateIO saved = {0};
    cpu_serialize(&saved);
    gpu_serialize(&saved);
    bool ok = !saved.error;
    for (unsigned i = 0; i < sizeof masks / sizeof masks[0] && ok; i++)
        for (unsigned debug = 0; debug < 2 && ok; debug++) {
            StateIO native = {0}, guest = {0};
            for (unsigned run = 0; run < 2 && ok; run++) {
                MEM_W8(0x8002455a, debug ? 2 : 0);
                MEM_W32(0x80024554, 0x8001935c); /* no-op diagnostic */
                memset(nc_fast_ptr(env - 1), 0x55, 22);
                MEM_W32(gp1, 0x03000000u | (masks[i] != 0));
                nc_cpu = arguments(masks[i], 2, 3, 13);
                if (!run && nc_credits_set_display_mask(&nc_cpu))
                    ok = false;
                nc_cpu.budget = debug ? 14 : 4096;
                if (run)
                    interp_call(&nc_cpu, 0x8001c244);
                else
                    ok = nc_credits_set_display_mask(&nc_cpu) && ok;
                StateIO *result = run ? &guest : &native;
                cpu_serialize(result);
                gpu_serialize(result);
                ok = ok && ((MEM_R32(gp1) >> 23) & 1) == (masks[i] == 0) &&
                     MEM_R8(env - 1) == 0x55 && MEM_R8(env + 20) == 0x55;
                for (unsigned j = 0; j < 20; j++)
                    ok = ok && MEM_R8(env + j) == (masks[i] ? 0x55 : 0xff);
                saved.loading = true;
                saved.pos = 0;
                cpu_serialize(&saved);
                gpu_serialize(&saved);
            }
            ok = ok && !saved.error && !native.error && !guest.error && native.len == guest.len &&
                 !memcmp(native.buf, guest.buf, guest.len);
            free(native.buf);
            free(guest.buf);
        }
    free(saved.buf);
    return ok;
}

static bool check_draw_primitive(void) {
    const u32 slot = 0x80024550, table = 0x80061000, sync = 0x80060c00, packet = 0x80062000;
    const u32 counts[] = {1, 0, 2, 255};
    StateIO saved = {0};
    cpu_serialize(&saved);
    gpu_serialize(&saved);
    bool ok = !saved.error;
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0] && ok; i++) {
        StateIO native = {0}, guest = {0};
        for (unsigned run = 0; run < 2 && ok; run++) {
            nc_cpu = arguments(packet, 2, 3, 17);
            if (i) {
                /* This sync clears the tag length and restores the disc driver before submission. */
                nc_cpu.r[8] = slot;
                nc_cpu.r[9] = MEM_R32(slot);
                MEM_W32(slot, table);
                MEM_W32(table + 0x3c, sync);
                MEM_W32(table + 0x14, 0x8001935c); /* no-op if the stale table is used */
                MEM_W32(sync, 0xa2040003); /* sb a0, 3(s0) */
                MEM_W32(sync + 4, 0x03e00008);
                MEM_W32(sync + 8, 0xad090000); /* sw t1, 0(t0) */
            }
            MEM_W32(packet, counts[i] << 24);
            MEM_W32(0x1f801810, 0xe1000000); /* draw mode zero */
            for (unsigned word = 0; word < counts[i]; word++)
                MEM_W32(packet + 4 + 4 * word, 0xe1000000u | ((word + 1) & 15));
            if (!run && nc_credits_draw_primitive(&nc_cpu))
                ok = false;
            nc_cpu.budget = i ? 18 : 4096;
            if (run)
                interp_call(&nc_cpu, 0x8001c880);
            else
                ok = nc_credits_draw_primitive(&nc_cpu) && ok;
            StateIO *result = run ? &guest : &native;
            cpu_serialize(result);
            gpu_serialize(result);
            ok = ok && (MEM_R32(0x1f801814) & 15) == (counts[i] & 15) &&
                 MEM_R8(packet + 3) == (i ? 0 : counts[i]);
            saved.loading = true;
            saved.pos = 0;
            cpu_serialize(&saved);
            gpu_serialize(&saved);
        }
        ok = ok && !saved.error && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    free(saved.buf);
    return ok;
}

static void defer_graphics_queue(u32 scratch) {
    /* Keep DMA busy so the original driver records the submission in queue slot zero. */
    MEM_W32(0x80023404, scratch);
    MEM_W32(0x800244a8, scratch + 4);
    MEM_W32(0x80024638, scratch + 8);
    MEM_W32(scratch + 8, 1u << 24);
    MEM_W32(0x800244a0, scratch + 16);
    MEM_W32(scratch + 20, 0x8001935c);
    MEM_W8(0x80024559, 1);
}

static bool check_load_tpage(void) {
    const u32 record = 0x80027488, pixels = 0x80062000;
    const struct { s32 mode, width; u16 words; } cases[] = {
        {0, 19, 4}, {0, -19, 0xfffc}, {1, 19, 9}, {1, -19, 0xfff7},
        {2, 19, 19}, {3, 19, 0x1234}, {-1, 19, 0x1234}
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    defer_graphics_queue(0x80060c00);
    MEM_W8(0x8002455a, 0);
    MEM_W8(0x80024558, 0); /* standard texture-page encoding */
    bool ok = !saved.error;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0] && ok; i++) {
        CPUState state = arguments(pixels, cases[i].mode, 2, i ? 39 : 4096);
        state.r[7] = 192;
        MEM_W32(state.r[29] + 16, 256);
        MEM_W32(state.r[29] + 20, cases[i].width);
        MEM_W32(state.r[29] + 24, 7);
        MEM_W16(state.r[29] - 28, 0x1234);
        MEM_W32(0x8002465c, 0);
        MEM_W32(0x80024660, 0);
        memset(nc_fast_ptr(record), 0xcc, 96);
        ok = compare(state, 0x8001b258, nc_credits_load_tpage, 38) &&
             nc_cpu.r[2] == (((cases[i].mode & 3u) << 7) | 0x53) &&
             MEM_R32(record + 8) == pixels && MEM_R16(record + 12) == 192 &&
             MEM_R16(record + 14) == 256 && MEM_R16(record + 16) == cases[i].words &&
             MEM_R16(record + 18) == 7;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_load_clut(u32 entry, bool (*routine)(CPUState *), u16 width) {
    const u32 record = 0x80027488, palette = 0x80062000;
    const u32 driver = MEM_R32(0x80024550);
    const u32 coords[][2] = {{32, 7}, {0xfffffff1, 0x123401ff}};
    StateIO saved = {0};
    cpu_serialize(&saved);
    defer_graphics_queue(0x80060c00);
    MEM_W8(0x8002455a, 0);
    bool ok = !saved.error;
    for (unsigned i = 0; i < sizeof coords / sizeof coords[0] && ok; i++) {
        u32 x = coords[i][0], y = coords[i][1];
        MEM_W32(0x8002465c, 0);
        MEM_W32(0x80024660, 0);
        memset(nc_fast_ptr(record), 0xcc, 96);
        CPUState state = arguments(palette, x, y, i ? 17 : 4096);
        ok = compare(state, entry, routine, 16) &&
             nc_cpu.r[2] == (((y & 0x3ff) << 6) | ((x >> 4) & 0x3f)) &&
             MEM_R32(record) == MEM_R32(driver + 0x20) && MEM_R32(record + 4) == record + 12 &&
             MEM_R32(record + 8) == palette && MEM_R16(record + 12) == (u16)x &&
             MEM_R16(record + 14) == (u16)y && MEM_R16(record + 16) == width &&
             MEM_R16(record + 18) == 1 && MEM_R8(record + 20) == 0xcc;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_move_image(void) {
    const u32 rect = 0x80062000, packet = 0x80024600, record = 0x80027488;
    const u32 driver = MEM_R32(0x80024550);
    const u32 sizes[] = {0x00020004, 0x00020000, 0x00000004, 0xffff0004};
    StateIO saved = {0};
    cpu_serialize(&saved);
    defer_graphics_queue(0x80060c00);
    MEM_W32(0x80024554, 0x8001935c); /* no-op diagnostic */
    bool ok = !saved.error;
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0] && ok; i++) {
        MEM_W8(0x8002455a, i ? 2 : 0);
        MEM_W32(0x8002465c, 0);
        MEM_W32(0x80024660, 0);
        MEM_W32(rect, 0x00100020);
        MEM_W32(rect + 4, sizes[i]);
        memset(nc_fast_ptr(packet + 8), 0xaa, 12);
        memset(nc_fast_ptr(record), 0xcc, 96);
        CPUState state = arguments(rect, 0x12340031, 0xabcd0042, i ? 13 : 4096);
        ok = compare(state, 0x8001c66c, nc_credits_move_image, 12);
        if (i == 1 || i == 2) {
            ok = ok && nc_cpu.r[2] == UINT32_MAX && MEM_R32(0x8002465c) == 0 &&
                 MEM_R32(packet + 8) == 0xaaaaaaaa && MEM_R32(record) == 0xcccccccc;
        } else {
            ok = ok && MEM_R32(record) == MEM_R32(driver + 0x18) &&
                 MEM_R32(record + 4) == record + 12 && MEM_R32(record + 8) == 0 &&
                 MEM_R32(packet + 8) == MEM_R32(rect) && MEM_R32(packet + 12) == 0x00420031 &&
                 MEM_R32(packet + 16) == sizes[i] &&
                 !memcmp(nc_fast_ptr(record + 12), nc_fast_ptr(packet), 20) &&
                 MEM_R8(record + 32) == 0xcc;
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_image_queue(u32 entry, bool (*routine)(CPUState *), u32 method_offset) {
    const bool clear = method_offset == 0x0c;
    const int threshold = clear ? 14 : 10;
    const u32 scratch = 0x80060c00, rect = scratch + 32, pixels = 0x80062000;
    const u32 record = 0x80027488, driver = MEM_R32(0x80024550);
    StateIO saved = {0};
    cpu_serialize(&saved);
    defer_graphics_queue(scratch);
    MEM_W32(0x80024554, 0x8001935c); /* no-op diagnostic */
    MEM_W32(rect, 0x00100020);
    MEM_W32(rect + 4, 0x00020004);
    bool ok = !saved.error;
    for (unsigned verbose = 0; verbose < 2 && ok; verbose++) {
        MEM_W8(0x8002455a, verbose ? 2 : 0);
        MEM_W32(0x8002465c, 0);
        MEM_W32(0x80024660, 0);
        memset(nc_fast_ptr(record), 0xcc, 96);
        CPUState state = arguments(rect, pixels, 3, verbose ? threshold + 1 : 4096);
        u32 queued_argument = pixels;
        if (clear) {
            state.r[5] = 0xff001234;
            state.r[6] = 0xaa005678;
            state.r[7] = 0x11009abc;
            queued_argument = routine == nc_credits_clear_image2 ? 0x80bc7834u : 0xbc7834u;
        }
        ok = compare(state, entry, routine, threshold) &&
             MEM_R32(record) == MEM_R32(driver + method_offset) && MEM_R32(record + 4) == record + 12 &&
             MEM_R32(record + 8) == queued_argument && !memcmp(nc_fast_ptr(record + 12), nc_fast_ptr(rect), 8) &&
             MEM_R8(record + 20) == 0xcc;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_draw_env_submission(bool linked) {
    const u32 env = 0x80062000, cached = 0x80024568, record = 0x80027488;
    const u32 ot = 0xa0068000;
    const int threshold = (linked ? 13 : 11) * NC_INSN_CYCLES + NC_LOAD_CYCLES;
    StateIO saved = {0};
    cpu_serialize(&saved);
    defer_graphics_queue(0x80060c00);
    MEM_W32(0x80024554, 0x8001935c); /* no-op diagnostic */
    bool ok = !saved.error;
    for (unsigned background = 0; ok && background < 2; background++) {
        memset(nc_fast_ptr(env), 0, 28);
        MEM_W32(env + 4, 0x00f00140); /* clip width and height */
        MEM_W32(env + 24, 0x12345600 | background);
        memset(nc_fast_ptr(env + 28), 0xab, 64);
        memset(nc_fast_ptr(cached), 0xdd, 92);
        memset(nc_fast_ptr(record), 0xcc, 96);
        MEM_W32(0x8002465c, 0);
        MEM_W32(0x80024660, 0);
        MEM_W8(0x8002455a, background ? 2 : 0);
        CPUState state = arguments(linked ? ot : env, linked ? env : 2, 3, background ? threshold + 1 : 4096);
        ok = compare(state, linked ? 0x8001ca18 : 0x8001c954,
                     linked ? nc_credits_draw_otag_env : nc_credits_put_draw_env, threshold) &&
             nc_cpu.r[2] == (linked ? cached : env) &&
             (MEM_R32(env + 28) & 0xffffff) == (linked ? ot & 0xffffff : 0xffffff) &&
             MEM_R32(record) == MEM_R32(MEM_R32(0x80024550) + 0x18) &&
             MEM_R32(record + 4) == record + 12 && MEM_R32(record + 8) == 0 &&
             !memcmp(nc_fast_ptr(record + 12), nc_fast_ptr(env + 28), 64) &&
             MEM_R8(record + 76) == 0xcc && !memcmp(nc_fast_ptr(cached), nc_fast_ptr(env), 92);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_draw_otag(void) {
    const u32 scratch = 0x80060c00, diagnostic = scratch + 32, alternate = scratch + 64;
    const u32 slot = 0x80024550, driver = MEM_R32(slot), ot = 0x80062000;
    const u32 noop = 0x8001935c, record = 0x80027488;
    StateIO saved = {0};
    cpu_serialize(&saved);
    defer_graphics_queue(scratch);
    MEM_W32(0x80024554, diagnostic);
    MEM_W32(diagnostic, 0xad440000);      /* sw a0, 0(t2) */
    MEM_W32(diagnostic + 4, 0x03e00008);  /* jr ra */
    MEM_W32(diagnostic + 8, 0xad090000);  /* sw t1, 0(t0): replace the driver */
    MEM_W32(alternate + 8, MEM_R32(driver + 8));
    MEM_W32(alternate + 0x18, noop);
    bool ok = !saved.error;
    for (unsigned verbose = 0; verbose < 2 && ok; verbose++)
        for (unsigned tight = 0; tight < 2 && ok; tight++) {
            MEM_W8(0x8002455a, verbose ? 2 : 1);
            MEM_W32(slot, driver);
            MEM_W32(0x8002465c, 0);
            MEM_W32(0x80024660, 0);
            MEM_W32(scratch + 28, 0);
            memset(nc_fast_ptr(record), 0xcc, 96);
            CPUState state = arguments(ot, 2, 3, tight ? 11 : 4096);
            state.r[8] = slot;
            state.r[9] = alternate;
            state.r[10] = scratch + 28;
            ok = compare(state, 0x8001c8e0, nc_credits_draw_otag, 10) &&
                 MEM_R32(record) == (verbose ? noop : MEM_R32(driver + 0x18)) &&
                 MEM_R32(record + 4) == ot && MEM_R32(record + 8) == 0 &&
                 MEM_R32(scratch + 28) == (verbose ? 0x80018600u : 0);
        }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_draw_sync(void) {
    const u32 slot = 0x80024550, diagnostic = 0x80060c00, table = 0x80061000;
    const u32 driver = MEM_R32(slot), printer = MEM_R32(0x80024554);
    const u8 debug = MEM_R8(0x8002455a);
    MEM_W8(0x8002455a, 0);
    bool ok = compare(arguments(1, 2, 3, 4096), 0x8001c2e0, nc_credits_draw_sync, 10);
    /* The diagnostic replaces the driver. Its two sync methods return mode and mode + 1. */
    MEM_W32(0x80024554, diagnostic);
    MEM_W32(diagnostic, 0x03e00008);      /* jr ra */
    MEM_W32(diagnostic + 4, 0xad090000);  /* sw t1, 0(t0) */
    MEM_W32(table + 0x3c, diagnostic + 8);
    MEM_W32(table + 0x80 + 0x3c, diagnostic + 16);
    MEM_W32(diagnostic + 8, 0x03e00008);
    MEM_W32(diagnostic + 12, 0x24820000); /* addiu v0, a0, 0 */
    MEM_W32(diagnostic + 16, 0x03e00008);
    MEM_W32(diagnostic + 20, 0x24820001); /* addiu v0, a0, 1 */
    const u32 modes[] = {0, 1, UINT32_MAX};
    for (unsigned i = 0; i < sizeof modes / sizeof modes[0] && ok; i++)
        for (unsigned verbose = 0; verbose < 2 && ok; verbose++) {
            MEM_W32(slot, table);
            MEM_W8(0x8002455a, verbose ? 2 : 1);
            CPUState state = arguments(modes[i], 2, 3, i ? 11 : 4096);
            state.r[8] = slot;
            state.r[9] = table + 0x80;
            ok = compare(state, 0x8001c2e0, nc_credits_draw_sync, 10) &&
                 nc_cpu.r[2] == modes[i] + verbose;
        }
    MEM_W32(slot, driver);
    MEM_W32(0x80024554, printer);
    MEM_W8(0x8002455a, debug);
    return ok;
}

static bool check_draw_sync_callback(void) {
    const u32 slot = 0x80024564, diagnostic = 0x80060c00;
    const u8 levels[] = {0, 1, 2, 255};
    const u32 handlers[] = {0, 0x8001935c, 0xa0060400};
    const int budgets[] = {11, 18, 29, 4096};
    const u32 previous_diagnostic = MEM_R32(0x80024554);
    /* The diagnostic changes the registered handler in its return delay slot. */
    MEM_W32(diagnostic, 0x03e00008);     /* jr ra */
    MEM_W32(diagnostic + 4, 0xad090000); /* sw t1, 0(t0) */
    MEM_W32(0x80024554, diagnostic);
    for (unsigned i = 0; i < sizeof levels; i++)
        for (unsigned j = 0; j < sizeof handlers / sizeof handlers[0]; j++)
            for (unsigned k = 0; k < sizeof budgets / sizeof budgets[0]; k++) {
                MEM_W8(0x8002455a, levels[i]);
                MEM_W32(slot, 0x800198ec);
                CPUState state = arguments(handlers[j], 0x1234, 0x5678, budgets[k]);
                state.r[8] = slot;
                state.r[9] = 0x80019364;
                if (!compare(state, 0x8001c1e8, nc_credits_draw_sync_callback, 10))
                    return false;
            }
    MEM_W32(0x80024554, previous_diagnostic);
    return true;
}

static bool check_cd_control(u32 entry, bool (*routine)(CPUState *)) {
    const bool fast = entry == 0x800200e4;
    const bool blocking = entry == 0x80020210;
    const int threshold = fast || blocking ? 22 : 24;
    /* Each CD_cw call records its arguments and the currently installed
     * callback, then reads the scripted result from the same trace row. */
    const u32 writer[] = {
        0xad040000, /* sw a0,0(t0) */
        0xad050004, /* sw a1,4(t0) */
        0xad060008, /* sw a2,8(t0) */
        0xad07000c, /* sw a3,12(t0) */
        0x8d2a0000, /* lw t2,0(t1) */
        0x00000000, /* nop */
        0xad0a0010, /* sw t2,16(t0) */
        0x8d020014, /* lw v0,20(t0) */
        0x03e00008, /* jr ra */
        0x25080018, /* addiu t0,t0,24 */
    };
    const u32 trace = 0x80060000, callback = 0x8001935c, location = 0x80060800;
    const struct {
        u32 command, params, needs_location, status;
        unsigned failures; /* bit i sets the result of CD_cw call i */
        unsigned calls, success;
    } cases[] = {
        {0x101, 0, 1, 0x10, 0, 1, 1},        /* status command skips preflight */
        {6, location, 0, 0, 1, 2, 1},         /* retry main command */
        {6, 0, 1, 0x10, 1, 2, 1},            /* preflight result is ignored */
        {6, location, 1, 0x10, 0, 3, 1},     /* status, Setloc, requested command */
        {6, location, 1, 0, 1, 3, 1},        /* Setloc fails once */
        {6, location, 1, 0, 15, 4, 0},       /* all Setloc attempts fail */
        {6, location, 1, 0, 0xaa, 8, 0},     /* main command fails four times */
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    if (blocking) {
        /* CD_sync records its mode in the result buffer and returns t3. */
        MEM_W32(0x80020adc, 0xaca40000); /* sw a0,0(a1) */
        MEM_W32(0x80020ae0, 0x03e00008); /* jr ra */
        MEM_W32(0x80020ae4, 0x01601021); /* addu v0,t3,zero */
    }
    for (unsigned i = 0; i < sizeof writer / sizeof writer[0]; i++)
        MEM_W32(0x80021028 + i * 4, writer[i]);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        u32 command = cases[i].command & 0xff;
        MEM_W32(0x800246d4 + 4 * command, cases[i].needs_location);
        MEM_W8(0x8002476c, cases[i].status);
        MEM_W32(0x8002475c, callback);
        for (unsigned j = 0; j < cases[i].calls; j++)
            MEM_W32(trace + 24 * j + 20, (cases[i].failures >> j) & 1);
        CPUState state = arguments(cases[i].command, cases[i].params, location + 8,
                                   i & 1 ? threshold + 1 : 4096);
        state.r[8] = trace;
        state.r[9] = 0x8002475c;
        state.r[11] = i == 3 ? 5 : 2; /* CdlDiskError or CdlComplete */
        MEM_W32(state.r[6], UINT32_MAX);
        bool sync_called = blocking && cases[i].success;
        bool success = cases[i].success && (!blocking || state.r[11] == 2);
        ok = compare(state, entry, routine, threshold) &&
             MEM_R32(state.r[6]) == (sync_called ? 0u : UINT32_MAX) &&
             nc_cpu.r[2] == success && nc_cpu.r[8] == trace + 24 * cases[i].calls &&
             MEM_R32(0x8002475c) == callback;
        for (unsigned j = 0; ok && j < cases[i].calls; j++) {
            u32 row = trace + 24 * j;
            u32 issued = MEM_R32(row);
            bool requested = issued == command;
            bool status_query = !requested && issued == 1;
            ok = MEM_R32(row + 8) == (fast || status_query ? 0 : state.r[6]) &&
                 MEM_R32(row + 12) == (fast && requested) &&
                 MEM_R32(row + 16) == (requested ? callback : 0);
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_rectangle_diagnostics(void) {
    const u32 rect = 0x80060800, callback = 0x80060c00, output = rect + 16;
    /* An exact fit, then each of the SDK's eight rejection branches. */
    const s16 cases[][4] = {{1, 2, 63, 30}, {0, 0, 65, 1}, {2, 0, 63, 1},
                            {0, 33, 1, 1}, {0, 31, 1, 2}, {0, 0, 0, 1},
                            {-1, 0, 1, 1}, {0, -1, 1, 1}, {0, 0, 1, -1}};
    StateIO saved = {0};
    cpu_serialize(&saved);
    MEM_W32(0x80024554, callback);
    MEM_W32(callback, 0xad240000);     /* sw a0,0(t1) */
    MEM_W32(callback + 4, 0x03e00008); /* jr ra */
    MEM_W32(callback + 8, 0x25290004); /* addiu t1,t1,4 */
    MEM_W16(0x8002455c, 64);
    MEM_W16(0x8002455e, 32);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        for (unsigned j = 0; j < 4; j++)
            MEM_W16(rect + j * 2, (u16)cases[i][j]);
        for (unsigned level = 0; ok && level < 4; level++) {
            MEM_W8(0x8002455a, level);
            MEM_W32(output, 0);
            MEM_W32(output + 4, 0);
            CPUState state = arguments(0x800185a8, rect, 0x5678, i & 1 ? 12 : 4096);
            state.r[9] = output;
            bool report = level == 2 || (level == 1 && i != 0);
            ok = compare(state, 0x8001c34c, nc_credits_check_rect, 11) &&
                 MEM_R32(output) == (report ? (level == 1 ? 0x8001857c : 0x8001859c) : 0) &&
                 MEM_R32(output + 4) == (report ? 0x80018588 : 0);
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_set_graph_reverse(void) {
    const u32 query = 0x80060c00, control = query + 16, output = control + 32;
    const u32 driver = MEM_R32(0x80024550);
    const int threshold = 11 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    StateIO saved = {0};
    cpu_serialize(&saved);
    MEM_W32(0x80024554, 0x8001935c);
    MEM_W32(driver + 0x28, query);
    MEM_W32(driver + 0x10, control);
    MEM_W32(query, 0x03e00008);       /* jr ra */
    MEM_W32(query + 4, 0x24820003);   /* addiu v0,a0,3 */
    MEM_W32(control, 0xad040000);     /* sw a0,0(t0) */
    MEM_W32(control + 4, 0x03e00008); /* jr ra */
    MEM_W32(control + 8, 0x25080004); /* addiu t0,t0,4 */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < 8; i++) {
        bool reverse = (i & 1) != 0, extra = (i & 2) != 0;
        MEM_W8(0x8002455b, 0x7f);
        MEM_W8(0x80024558, extra ? 2 : 1);
        MEM_W8(0x8002455a, i & 4 ? 2 : 1);
        MEM_W32(output + 4, 0);
        CPUState state = arguments(reverse ? 0x180 : 0x100, 0, 0, i & 4 ? threshold + 1 : 4096);
        state.r[8] = output;
        ok = compare(state, 0x8001bf9c, nc_credits_set_graph_reverse, threshold) &&
             nc_cpu.r[2] == 0x7f && MEM_R8(0x8002455b) == (u8)state.r[4] &&
             MEM_R32(output) == (reverse ? 0x0800008b : 0x0800000b) &&
             MEM_R32(output + 4) == (extra ? (reverse ? 0x20000501 : 0x20000504) : 0);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_set_graph_queue(void) {
    const u32 mode = 0x80024559, diagnostic = 0x80060c00;
    const u32 requested[] = {1, 0, 0x101, 7};
    const int threshold = 11 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    StateIO saved = {0};
    cpu_serialize(&saved);
    MEM_W32(diagnostic, 0x03e00008);     /* jr ra */
    MEM_W32(diagnostic + 4, 0xa1090000); /* sb t1, 0(t0) */
    MEM_W32(0x80024554, diagnostic);
    MEM_W32(MEM_R32(0x80024550) + 0x34, 0x8001935c);
    MEM_W32(MEM_R32(0x800244a0) + 4, 0x8001935c);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof requested / sizeof requested[0]; i++) {
        MEM_W8(mode, 1);
        MEM_W8(0x8002455a, i == 3 ? 2 : 0);
        CPUState state = arguments(requested[i], 0x1234, 0x5678, i ? threshold + 1 : 4096);
        state.r[8] = mode;
        state.r[9] = 7;
        ok = compare(state, 0x8001c11c, nc_credits_set_graph_queue, threshold) &&
             nc_cpu.r[2] == 1 && MEM_R8(mode) == (u8)requested[i];
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_set_graph_debug(void) {
    const u32 levels[] = {0, 1, 2, 0x100, 0xffffff80, 0xffffffff};
    /* Exercise a callback that preserves the arguments and another that uses them. */
    const u32 callbacks[] = {0x8001935c, 0x80019a60};
    const int budgets[] = {32, 39, 53, 4096};
    u32 previous = MEM_R32(0x80024554);
    for (unsigned i = 0; i < sizeof levels / sizeof levels[0]; i++)
        for (unsigned j = 0; j < sizeof callbacks / sizeof callbacks[0]; j++)
            for (unsigned k = 0; k < sizeof budgets / sizeof budgets[0]; k++) {
                MEM_W32(0x80024554, callbacks[j]);
                MEM_W32(0x80024558, 0x807f0012 + i);
                CPUState state = arguments(levels[i], 0xabcd, 0x1234, budgets[k]);
                if (!compare(state, 0x8001c0b0, nc_credits_set_graph_debug, 31))
                    return false;
            }
    MEM_W32(0x80024554, previous);
    return true;
}

static bool check_gpu_parameter(void) {
    const u32 data_slot = 0x80024628, control_slot = 0x8002462c;
    const u32 data_port = MEM_R32(data_slot), control_port = MEM_R32(control_slot);
    CPUState state = arguments(3, 0, 0, 19);
    if (!compare(state, 0x8001e124, nc_credits_gpu_parameter, 18))
        return false;

    const u32 data = 0x80060800;
    const u32 selectors[] = {0, 2, 5, 0x10000007, 0x8a123456, 0xffffffff};
    const u32 replies[] = {0, 0x00ffffff, 0xff000000, 0xab123456};
    for (unsigned i = 0; i < sizeof selectors / sizeof selectors[0]; i++)
        for (unsigned j = 0; j < sizeof replies / sizeof replies[0]; j++)
            for (unsigned alias = 0; alias < 2; alias++) {
                MEM_W32(data_slot, data);
                MEM_W32(control_slot, alias ? 0xa0060800 : SCRATCH_BASE);
                MEM_W32(data, replies[j]);
                state = arguments(selectors[i], 0, 0, 19);
                if (!compare(state, 0x8001e124, nc_credits_gpu_parameter, 18))
                    return false;
            }

    /* The GP1 write may redirect the data pointer itself. */
    MEM_W32(control_slot, data_slot);
    MEM_W32(data_slot, data);
    MEM_W32(data, 0x11223344);
    MEM_W32(SCRATCH_BASE, 0x55667788);
    state = arguments(SCRATCH_BASE, 0, 0, 19);
    bool ok = compare(state, 0x8001e124, nc_credits_gpu_parameter, 18);
    MEM_W32(data_slot, data_port);
    MEM_W32(control_slot, control_port);
    return ok;
}

static bool check_bios_gpu_command(void) {
    const u32 gp0 = MEM_R32(0x80024628), status = MEM_R32(0x8002462c);
    const u32 saved = (MEM_R32(status) >> 11) & 3;
    for (u32 mask = 0; mask < 4; mask++) {
        CPUState state = arguments(0xe6000000 | mask, 2, 3, mask & 1 ? 4 : 4096);
        if (!compare(state, 0x8001eca4, nc_credits_bios_gpu_command, 3))
            return false;
        /* GPU state is separate from the CPU snapshot. Verify each GP0 write. */
        for (unsigned native = 0; native < 2; native++) {
            MEM_W32(gp0, 0xe6000000 | (mask ^ 3));
            nc_cpu = state;
            if (native) {
                if (!nc_credits_bios_gpu_command(&nc_cpu))
                    return false;
            } else {
                interp_call(&nc_cpu, 0x8001eca4);
            }
            if (((MEM_R32(status) >> 11) & 3) != mask)
                return false;
        }
    }
    MEM_W32(gp0, 0xe6000000 | saved);
    return true;
}

static bool check_gpu_version(void) {
    const u32 gp0 = 0x80024628, gp1 = 0x8002462c;
    const u32 saved0 = MEM_R32(gp0), saved1 = MEM_R32(gp1);
    const u32 data = 0x80060840, control = SCRATCH_BASE + 8;
    for (unsigned path = 0; path < 3; path++)
        for (unsigned extended = 0; extended < 2; extended++) {
            MEM_W32(gp0, path == 1 ? control : data);
            MEM_W32(gp1, control);
            MEM_W32(data, path == 2 ? 0xab000002 : 0);
            MEM_W32(control, 0xffffffff);
            CPUState state = arguments(extended ? 8 : 0, 2, 3, extended ? 59 : 4096);
            u32 expected = path == 2 ? 3 + extended : path == 1 ? 1 + extended : 0;
            if (!compare(state, 0x8001eb9c, nc_credits_gpu_version, 58) || nc_cpu.r[2] != expected)
                return false;
        }
    MEM_W32(gp0, saved0);
    MEM_W32(gp1, saved1);

    StateIO initial = {0}, native = {0}, guest = {0};
    gpu_serialize(&initial);
    CPUState state = arguments(8, 2, 3, 4096);
    nc_cpu = state;
    bool ok = nc_credits_gpu_version(&nc_cpu);
    gpu_serialize(&native);
    initial.loading = true;
    gpu_serialize(&initial);
    nc_cpu = state;
    interp_call(&nc_cpu, 0x8001eb9c);
    gpu_serialize(&guest);
    ok = ok && !initial.error && !native.error && !guest.error && native.len == guest.len &&
         !memcmp(native.buf, guest.buf, guest.len);
    initial.pos = 0;
    gpu_serialize(&initial);
    free(initial.buf);
    free(native.buf);
    free(guest.buf);
    return ok;
}

static bool check_gpu_command_chain(void) {
    const u32 slots = 0x8002462c, head = 0x80060840;
    u32 ports[4];
    for (unsigned i = 0; i < 4; i++)
        ports[i] = MEM_R32(slots + 4 * i);
    const u32 targets[][4] = {
        {SCRATCH_BASE, head + 4, head + 8, head + 12},
        {head, 0xa0060840, head, head},
        {SCRATCH_BASE, slots + 8, SCRATCH_BASE + 4, SCRATCH_BASE + 8},
    };
    const u32 heads[] = {head, head + 4, 0xa0060848, SCRATCH_BASE + 16};
    for (unsigned i = 0; i < sizeof targets / sizeof targets[0]; i++)
        for (unsigned j = 0; j < sizeof heads / sizeof heads[0]; j++) {
            memset(nc_fast_ptr(head), 0x5a, 24);
            memset(nc_fast_ptr(SCRATCH_BASE), 0xa5, 24);
            for (unsigned k = 0; k < 4; k++)
                MEM_W32(slots + 4 * k, targets[i][k]);
            if (!compare(arguments(heads[j], 0, 0, 28), 0x8001e0d8, nc_credits_gpu_command_chain, 27))
                return false;
        }
    for (unsigned i = 0; i < 4; i++)
        MEM_W32(slots + 4 * i, ports[i]);

    /* A two-node list sets the mask bits and draw mode through GPU DMA. */
    MEM_W32(head, 0x01000000 | ((head + 8) & 0xffffff));
    MEM_W32(head + 4, 0xe6000003);
    MEM_W32(head + 8, 0x01ffffff);
    MEM_W32(head + 12, 0xe100001b);
    u32 old_status = MEM_R32(ports[0]);
    for (unsigned native = 0; native < 2; native++) {
        MEM_W32(ports[0], 0x04000000);
        MEM_W32(MEM_R32(0x80024628), 0xe6000000);
        MEM_W32(MEM_R32(0x80024628), 0xe1000000);
        nc_cpu = arguments(head, 0, 0, 28);
        if (native) {
            if (!nc_credits_gpu_command_chain(&nc_cpu))
                return false;
        } else {
            interp_call(&nc_cpu, 0x8001e0d8);
        }
        u32 status = MEM_R32(ports[0]);
        if (((status >> 29) & 3) != 2 || (status & 0x1fff) != 0x181b ||
            MEM_R32(ports[1]) != (head & 0xffffff) || MEM_R32(ports[2]) != 0 ||
            MEM_R32(ports[3]) != 0x401)
            return false;
    }
    MEM_W32(MEM_R32(0x80024628), 0xe6000000 | ((old_status >> 11) & 3));
    MEM_W32(MEM_R32(0x80024628), 0xe1000000 | (old_status & 0x7ff));
    MEM_W32(ports[0], 0x04000000 | ((old_status >> 29) & 3));
    return true;
}

static bool check_gpu_fill_bytes(void) {
    const u32 bases[] = {0x80060000, SCRATCH_BASE, 0xa0060000};
    const u32 counts[] = {0, 1, 7, 256};
    const int budgets[] = {4, 7, 9, 4096};
    for (unsigned i = 0; i < sizeof bases / sizeof bases[0]; i++)
        for (unsigned j = 0; j < sizeof counts / sizeof counts[0]; j++)
            for (unsigned k = 0; k < sizeof budgets / sizeof budgets[0]; k++) {
                u32 count = counts[j], base = bases[i];
                memset(nc_fast_ptr(base), 0x55, count + 2);
                CPUState state = arguments(base + 1, 0x123456ab, count, budgets[k]);
                if (!compare(state, 0x8001ec78, nc_credits_gpu_fill_bytes, 3))
                    return false;
                if (MEM_R8(base) != 0x55 || MEM_R8(base + count + 1) != 0x55)
                    return false;
                for (u32 n = 0; n < count; n++)
                    if (MEM_R8(base + 1 + n) != 0xab)
                        return false;
            }
    return true;
}

static bool check_reset_graph(void) {
    const u32 state_base = 0x80024558, trace = 0x80061000, stub = trace + 0x100;
    const u32 driver = trace + 0x200, modes[] = {0, 0x103, 1, 2, 4, 5, 6, 0xfffffff7};
    /* Record each external call by return address and first argument. The
     * real byte-fill routine still runs, including both environment fills. */
    const u32 calls[] = {0x80019a60, 0x8001a698, 0x8001eca4, 0x8001e758};
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    for (unsigned i = 0; i < sizeof calls / sizeof calls[0]; i++) {
        MEM_W32(calls[i], 0x08000000 | ((stub >> 2) & 0x03ffffff)); /* j stub */
        MEM_W32(calls[i] + 4, 0);
    }
    MEM_W32(stub, 0xad1f0000);      /* sw ra,0(t0) */
    MEM_W32(stub + 4, 0xad040004);  /* sw a0,4(t0) */
    MEM_W32(stub + 8, 0x25080008);  /* addiu t0,t0,8 */
    MEM_W32(stub + 12, 0x03e00008); /* jr ra */
    MEM_W32(stub + 16, 0x01601021); /* move v0,t3 */
    MEM_W32(0x80024550, driver);
    MEM_W32(0x80024554, stub);
    MEM_W32(driver + 0x34, stub);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof modes / sizeof modes[0]; i++) {
        u8 type = i & 3, debug = i & 1 ? 2 : 1;
        memset(nc_fast_ptr(state_base), 0xa5, 0x80);
        MEM_W8(state_base + 2, debug);
        MEM_W16(0x800245d8 + type * 4, 1024 + type);
        MEM_W16(0x800245ec + type * 4, 512 + type);
        CPUState state = arguments(modes[i], 2, 3, i & 1 ? 8 : 4096);
        state.r[8] = trace;
        state.r[11] = 0x100 + type;
        ok = compare(state, 0x8001be40, nc_credits_reset_graph, 7);
        if ((modes[i] & 7) == 0 || (modes[i] & 7) == 3) {
            ok = ok && nc_cpu.r[2] == type && nc_cpu.r[8] == trace + 32 &&
                 MEM_R32(trace) == 0x8001be88 && MEM_R32(trace + 8) == 0x8001bea0 &&
                 MEM_R32(trace + 16) == 0x8001beb8 && MEM_R32(trace + 20) == (driver & 0xffffff) &&
                 MEM_R32(trace + 24) == 0x8001bec0 && MEM_R32(trace + 28) == modes[i] &&
                 MEM_R8(state_base) == type && MEM_R8(state_base + 1) == 1 &&
                 MEM_R16(state_base + 4) == 1024 + type && MEM_R16(state_base + 6) == 512 + type;
            for (unsigned j = 8; ok && j < 0x80; j++)
                ok = MEM_R8(state_base + j) == (j < 16 ? 0 : 0xff);
        } else {
            u32 last = trace + (debug >= 2 ? 8 : 0);
            ok = ok && nc_cpu.r[2] == state.r[11] && nc_cpu.r[8] == last + 8 &&
                 MEM_R32(last) == 0x8001bf84 && MEM_R32(last + 4) == 1 &&
                 (debug < 2 || MEM_R32(trace) == 0x8001bf68) && MEM_R8(state_base) == 0xa5;
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_gpu_driver_reset(void) {
    const u32 ports = 0x80061000, first = 0x80024d30, second = 0x80027488;
    const u32 modes[] = {0, 1, 2, 3, 4, 5, 6, 7, 0x100, 0xfffffff9};
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Return the full mode plus seven from the version probe. */
    MEM_W32(0x8001eb9c, 0x03e00008);
    MEM_W32(0x8001eba0, 0x24820007);
    MEM_W32(0x8002462c, ports);
    MEM_W32(0x80024638, ports + 4);
    MEM_W32(0x80024648, ports + 8);
    MEM_W32(0x800244a8, ports + 12);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof modes / sizeof modes[0]; i++) {
        MEM_W32(ports, 0x12345678);
        MEM_W32(ports + 4, 0x01000201);
        MEM_W32(ports + 8, 0x12345000);
        MEM_W16(ports + 12, 0x145);
        MEM_W32(0x8002465c, 2);
        MEM_W32(0x80024660, 63);
        memset(nc_fast_ptr(first - 1), 0xa5, 0x102);
        memset(nc_fast_ptr(second - 1), 0xa5, 0x1802);
        CPUState state = arguments(modes[i], 2, 3, i & 1 ? 7 : 4096);
        u32 mode = modes[i] & 7;
        bool full = mode == 0, reset = full || mode == 1 || mode == 3;
        ok = compare(state, 0x8001e758, nc_credits_reset_gpu_driver, 6) &&
             nc_cpu.r[2] == (full ? modes[i] + 7 : 0) &&
             MEM_R32(0x8002465c) == 0 && MEM_R32(0x80024660) == 0 &&
             MEM_R32(0x8002466c) == 0x145 && MEM_R16(ports + 12) == 0x145 &&
             MEM_R32(ports) == (full ? 0 : reset ? 0x01000000u : 0x12345678u) &&
             MEM_R32(ports + 4) == (reset ? 0x401 : 0x01000201) &&
             MEM_R32(ports + 8) == (reset ? 0x12345800 : 0x12345000) &&
             MEM_R8(first - 1) == 0xa5 && MEM_R8(first + 0x100) == 0xa5 &&
             MEM_R8(second - 1) == 0xa5 && MEM_R8(second + 0x1800) == 0xa5;
        for (unsigned j = 0; ok && j < 0x1800; j++)
            ok = MEM_R8(second + j) == (full ? 0 : 0xa5) &&
                 (j >= 0x100 || MEM_R8(first + j) == (full ? 0 : 0xa5));
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_queue_alarm_expiry(void) {
    const u32 ports = 0x80061000, trace = ports + 32;
    const u32 report[] = {
        0xad240000, /* sw a0,0(t1) */
        0xad250004, /* sw a1,4(t1) */
        0xad260008, /* sw a2,8(t1) */
        0xad27000c, /* sw a3,12(t1) */
        0x03e00008, /* jr ra */
        0x25290010, /* addiu t1,t1,16 */
    };
    const struct {
        u32 frame, deadline, count;
        bool expired, count_advanced;
    } cases[] = {
        {7, 7, 0xf0000, false, true}, {7, 7, 0xf0001, true, true},
        {8, 7, 3, true, false}, {0xffffffff, 0, 0xffffffff, false, true},
        {0, 0xffffffff, 2, true, false},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    for (unsigned j = 0; j < sizeof report / sizeof report[0]; j++)
        MEM_W32(0x80019a60 + 4 * j, report[j]);
    MEM_W32(0x8002462c, ports);      /* GPUSTAT / GP1 */
    MEM_W32(0x80024630, ports + 4);  /* MADR */
    MEM_W32(0x80024638, ports + 8);  /* CHCR */
    MEM_W32(0x80024648, ports + 12); /* DPCR */
    MEM_W32(0x800244a8, ports + 16); /* I_MASK */
    MEM_W32(0x80023400, ports);
    MEM_W32(0x80023404, ports + 4);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        MEM_W32(0x800244d4, cases[i].frame);
        MEM_W32(0x80024670, cases[i].deadline);
        MEM_W32(0x80024674, cases[i].count);
        MEM_W32(0x8002465c, 2);
        MEM_W32(0x80024660, 63);
        MEM_W32(ports, 0x12345678);
        MEM_W32(ports + 4, 0x807654);
        MEM_W32(ports + 8, 0x01000201);
        MEM_W32(ports + 12, 0x12345000);
        MEM_W16(ports + 16, 0x145);
        CPUState state = arguments(1, 2, 3, i & 1 ? 5 : 4096);
        state.r[9] = trace;
        ok = compare(state, 0x8001ea30, nc_credits_check_queue_alarm, 4) &&
             nc_cpu.r[2] == (cases[i].expired ? UINT32_MAX : 0) &&
             MEM_R32(0x80024674) == cases[i].count + cases[i].count_advanced &&
             nc_cpu.r[9] == trace + (cases[i].expired ? 32 : 0) &&
             MEM_R16(ports + 16) == 0x145;
        if (cases[i].expired)
            ok = ok && MEM_R32(trace) == 0x80018660 && MEM_R32(trace + 4) == 3 &&
                 MEM_R32(trace + 8) == 0x12345678 && MEM_R32(trace + 12) == 0x01000201 &&
                 MEM_R32(trace + 16) == 0x80018694 && MEM_R32(state.r[29] - 16) == 0x807654 &&
                 MEM_R32(0x8002465c) == 0 && MEM_R32(0x80024660) == 0 &&
                 MEM_R32(0x8002466c) == 0x145 && MEM_R32(ports) == 0x01000000 &&
                 MEM_R32(ports + 8) == 0x401 && MEM_R32(ports + 12) == 0x12345800;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_set_queue_alarm(void) {
    const u32 frames[] = {0, 1, 0x7fffffff, 0xffffff0f, 0xffffff10, 0xffffffff};
    const int budgets[] = {5, 15, 32, 4096};
    const u32 timer_slot = 0x80023404, timer = MEM_R32(timer_slot);
    /* Keep VSync's timer read identical on each replay. */
    MEM_W32(timer_slot, 0x80060c00);
    for (unsigned i = 0; i < sizeof frames / sizeof frames[0]; i++)
        for (unsigned j = 0; j < sizeof budgets / sizeof budgets[0]; j++) {
            MEM_W32(0x800244d4, frames[i]);
            MEM_W32(0x80024670, 0xdecafbad);
            MEM_W32(0x80024674, 0xabcdef01);
            CPUState state = arguments(0x1357, 0xabcd, 0x5678, budgets[j]);
            if (i & 1)
                state.r[29] = SCRATCH_BASE + 0x100;
            if (!compare(state, 0x8001e9fc, nc_credits_set_queue_alarm, 4) ||
                MEM_R32(0x80024670) != frames[i] + 240 || MEM_R32(0x80024674) != 0)
                return false;
        }
    MEM_W32(timer_slot, timer);
    return true;
}

static bool check_queue_command(void) {
    const u32 scratch = 0x80060c00, command = 0x8001935c;
    const int budgets[] = {6, 11, 32, 4096};
    /* Give the original queue code deterministic timer and interrupt registers.
     * A busy DMA channel leaves the submitted item in the ring. */
    MEM_W32(0x80023404, scratch);
    MEM_W32(0x800244a8, scratch + 4);
    MEM_W32(0x80024638, scratch + 8);
    MEM_W32(0x800244a0, scratch + 16);
    MEM_W32(scratch + 20, command);
    for (unsigned queued = 0; queued < 2; queued++)
        for (unsigned head = 0; head <= 63; head += 63)
            for (unsigned i = 0; i < sizeof budgets / sizeof budgets[0]; i++) {
                u32 data = i & 1 ? SCRATCH_BASE + 4 : scratch + 32;
                u32 argument = 0xabcd0000 + i;
                MEM_W32(scratch + 8, queued << 24);
                MEM_W8(0x80024559, (u8)queued);
                MEM_W32(0x8002465c, head);
                MEM_W32(0x80024660, head);
                CPUState state = arguments(command, data, argument, budgets[i]);
                if (!compare(state, 0x8001e154, nc_credits_queue_command, 5))
                    return false;
                u32 record = queued ? 0x80027488 + head * 96 : 0x8002464c;
                if (MEM_R32(record) != command || MEM_R32(record + 4) != data ||
                    MEM_R32(record + 8) != argument || nc_cpu.r[2] != queued)
                    return false;
            }
    return true;
}

static bool check_gpu_command_block(void) {
    const u32 source = 0x80060800, data_slot = 0x80024628, control_slot = 0x8002462c;
    const u32 data_port = MEM_R32(data_slot), control_port = MEM_R32(control_slot);
    const u32 outputs[] = {SCRATCH_BASE, source + 4, 0xa0060800, data_slot};
    const int budgets[] = {11, 17, 26, 53, 4096};
    for (unsigned count = 0; count <= 5; count++)
        for (unsigned d = 0; d < sizeof outputs / sizeof outputs[0]; d++)
            for (unsigned b = 0; b < sizeof budgets / sizeof budgets[0]; b++) {
                for (unsigned i = 0; i < 5; i++)
                    MEM_W32(source + i * 4, SCRATCH_BASE + i * 4);
                MEM_W32(data_slot, outputs[d]);
                MEM_W32(control_slot, SCRATCH_BASE + 64);
                CPUState state = arguments(source, count, 0, budgets[b]);
                if (!compare(state, 0x8001e088, nc_credits_gpu_command_block, 10))
                    return false;
            }
    MEM_W32(data_slot, data_port);
    MEM_W32(control_slot, control_port);

    /* Send three draw-mode words through GP0 and observe the final GPU status. */
    u32 old_status = MEM_R32(control_port);
    for (unsigned native = 0; native < 2; native++) {
        MEM_W32(control_port, 0x04000002);
        MEM_W32(data_port, 0xe1000000);
        for (unsigned i = 0; i < 3; i++)
            MEM_W32(source + 4 * i, 0xe1000011 + i);
        nc_cpu = arguments(source, 3, 0, 4096);
        if (native) {
            if (!nc_credits_gpu_command_block(&nc_cpu))
                return false;
        } else {
            interp_call(&nc_cpu, 0x8001e088);
        }
        u32 status = MEM_R32(control_port);
        if (((status >> 29) & 3) != 0 || (status & 0x1ff) != 0x13)
            return false;
    }
    MEM_W32(data_port, 0xe1000000 | (old_status & 0x7ff));
    MEM_W32(control_port, 0x04000000 | ((old_status >> 29) & 3));
    return true;
}

static bool check_gpu_control(void) {
    const u32 cache = 0x80024d30, port = MEM_R32(0x8002462c);
    const u32 outputs[] = {SCRATCH_BASE + 4, 0xa0060500, cache + 4};
    const u32 words[] = {0, 0x03123480, 0x04ff00aa, 0x078040ff, 0xffffffff};
    for (unsigned i = 0; i < sizeof outputs / sizeof outputs[0]; i++)
        for (unsigned j = 0; j < sizeof words / sizeof words[0]; j++) {
            MEM_W32(0x8002462c, outputs[i]);
            MEM_W32(outputs[i], 0xabcdef12);
            MEM_W8(cache + (words[j] >> 24), 0x5a);
            if (!compare(arguments(words[j], 0, 0, 13), 0x8001e04c, nc_credits_gpu_control, 12))
                return false;
        }
    MEM_W32(0x8002462c, port);
    const u32 indices[] = {0, 4, 255, 256, 0xffffffff, SCRATCH_BASE - cache};
    for (unsigned i = 0; i < sizeof indices / sizeof indices[0]; i++) {
        MEM_W8(cache + indices[i], (u8)(0x90 + i));
        if (!compare(arguments(indices[i], 0, 0, 8), 0x8001e074, nc_credits_gpu_control_byte, 7))
            return false;
    }

    /* Exercise the real GP1 port from identical initial DMA directions. */
    u32 old_direction = (MEM_R32(port) >> 29) & 3;
    for (u32 dir = 0; dir < 4; dir++) {
        CPUState start = arguments(0x04000000 | dir, 0, 0, 13);
        MEM_W32(port, 0x04000000 | (dir ^ 3));
        nc_cpu = start;
        if (!nc_credits_gpu_control(&nc_cpu))
            return false;
        u32 native_direction = (MEM_R32(port) >> 29) & 3;
        MEM_W32(port, 0x04000000 | (dir ^ 3));
        nc_cpu = start;
        interp_call(&nc_cpu, 0x8001e04c);
        if (native_direction != dir || ((MEM_R32(port) >> 29) & 3) != dir)
            return false;
    }
    MEM_W32(port, 0x04000000 | old_direction);
    return true;
}

static bool check_display_field(void) {
    const u32 driver = MEM_R32(0x80024550), status_port = MEM_R32(0x8002462c);
    const u32 alternate_driver = 0xa0060a00, callback = 0x80060b00;
    const u32 words[] = {0, 0x7fffffff, 0x80000000, 0xffffffff};
    const int budgets[] = {13, 17, 24, 4096};
    CPUState state = arguments(0, 0, 0, 4096);
    /* Exercise the disc's original driver table and GPU status port first. */
    if (!compare(state, 0x8001d7ec, nc_credits_graphics_status, 10) ||
        !compare(state, 0x8001d014, nc_credits_display_field, 12))
        return false;

    MEM_W32(callback, 0x03e00008);     /* jr ra */
    MEM_W32(callback + 4, 0x00801021); /* move v0, a0 */
    MEM_W32(alternate_driver + 0x38, callback);
    MEM_W32(0x8002462c, SCRATCH_BASE);
    for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++) {
        MEM_W32(SCRATCH_BASE, words[i]);
        if (!compare(arguments(0, 0, 0, 11), 0x8001d7ec, nc_credits_graphics_status, 10))
            return false;
        for (unsigned j = 0; j < sizeof budgets / sizeof budgets[0]; j++)
            for (unsigned alternate = 0; alternate < 2; alternate++) {
                MEM_W32(0x80024550, alternate ? alternate_driver : driver);
                state = arguments(~words[i], 0, 0, budgets[j]);
                if (!compare(state, 0x8001d014, nc_credits_display_field, 12))
                    return false;
            }
    }
    MEM_W32(0x80024550, driver);
    MEM_W32(0x8002462c, status_port);
    return true;
}

static bool check_display_x(void) {
    const u32 rects[] = {0x80060800, SCRATCH_BASE + 2};
    const u8 graphs[] = {0, 1, 2, 3, 255};
    const s16 values[] = {-32768, -33, -1, 0, 1, 255, 1024, 32767};
    for (unsigned g = 0; g < sizeof graphs; g++)
        for (unsigned reverse = 0; reverse < 2; reverse++)
            for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++)
                for (unsigned j = 0; j < sizeof rects / sizeof rects[0]; j++) {
                    MEM_W8(0x80024558, graphs[g]);
                    MEM_W8(0x8002455b, reverse ? 0x80 : 0);
                    MEM_W16(rects[j], (u16)values[i]);
                    MEM_W16(rects[j] + 4, (u16)values[(i + 3) % 8]);
                    CPUState state = arguments(rects[j], 0, 0, 38);
                    if (!compare(state, 0x8001d730, nc_credits_display_x, 37))
                        return false;
                }
    return true;
}

static bool check_draw_offset(void) {
    const u8 types[] = {0, 1, 2, 255};
    const u32 coords[] = {0, 0x7ff, 0x800, 0xfff, 0x1000, 0x80008000, 0xffffffff};
    const u32 packet = 0x80060400;
    const u32 points[] = {SCRATCH_BASE + 2, 0xa0060400, packet + 2, packet + 4};
    for (unsigned g = 0; g < sizeof types; g++)
        for (unsigned i = 0; i < sizeof coords / sizeof coords[0]; i++) {
            u32 x = coords[i], y = coords[(i + 3) % 7];
            MEM_W8(0x80024558, types[g]);
            if (!compare(arguments(x, y, 0, 19), 0x8001d664, nc_credits_draw_offset_word, 18))
                return false;
            for (unsigned j = 0; j < sizeof points / sizeof points[0]; j++) {
                memset(nc_fast_ptr(packet), 0xa5, 16);
                MEM_W16(points[j], (u16)x);
                MEM_W16(points[j] + 2, (u16)y);
                if (!compare(arguments(packet, points[j], 0, 44), 0x8001d108,
                             nc_credits_draw_offset_packet, 43))
                    return false;
            }
        }
    return true;
}

static bool check_texture_window(void) {
    const u32 packet = 0x80060500, sp = 0x801fef00;
    const u32 rects[] = {0, SCRATCH_BASE + 4, packet, 0xa0060504, sp - 16, sp - 40};
    const u16 values[] = {0, 1, 7, 8, 31, 255, 256, 0xffff, 0x807f};
    for (unsigned build = 0; build <= 1; build++)
        for (unsigned i = 0; i < sizeof rects / sizeof rects[0]; i++)
            for (unsigned j = 0; j < sizeof values / sizeof values[0]; j++) {
                u32 rect = rects[i];
                memset(nc_fast_ptr(packet), 0xa5, 16);
                memset(nc_fast_ptr(sp - 40), 0xce, 40);
                if (rect)
                    for (unsigned k = 0; k < 4; k++)
                        MEM_W16(rect + 2 * k, values[(j + k) % 9]);
                int cost = (rect ? 39 : 7) + (build ? 19 : 0);
                CPUState state = arguments(build ? packet : rect, rect, 0xa98, cost + 1);
                if (!compare(state, build ? 0x8001d048 : 0x8001d6ac,
                             build ? nc_credits_texture_window_packet : nc_credits_texture_window_word,
                             cost))
                    return false;
            }
    return true;
}

static bool check_draw_mode_packet(void) {
    const u32 packet = 0x80060500;
    const u32 rects[] = {0, SCRATCH_BASE + 8, packet, 0xa0060504, 0x801feed0};
    const u8 types[] = {0, 1, 2, 255};
    for (unsigned i = 0; i < sizeof types; i++)
        for (unsigned flags = 0; flags < 4; flags++)
            for (unsigned j = 0; j < sizeof rects / sizeof rects[0]; j++) {
                CPUState state = arguments(packet, flags & 1, flags & 2, 89);
                state.r[7] = 0xf00d8f21;
                memset(nc_fast_ptr(packet), 0xa5, 16);
                if (rects[j]) {
                    MEM_W32(rects[j], 0x80ff00fe);
                    MEM_W32(rects[j] + 4, 0x00100040);
                }
                MEM_W8(0x80024558, types[i]);
                MEM_W32(state.r[29] + 16, rects[j]);
                if (!compare(state, 0x8001d174, nc_credits_draw_mode_packet, 88))
                    return false;
            }
    return true;
}

static bool check_transfer_packets(void) {
    const u32 packet = 0x80060500;
    const u32 rects[] = {SCRATCH_BASE + 4, packet, 0xa0060504, packet + 8, packet + 12};
    const s16 sizes[][2] = {{0, 0}, {0, 5}, {3, 0}, {1, 1}, {3, 7}, {4, 6}, {5, 5},
                            {-1, 1}, {-3, 1}, {-3, -2}, {-32768, -32768}, {32767, -32768}};
    for (unsigned load = 0; load <= 1; load++)
        for (unsigned i = 0; i < sizeof rects / sizeof rects[0]; i++)
            for (unsigned j = 0; j < sizeof sizes / sizeof sizes[0]; j++) {
                memset(nc_fast_ptr(packet), 0xa5, 72);
                MEM_W32(rects[i], 0xfedc1234);
                MEM_W16(rects[i] + 4, (u16)sizes[j][0]);
                MEM_W16(rects[i] + 6, (u16)sizes[j][1]);
                int threshold = load ? 35 : 32;
                CPUState state = arguments(packet, rects[i], 0x8765abcd, threshold + 1);
                state.r[7] = 0x1234cdef;
                if (!compare(state, load ? 0x8001bb70 : 0x8001bb10,
                             load ? nc_credits_draw_load : nc_credits_draw_move, threshold))
                    return false;
            }
    return true;
}

static bool check_merge_primitives(void) {
    const u32 first = 0x80060800;
    const u32 seconds[] = {first + 4, SCRATCH_BASE + 12, first, 0xa0060800};
    const u8 sizes[][2] = {{0, 0}, {1, 14}, {7, 8}, {8, 8}, {0, 16}, {255, 255}};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        for (unsigned j = 0; j < sizeof seconds / sizeof seconds[0]; j++) {
            MEM_W32(first, 0x00abcdef | ((u32)sizes[i][0] << 24));
            MEM_W32(seconds[j], 0x00123456 | ((u32)sizes[i][1] << 24));
            CPUState state = arguments(first, seconds[j], 0, 18);
            if (!compare(state, 0x8001bbdc, nc_credits_merge_primitives, 17))
                return false;
        }
    return true;
}

static bool check_priority_packet(void) {
    const u32 locations[] = {0x80060300, 0xa0060310, SCRATCH_BASE + 4};
    const u32 flags[] = {0, 1, 2, 0x80000000, 0xffffffff};
    for (unsigned i = 0; i < sizeof locations / sizeof locations[0]; i++)
        for (unsigned j = 0; j < sizeof flags / sizeof flags[0]; j++)
            for (unsigned k = 0; k < sizeof flags / sizeof flags[0]; k++) {
                memset(nc_fast_ptr(locations[i]), 0xa5, 16);
                int cost = flags[j] ? 10 : 9;
                CPUState state = arguments(locations[i], flags[j], flags[k], cost + 1);
                if (!compare(state, 0x8001d14c, nc_credits_priority_packet, cost))
                    return false;
            }
    return true;
}

static bool check_terminator(void) {
    const u32 tags[] = {0, 0x11100000, 0xa5ffffff, 0x01400000, 0xffabcdef};
    for (unsigned i = 0; i < sizeof tags / sizeof tags[0]; i++)
        for (unsigned j = 0; j < 2; j++) {
            u32 base = j ? SCRATCH_BASE : 0x80060300;
            MEM_W32(base, tags[i]);
            if (!compare(arguments(base, 0, 0, 9), 0x8001b80c, nc_credits_end_packet, 8))
                return false;
        }
    return true;
}

static bool check_palette(void) {
    const u32 coordinates[] = {0, 15, 16, 63, 255, 511, 1023, 1024, 0x80000000, 0xffffffff};
    for (unsigned x = 0; x < sizeof coordinates / sizeof coordinates[0]; x++)
        for (unsigned y = 0; y < sizeof coordinates / sizeof coordinates[0]; y++)
            if (!compare(arguments(coordinates[x], coordinates[y], 0, 7), 0x8001b610,
                         nc_credits_palette_id, 6))
                return false;
    return true;
}

static bool read_video_mode(CPUState *c) {
    return nc_credits_video_mode(c, false);
}
static bool write_video_mode(CPUState *c) {
    return nc_credits_video_mode(c, true);
}
static bool check_video_mode(void) {
    const u32 modes[] = {0, 1, 2, 0x807f8000, 0xffffffff};
    for (unsigned i = 0; i < 5; i++)
        for (unsigned j = 0; j < 5; j++) {
            MEM_W32(0x80024504, modes[i]);
            CPUState state = arguments(modes[j], 0, 0, 9);
            if (!compare(state, 0x8001b538, read_video_mode, 6) ||
                !compare(state, 0x8001b520, write_video_mode, 8))
                return false;
        }
    return true;
}

static bool check_default_display(void) {
    const u32 dest[] = {0x80060104, 0xa0060112, SCRATCH_BASE + 4, 0x801fef10};
    const u32 values[] = {0, 320, 0x80017fff, 0xffffffff};
    for (unsigned i = 0; i < 4; i++)
        for (unsigned j = 0; j < 4; j++) {
            CPUState state = arguments(dest[i], values[j], values[(j + 1) % 4], 18);
            state.r[7] = values[(j + 2) % 4];
            memset(nc_fast_ptr(dest[i]), 0xab, 24);
            MEM_W32(state.r[29] + 0x10, values[(j + 3) % 4]);
            if (!compare(state, 0x8001b4e4, nc_credits_default_display, 17))
                return false;
        }
    return true;
}

static bool graph_type(CPUState *c) {
    return nc_credits_graph_status(c, false);
}

static bool graph_debug(CPUState *c) {
    return nc_credits_graph_status(c, true);
}

static bool check_cd_test_parameter_count(void) {
    const u32 slot = 0x80024a04, saved = MEM_R32(slot);
    const u32 counts[] = {0, 1, 0x12345678, 0xffffffff};
    const int budgets[] = {5, 4096};
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++)
        for (unsigned j = 0; j < sizeof budgets / sizeof budgets[0]; j++) {
            MEM_W32(slot, ~counts[i]);
            CPUState state = arguments(counts[i], 2, 3, budgets[j]);
            if (!compare(state, 0x80021b44, nc_credits_cd_set_test_parameter_count, 4) ||
                MEM_R32(slot) != counts[i])
                return false;
        }
    MEM_W32(slot, saved);
    return true;
}

static bool spu_write_delay(CPUState *c) {
    return nc_credits_spu_set_delay(c, false);
}

static bool spu_read_delay(CPUState *c) {
    return nc_credits_spu_set_delay(c, true);
}

static bool check_spu_dma_complete(void) {
    const u32 spu = 0x80060000, callback = 0x80060400, trace = callback + 16;
    const u32 event = 0x8001fcd8;
    const int threshold = 4 * NC_INSN_CYCLES + NC_LOAD_CYCLES;
    StateIO saved = {0}, device = {0};
    cpu_serialize(&saved);
    spu_serialize(&device);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    MEM_W32(callback, 0x03e00008);
    MEM_W32(callback + 4, 0xaf030000); /* sw v1, 0(t8): poll count */
    MEM_W32(event, 0xaf040004);        /* sw a0, 4(t8): event class */
    MEM_W32(event + 4, 0x03e00008);
    MEM_W32(event + 8, 0xaf050008);    /* sw a1, 8(t8): event spec */
    bool ok = !saved.error && !device.error;
    for (unsigned i = 0; ok && i < 3; i++) {
        bool stuck = i == 2;
        /* SPUSTAT mirrors SPUCNT, so a write to the status port leaves the
         * mode bits set and exercises the bounded polling path. */
        if (stuck)
            spu_write(0x1aa, 0x30);
        MEM_W32(0x80024678, stuck ? 0x1f801c04u : spu);
        MEM_W16(spu + 0x1aa, 0x8134);
        MEM_W32(0x800246b0, i ? callback : 0);
        MEM_W32(0x800246c8, i ? 1 : 0);
        memset(nc_fast_ptr(trace), 0xff, 12);
        CPUState state = arguments(0, 0, 0, i ? threshold + 1 : 4096);
        state.r[24] = trace;
        ok = compare(state, 0x8001f58c, nc_credits_spu_dma_complete, threshold) &&
             (stuck || MEM_R16(spu + 0x1aa) == 0x8104) &&
             MEM_R32(trace) == (stuck ? 0xf01u : i ? 0 : UINT32_MAX) &&
             MEM_R32(trace + 4) == (i ? UINT32_MAX : 0xf0000009u) &&
             MEM_R32(trace + 8) == (i ? UINT32_MAX : 0x20u);
    }
    device.loading = saved.loading = true;
    spu_serialize(&device);
    cpu_serialize(&saved);
    free(device.buf);
    free(saved.buf);
    nc_set_module(module);
    return ok && !device.error && !saved.error;
}

static bool check_spu_read_blocks(void) {
    const u32 spu = 0x80060000, dma = 0x80060400, buffer = 0x80061000;
    const int threshold = 9 * NC_INSN_CYCLES + NC_LOAD_CYCLES;
    const u32 blocks[] = {0, 1, 0x10002};
    StateIO saved = {0};
    cpu_serialize(&saved);
    MEM_W32(0x80024678, spu);
    MEM_W32(0x80024684, dma + 8);
    MEM_W32(0x8002468c, dma + 12);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof blocks / sizeof blocks[0]; i++) {
        /* Alias the base register to the block-register pointer on the last run. */
        bool alias = i == 2;
        u32 base_reg = alias ? 0x80024680 : dma;
        MEM_W32(0x8002467c, base_reg);
        MEM_W32(0x80024680, dma + 4);
        MEM_W32(0x800246c8, 0);
        MEM_W16(spu + 0x1aa, 0x8104);
        memset(nc_fast_ptr(dma), 0xff, 16);
        CPUState state = arguments(buffer, 0xabcd8123, blocks[i], i ? threshold + 1 : 4096);
        ok = compare(state, 0x8001f65c, nc_credits_spu_read_blocks, threshold) &&
             MEM_R16(spu + 0x1a6) == 0x8123 && MEM_R16(spu + 0x1aa) == 0x8134 &&
             MEM_R32(base_reg) == buffer && MEM_R32(alias ? buffer : dma + 4) == (blocks[i] << 16 | 16) &&
             MEM_R32(dma + 8) == 0x01000200 && MEM_R32(0x800246c8) == 1;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_spu_transfer(bool reading, bool io) {
    const u32 spu = 0x80060000, dma = 0x80060400, buffer = 0x80061000;
    const int threshold = reading ? 11 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES
                                  : 8 * NC_INSN_CYCLES + NC_LOAD_CYCLES;
    const u32 sizes[] = {0, 64, 65};
    StateIO saved = {0};
    cpu_serialize(&saved);
    /* RAM-backed register banks expose the driver's writes to the comparison. */
    MEM_W32(0x80024678, spu);
    MEM_W32(0x8002467c, dma);
    MEM_W32(0x80024680, dma + 4);
    MEM_W32(0x80024684, dma + 8);
    MEM_W32(0x8002468c, dma + 12);
    for (unsigned i = 0; i < 34; i++)
        MEM_W16(buffer + 2 * i, 0x7000 + i);
    bool ok = !saved.error;
    MEM_W32(0x80024694, io ? 0x100 : 0);
    for (unsigned i = 0; ok && i < sizeof sizes / sizeof sizes[0]; i++) {
        MEM_W16(0x80024690, 0x1234);
        MEM_W32(0x800246a0, i ? 35 : 3);
        memset(nc_fast_ptr(dma), 0xff, 16);
        MEM_W16(spu + 0x1a8, 0xbeef);
        MEM_W16(spu + 0x1ae, 0);
        CPUState state = arguments(buffer, sizes[i], 0x1234, i ? threshold + 1 : 4096);
        ok = compare(state, reading ? 0x8001fa30 : 0x8001f9a8,
                     reading ? nc_credits_spu_read : nc_credits_spu_write, threshold) &&
             nc_cpu.r[2] == sizes[i] && MEM_R16(spu + 0x1a6) == 0x1234;
        if (io) {
            const u16 last = sizes[i] ? MEM_R16(buffer + ((sizes[i] - 1) & ~1u)) : 0xbeef;
            ok = ok && MEM_R16(spu + 0x1a8) == last && MEM_R32(dma + 8) == UINT32_MAX;
        } else {
            ok = ok && MEM_R32(dma) == buffer &&
                 MEM_R32(dma + 8) == (reading ? 0x01000200u : 0x01000201u) &&
                 MEM_R32(dma + 4) == (((sizes[i] + 63) / 64) << 16 | 16);
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_spu_write_register(void) {
    const u32 slot = 0x80024678, shift_slot = 0x800246a0;
    const u32 original = MEM_R32(slot), old_shift = MEM_R32(shift_slot);
    const u32 bases[] = {original, 0x80060000, SCRATCH_BASE};
    const u32 shifts[] = {0, 3, 31, 35};
    const u32 modes[] = {0, 1, UINT32_MAX};
    for (unsigned i = 0; i < sizeof bases / sizeof bases[0]; i++) {
        MEM_W32(slot, bases[i]);
        u32 addr = bases[i] + 6; /* voice 0 start address for the hardware case */
        u16 saved = MEM_R16(addr);
        for (unsigned j = 0; j < sizeof modes / sizeof modes[0]; j++)
            for (unsigned k = 0; k < sizeof shifts / sizeof shifts[0]; k++) {
                u32 value = 0x89abcdef;
                u16 expected = (u16)(value >> (modes[j] ? shifts[k] & 31 : 0));
                MEM_W32(shift_slot, shifts[k]);
                MEM_W16(addr, (u16)~expected);
                CPUState state = arguments(3, value, modes[j], k ? 16 : 4096);
                /* CPU snapshots omit SPU registers. Check the native store directly. */
                nc_cpu = state;
                if (!nc_credits_spu_write_register(&nc_cpu) || MEM_R16(addr) != expected)
                    return false;
                MEM_W16(addr, (u16)~expected);
                if (!compare(state, 0x8001fa98, nc_credits_spu_write_register, 15) ||
                    MEM_R16(addr) != expected)
                    return false;
            }
        MEM_W16(addr, saved);
    }
    MEM_W32(slot, original);
    MEM_W32(shift_slot, old_shift);
    return true;
}

static bool check_spu_set_address(void) {
    const u32 slot = 0x80024678, config = 0x8002469c;
    const u32 base = MEM_R32(slot);
    u32 saved[4];
    for (unsigned i = 0; i < 4; i++)
        saved[i] = MEM_R32(config + i * 4);
    const struct {
        u32 align, shift, unit, mask, address;
    } cases[] = {
        {0, 3, 8, 7, 0x12345}, {1, 3, 8, 7, 0x12348},
        {1, 3, 8, 7, 0x12345}, {1, 35, 8, 7, 0xfffffffe},
        {1, 1, 0, 3, 5}, {1, 31, 0, 3, 0},
    };
    const u32 indices[] = {3, (u32)-1, (u32)-2};
    u16 old = MEM_R16(base + 6);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        MEM_W32(config, cases[i].align);
        MEM_W32(config + 4, cases[i].shift);
        MEM_W32(config + 8, cases[i].unit);
        MEM_W32(config + 12, cases[i].mask);
        u32 aligned = cases[i].address;
        if (cases[i].align && (cases[i].unit ? aligned % cases[i].unit : aligned))
            aligned = (aligned + cases[i].unit) & ~cases[i].mask;
        u16 units = (u16)(aligned >> (cases[i].shift & 31));
        for (unsigned j = 0; j < sizeof indices / sizeof indices[0]; j++) {
            CPUState state = arguments(indices[j], cases[i].address, 7, i & 1 ? 48 : 4096);
            u16 initial = (u16)~units;
            MEM_W16(base + 6, initial);
            nc_cpu = state;
            if (!nc_credits_spu_set_address(&nc_cpu) || MEM_R16(base + 6) != (j ? initial : units))
                return false;
            MEM_W16(base + 6, initial);
            if (!compare(state, 0x8001fae0, nc_credits_spu_set_address, 47) ||
                nc_cpu.r[2] != (j == 1 ? units : aligned))
                return false;
        }
    }
    MEM_W16(base + 6, old);
    for (unsigned i = 0; i < 4; i++)
        MEM_W32(config + i * 4, saved[i]);
    return true;
}

static bool check_spu_read_address(void) {
    const u32 slot = 0x80024678, shift_slot = 0x800246a0;
    const u32 base = MEM_R32(slot), shift = MEM_R32(shift_slot);
    const u32 bases[] = {base, 0x80060000, SCRATCH_BASE};
    const u32 modes[] = {UINT32_MAX, 0, 1};
    const u32 shifts[] = {3, 31, 32, 35};
    if (base != 0x1f801c00 || shift != 3)
        return false;
    for (unsigned i = 0; i < sizeof bases / sizeof bases[0]; i++) {
        MEM_W32(slot, bases[i]);
        /* Voice 0 start address on the SPU register path. */
        u32 addr = bases[i] + 6;
        u16 old = MEM_R16(addr);
        MEM_W16(addr, 0x8021);
        for (unsigned j = 0; j < sizeof modes / sizeof modes[0]; j++)
            for (unsigned k = 0; k < sizeof shifts / sizeof shifts[0]; k++) {
                MEM_W32(shift_slot, shifts[k]);
                CPUState state = arguments(3, modes[j], 7, k ? 21 : 4096);
                u32 expected = modes[j] == UINT32_MAX ? 0x8021u : 0x8021u << (shifts[k] & 31);
                if (!compare(state, 0x8001fb84, nc_credits_spu_read_address, 20) ||
                    nc_cpu.r[2] != expected || MEM_R16(addr) != 0x8021)
                    return false;
            }
        MEM_W16(addr, old);
    }
    MEM_W32(slot, base);
    MEM_W32(shift_slot, shift);
    return true;
}

static bool check_spu_dma_priority(void) {
    const u32 slot = 0x80024688, original = MEM_R32(slot);
    const u32 targets[] = {original, SCRATCH_BASE + 4, slot};
    const u32 modes[] = {0, 1, UINT32_MAX};
    if (original != 0x1f8010f0)
        return false;
    for (unsigned i = 0; i < sizeof targets / sizeof targets[0]; i++) {
        u32 first = targets[i];
        /* With the pointer aimed at itself the first store redirects the second. */
        u32 second = first == slot ? slot & 0xfff8ffff : first;
        u32 saved = MEM_R32(second);
        for (unsigned j = 0; j < sizeof modes / sizeof modes[0]; j++) {
            MEM_W32(slot, first);
            MEM_W32(second, 0xa5af6789);
            u32 expected = (first == second ? 0xa5a86789 : 0xa5af6789) |
                           (modes[j] ? 0x30000 : 0x50000);
            CPUState state = arguments(modes[j], 2, 3, j ? 28 : 4096);
            nc_cpu = state;
            if (!nc_credits_spu_set_dma_priority(&nc_cpu) || MEM_R32(second) != expected)
                return false;
            MEM_W32(slot, first);
            MEM_W32(second, 0xa5af6789);
            if (!compare(state, 0x8001fbc0, nc_credits_spu_set_dma_priority, 27) ||
                nc_cpu.r[2] != second || nc_cpu.r[3] != expected)
                return false;
        }
        MEM_W32(second, saved);
    }
    MEM_W32(slot, original);
    return true;
}

static bool check_spu_wait(void) {
    const u32 stacks[] = {0x80061000, 0xa0061000, SCRATCH_BASE + 0x100};
    const int budgets[] = {7, 13, 512, 5536, 5537};
    u32 product = 13;
    for (unsigned i = 0; i < 240; i++)
        product *= 3;
    for (unsigned i = 0; i < sizeof stacks / sizeof stacks[0]; i++)
        for (unsigned j = 0; j < sizeof budgets / sizeof budgets[0]; j++) {
            CPUState state = arguments(1, 2, 3, budgets[j]);
            state.r[29] = stacks[i];
            MEM_W32(stacks[i] - 8, UINT32_MAX);
            MEM_W32(stacks[i] - 4, 0);
            if (!compare(state, 0x8001fc78, nc_credits_spu_wait, 6) ||
                MEM_R32(stacks[i] - 8) != 240 || MEM_R32(stacks[i] - 4) != product)
                return false;
        }
    return true;
}

static bool check_tim_info(void) {
    const u32 input = 0x80061000;
    const u32 outputs[] = {SCRATCH_BASE + 0x100, input, input + 4, input - 8};
    const u32 modes[] = {0, 2, 8, 9};
    for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++)
        for (unsigned j = 0; j < sizeof outputs / sizeof outputs[0]; j++) {
            memset(nc_fast_ptr(outputs[j]), 0xcc, 28);
            for (unsigned n = 0; n < 64; n += 2)
                MEM_W16(input + n, (u16)(0x8081 + 37 * n));
            MEM_W32(input, modes[i]);
            MEM_W32(input + 4, 19); /* next block uses the length rounded down to words */
            CPUState state = arguments(input, outputs[j], 7, j ? 64 : 4096);
            if (!compare(state, 0x8001ecb4, nc_credits_tim_info, 63))
                return false;
            if (j == 0) {
                u32 pixels = input + ((modes[i] & 8) ? 24 : 8);
                if (MEM_R32(outputs[j] + 12) != pixels + 8 ||
                    MEM_R32(outputs[j] + 24) != ((modes[i] & 8) ? input + 16 : 0xcccccccc))
                    return false;
            }
        }
    return true;
}

static bool check_spu_delays(void) {
    const u32 slot = 0x8002468c, original = MEM_R32(slot);
    const u32 addresses[] = {original, 0x80060000, SCRATCH_BASE + 4};
    const u32 words[] = {0, 0xffffffff, 0xa5192345};
    if (original != 0x1f801014)
        return false;
    for (unsigned i = 0; i < sizeof addresses / sizeof addresses[0]; i++) {
        MEM_W32(slot, addresses[i]);
        for (unsigned j = 0; j < sizeof words / sizeof words[0]; j++)
            for (unsigned reading = 0; reading < 2; reading++) {
                if (i)
                    MEM_W32(addresses[i], words[j]);
                u32 expected = (MEM_R32(addresses[i]) & 0xf0ffffff) |
                               (reading ? 0x22000000 : 0x20000000);
                CPUState state = arguments(1, 2, 3, j ? 16 : 4096);
                if (!compare(state, reading ? 0x8001fc4c : 0x8001fc20,
                             reading ? spu_read_delay : spu_write_delay, 15) ||
                    nc_cpu.r[2] != expected || (i && MEM_R32(addresses[i]) != expected))
                    return false;
            }
    }
    MEM_W32(slot, original);
    return true;
}

static bool check_cd_default_callbacks(void) {
    const struct {
        u32 entry, spec;
        bool (*callback)(CPUState *);
    } cases[] = {
        {0x8001fd78, 0x20, nc_credits_cd_default_sync},
        {0x8001fda0, 0x40, nc_credits_cd_default_ready},
        {0x8001fdc8, 0x40, nc_credits_cd_default_read},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        CPUState event = arguments(0xf0000003, cases[i].spec, 0x2000, 4096);
        event.r[9] = 8; /* OpenEvent, polling mode */
        bios_call(&event, 0xb0);
        event.r[4] = event.r[2];
        if (event.r[4] == UINT32_MAX)
            return false;
        event.r[9] = 12; /* EnableEvent */
        bios_call(&event, 0xb0);
        for (unsigned j = 0; j < 2; j++) {
            CPUState state = arguments(1, 2, 3, j ? 7 : 4096);
            if (j)
                state.r[29] = SCRATCH_BASE + 0x100;
            nc_cpu = state;
            if (!cases[i].callback(&nc_cpu))
                return false;
            event.r[9] = 11; /* TestEvent consumes the notification. */
            bios_call(&event, 0xb0);
            if (!event.r[2] || !compare(state, cases[i].entry, cases[i].callback, 6))
                return false;
            bios_call(&event, 0xb0);
            if (!event.r[2])
                return false;
        }
        /* Exercise the BIOS stub directly with a matching and a different event. */
        for (unsigned j = 0; j < 2; j++) {
            CPUState state = arguments(0xf0000003, cases[i].spec + j, 0, j ? 4096 : 4);
            event.r[9] = 11;
            nc_cpu = state;
            if (!nc_credits_deliver_event(&nc_cpu))
                return false;
            bios_call(&event, 0xb0);
            if (event.r[2] != (j == 0) ||
                !compare(state, 0x8001fcd8, nc_credits_deliver_event, 3))
                return false;
            bios_call(&event, 0xb0);
            if (event.r[2] != (j == 0))
                return false;
        }
        event.r[9] = 9; /* CloseEvent */
        bios_call(&event, 0xb0);
    }
    return true;
}

static bool check_callback_controls(void) {
    const u32 slot = 0x800244a0, original = MEM_R32(slot), table = 0x80061000;
    const u32 debug = 0x80024768, saved_debug = MEM_R32(debug);
    const struct {
        u32 entry, offset;
        int threshold;
        bool (*routine)(CPUState *);
    } cases[] = {
        {0x8001a6f8, 4, 12, nc_credits_dma_callback},
        {0x8001a728, 20, 13, nc_credits_vsync_callback},
        {0x8001a75c, 20, 12, nc_credits_vsync_callbacks},
        {0x8001a78c, 16, 12, nc_credits_stop_callback},
        {0x8001a7bc, 24, 12, nc_credits_restart_callback},
    };
    MEM_W32(slot, table);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        for (unsigned offset = 0; offset < 28; offset += 4)
            MEM_W32(table + offset, 0x8001fe20); /* CdLastPos */
        MEM_W32(table + cases[i].offset, 0x8001febc); /* CdSetDebug records a0 */
        for (unsigned j = 0; j < 2; j++) {
            MEM_W32(debug, 0xabcd1234);
            CPUState state = arguments(0x8001935c, 0x13579, 3, j ? cases[i].threshold + 1 : 4096);
            if (j)
                state.r[29] = SCRATCH_BASE + 0x100;
            bool default_slot = cases[i].entry == 0x8001a728;
            if (!compare(state, cases[i].entry, cases[i].routine, cases[i].threshold) ||
                nc_cpu.r[2] != 0xabcd1234 || MEM_R32(debug) != (default_slot ? 0 : state.r[4]) ||
                nc_cpu.r[5] != (default_slot ? state.r[4] : state.r[5]))
                return false;
        }
    }
    MEM_W32(debug, saved_debug);
    MEM_W32(slot, original);
    return true;
}

static bool read_irq_mask(CPUState *c) {
    return nc_credits_interrupt_mask(c, false);
}

static bool write_irq_mask(CPUState *c) {
    return nc_credits_interrupt_mask(c, true);
}

static bool check_pad_patch(void) {
    CPUState query = arguments(0, 0, 0, 4096);
    query.r[9] = 0x57;
    bios_call(&query, 0xb0);
    const u32 slot = query.r[2] + 0x16c, original = MEM_R32(slot);
    const u32 destinations[] = {original + 0x594, SCRATCH_BASE + 4};
    const u32 saved_ra = MEM_R32(0x80024c80);
    bool ok = true;
    for (unsigned i = 0; i < sizeof destinations / sizeof destinations[0] && ok; i++) {
        u32 start = destinations[i];
        u8 saved[52]; /* eleven words and two guards */
        memcpy(saved, nc_fast_ptr(start - 4), sizeof saved);
        memset(nc_fast_ptr(start - 4), 0xa5, sizeof saved);
        MEM_W32(slot, start - 0x594);
        CPUState state = arguments(7, 2, 3, i ? 4096 : 72);
        state.cop0[12] = 0x401;
        ok = compare(state, 0x8001a3e4, nc_credits_patch_pad, 71) &&
             MEM_R32(0x80024c80) == state.r[31] && (nc_cpu.cop0[12] & 0x401) == 0 &&
             MEM_R32(start - 4) == 0xa5a5a5a5 && MEM_R32(start + 44) == 0xa5a5a5a5;
        for (unsigned word = 0; word < 11; word++)
            ok = ok && MEM_R32(start + 4 * word) == 0;
        memcpy(nc_fast_ptr(start - 4), saved, sizeof saved);
    }
    MEM_W32(slot, original);
    MEM_W32(0x80024c80, saved_ra);
    return ok && compare(arguments(7, 2, 3, 4), 0x8001a434, nc_credits_flush_cache, 3);
}

static bool check_pad_setup(void) {
    const struct {
        u32 entry;
        int threshold;
        u32 expected_return;
        u32 initial_irq_bits, expected_irq_bits;
        bool (*routine)(CPUState *);
    } cases[] = {
        {0x8001a28c, 3, 0, 0x401, 0x401, nc_credits_bios_stop_pad},
        {0x8001a2bc, 11, 1, 0x401, 0x401, nc_credits_legacy_pad_init},
        {0x8001a3d4, 3, 1, 0, 0x401, nc_credits_bios_pad_init_and_start},
        {0x8001a324, 11, 1, 0x401, 0, nc_credits_sdk_init_pad},
        {0x8001a3b4, 3, 1, 0x401, 0x401, nc_credits_bios_init_pad},
        {0x8001a38c, 4, 0, 0, 0x401, nc_credits_start_pad},
        {0x8001a3c4, 3, 1, 0, 0x401, nc_credits_bios_start_pad},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    bios_serialize(&saved);
    bool ok = !saved.error;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0] && ok; i++)
        for (unsigned started = 0; started < 2 && ok; started++) {
            StateIO native = {0}, guest = {0};
            for (unsigned run = 0; run < 2 && ok; run++) {
                nc_cpu = arguments(0x80061000, 34, 0x80061040, cases[i].threshold);
                nc_cpu.r[7] = 32;
                nc_cpu.r[9] = started ? 0x13 : 0x14;
                bios_call(&nc_cpu, 0xb0);
                nc_cpu.cop0[12] = cases[i].initial_irq_bits;
                nc_cpu.r[2] = UINT32_MAX;
                if (!run && cases[i].routine(&nc_cpu))
                    ok = false;
                nc_cpu.budget = started ? 4096 : cases[i].threshold + 1;
                if (run)
                    interp_call(&nc_cpu, cases[i].entry);
                else
                    ok = cases[i].routine(&nc_cpu) && ok;
                StateIO *result = run ? &guest : &native;
                cpu_serialize(result);
                bios_serialize(result);
                ok = ok && nc_cpu.r[2] == cases[i].expected_return &&
                     (nc_cpu.cop0[12] & 0x401) == cases[i].expected_irq_bits;
                saved.loading = true;
                saved.pos = 0;
                cpu_serialize(&saved);
                bios_serialize(&saved);
            }
            ok = ok && !saved.error && !native.error && !guest.error && native.len == guest.len &&
                 !memcmp(native.buf, guest.buf, guest.len);
            free(native.buf);
            free(guest.buf);
        }
    free(saved.buf);
    return ok;
}

static bool check_interrupt_auto_clear(void) {
    for (u32 flag = 0; flag <= 1; flag++) {
        CPUState pad = arguments(flag, 0x12345678, 3, 4);
        if (!compare(pad, 0x8001a2ac, nc_credits_change_clear_pad, 3) ||
            nc_cpu.r[2] != 0 || nc_cpu.r[9] != 0x5b || nc_cpu.r[10] != 0xb0)
            return false;
        for (u32 counter = 0; counter < 4; counter++) {
            CPUState state = arguments(counter, flag, 3, 4);
            if (!compare(state, 0x8001a688, nc_credits_change_clear_counter, 3) ||
                nc_cpu.r[2] != 1 || nc_cpu.r[9] != 0x0a || nc_cpu.r[10] != 0xc0)
                return false;
        }
    }
    return true;
}

static bool check_irq_entry_hooks(void) {
    const struct {
        u32 entry, service, buffer;
        bool (*routine)(CPUState *);
    } cases[] = {
        {0x8001ae58, 0x18, 0x12345678, nc_credits_reset_irq_entry},
        {0x8001ae68, 0x19, 0x80023450, nc_credits_hook_irq_entry},
        {0x8001ae68, 0x19, 0, nc_credits_hook_irq_entry},
    };
    StateIO saved = {0};
    bios_serialize(&saved);
    bool ok = !saved.error;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0] && ok; i++) {
        CPUState state = arguments(cases[i].buffer, 2, 3, 4);
        ok = compare(state, cases[i].entry, cases[i].routine, 3) &&
             nc_cpu.r[9] == cases[i].service && nc_cpu.r[10] == 0xb0;
        StateIO native = {0}, guest = {0};
        for (unsigned run = 0; run < 2 && ok; run++) {
            nc_cpu = arguments(0x80061000, 0, 0, 4096);
            nc_cpu.r[9] = 0x19;
            bios_call(&nc_cpu, 0xb0); /* replace or clear an existing hook */
            nc_cpu = state;
            if (run)
                interp_call(&nc_cpu, cases[i].entry);
            else
                ok = cases[i].routine(&nc_cpu);
            bios_serialize(run ? &guest : &native);
        }
        ok = ok && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    saved.loading = true;
    bios_serialize(&saved);
    ok = ok && !saved.error;
    free(saved.buf);
    return ok;
}

static bool check_critical_sections(void) {
    const u32 enable_bits[] = {0, 1, 0x400, 0x401};
    for (unsigned i = 0; i < sizeof enable_bits / sizeof enable_bits[0]; i++) {
        CPUState state = arguments(0x12345678, 0, 0, 5);
        u32 status = 0x20001000 | enable_bits[i];
        state.cop0[12] = status;
        if (!compare(state, 0x8001a444, nc_credits_enter_critical_section, 4) ||
            nc_cpu.r[4] != 1 || nc_cpu.r[2] != (enable_bits[i] == 0x401) ||
            nc_cpu.cop0[12] != (status & ~0x401u))
            return false;
        if (!compare(state, 0x8001ae78, nc_credits_exit_critical_section, 4) ||
            nc_cpu.r[4] != 2 || nc_cpu.r[2] != state.r[2] ||
            nc_cpu.cop0[12] != (status | 0x401u))
            return false;
    }
    return true;
}

static bool check_save_irq_context(void) {
    const u32 buffers[] = {0x80061004, 0xa0061004, SCRATCH_BASE + 4};
    const unsigned registers[] = {31, 29, 30, 16, 17, 18, 19, 20, 21, 22, 23, 28};
    for (unsigned i = 0; i < sizeof buffers / sizeof buffers[0]; i++) {
        u32 buffer = buffers[i];
        memset(nc_fast_ptr(buffer - 4), 0xa5, 56);
        CPUState state = arguments(buffer, 0, 0, i == 0 ? 16 : 4096);
        if (!compare(state, 0x8001ada8, nc_credits_save_irq_context, 15) ||
            nc_cpu.r[2] != 0 || MEM_R32(buffer - 4) != 0xa5a5a5a5 ||
            MEM_R32(buffer + 48) != 0xa5a5a5a5)
            return false;
        for (unsigned word = 0; word < sizeof registers / sizeof registers[0]; word++)
            if (MEM_R32(buffer + 4 * word) != state.r[registers[word]])
                return false;
    }
    return true;
}

static void interrupt_start_state(StateIO *io) {
    cpu_serialize(io);
    timers_serialize(io);
    dma_serialize(io);
    irq_serialize(io);
}

static bool check_set_interrupt_handler(void) {
    const struct {
        unsigned irq;
        u32 old_handler, new_handler;
        bool active;
    } cases[] = {
        {0, 0, 0x8001935c, true}, {0, 0x8001935c, 0, true},
        {4, 0, 0x8001935c, true}, {5, 0x8001935c, 0, true},
        {6, 0x8001935c, 0x800198ec, true}, {2, 0, 0x8001935c, true},
        {2, 0x8001935c, 0x8001935c, true}, {2, 0x8001935c, 0, false},
    };
    StateIO saved = {0};
    interrupt_start_state(&saved);
    bios_serialize(&saved);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        StateIO native = {0}, guest = {0};
        u32 slot = 0x8002341c + 4 * cases[i].irq;
        u16 bit = 1u << cases[i].irq;
        u16 hardware = 0x280 | (cases[i].old_handler ? bit : 0);
        u16 registered = 0x108 | (cases[i].old_handler ? bit : 0);
        bool change = cases[i].active && cases[i].old_handler != cases[i].new_handler;
        for (unsigned run = 0; ok && run < 2; run++) {
            MEM_W16(0x80023418, cases[i].active);
            MEM_W32(slot, cases[i].old_handler);
            MEM_W16(0x80023448, registered);
            MEM_W16(0x1f801074, hardware);
            nc_cpu = arguments(cases[i].irq, cases[i].new_handler, 3, 19);
            if (!run && nc_credits_set_interrupt_handler(&nc_cpu))
                ok = false;
            nc_cpu.budget = i & 1 ? 4096 : 20;
            if (run)
                interp_call(&nc_cpu, 0x8001aaf4);
            else
                ok = ok && nc_credits_set_interrupt_handler(&nc_cpu);
            u16 enabled = cases[i].new_handler ? bit : 0;
            ok = ok && nc_cpu.r[2] == cases[i].old_handler &&
                 MEM_R32(slot) == (change ? cases[i].new_handler : cases[i].old_handler) &&
                 MEM_R16(0x80023448) == (change ? ((registered & ~bit) | enabled) : registered) &&
                 MEM_R16(0x1f801074) == (change ? ((hardware & ~bit) | enabled) : hardware);
            StateIO *result = run ? &guest : &native;
            interrupt_start_state(result);
            bios_serialize(result);
            saved.loading = true;
            saved.pos = 0;
            interrupt_start_state(&saved);
            bios_serialize(&saved);
        }
        ok = ok && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_start_interrupts(void) {
    const int threshold = 8 * NC_INSN_CYCLES + NC_LOAD_CYCLES;
    StateIO saved = {0};
    interrupt_start_state(&saved);
    bios_serialize(&saved);
    bool ok = !saved.error;
    for (unsigned active = 0; ok && active < 2; active++) {
        StateIO native = {0}, guest = {0};
        for (unsigned run = 0; ok && run < 2; run++) {
            MEM_W16(0x80023418, active);
            nc_cpu = arguments(1, 2, 3, threshold);
            if (!run && nc_credits_start_interrupts(&nc_cpu))
                ok = false;
            nc_cpu.budget = active ? 4096 : threshold + 1;
            if (run)
                interp_call(&nc_cpu, 0x8001a830);
            else
                ok = ok && nc_credits_start_interrupts(&nc_cpu);
            ok = ok && nc_cpu.r[2] == (active ? 0 : 0x80023418u);
            if (!active) {
                u32 table = MEM_R32(0x800244a0);
                ok = ok && MEM_R16(0x80023418) == 1 && MEM_R32(0x80023450) == 0x8001a898 &&
                     MEM_R32(0x80023454) == 0x80024430 && MEM_R32(table + 20) == 0x8001af58 &&
                     MEM_R32(table + 4) == 0x8001b184;
            }
            StateIO *result = run ? &guest : &native;
            interrupt_start_state(result);
            bios_serialize(result);
            saved.loading = true;
            saved.pos = 0;
            interrupt_start_state(&saved);
            bios_serialize(&saved);
        }
        ok = ok && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_interrupt_driver_control(bool stop) {
    u32 entry = stop ? 0x8001ac48 : 0x8001acf4;
    bool (*routine)(CPUState *) = stop ? nc_credits_stop_interrupts : nc_credits_restart_interrupts;
    StateIO saved = {0};
    interrupt_start_state(&saved);
    bios_serialize(&saved);
    bool ok = !saved.error;
    for (unsigned active = 0; active < 2 && ok; active++) {
        for (unsigned tight = 0; tight < 2 && ok; tight++) {
            StateIO native = {0}, guest = {0};
            for (unsigned run = 0; run < 2 && ok; run++) {
                MEM_W16(0x80023418, active ? 0x8000 : 0);
                MEM_W16(0x8002344a, stop ? 0x11 : 0x40);
                MEM_W32(0x8002344c, 0x08765432);
                MEM_W16(0x1f801070, 0);
                MEM_W16(0x1f801074, stop ? 0x40 : 0);
                MEM_W32(0x1f8010f0, stop ? 0xfedcba98 : 0x01234567);
                if (stop) {
                    irq_raise(IRQ_DMA);
                    nc_cpu = arguments(0x80023450, 0, 0, 4096);
                    nc_cpu.r[9] = 0x19;
                    bios_call(&nc_cpu, 0xb0); /* install the hook that stopIntr clears */
                }
                nc_cpu = arguments(7, 2, 3, 11);
                nc_cpu.cop0[12] = stop ? 0x401 : 0;
                if (!run && routine(&nc_cpu))
                    ok = false;
                nc_cpu.budget = tight ? 12 : 4096;
                if (run)
                    interp_call(&nc_cpu, entry);
                else
                    ok = routine(&nc_cpu) && ok;
                StateIO *result = run ? &guest : &native;
                interrupt_start_state(result);
                bios_serialize(result);
                if (stop) {
                    ok = ok && nc_cpu.r[2] == (active ? 0x80023418u : 0) &&
                         MEM_R16(0x80023418) == 0 && MEM_R16(0x1f801074) == (active ? 0 : 0x40) &&
                         (MEM_R16(0x1f801070) & 8) == (active ? 0 : 8) &&
                         MEM_R32(0x1f8010f0) == (active ? 0x76543210u : 0xfedcba98u) &&
                         MEM_R16(0x8002344a) == (active ? 0x40 : 0x11) &&
                         MEM_R32(0x8002344c) == (active ? 0xfedcba98u : 0x08765432u) &&
                         (nc_cpu.cop0[12] & 0x401) == (active ? 0 : 0x401);
                } else {
                    ok = ok && nc_cpu.r[2] == (active ? 0 : 0x80023418u) &&
                         MEM_R16(0x80023418) == (active ? 0x8000 : 1) &&
                         MEM_R16(0x1f801074) == (active ? 0 : 0x40) &&
                         MEM_R32(0x1f8010f0) == (active ? 0x01234567u : 0x08765432u) &&
                         (nc_cpu.cop0[12] & 0x401) == (active ? 0 : 0x401);
                }
                saved.loading = true;
                saved.pos = 0;
                interrupt_start_state(&saved);
                bios_serialize(&saved);
            }
            ok = ok && !saved.error && !native.error && !guest.error && native.len == guest.len &&
                 !memcmp(native.buf, guest.buf, guest.len);
            free(native.buf);
            free(guest.buf);
        }
    }
    free(saved.buf);
    return ok;
}

static bool check_interrupt_start(bool dma) {
    const u32 slots = dma ? 0x800244e0 : 0x800244b4;
    const u32 handler = dma ? 0x80023428 : 0x8002341c;
    const u32 trap = dma ? 0x8001b000 : 0x8001aee0;
    const u32 entry = dma ? 0x8001afb0 : 0x8001ae88;
    const unsigned words = dma ? 8 : 9; /* VSync also clears the frame count. */
    const int threshold = dma ? 6 : 14;
    bool (*start)(CPUState *) = dma ? nc_credits_start_dma : nc_credits_start_vsync;
    StateIO initial = {0};
    interrupt_start_state(&initial);
    bool ok = !initial.error;
    for (unsigned i = 0; i < 3 && ok; i++) {
        StateIO native = {0}, guest = {0};
        for (unsigned run = 0; run < 2 && ok; run++) {
            memset(nc_fast_ptr(slots), 0xa5, words * 4);
            if (dma)
                MEM_W32(0x1f8010f4, 0x7f0000);
            MEM_W16(0x80023418, i != 2);
            MEM_W32(handler, i == 1 ? trap : 0);
            MEM_W16(0x80023448, 0);
            MEM_W16(0x1f801074, 0x40);
            nc_cpu = arguments(0, 0, 0, threshold);
            if (run == 0 && start(&nc_cpu))
                ok = false;
            nc_cpu.budget = i ? 4096 : threshold + 1;
            if (run)
                interp_call(&nc_cpu, entry);
            else
                ok = start(&nc_cpu) && ok;
            interrupt_start_state(run ? &guest : &native);
            ok = ok && nc_cpu.r[2] == (dma ? 0x8001b184u : 0x8001af58u) &&
                 (dma ? (MEM_R32(0x1f8010f4) & 0x7f0000) == 0
                      : (MEM_R32(0x1f801114) & 0x3ff) == 0x107) &&
                 MEM_R32(handler) == (i == 2 ? 0u : trap) &&
                 MEM_R16(0x1f801074) == (i == 0 ? (dma ? 0x48 : 0x41) : 0x40);
            for (unsigned word = 0; word < words; word++)
                ok = ok && MEM_R32(slots + 4 * word) == 0;
            initial.loading = true;
            initial.pos = 0;
            interrupt_start_state(&initial);
        }
        ok = ok && !initial.error && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    free(initial.buf);
    return ok;
}

static void cd_transfer_state(StateIO *io) {
    interrupt_start_state(io);
    cdrom_serialize(io);
}

static bool check_cd_get_sector(bool driver) {
    const u32 entry = driver ? 0x80021a44 : 0x80020378;
    bool (*routine)(CPUState *) = driver ? nc_credits_cd_sector_dma : nc_credits_cd_get_sector;
    const int threshold = driver ? 3 * NC_INSN_CYCLES + NC_LOAD_CYCLES : 4;
    const u32 dest = 0x80062000;
    u8 sector[DISC_DATA_SECTOR];
    if (!disc_read_data(16, sector))
        return false;
    StateIO saved = {0};
    cd_transfer_state(&saved);
    bool ok = !saved.error;
    const unsigned counts[] = {1, DISC_DATA_SECTOR / 4};
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0] && ok; i++) {
        StateIO native = {0}, guest = {0};
        for (unsigned run = 0; run < 2 && ok; run++) {
            /* Read the ISO volume descriptor at 00:02:16 (BCD), then let the SDK request its FIFO. */
            cdrom_tick(0);
            cdrom_write(0, 0);
            cdrom_write(2, 0);
            cdrom_write(2, 2);
            cdrom_write(2, 0x16);
            cdrom_write(1, 2); /* Setloc */
            cdrom_write(1, 6); /* ReadN */
            cdrom_tick(150000 + PSX_CPU_HZ / 75);
            memset(nc_fast_ptr(dest - 1), 0xa5, sizeof sector + 2);
            nc_cpu = arguments(dest, counts[i], 3, threshold);
            if (!run && routine(&nc_cpu))
                ok = false;
            nc_cpu.budget = i ? threshold + 1 : 4096;
            if (run)
                interp_call(&nc_cpu, entry);
            else
                ok = routine(&nc_cpu) && ok;
            ok = ok && nc_cpu.r[2] == (driver ? 0u : 1u) && !memcmp(nc_fast_ptr(dest), sector, 4 * counts[i]) &&
                 MEM_R8(dest - 1) == 0xa5 && MEM_R8(dest + 4 * counts[i]) == 0xa5;
            cd_transfer_state(run ? &guest : &native);
            saved.loading = true;
            saved.pos = 0;
            cd_transfer_state(&saved);
        }
        ok = ok && !saved.error && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    free(saved.buf);
    return ok;
}

static bool check_cd_data_callback(void) {
    const u32 table = MEM_R32(0x800244a0), slot = 0x800244ec, control = 0x80061000;
    const u32 handler = 0x8001935c;
    StateIO saved = {0};
    cpu_serialize(&saved);
    MEM_W32(table + 4, 0x8001b184); /* DMA registration method installed by the SDK */
    MEM_W32(0x800244dc, control);
    bool ok = !saved.error;
    for (unsigned install = 0; install < 2 && ok; install++) {
        u32 previous = install ? 0 : handler;
        u32 replacement = install ? handler : 0;
        MEM_W32(slot, previous);
        MEM_W32(control, install ? 0 : 0x00880000);
        CPUState state = arguments(replacement, 1, 2, install ? 6 : 4096);
        ok = compare(state, 0x8002039c, nc_credits_cd_data_callback, 5) &&
             nc_cpu.r[2] == previous && MEM_R32(slot) == replacement &&
             MEM_R32(control) == (install ? 0x00880000u : 0x00800000u);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_cd_sync(void) {
    const u32 scratch = 0x80061000, result = scratch + 9, response = 0x80024e30;
    const u8 statuses[] = {0, 2, 5}; /* pending, complete, error */
    StateIO saved = {0};
    cpu_serialize(&saved);
    MEM_W16(0x8002341a, 0); /* interrupt handler is inactive */
    MEM_W32(0x80023400, scratch);
    MEM_W32(0x80023404, scratch + 4);
    for (unsigned i = 0; i < 8; i++)
        MEM_W8(response + i, 0x50 + i);
    bool ok = !saved.error;
    for (unsigned i = 0; i < sizeof statuses && ok; i++)
        for (unsigned buffered = 0; buffered < 2 && ok; buffered++) {
            u8 status = statuses[i];
            MEM_W8(0x80024a38, status);
            memset(nc_fast_ptr(result - 1), 0xcc, 10);
            CPUState state = arguments(status ? 0 : 1, buffered ? result : 0, 3,
                                       buffered ? 5 : 4096);
            ok = compare(state, 0x8001ff3c, nc_credits_cd_sync, 4) &&
                 nc_cpu.r[2] == status && MEM_R8(0x80024a38) == (status ? 2 : 0) &&
                 MEM_R8(result - 1) == 0xcc && MEM_R8(result + 8) == 0xcc;
            for (unsigned j = 0; j < 8 && ok; j++)
                ok = MEM_R8(result + j) == (buffered && status ? 0x50 + j : 0xcc);
        }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_cd_ready(void) {
    const u32 scratch = 0x80061000, result = scratch + 9;
    const u32 ready = 0x80024a39, response = 0x80024e38;
    StateIO saved = {0};
    cpu_serialize(&saved);
    MEM_W16(0x8002341a, 0);
    MEM_W32(0x80023400, scratch);
    MEM_W32(0x80023404, scratch + 4);
    for (unsigned j = 0; j < 16; j++)
        MEM_W8(response + j, 0x50 + j);
    bool ok = !saved.error;
    /* The second pending response takes priority and leaves the first queued. */
    for (unsigned mask = 0; mask < 4 && ok; mask++)
        for (unsigned buffered = 0; buffered < 2 && ok; buffered++) {
            u8 first = mask & 1, second = (mask & 2) ? 4 : 0;
            MEM_W8(ready, first);
            MEM_W8(ready + 1, second);
            memset(nc_fast_ptr(result - 1), 0xcc, 10);
            CPUState state = arguments(mask ? 0 : 1, buffered ? result : 0, 3,
                                       buffered ? 5 : 4096);
            ok = compare(state, 0x8001ff5c, nc_credits_cd_ready, 4) &&
                 nc_cpu.r[2] == (second ? second : first) && MEM_R8(ready + 1) == 0 &&
                 MEM_R8(ready) == (second ? first : 0) && MEM_R8(result - 1) == 0xcc &&
                 MEM_R8(result + 8) == 0xcc;
            for (unsigned j = 0; j < 8 && ok; j++)
                ok = MEM_R8(result + j) == (buffered && mask ? (second ? 0x58 : 0x50) + j : 0xcc);
        }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_cd_data_sync(void) {
    const u32 scratch = 0x80061000;
    StateIO saved = {0};
    cpu_serialize(&saved);
    /* Back the VSync counters and DMA status register with deterministic RAM. */
    MEM_W32(0x80023400, scratch);
    MEM_W32(0x80023404, scratch + 4);
    MEM_W32(0x80024a64, scratch + 8);
    MEM_W32(0x800244d4, 100);
    bool ok = !saved.error;
    for (unsigned busy = 0; busy < 2 && ok; busy++)
        for (unsigned tight = 0; tight < 2 && ok; tight++) {
            MEM_W32(scratch + 8, busy << 24);
            CPUState state = arguments(busy, 2, 3, tight ? 5 : 4096);
            ok = compare(state, 0x800203c0, nc_credits_cd_data_sync, 4) &&
                 nc_cpu.r[2] == busy && MEM_R32(0x80024e48) == 1060 &&
                 MEM_R32(0x80024e4c) == 1;
        }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_clear_otag(void) {
    const u32 counts[] = {1, 2, 19};
    StateIO saved = {0};
    cpu_serialize(&saved);
    bool ok = !saved.error;
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0] && ok; i++) {
        u32 table = i == 1 ? SCRATCH_BASE + 32 : 0xa0062000;
        u32 last = table + 4 * (counts[i] - 1);
        MEM_W8(0x8002455a, i ? 2 : 0);
        MEM_W32(0x80024554, 0x8001935c); /* no-op diagnostic */
        memset(nc_fast_ptr(table - 4), 0xcc, (counts[i] + 2) * 4);
        CPUState state = arguments(table, counts[i], 3, i ? 13 : 4096);
        ok = compare(state, 0x8001c730, nc_credits_clear_otag, 12) &&
             nc_cpu.r[2] == last && MEM_R32(last) == 0x24614 &&
             MEM_R32(table - 4) == 0xcccccccc && MEM_R32(last + 4) == 0xcccccccc;
        for (u32 tag = table; tag < last && ok; tag += 4)
            ok = MEM_R32(tag) == ((tag + 4) & 0xffffff);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_gpu_queue_sync(void) {
    const u32 dma = 0x80061000, gpu = dma + 4, tail = 0x80024660;
    const u32 start = 0x8001e9fc, execute = 0x8001e45c, poll = 0x8001ea30;
    const struct {
        u32 mode, head, tail, dma_busy, gpu_ready, timeout;
        u32 result, polls, executions;
    } cases[] = {
        {0, 3, 3, 0, 1, 0, 0, 0, 0},           /* idle */
        {0, 4, 3, 0, 1, 0, 0, 1, 1},           /* drain pending entry */
        {0, 4, 3, 0, 1, 1, UINT32_MAX, 1, 1},  /* queue alarm */
        {0, 3, 3, 1, 1, 0, 0, 1, 0},           /* DMA completes */
        {0, 3, 3, 0, 0, 0, 0, 1, 0},           /* GPU becomes ready */
        {0, 3, 3, 1, 0, 1, UINT32_MAX, 1, 0},  /* hardware alarm */
        {1, 2, 63, 0, 1, 0, 3, 0, 1},          /* wrapped queue */
        {1, 2, 63, 1, 0, 0, 3, 0, 1},
        {1, 3, 3, 0, 0, 0, 1, 0, 0},
        {2, 3, 3, 1, 1, 0, 1, 0, 0},
        {1, 3, 3, 0, 1, 0, 0, 0, 0},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    MEM_W32(0x80024638, dma);
    MEM_W32(0x8002462c, gpu);
    MEM_W32(start, 0x03e00008);       /* jr ra */
    MEM_W32(start + 4, 0x258c0001);   /* addiu t4,t4,1 */
    MEM_W32(execute, 0xad090000);     /* sw t1,0(t0): advance tail */
    MEM_W32(execute + 4, 0x03e00008);
    MEM_W32(execute + 8, 0x25ad0001); /* addiu t5,t5,1 */
    MEM_W32(poll, 0xad400000);         /* sw zero,0(t2): DMA complete */
    MEM_W32(poll + 4, 0xad4b0004);    /* sw t3,4(t2): GPU ready */
    MEM_W32(poll + 8, 0x25ce0001);    /* addiu t6,t6,1 */
    MEM_W32(poll + 12, 0x03e00008);
    MEM_W32(poll + 16, 0x01e01021);   /* move v0,t7 */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        MEM_W32(dma, cases[i].dma_busy << 24);
        MEM_W32(gpu, cases[i].gpu_ready << 26);
        MEM_W32(0x8002465c, cases[i].head);
        MEM_W32(tail, cases[i].tail);
        CPUState state = arguments(cases[i].mode, 2, 3, i & 1 ? 5 : 4096);
        state.r[8] = tail;
        state.r[9] = cases[i].head;
        state.r[10] = dma;
        state.r[11] = 1u << 26;
        state.r[12] = state.r[13] = state.r[14] = 0;
        state.r[15] = cases[i].timeout;
        ok = compare(state, 0x8001e8b4, nc_credits_sync_gpu_queue, 4) &&
             nc_cpu.r[2] == cases[i].result && nc_cpu.r[12] == (cases[i].mode == 0) &&
             nc_cpu.r[13] == cases[i].executions && nc_cpu.r[14] == cases[i].polls;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_clear_ot_dma(void) {
    const u32 regs = 0x80061000, table = 0xa0062000, control = regs + 8;
    const u32 start = 0x8001e9fc, poll = 0x8001ea30;
    const int threshold = 9 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    for (unsigned i = 0; i < 4; i++)
        MEM_W32(0x8002463c + 4 * i, regs + 4 * i);
    /* Capture CHCR at alarm setup, then simulate completion or a busy channel.
     * Each later poll clears CHCR and supplies the selected alarm result. */
    MEM_W32(start, 0x8d0b0000);      /* lw t3, 0(t0) */
    MEM_W32(start + 4, 0x03e00008);
    MEM_W32(start + 8, 0xad090000);  /* sw t1, 0(t0) */
    MEM_W32(poll, 0xad000000);       /* sw zero, 0(t0) */
    MEM_W32(poll + 4, 0x03e00008);
    MEM_W32(poll + 8, 0x01401021);   /* move v0, t2 */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < 3; i++) {
        u32 count = i ? 7 : 1;
        MEM_W32(regs + 12, 0x1234567);
        CPUState state = arguments(table, count, 0, i ? threshold + 1 : 4096);
        state.r[8] = control;
        state.r[9] = i ? 1u << 24 : 0;
        state.r[10] = i == 2;
        ok = compare(state, 0x8001d804, nc_credits_clear_ot_dma, threshold) &&
             nc_cpu.r[2] == (i == 2 ? UINT32_MAX : count) && nc_cpu.r[11] == 0x11000002 &&
             MEM_R32(regs) == table + count * 4 - 4 && MEM_R32(regs + 4) == count &&
             MEM_R32(control) == 0 && MEM_R32(regs + 12) == 0x09234567;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_clear_otag_reverse(void) {
    const u32 counts[] = {1, 7};
    StateIO saved = {0};
    interrupt_start_state(&saved);
    bool ok = !saved.error;
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0] && ok; i++) {
        const u32 table = i ? 0xa0062000 : 0x80062000;
        StateIO native = {0}, guest = {0};
        for (unsigned run = 0; run < 2 && ok; run++) {
            MEM_W8(0x8002455a, i ? 2 : 0);
            MEM_W32(0x80024554, 0x8001935c); /* no-op diagnostic */
            memset(nc_fast_ptr(table - 4), 0xcc, (counts[i] + 2) * 4);
            nc_cpu = arguments(table, counts[i], 3, 12);
            if (!run && nc_credits_clear_otag_reverse(&nc_cpu))
                ok = false;
            nc_cpu.budget = i ? 13 : 4096;
            if (run)
                interp_call(&nc_cpu, 0x8001c7e8);
            else
                ok = nc_credits_clear_otag_reverse(&nc_cpu) && ok;
            ok = ok && nc_cpu.r[2] == table && MEM_R32(table) == 0x24614 &&
                 MEM_R32(table - 4) == 0xcccccccc && MEM_R32(table + 4 * counts[i]) == 0xcccccccc;
            for (unsigned tag = 1; tag < counts[i] && ok; tag++)
                ok = MEM_R32(table + 4 * tag) == ((table + 4 * (tag - 1)) & 0xffffff);
            interrupt_start_state(run ? &guest : &native);
            saved.loading = true;
            saved.pos = 0;
            interrupt_start_state(&saved);
        }
        ok = ok && !saved.error && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    free(saved.buf);
    return ok;
}

static bool check_dma_slot(void) {
    const u32 dicr = 0x1f8010f4, ram = 0x80061000, initial_control = 0x00410012;
    const struct {
        u32 channel, previous, replacement, control;
    } cases[] = {
        {0, 0, 0x8001935c, 0x00c10012}, {6, 0x8001935c, 0, 0x00810012},
        {3, 0x8001935c, 0x8001935c, initial_control},
        {7, 0x8001935c, 0, initial_control}, {0x40000001, 0, 0x8001935c, 0x00c30012},
    };
    StateIO saved = {0};
    interrupt_start_state(&saved);
    bool ok = !saved.error;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0] && ok; i++) {
        u32 slot = 0x800244e0u + 4u * cases[i].channel;
        for (unsigned hardware = 0; hardware < 2 && ok; hardware++) {
            u32 port = hardware ? dicr : ram;
            u32 flags = hardware ? 0 : 0xa5000000;
            u32 expected = cases[i].control;
            if (cases[i].previous == cases[i].replacement)
                expected |= flags;
            StateIO native = {0}, guest = {0};
            for (unsigned run = 0; run < 2 && ok; run++) {
                MEM_W32(0x800244dc, port);
                MEM_W32(port, initial_control | flags);
                MEM_W32(slot, cases[i].previous);
                nc_cpu = arguments(cases[i].channel, cases[i].replacement, 0, 34);
                if (!run && nc_credits_set_dma_slot(&nc_cpu))
                    ok = false;
                nc_cpu.budget = 35;
                if (run)
                    interp_call(&nc_cpu, 0x8001b184);
                else
                    ok = nc_credits_set_dma_slot(&nc_cpu) && ok;
                interrupt_start_state(run ? &guest : &native);
                ok = ok && nc_cpu.r[2] == cases[i].previous &&
                     MEM_R32(slot) == cases[i].replacement && MEM_R32(port) == expected;
                saved.loading = true;
                saved.pos = 0;
                interrupt_start_state(&saved);
            }
            ok = ok && !saved.error && !native.error && !guest.error && native.len == guest.len &&
                 !memcmp(native.buf, guest.buf, guest.len);
            free(native.buf);
            free(guest.buf);
        }
    }
    free(saved.buf);
    return ok;
}

static bool check_vblank_dispatch(void) {
    const u32 slots = 0x800244b4, counter = slots + 32, noop = 0x8001935c;
    u8 saved[36];
    memcpy(saved, nc_fast_ptr(slots), sizeof saved);
    bool ok = true;
    for (unsigned pattern = 0; pattern < 3 && ok; pattern++) {
        for (unsigned j = 0; j < 2 && ok; j++) {
            memset(nc_fast_ptr(slots), 0, 32);
            if (pattern == 1) {
                for (unsigned i = 0; i < 8; i++)
                    MEM_W32(slots + i * 4, noop);
            } else if (pattern == 2) {
                MEM_W32(slots, 0x8001af58); /* install a callback in slot seven */
            }
            u32 count = pattern == 2 ? UINT32_MAX : 17;
            MEM_W32(counter, count);
            CPUState state = arguments(7, noop, 0, j ? 4096 : 19);
            ok = compare(state, 0x8001aee0, nc_credits_dispatch_vblank, 18) &&
                 MEM_R32(counter) == count + 1 && nc_cpu.r[2] == 0 &&
                 MEM_R32(slots + 28) == (pattern ? noop : 0);
        }
    }
    memcpy(nc_fast_ptr(slots), saved, sizeof saved);
    return ok;
}

static bool check_vsync_slot(void) {
    const struct {
        u32 index, previous, replacement;
    } cases[] = {
        {0, 0, 0x80012340}, {7, 0x80012340, 0}, {3, 0x80012340, 0x80012340},
        {0x40000001, 0x80011230, 0x80012340},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        u32 slot = 0x800244b4u + 4u * cases[i].index;
        u32 saved = MEM_R32(slot);
        MEM_W32(slot, cases[i].previous);
        CPUState state = arguments(cases[i].index, cases[i].replacement, 3,
                                   NC_LOAD_CYCLES + 11);
        bool ok = compare(state, 0x8001af58, nc_credits_set_vsync_slot, NC_LOAD_CYCLES + 10) &&
                  nc_cpu.r[4] == slot && nc_cpu.r[2] == cases[i].previous &&
                  MEM_R32(slot) == cases[i].replacement;
        MEM_W32(slot, saved);
        if (!ok)
            return false;
    }
    return true;
}

static bool check_clear_irq_words(u32 entry) {
    const struct {
        u32 address, count;
    } cases[] = {
        {0x80061004, 0}, {0x80061004, 1}, {0xa0061004, 8},
        {SCRATCH_BASE + 4, 17}, {0x80061004, 0x41a},
    };
    const int budgets[] = {4, 13, 8192};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++)
        for (unsigned j = 0; j < sizeof budgets / sizeof budgets[0]; j++) {
            u32 start = cases[i].address, bytes = 4 * cases[i].count;
            memset(nc_fast_ptr(start - 4), 0xa5, bytes + 8);
            CPUState state = arguments(start, cases[i].count, 3, budgets[j]);
            if (!compare(state, entry, nc_credits_clear_irq_words, 3) ||
                nc_cpu.r[4] != start + bytes || MEM_R32(start - 4) != 0xa5a5a5a5 ||
                MEM_R32(start + bytes) != 0xa5a5a5a5)
                return false;
            for (u32 offset = 0; offset < bytes; offset += 4)
                if (MEM_R32(start + offset) != 0)
                    return false;
        }
    return true;
}

static bool check_irq_accessors(void) {
    const u32 slot = 0x800244a8, original = MEM_R32(slot), flag = 0x8002341a;
    const u16 saved = MEM_R16(flag), old_mask = MEM_R16(original);
    const u16 values[] = {0, 1, 0x8000, 0xffff};
    const u32 ports[] = {0x80061002, SCRATCH_BASE + 6};
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++) {
        MEM_W16(flag, values[i]);
        CPUState state = arguments(0xabcd5432, 2, 3, i ? 12 : 4096);
        if (!compare(state, 0x8001a7ec, nc_credits_check_callback, 6) || nc_cpu.r[2] != values[i])
            return false;
        for (unsigned j = 0; j < sizeof ports / sizeof ports[0]; j++) {
            MEM_W32(slot, ports[j]);
            MEM_W16(ports[j], values[i]);
            if (!compare(state, 0x8001a7fc, read_irq_mask, 10) ||
                !compare(state, 0x8001a814, write_irq_mask, 11) ||
                nc_cpu.r[2] != values[i] || MEM_R16(ports[j]) != 0x5432)
                return false;
        }
    }
    MEM_W16(flag, saved);
    MEM_W32(slot, original);
    for (unsigned native = 0; native < 2; native++) {
        MEM_W16(original, 0x307);
        nc_cpu = arguments(0x1234ffff, 2, 3, 12);
        if (native) {
            if (!write_irq_mask(&nc_cpu))
                return false;
        } else {
            interp_call(&nc_cpu, 0x8001a814);
        }
        if (nc_cpu.r[2] != 0x307 || MEM_R16(original) != 0x7ff)
            return false;
        nc_cpu.budget = 11;
        if (!read_irq_mask(&nc_cpu) || nc_cpu.r[2] != 0x7ff)
            return false;
    }
    MEM_W16(original, old_mask);
    return true;
}

static bool check_interrupt_driver_calls(void) {
    const u32 slot = 0x800244a0, original = MEM_R32(slot);
    const u32 table = 0x80061000, flag = 0x80023418, handler = 0x80023424;
    u16 saved_flag = MEM_R16(flag);
    u32 saved_handler = MEM_R32(handler);
    MEM_W16(flag, 1);
    MEM_W32(handler, 0x80021b54);
    /* Distinct return values detect dispatch through the wrong table slot. */
    MEM_W32(table + 8, 0x8001fe20);  /* CdLastPos */
    MEM_W32(table + 12, 0x8001fdf0); /* CdStatus */
    for (unsigned alternate = 0; alternate < 2; alternate++) {
        MEM_W32(slot, alternate ? table : original);
        for (unsigned install = 0; install < 2; install++)
            for (unsigned low = 0; low < 2; low++) {
                CPUState state = arguments(2, 0x80021b54, 7, low ? 13 : 4096);
                if (low)
                    state.r[29] = SCRATCH_BASE + 0x100;
                if (!compare(state, install ? 0x8001a6c8 : 0x8001a698,
                             install ? nc_credits_interrupt_callback : nc_credits_reset_callback, 12))
                    return false;
                if (alternate && nc_cpu.r[2] != (install ? 0x80024778 : MEM_R8(0x8002476c)))
                    return false;
            }
    }
    MEM_W32(slot, original);
    MEM_W16(flag, saved_flag);
    MEM_W32(handler, saved_handler);
    return true;
}

static bool check_cd_init_interrupt(void) {
    const u32 flag = 0x80023418, handler = 0x80023424, enabled = 0x80023448;
    const u32 mask_slot = 0x800244a8, mask = 0x80061000;
    const u32 cleared[] = {0x80024760, 0x8002475c, 0x80024770, 0x8002476c};
    const u32 previous[] = {0, 0x8001935c, 0x80021b54};
    u32 saved_words[4], saved_mask = MEM_R32(mask_slot), saved_handler = MEM_R32(handler);
    u16 saved_flag = MEM_R16(flag), saved_enabled = MEM_R16(enabled);
    for (unsigned i = 0; i < 4; i++)
        saved_words[i] = MEM_R32(cleared[i]);
    MEM_W16(flag, 1); /* callback subsystem already started */
    MEM_W32(mask_slot, mask);
    for (unsigned i = 0; i < sizeof previous / sizeof previous[0]; i++)
        for (unsigned j = 0; j < 2; j++) {
            MEM_W32(handler, previous[i]);
            MEM_W16(mask, 0x41);
            MEM_W16(enabled, 0x20);
            for (unsigned k = 0; k < 4; k++)
                MEM_W32(cleared[k], UINT32_MAX);
            CPUState state = arguments(7, 2, 3, j ? 13 : 4096);
            if (j)
                state.r[29] = SCRATCH_BASE + 0x100;
            if (!compare(state, 0x8002169c, nc_credits_cd_init_interrupt, 12) ||
                nc_cpu.r[2] != previous[i] || MEM_R32(handler) != 0x80021b54 ||
                MEM_R16(mask) != (previous[i] == 0x80021b54 ? 0x41 : 0x45))
                return false;
            for (unsigned k = 0; k < 4; k++)
                if (MEM_R32(cleared[k]) != 0)
                    return false;
        }
    MEM_W32(mask_slot, saved_mask);
    u16 irq_mask = MEM_R16(saved_mask);
    for (unsigned native = 0; native < 2; native++) {
        MEM_W16(saved_mask, 0x41);
        MEM_W32(handler, 0);
        MEM_W16(enabled, 0);
        nc_cpu = arguments(7, 2, 3, 4096);
        if (native) {
            if (!nc_credits_cd_init_interrupt(&nc_cpu))
                return false;
        } else {
            interp_call(&nc_cpu, 0x8002169c);
        }
        if (MEM_R16(saved_mask) != 0x45 || MEM_R16(enabled) != 4)
            return false;
    }
    MEM_W16(saved_mask, irq_mask);
    MEM_W32(handler, saved_handler);
    MEM_W16(enabled, saved_enabled);
    MEM_W16(flag, saved_flag);
    for (unsigned i = 0; i < 4; i++)
        MEM_W32(cleared[i], saved_words[i]);
    return true;
}

static bool check_cd_init_volume(void) {
    const u32 slots = 0x80024a20, spu = 0x80061000, ports = SCRATCH_BASE + 0x40;
    u8 saved[24];
    memcpy(saved, nc_fast_ptr(slots), sizeof saved);
    for (unsigned i = 0; i < 4; i++)
        MEM_W32(slots + 4 * i, ports + i);
    MEM_W32(slots + 20, spu);
    for (unsigned active = 0; active < 4; active++)
        for (unsigned alias = 0; alias < 2; alias++) {
            MEM_W16(spu + 0x1b8, active & 1);
            MEM_W16(spu + 0x1ba, active & 2);
            MEM_W32(spu + 0x180, 0x81234567);
            MEM_W32(ports, UINT32_MAX);
            CPUState state = arguments(1, 2, 3, alias ? 92 : 4096);
            if (alias)
                state.r[29] = ports + 8;
            if (!compare(state, 0x800215a8, nc_credits_cd_init_volume, 91) ||
                MEM_R32(spu + 0x180) != (active ? 0x81234567 : 0x3fff3fff) ||
                MEM_R32(spu + 0x1b0) != 0x3fff3fff || MEM_R16(spu + 0x1aa) != 0xc001)
                return false;
        }
    memcpy(nc_fast_ptr(slots), saved, sizeof saved);
    StateIO before = {0}, native = {0}, guest = {0};
    spu_serialize(&before);
    cdrom_serialize(&before);
    bool ok = !before.error;
    for (unsigned run = 0; run < 2 && ok; run++) {
        nc_cpu = arguments(1, 2, 3, 4096);
        if (run)
            interp_call(&nc_cpu, 0x800215a8);
        else
            ok = nc_credits_cd_init_volume(&nc_cpu);
        StateIO *result = run ? &guest : &native;
        spu_serialize(result);
        cdrom_serialize(result);
        before.loading = true;
        before.pos = 0;
        spu_serialize(&before);
        cdrom_serialize(&before);
    }
    ok = ok && !before.error && !native.error && !guest.error && native.len == guest.len &&
         !memcmp(native.buf, guest.buf, guest.len);
    free(before.buf);
    free(native.buf);
    free(guest.buf);
    return ok;
}

static bool check_cd_mix(void) {
    const u32 slots = 0x80024a20, ports = SCRATCH_BASE + 0x40, mix = 0x80061000;
    u8 saved[16];
    memcpy(saved, nc_fast_ptr(slots), sizeof saved);
    for (unsigned alias = 0; alias < 2; alias++)
        for (unsigned wrapper = 0; wrapper < 2; wrapper++)
            for (unsigned low = 0; low < 2; low++) {
                u32 input = alias ? ports : mix;
                for (unsigned i = 0; i < 4; i++)
                    MEM_W32(slots + 4 * i, ports + i);
                MEM_W32(ports, 0);
                MEM_W32(input, 0x81234567);
                CPUState state = arguments(input, 2, 3, low ? (wrapper ? 5 : 57) : 4096);
                if (!compare(state, wrapper ? 0x80020354 : 0x80021440,
                             wrapper ? nc_credits_cd_mix : nc_credits_cd_volume, wrapper ? 4 : 56) ||
                    nc_cpu.r[2] != wrapper ||
                    MEM_R32(ports) != (alias ? 0x20450203 : 0x20812303))
                    return false;
            }
    memcpy(nc_fast_ptr(slots), saved, sizeof saved);
    /* The emulated CD controller implements the bank select. */
    u32 index = MEM_R32(slots);
    u8 bank = MEM_R8(index) & 3;
    for (unsigned native = 0; native < 2; native++) {
        MEM_W8(index, 0);
        nc_cpu = arguments(mix, 2, 3, 57);
        if (native) {
            if (!nc_credits_cd_volume(&nc_cpu))
                return false;
        } else {
            interp_call(&nc_cpu, 0x80021440);
        }
        if ((MEM_R8(index) & 3) != 3)
            return false;
    }
    MEM_W8(index, bank);
    return true;
}

static bool check_cd_audio_ready(void) {
    const u32 trace = 0x80062000, response = trace + 32;
    const u32 diagnostic = 0x80019a60, play = 0x80022f84;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Capture printf arguments and allow the diagnostic to change the response. */
    MEM_W32(diagnostic, 0xaf040000);      /* sw a0, 0(t8) */
    MEM_W32(diagnostic + 4, 0xaf050004);  /* sw a1, 4(t8) */
    MEM_W32(diagnostic + 8, 0xaf060008);  /* sw a2, 8(t8) */
    MEM_W32(diagnostic + 12, 0xaf07000c); /* sw a3, 12(t8) */
    MEM_W32(diagnostic + 16, 0x03e00008);
    MEM_W32(diagnostic + 20, 0xa2190000); /* sb t9, 0(s0) */
    MEM_W32(play, 0x03e00008);
    MEM_W32(play + 4, 0xaf1f0010); /* sw ra, 16(t8) */
    MEM_W32(0x80023400, trace + 24);
    MEM_W32(0x80023404, trace + 28);
    MEM_W32(0x800244d4, 1234);
    const struct { u32 irq; u8 status, after; bool log, play; } cases[] = {
        {0x104, 1, 1, true, true}, {5, 1, 0, false, false},
        {1, 0x10, 0x10, true, false}, {1, 0, 1, true, false}, {1, 0x10, 0, true, true},
    };
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        bool end = (cases[i].irq & 0xff) == 4;
        memset(nc_fast_ptr(trace), 0, 20);
        MEM_W32(0x80024c34, UINT32_MAX);
        MEM_W32(0x80024c38, 0);
        MEM_W16(response, 0xab00 | cases[i].status);
        CPUState state = arguments(cases[i].irq, response, 0, i ? 8 : 4096);
        state.r[24] = trace;
        state.r[25] = cases[i].after;
        ok = compare(state, 0x80022ebc, nc_credits_cd_audio_ready, 7) &&
             MEM_R32(0x80024c34) == (end ? 0 : UINT32_MAX) &&
             MEM_R32(0x80024c38) == (!end && cases[i].log) &&
             MEM_R32(trace + 16) == (cases[i].play ? 0x80022f70u : 0);
        if (end)
            ok = ok && MEM_R32(trace) == 0x80018bfc && MEM_R32(trace + 4) == UINT32_MAX &&
                 MEM_R32(trace + 8) == 1234;
        else if (cases[i].log)
            ok = ok && MEM_R32(trace) == 0x80018c28 && MEM_R32(trace + 8) == cases[i].status &&
                 MEM_R32(trace + 12) == 0xab;
        else
            ok = ok && MEM_R32(trace) == 0;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_play_track(void) {
    const u32 trace = 0x80062000, index = 0x80024c34, playlist = 0x80024aa4;
    const int threshold = 11 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    const u32 diagnostic = 0x80019a60, control = 0x8001ffac;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Change the index during the diagnostic to check the subsequent reload. */
    MEM_W32(diagnostic, 0xaf05000c);      /* sw a1, 12(t8) */
    MEM_W32(diagnostic + 4, 0x03e00008);
    MEM_W32(diagnostic + 8, 0xad190000);  /* sw t9, 0(t0) */
    MEM_W32(control, 0xaf040000);         /* sw a0, 0(t8) */
    MEM_W32(control + 4, 0xaf050004);     /* sw a1, 4(t8) */
    MEM_W32(control + 8, 0x03e00008);
    MEM_W32(control + 12, 0xaf060008);    /* sw a2, 8(t8) */
    const struct { u32 track, repeat, final_index, selected; } cases[] = {
        {5, 0, 1, 5}, {0, 0, UINT32_MAX, 0}, {0, 1, 0, 3}, {9, 0, 2, 7},
    };
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        memset(nc_fast_ptr(trace), 0xff, 16);
        MEM_W32(playlist, 3);
        MEM_W32(playlist + 4, cases[i].track);
        MEM_W32(playlist + 8, 0);
        MEM_W32(0x80024aa0, 100);
        MEM_W32(index, 1);
        MEM_W32(0x80024c38, 1);
        MEM_W32(0x80024c3c, cases[i].repeat);
        MEM_W32(0x800273e4, 7);
        CPUState state = arguments(0, 0, 0, i ? threshold + 1 : 4096);
        state.r[8] = index;
        state.r[24] = trace;
        state.r[25] = 2;
        bool active = cases[i].final_index != UINT32_MAX;
        ok = compare(state, 0x80022f84, nc_credits_cd_play_track, threshold) &&
             MEM_R32(index) == cases[i].final_index && MEM_R32(0x80024c38) == 0 &&
             MEM_R32(0x80024aa0) == 100u + active &&
             MEM_R32(trace + 12) == (cases[i].track > 7 ? cases[i].track : UINT32_MAX);
        if (active)
            ok = ok && MEM_R32(trace) == 3 && MEM_R32(trace + 8) == 0 &&
                 MEM_R32(trace + 4) == 0x80027254 + 4 * cases[i].selected &&
                 MEM_R32(playlist + 4 * cases[i].final_index) == cases[i].selected;
        else
            ok = ok && MEM_R32(trace) == UINT32_MAX;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_get_toc(void) {
    const u32 driver = 0x800230d0;
    const u32 buffers[] = {0x80062000, SCRATCH_BASE + 16};
    const u32 results[] = {0, 12};
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Record both the mode and the call site through the supplied buffer. */
    MEM_W32(driver, 0xaca40000);      /* sw a0, 0(a1) */
    MEM_W32(driver + 4, 0xacbf0004);  /* sw ra, 4(a1) */
    MEM_W32(driver + 8, 0x03e00008);  /* jr ra */
    MEM_W32(driver + 12, 0x00c01021); /* move v0, a2 */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof buffers / sizeof buffers[0]; i++) {
        CPUState state = arguments(buffers[i], 0x76543210, results[i], i ? 6 : 4096);
        ok = compare(state, 0x800230ac, nc_credits_cd_get_toc, 5) &&
             nc_cpu.r[2] == results[i] && MEM_R32(buffers[i]) == 1 &&
             MEM_R32(buffers[i] + 4) == 0x800230c0;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static void advance_vsync_field(void) {
    MEM_W32(0x80060800, 0x80080000);
}

static bool check_vsync(void) {
    const u32 status = 0x80060800, timer = status + 4, trace = status + 16;
    const u32 wait_stub[] = {
        0xad040000, /* sw a0,0(t0) */
        0xad050004, /* sw a1,4(t0) */
        0x8d0a0008, /* lw t2,8(t0) */
        0x2508000c, /* addiu t0,t0,12 */
        0x03e00008, /* jr ra */
        0xad2a0000, /* sw t2,0(t1) */
    };
    const struct {
        s32 mode;
        u32 before_field, after_field;
        bool advance_on_vblank;
    } cases[] = {
        {-1, 0, 0, false}, {1, 0, 0, false}, {0, 0, 0, false},
        {4, 0x80000, 0x80080000, false}, {2, 0x80000, 0x80000, true},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    void (*saved_hook)(void) = nc_frame_hook;
    for (unsigned i = 0; i < sizeof wait_stub / sizeof wait_stub[0]; i++)
        MEM_W32(0x8001a59c + i * 4, wait_stub[i]);
    MEM_W32(0x80023400, status);
    MEM_W32(0x80023404, timer);
    MEM_W32(timer, 3);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        bool waits = cases[i].mode == 0 || cases[i].mode > 1;
        MEM_W32(0x800244d4, 9);
        MEM_W32(0x8002340c, 7);
        MEM_W32(0x80023408, 0xfffe);
        MEM_W32(status, 0);
        MEM_W32(trace + 8, cases[i].before_field);
        MEM_W32(trace + 20, cases[i].after_field);
        nc_frame_hook = cases[i].advance_on_vblank ? advance_vsync_field : NULL;
        CPUState state = arguments((u32)cases[i].mode, 0, 0, i & 1 ? 2 : 4096);
        state.r[8] = trace;
        state.r[9] = status;
        ok = compare(state, 0x8001a454, nc_credits_vsync, 1) &&
             nc_cpu.r[2] == (cases[i].mode < 0 ? 9 : 5) &&
             MEM_R32(0x8002340c) == (waits ? 9 : 7) &&
             MEM_R32(0x80023408) == (waits ? 3 : 0xfffe) &&
             nc_cpu.r[8] == trace + (waits ? 24 : 0);
        if (waits) {
            u32 skip = cases[i].mode > 0 ? (u32)cases[i].mode - 1 : 0;
            ok = ok && MEM_R32(trace) == 7 + skip && MEM_R32(trace + 4) == skip &&
                 MEM_R32(trace + 12) == 10 && MEM_R32(trace + 16) == 1;
        }
    }
    nc_frame_hook = saved_hook;
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_wait_vblank(void) {
    const u32 frame = 0x800244d4;
    const u32 drivers[] = {0x8001a638, 0x8001a2ac, 0x8001a688};
    const u32 trace = 0x80062000;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    for (unsigned i = 0; i < 3; i++) {
        MEM_W32(drivers[i], 0x03e00008); /* jr ra */
        MEM_W32(drivers[i] + 4, 0xaf040000 + i * 4); /* sw a0, offset(t8) */
    }
    const struct {
        u32 initial_frame, target, timeout, remaining;
        bool alias_frame, timed_out;
    } cases[] = {
        {1, 1, 1, 32768, false, false},
        {0x80000000u, 0x80000001u, 1, 32767, true, false},
        {0, 1, 0, UINT32_MAX, false, true},
        {0, 1, 1, UINT32_MAX, false, true},
    };
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        CPUState state = arguments(cases[i].target, cases[i].timeout, 0, i ? 4 : 4096);
        state.r[24] = trace;
        MEM_W32(frame, cases[i].initial_frame);
        /* Saving ra over the frame counter makes the next poll reach the target. */
        if (cases[i].alias_frame)
            state.r[29] = frame + 8;
        memset(nc_fast_ptr(trace), 0xff, 12);
        ok = compare(state, 0x8001a59c, nc_credits_wait_vblank, 3) &&
             MEM_R32(state.r[29] - 16) == cases[i].remaining &&
             MEM_R32(trace) == (cases[i].timed_out ? 0x800182f8u : UINT32_MAX) &&
             MEM_R32(trace + 4) == (cases[i].timed_out ? 0 : UINT32_MAX) &&
             MEM_R32(trace + 8) == (cases[i].timed_out ? 3 : UINT32_MAX);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_init(void) {
    const u32 reset = 0x8001fe30, diagnostic = 0x80019a60, responses = 0x80062000;
    const u32 slots[] = {0x8002475c, 0x80024760, 0x80024a70};
    const u32 handlers[] = {0x8001fd78, 0x8001fda0, 0x8001fdc8};
    const unsigned attempts[] = {1, 5, 0}; /* zero means all resets fail */
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Consume one reset response per call and record the diagnostic argument.
     * The callback setters themselves run from the disc image. */
    MEM_W32(reset, 0x8d220000);     /* lw v0, 0(t1) */
    MEM_W32(reset + 4, 0x03e00008);
    MEM_W32(reset + 8, 0x25290004); /* addiu t1, t1, 4 */
    MEM_W32(diagnostic, 0x03e00008);
    MEM_W32(diagnostic + 4, 0xad440000); /* sw a0, 0(t2) */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof attempts / sizeof attempts[0]; i++) {
        for (unsigned j = 0; j < 5; j++)
            MEM_W32(responses + 4 * j, j + 1 == attempts[i] ? 1 : 2);
        for (unsigned j = 0; j < 3; j++)
            MEM_W32(slots[j], UINT32_MAX);
        MEM_W32(responses + 20, 0);
        CPUState state = arguments(0, 0, 0, i ? 5 : 4096);
        state.r[9] = responses;
        state.r[10] = responses + 20;
        ok = compare(state, 0x8001fce8, nc_credits_cd_init, 4) &&
             nc_cpu.r[2] == (attempts[i] != 0) &&
             nc_cpu.r[9] == responses + 4 * (attempts[i] ? attempts[i] : 5) &&
             MEM_R32(responses + 20) == (attempts[i] ? 0 : 0x80018734u);
        for (unsigned j = 0; ok && j < 3; j++)
            ok = MEM_R32(slots[j]) == (attempts[i] ? handlers[j] : UINT32_MAX);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_reset(void) {
    const u32 drivers[] = {0x8002169c, 0x800216ec, 0x800215a8};
    const u32 modes[] = {2, 1, 0, UINT32_MAX};
    const u32 trace = 0x80062000;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Isolate CdReset from the drive. Each callee records its return PC and
     * returns a controlled result from t1, t2 or t3 in the delay slot. */
    for (unsigned i = 0; i < 3; i++) {
        MEM_W32(drivers[i], 0xad1f0000u + 4 * i); /* sw ra, offset(t0) */
        MEM_W32(drivers[i] + 4, 0x03e00008);      /* jr ra */
        MEM_W32(drivers[i] + 8, ((9u + i) << 21) | 0x1021u);
    }
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof modes / sizeof modes[0]; i++) {
        for (unsigned errors = 0; ok && errors < 4; errors++) {
            memset(nc_fast_ptr(trace), 0, 12);
            CPUState state = arguments(modes[i], 0, 0, errors ? 7 : 4096);
            state.r[8] = trace;
            state.r[9] = UINT32_MAX;
            state.r[10] = errors & 1;
            state.r[11] = errors & 2;
            bool interrupts_only = modes[i] == 2;
            bool volume = modes[i] == 1 && !state.r[10];
            bool success = interrupts_only || (!state.r[10] && !(volume && state.r[11]));
            ok = compare(state, 0x8001fe30, nc_credits_cd_reset, 6) &&
                 nc_cpu.r[2] == (u32)success &&
                 MEM_R32(trace) == (interrupts_only ? 0x8001fe50u : 0) &&
                 MEM_R32(trace + 4) == (interrupts_only ? 0 : 0x8001fe60u) &&
                 MEM_R32(trace + 8) == (volume ? 0x8001fe7cu : 0);
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_flush(void) {
    const u32 slots = 0x80024a20, ram = 0x80061000;
    u8 saved[0x40];
    memcpy(saved, nc_fast_ptr(slots - 0x20), sizeof saved);
    for (unsigned pending = 0; pending < 2; pending++)
        for (unsigned wrapper = 0; wrapper < 2; wrapper++)
            for (unsigned low = 0; low < 2; low++) {
                MEM_W32(slots, ram);
                MEM_W32(slots + 8, ram + 2);
                /* Acknowledge through the pointer itself to redirect the next status read. */
                MEM_W32(slots + 12, pending ? slots + 12 : ram + 3);
                MEM_W8(0x80024a07, 0);
                MEM_W32(slots + 16, SCRATCH_BASE + 4);
                MEM_W32(ram, 0);
                MEM_W32(0x80024a38, UINT32_MAX);
                CPUState state = arguments(9, 2, 3, low ? 7 : 4096);
                if (!compare(state, wrapper ? 0x8001fe9c : 0x800214c8,
                             wrapper ? nc_credits_cd_flush : nc_credits_cd_flush_controller,
                             wrapper ? 4 : 6) || MEM_R32(SCRATCH_BASE + 4) != 0x1325 ||
                    MEM_R32(0x80024a38) != 0xff000002 || MEM_R8(ram + 2) != (pending ? 7 : 0))
                    return false;
            }
    memcpy(nc_fast_ptr(slots - 0x20), saved, sizeof saved);

    StateIO before = {0}, native = {0}, guest = {0};
    cdrom_serialize(&before);
    irq_serialize(&before);
    bool ok = !before.error;
    for (unsigned run = 0; run < 2 && ok; run++) {
        cdrom_tick(0);
        cdrom_write(0, 0);
        cdrom_write(1, 1); /* queue two Getstat acknowledgements */
        cdrom_write(1, 1);
        cdrom_tick(25000);
        cdrom_write(0, 1);
        ok = (cdrom_read(3) & 7) != 0;
        nc_cpu = arguments(9, 2, 3, 4096);
        if (run)
            interp_call(&nc_cpu, 0x800214c8);
        else
            ok = nc_credits_cd_flush_controller(&nc_cpu) && ok;
        StateIO *result = run ? &guest : &native;
        cdrom_serialize(result);
        irq_serialize(result);
        cdrom_write(0, 1);
        ok = ok && (cdrom_read(3) & 7) == 0;
        before.loading = true;
        before.pos = 0;
        cdrom_serialize(&before);
        irq_serialize(&before);
    }
    ok = ok && !before.error && !native.error && !guest.error && native.len == guest.len &&
         !memcmp(native.buf, guest.buf, guest.len);
    free(before.buf);
    free(native.buf);
    free(guest.buf);
    return ok;
}

static bool check_cd_setters(void) {
    const struct {
        u32 entry, slot;
        bool (*routine)(CPUState *);
    } setters[] = {
        {0x8001febc, 0x80024768, nc_credits_cd_set_debug},
        {0x8001ff7c, 0x8002475c, nc_credits_cd_sync_callback},
        {0x8001ff94, 0x80024760, nc_credits_cd_ready_callback},
        {0x80022cb4, 0x80024a70, nc_credits_cd_read_callback},
    };
    const u32 values[] = {0, 1, 0x8002292c, 0xffffffff};
    for (unsigned i = 0; i < sizeof setters / sizeof setters[0]; i++) {
        u32 saved = MEM_R32(setters[i].slot);
        for (unsigned j = 0; j < sizeof values / sizeof values[0]; j++) {
            u32 previous = ~values[j];
            MEM_W32(setters[i].slot, previous);
            CPUState state = arguments(values[j], 2, 3, (j & 1) ? 4096 : 9);
            if (!compare(state, setters[i].entry, setters[i].routine, 8) ||
                nc_cpu.r[2] != previous || MEM_R32(setters[i].slot) != values[j])
                return false;
        }
        MEM_W32(setters[i].slot, saved);
    }
    return true;
}

static bool check_cd_names(void) {
    const u32 inputs[] = {0, 1, 6, 7, 27, 28, 255, 0xffff0006, 0xffffff1b, 0xffffffff};
    for (unsigned i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        u32 index = inputs[i] & 0xff;
        CPUState state = arguments(inputs[i], 2, 3, 14);
        u32 command = index < 28 ? MEM_R32(0x80024780 + index * 4) : 0x8001874c;
        u32 interrupt = index < 7 ? MEM_R32(0x80024800 + index * 4) : 0x8001874c;
        if (!compare(state, 0x8001fed4, nc_credits_cd_command_name, 13) ||
            nc_cpu.r[2] != command ||
            !compare(state, 0x8001ff08, nc_credits_cd_interrupt_name, 13) ||
            nc_cpu.r[2] != interrupt)
            return false;
    }
    return true;
}

static bool check_cd_interrupt_callback(void) {
    const u32 getintr = 0x80020564, handler = 0x80061000, trace = 0x80062000;
    const u32 flags = trace + 64, port = trace + 96;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    MEM_W32(getintr, 0x8f220000);     /* lw v0, 0(t9) */
    MEM_W32(getintr + 4, 0x03e00008);
    MEM_W32(getintr + 8, 0x27390004); /* addiu t9, t9, 4 */
    MEM_W32(handler, 0xaf040000);     /* sw a0, 0(t8) */
    MEM_W32(handler + 4, 0xaf050004); /* sw a1, 4(t8) */
    MEM_W32(handler + 8, 0x03e00008);
    MEM_W32(handler + 12, 0x27180008); /* addiu t8, t8, 8 */
    const u32 events[] = {1, 4, 2, 6, 0};
    for (unsigned i = 0; i < sizeof events / sizeof events[0]; i++)
        MEM_W32(flags + 4 * i, events[i]);
    MEM_W32(0x80024a20, port);
    MEM_W8(0x80024a38, 2);
    MEM_W8(0x80024a39, 5);
    const int threshold = 9 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    bool ok = !saved.error;
    for (unsigned enabled = 0; ok && enabled < 4; enabled++) {
        MEM_W8(port, 0xd3);
        MEM_W32(0x80024760, enabled & 1 ? handler : 0);
        MEM_W32(0x8002475c, enabled & 2 ? handler : 0);
        memset(nc_fast_ptr(trace), 0, 32);
        CPUState state = arguments(0, 0, 0, enabled ? threshold + 1 : 4096);
        state.r[24] = trace;
        state.r[25] = flags;
        ok = compare(state, 0x80021b54, nc_credits_cd_interrupt_callback, threshold) &&
             nc_cpu.r[25] == flags + sizeof events && MEM_R8(port) == 3;
        u32 cursor = trace;
        for (unsigned pair = 0; pair < 2; pair++) {
            if (enabled & 1) {
                ok = ok && MEM_R32(cursor) == 5 && MEM_R32(cursor + 4) == 0x80024e38;
                cursor += 8;
            }
            if (enabled & 2) {
                ok = ok && MEM_R32(cursor) == 2 && MEM_R32(cursor + 4) == 0x80024e30;
                cursor += 8;
            }
        }
        ok = ok && nc_cpu.r[24] == cursor;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_read(void) {
    const u32 trace = 0x80062000, buffer = 0x80064000;
    const u32 pause = 0x80020210, retry = 0x80022908;
    const int threshold = 9 * NC_INSN_CYCLES + NC_LOAD_CYCLES;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Capture the pause and retry calls while running the original callback
     * setters, status query and VSync frame query. */
    MEM_W32(pause, 0xaf040000);       /* sw a0, 0(t8) */
    MEM_W32(pause + 4, 0xaf050004);   /* sw a1, 4(t8) */
    MEM_W32(pause + 8, 0x03e00008);
    MEM_W32(pause + 12, 0xaf060008);  /* sw a2, 8(t8) */
    MEM_W32(retry, 0xaf04000c);       /* sw a0, 12(t8) */
    MEM_W32(retry + 4, 0x03e00008);
    MEM_W32(retry + 8, 0x03201021);   /* move v0, t9 */
    MEM_W32(0x80023400, trace + 16);
    MEM_W32(0x80023404, trace + 20);
    MEM_W32(0x800244d4, 1234);
    const struct { u32 mode, words, status, result; } cases[] = {
        {0x80, 512, 1, 0}, {0xa0, 585, 0x20, 1},
        {0x90, 582, 0x40, UINT32_MAX}, {0xb0, 582, 0x80, 7},
    };
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        memset(nc_fast_ptr(trace), 0xff, 16);
        MEM_W8(0x8002476c, cases[i].status);
        MEM_W32(0x8002475c, 0x8001fd78);
        MEM_W32(0x80024760, 0x8001fda0);
        CPUState state = arguments(i + 1, buffer, cases[i].mode, i ? threshold + 1 : 4096);
        state.r[24] = trace;
        state.r[25] = cases[i].result;
        ok = compare(state, 0x80022ae0, nc_credits_cd_read, threshold) &&
             nc_cpu.r[2] == ((s32)cases[i].result > 0) &&
             MEM_R32(0x80024a84) == cases[i].words && MEM_R32(0x80024a80) == (cases[i].mode | 0x20) &&
             MEM_R32(0x80024a78) == buffer && MEM_R32(0x80024a74) == i + 1 &&
             MEM_R32(0x80024a90) == 1234 && MEM_R32(0x8002475c) == 0 && MEM_R32(0x80024760) == 0 &&
             MEM_R32(0x80024a98) == 0x8001fd78 && MEM_R32(0x80024a9c) == 0x8001fda0 &&
             MEM_R32(trace + 12) == 0;
        if (cases[i].status & 0xe0)
            ok = ok && MEM_R32(trace) == 9 && MEM_R32(trace + 4) == 0 && MEM_R32(trace + 8) == 0;
        else
            ok = ok && MEM_R32(trace) == UINT32_MAX;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_read_sync(void) {
    const u32 vsync = 0x8001a454, retry = 0x80022908, ready = 0x8001ff5c;
    const u32 trace = 0x80062000, response = trace + 16;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Return successive frame numbers. Record the retry and ready arguments. */
    MEM_W32(vsync, 0x01201021);      /* move v0, t1 */
    MEM_W32(vsync + 4, 0x03e00008);
    MEM_W32(vsync + 8, 0x25290001);  /* addiu t1, t1, 1 */
    MEM_W32(retry, 0x03e00008);
    MEM_W32(retry + 4, 0xaf040000);  /* sw a0, 0(t8) */
    MEM_W32(ready, 0xaf040004);      /* sw a0, 4(t8) */
    MEM_W32(ready + 4, 0x03e00008);
    MEM_W32(ready + 8, 0xaf050008);  /* sw a1, 8(t8) */
    const struct {
        u32 mode, first_frame, remaining, retry_result, expected, polls;
        bool retried;
    } cases[] = {
        {1, 59, 3, 0, 3, 2, false},       /* exactly at the stall deadline */
        {0, 59, 3, 0, 0, 4, true},        /* wait through the next poll */
        {1, 0, UINT32_MAX, 2, 2, 1, true},
        {0, 1201, 3, 0, UINT32_MAX, 1, false},
        {0, 1, 0, 0, 0, 2, false},
    };
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        MEM_W32(0x80024a90, 0);
        MEM_W32(0x80024a8c, 0);
        MEM_W32(0x80024a88, cases[i].remaining);
        MEM_W32(0x80024a74, cases[i].retry_result);
        memset(nc_fast_ptr(trace), 0, 12);
        CPUState state = arguments(cases[i].mode, response, 0, i ? 11 : 4096);
        state.r[24] = trace;
        state.r[9] = cases[i].first_frame;
        ok = compare(state, 0x80022be8, nc_credits_cd_read_sync, 10) &&
             nc_cpu.r[2] == cases[i].expected &&
             nc_cpu.r[9] == cases[i].first_frame + cases[i].polls &&
             MEM_R32(trace) == cases[i].retried && MEM_R32(trace + 4) == 1 &&
             MEM_R32(trace + 8) == response;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_read_sectors(void) {
    const u32 drivers[] = {0x8001ffac, 0x80022ae0, 0x80022be8};
    const u32 trace = 0x80062000, dest = 0x80064000;
    const u32 results[] = {0, 1, UINT32_MAX};
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Run the disc's sector conversion. Capture the three drive calls in RAM
     * and supply a sync result through t9 so each outcome is reproducible. */
    for (unsigned i = 0; i < 3; i++) {
        u32 offset = 16 * i, driver = drivers[i];
        MEM_W32(driver, 0xaf040000 + offset);     /* sw a0, offset(t8) */
        MEM_W32(driver + 4, 0xaf050004 + offset); /* sw a1, offset+4(t8) */
        MEM_W32(driver + 8, 0xaf060008 + offset); /* sw a2, offset+8(t8) */
        MEM_W32(driver + 12, 0x03e00008);         /* jr ra */
        /* Only CdReadSync returns t9. Earlier calls return (t9 == 0). */
        MEM_W32(driver + 16, i == 2 ? 0x03201021 : 0x2f220001);
    }
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof results / sizeof results[0]; i++) {
        CPUState state = arguments(i + 1, 75, dest, i ? 10 : 4096);
        state.r[24] = trace;
        state.r[25] = results[i];
        u32 position = state.r[29] - 40 + 16;
        memset(nc_fast_ptr(trace), 0xff, 48);
        ok = compare(state, 0x80022560, nc_credits_cd_read_sectors, 9) &&
             nc_cpu.r[2] == (results[i] == 0) && MEM_R32(trace) == 2 &&
             MEM_R32(trace + 4) == position && MEM_R32(trace + 8) == 0 &&
             (MEM_R32(position) & 0xffffff) == 0x000300 &&
             MEM_R32(trace + 16) == i + 1 && MEM_R32(trace + 20) == dest &&
             MEM_R32(trace + 24) == 0x80 && MEM_R32(trace + 32) == 0 &&
             MEM_R32(trace + 36) == 0;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_sector_to_position(void) {
    const u32 sectors[] = {0, 74, 75, 4349, 4350, 449849, 449850, (u32)-150,
                           (u32)-151, (u32)-4651, 0x7fffffff, 0x80000000};
    const u32 addresses[] = {0x80060001, 0xa0060003, SCRATCH_BASE + 5};
    for (unsigned i = 0; i < sizeof sectors / sizeof sectors[0]; i++)
        for (unsigned j = 0; j < sizeof addresses / sizeof addresses[0]; j++) {
            u32 address = addresses[j];
            memset(nc_fast_ptr(address - 1), 0xcc, 5);
            CPUState state = arguments(sectors[i], address, 3, j ? 66 : 4096);
            if (!compare(state, 0x800203e0, nc_credits_cd_sector_to_position, 65) ||
                nc_cpu.r[2] != address || MEM_R8(address - 1) != 0xcc ||
                MEM_R8(address + 3) != 0xcc)
                return false;
        }
    return true;
}

static bool check_cd_search_directory(void) {
    const u32 parents = 0x80025458, name = 0x80060000;
    u8 saved[128 * 44];
    memcpy(saved, nc_fast_ptr(parents), sizeof saved);
    memcpy(nc_fast_ptr(name), "CREDITS", 8);
    const struct {
        unsigned count;
        int match, expected;
    } cases[] = {{0, -1, -1}, {1, 0, 1}, {3, 2, 3}, {3, -1, -1},
                 {128, 127, 128}, {128, -1, -1}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        memset(nc_fast_ptr(parents), 0, sizeof saved);
        for (unsigned n = 0; n < cases[i].count; n++) {
            bool match = (int)n == cases[i].match;
            MEM_W32(parents + n * 44, match || n == 1 ? 7 : 2);
            memcpy(nc_fast_ptr(parents + n * 44 + 8), match || n != 1 ? "CREDITS" : "CREDITX", 8);
        }
        for (unsigned j = 0; j < 2; j++) {
            CPUState state = arguments(7, name, 3, j ? 14 : 4096);
            if (j)
                state.r[29] = SCRATCH_BASE + 0x100;
            if (!compare(state, 0x80022214, nc_credits_cd_search_directory, 13) ||
                nc_cpu.r[2] != (u32)cases[i].expected)
                return false;
        }
    }
    memcpy(nc_fast_ptr(parents), saved, sizeof saved);
    return true;
}

static bool check_cd_copy_identifier(void) {
    const u32 src = 0x80060004;
    const u32 dests[] = {SCRATCH_BASE + 1, src, src - 1, src + 1, 0xa0060005};
    const u32 counts[] = {0, 1, 12, 31};
    for (unsigned i = 0; i < sizeof dests / sizeof dests[0]; i++)
        for (unsigned j = 0; j < sizeof counts / sizeof counts[0]; j++) {
            memset(nc_fast_ptr(SCRATCH_BASE), 0xcc, 40);
            for (unsigned k = 0; k < 40; k++)
                MEM_W8(src - 2 + k, (u8)(k * 43));
            CPUState state = arguments(dests[i], src, counts[j], i & 1 ? 4096 : 4);
            if (!compare(state, 0x800225cc, nc_credits_cd_copy_identifier, 3))
                return false;
        }
    return true;
}

static bool check_strcmp(void) {
    const u32 left = 0x80060001, right = SCRATCH_BASE + 5;
    const u8 a[] = {'C', 'D', 0xff, 0}, b[] = {'C', 'D', 0x80, 0};
    memcpy(nc_fast_ptr(left), a, sizeof a);
    memcpy(nc_fast_ptr(right), b, sizeof b);
    const struct {
        u32 left, right;
        s32 difference;
    } cases[] = {
        {0, 0, 0}, {left, 0, 1}, {0, right, -1}, {left, left, 0},
        {left, right, 127}, {right, left, -127}, {left + 3, right + 3, 0},
        {left + 3, right + 2, -128}, {left, left + 0x20000000, 0},
    };
    const int budgets[] = {3, 6, 19, 4096};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++)
        for (unsigned j = 0; j < sizeof budgets / sizeof budgets[0]; j++) {
            CPUState state = arguments(cases[i].left, cases[i].right, 7, budgets[j]);
            if (!compare(state, 0x80022600, nc_credits_strcmp, 2) ||
                nc_cpu.r[2] != (u32)cases[i].difference)
                return false;
        }
    return true;
}

static bool check_strncmp(void) {
    const u32 left = 0x80060001, right = SCRATCH_BASE + 5;
    const struct {
        u32 left, right, limit;
        s32 difference;
    } cases[] = {
        {0, 0, 0, 0}, {0, right, 0, -1}, {left, 0, 0, 1},
        {left, right, 0, 0}, {left, right, 2, 0}, {left, right, 3, 127},
        {right, left, 4, -127}, {left, left, 99, 0},
        {left, right, 0xffffffff, 0}, {left, right, 0x80000000, 127},
    };
    const int budgets[] = {3, 8, 17, 4096};
    const u8 a[] = {'C', 'D', 0xff, 0}, b[] = {'C', 'D', 0x80, 0};
    memcpy(nc_fast_ptr(left), a, sizeof a);
    memcpy(nc_fast_ptr(right), b, sizeof b);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++)
        for (unsigned j = 0; j < sizeof budgets / sizeof budgets[0]; j++) {
            CPUState state = arguments(cases[i].left, cases[i].right, cases[i].limit, budgets[j]);
            if (!compare(state, 0x80022664, nc_credits_strncmp, 2) ||
                nc_cpu.r[2] != (u32)cases[i].difference)
                return false;
        }
    return true;
}

static bool check_cd_names_equal(void) {
    const u32 left = 0x80060001, right = SCRATCH_BASE + 5;
    const struct {
        const char *left, *right;
        bool equal;
    } cases[] = {
        {"", "", true}, {"A", "AB", false}, {"END.TIM;1", "END.TIM;1", true},
        {"ABCDEFGHIJK1", "ABCDEFGHIJK2", false},
        {"ABCDEFGHIJKL1", "ABCDEFGHIJKL2", true},
        {NULL, NULL, true}, {NULL, "A", false}, {"A", NULL, false},
    };
    const int budgets[] = {5, 14, 4096};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        if (cases[i].left)
            memcpy(nc_fast_ptr(left), cases[i].left, strlen(cases[i].left) + 1);
        if (cases[i].right)
            memcpy(nc_fast_ptr(right), cases[i].right, strlen(cases[i].right) + 1);
        for (unsigned j = 0; j < sizeof budgets / sizeof budgets[0]; j++) {
            CPUState state = arguments(cases[i].left ? left : 0, cases[i].right ? right : 0,
                                       0xfeed, budgets[j]);
            if (j == 1)
                state.r[29] = SCRATCH_BASE + 0x100;
            if (!compare(state, 0x80021f14, nc_credits_cd_names_equal, 4) ||
                nc_cpu.r[2] != cases[i].equal)
                return false;
        }
    }
    return true;
}

static bool check_cd_position_to_sector(void) {
    const struct {
        u8 minute, second, frame;
        s32 sector;
    } cases[] = {
        {0, 0, 0, -150}, {0, 0x01, 0x74, -1}, {0, 0x02, 0, 0},
        {0x01, 0, 0, 4350}, {0x99, 0x59, 0x74, 449849},
        {0xff, 0xff, 0xff, (165 * 60 + 165) * 75 + 165 - 150},
    };
    const u32 addresses[] = {0x80060001, 0xa0060003, SCRATCH_BASE + 5};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++)
        for (unsigned j = 0; j < sizeof addresses / sizeof addresses[0]; j++) {
            u32 address = addresses[j];
            MEM_W8(address, cases[i].minute);
            MEM_W8(address + 1, cases[i].second);
            MEM_W8(address + 2, cases[i].frame);
            CPUState state = arguments(address, 2, 3, j ? 39 : 4096);
            if (!compare(state, 0x800204e4, nc_credits_cd_position_to_sector, 38) ||
                nc_cpu.r[2] != (u32)cases[i].sector)
                return false;
        }
    return true;
}

static bool check_cd_state(void) {
    const u8 values[] = {0, 1, 0x7f, 0x80, 0xff};
    for (unsigned i = 0; i < sizeof values; i++) {
        MEM_W8(0x8002476c, values[i]);
        MEM_W8(0x8002477c, values[(i + 1) % sizeof values]);
        MEM_W8(0x8002477d, values[(i + 2) % sizeof values]);
        MEM_W32(0x80024778, 0x12345678);
        for (int extra = 1; extra <= 2; extra++) {
            CPUState state = arguments(1, 2, 3, 6 + extra);
            if (!compare(state, 0x8001fdf0, nc_credits_cd_status, 6) ||
                !compare(state, 0x8001fe00, nc_credits_cd_mode, 6) ||
                !compare(state, 0x8001fe10, nc_credits_cd_last_command, 6))
                return false;
            state.budget = 4 + extra;
            if (!compare(state, 0x8001fe20, nc_credits_cd_last_position, 4))
                return false;
        }
    }
    return true;
}

static bool check_graph_status(void) {
    const u32 words[] = {0xff010000, 0x807fff01, 0x018000ff, 0x00ff0280};
    for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++) {
        MEM_W32(0x80024558, words[i]);
        CPUState state = arguments(0, 0, 0, 7);
        if (!compare(state, 0x8001c1c8, graph_type, 6) ||
            !compare(state, 0x8001c1d8, graph_debug, 6))
            return false;
    }
    return true;
}

static bool check_texture_page(void) {
    const u8 modes[] = {0, 1, 2, 0xff};
    const u32 coords[] = {0, 63, 64, 0x100, 0x200, 0x3ff, 0x400, 0xffffffff};
    for (unsigned mode = 0; mode < sizeof modes; mode++)
        for (unsigned i = 0; i < sizeof coords / sizeof coords[0]; i++) {
            CPUState state = arguments(i, i >> 1, coords[i], 63);
            state.r[7] = coords[(i + 3) % 8];
            if (i & 1)
                state.r[29] = SCRATCH_BASE + 0x100;
            memset(nc_fast_ptr(state.r[29] - 0x28), 0xcc, 0x28);
            MEM_W8(0x80024558, modes[mode]);
            if (!compare(state, 0x8001b548, nc_credits_texture_page, 62))
                return false;
        }
    return true;
}

static bool check_default_draw(void) {
    const u32 heights[] = {0, 256, 257, 288, 289, 0x10001, 0xffffffff, 0x80000000};
    /* Include a saved-register overlap and an output alias of the graph-type byte. */
    const u32 dest[] = {0x80060300, SCRATCH_BASE + 2, 0x801feed0, 0xa0024558};
    for (unsigned mode = 0; mode < 4; mode++)
        for (unsigned i = 0; i < sizeof heights / sizeof heights[0]; i++)
            for (unsigned j = 0; j < sizeof dest / sizeof dest[0]; j++) {
                CPUState state = arguments(dest[j], 0x10000 + i, 0xfffffff0 + j, 84);
                state.r[7] = 320;
                memset(nc_fast_ptr(dest[j]), 0xab, 32);
                MEM_W32(state.r[29] + 0x10, heights[i]);
                MEM_W32(0x80024504, mode >> 1);
                MEM_W8(0x80024558, mode & 1);
                if (!compare(state, 0x8001b414, nc_credits_default_draw, 83))
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
    pass = pass && check_clear_otag();
    pass = pass && check_clear_ot_dma();
    pass = pass && check_clear_otag_reverse();
    pass = pass && check_cd_get_sector(false);
    pass = pass && check_cd_get_sector(true);
    pass = pass && check_bios_a0_stub(0x80023364, 0x39, nc_credits_bios_init_heap);
    pass = pass && check_bios_a0_stub(0x8001ae30, 0x72, nc_credits_bios_96_remove);
    pass = pass && check_bios_write();
    pass = pass && check_put_char();
    pass = pass && check_put_string();
    pass = pass && check_find_byte();
    pass = pass && check_fill();
    pass = pass && check_length();
    pass = pass && check_append();
    pass = pass && check_printf();
    pass = pass && check_case();
    pass = pass && check_primitives();
    pass = pass && check_packet_flags();
    pass = pass && check_prepend();
    pass = pass && check_link();
    pass = pass && check_link_writer();
    pass = pass && check_terminator();
    pass = pass && check_priority_packet();
    pass = pass && check_merge_primitives();
    pass = pass && check_transfer_packets();
    pass = pass && check_draw_mode_packet();
    pass = pass && check_texture_window();
    pass = pass && check_draw_offset();
    pass = pass && check_display_field();
    pass = pass && check_gpu_control();
    pass = pass && check_gpu_command_block();
    pass = pass && check_bios_gpu_command();
    pass = pass && check_gpu_version();
    pass = pass && check_gpu_command_chain();
    pass = pass && check_gpu_parameter();
    pass = pass && check_display_x();
    pass = pass && check_cd_control(0x8001ffac, nc_credits_cd_control) &&
           check_cd_control(0x800200e4, nc_credits_cd_control_f) &&
           check_cd_control(0x80020210, nc_credits_cd_control_b);
    pass = pass && check_rectangle_diagnostics();
    pass = pass && check_set_graph_reverse();
    pass = pass && check_set_graph_queue();
    pass = pass && check_set_graph_debug();
    pass = pass && check_display_mask();
    pass = pass && check_draw_primitive();
    pass = pass && check_load_tpage();
    pass = pass && check_load_clut(0x8001b344, nc_credits_load_clut, 256);
    pass = pass && check_load_clut(0x8001b3ac, nc_credits_load_clut2, 16);
    pass = pass && check_move_image();
    pass = pass && check_image_queue(0x8001c474, nc_credits_clear_image, 0x0c);
    pass = pass && check_image_queue(0x8001c508, nc_credits_clear_image2, 0x0c);
    pass = pass && check_image_queue(0x8001c5a4, nc_credits_load_image, 0x20);
    pass = pass && check_image_queue(0x8001c608, nc_credits_store_image, 0x1c);
    pass = pass && check_draw_env_submission(false);
    pass = pass && check_draw_env_submission(true);
    pass = pass && check_draw_otag();
    pass = pass && check_draw_sync();
    pass = pass && check_draw_sync_callback();
    pass = pass && check_dump_environment(false, 0);
    for (unsigned graph = 0; graph <= 2; graph++)
        pass = pass && check_dump_environment(true, graph);
    pass = pass && check_dump_palette();
    pass = pass && check_dump_texture_page();
    pass = pass && check_environment_reads();
    pass = pass && check_draw_area();
    pass = pass && check_draw_corners();
    pass = pass && check_draw_mode_words();
    pass = pass && check_palette();
    pass = pass && check_video_mode();
    pass = pass && check_default_display();
    pass = pass && check_default_draw();
    pass = pass && check_cd_test_parameter_count();
    pass = pass && check_spu_dma_complete();
    pass = pass && check_spu_read_blocks();
    pass = pass && check_spu_transfer(true, false);
    pass = pass && check_spu_transfer(false, false);
    pass = pass && check_spu_transfer(false, true);
    pass = pass && check_spu_write_register();
    pass = pass && check_spu_set_address();
    pass = pass && check_spu_read_address();
    pass = pass && check_spu_dma_priority();
    pass = pass && check_spu_wait();
    pass = pass && check_tim_info();
    pass = pass && check_spu_delays();
    pass = pass && check_cd_default_callbacks();
    pass = pass && check_callback_controls();
    pass = pass && check_interrupt_driver_control(false);
    pass = pass && check_interrupt_driver_control(true);
    pass = pass && check_pad_patch();
    pass = pass && check_pad_setup();
    pass = pass && check_interrupt_auto_clear();
    pass = pass && check_irq_entry_hooks();
    pass = pass && check_critical_sections();
    pass = pass && check_save_irq_context();
    pass = pass && check_start_interrupts();
    pass = pass && check_set_interrupt_handler();
    pass = pass && check_interrupt_start(false);
    pass = pass && check_interrupt_start(true);
    pass = pass && check_cd_data_callback();
    pass = pass && check_cd_sync();
    pass = pass && check_cd_ready();
    pass = pass && check_cd_data_sync();
    pass = pass && check_dma_slot();
    pass = pass && check_vblank_dispatch();
    pass = pass && check_vsync_slot();
    pass = pass && check_clear_irq_words(0x8001ad7c);
    pass = pass && check_clear_irq_words(0x8001af84);
    pass = pass && check_clear_irq_words(0x8001b22c);
    pass = pass && check_irq_accessors();
    pass = pass && check_interrupt_driver_calls();
    pass = pass && check_cd_init_interrupt();
    pass = pass && check_cd_init_volume();
    pass = pass && check_cd_mix();
    pass = pass && check_cd_audio_ready();
    pass = pass && check_cd_play_track();
    pass = pass && check_cd_get_toc();
    pass = pass && check_vsync();
    pass = pass && check_wait_vblank();
    pass = pass && check_cd_init();
    pass = pass && check_cd_reset();
    pass = pass && check_cd_flush();
    pass = pass && check_cd_setters();
    pass = pass && check_cd_names();
    pass = pass && check_cd_interrupt_callback();
    pass = pass && check_cd_read();
    pass = pass && check_cd_read_sync();
    pass = pass && check_cd_read_sectors();
    pass = pass && check_cd_sector_to_position();
    pass = pass && check_cd_search_directory();
    pass = pass && check_cd_copy_identifier();
    pass = pass && check_strcmp();
    pass = pass && check_strncmp();
    pass = pass && check_cd_names_equal();
    pass = pass && check_cd_position_to_sector();
    pass = pass && check_cd_state();
    pass = pass && check_graph_status();
    pass = pass && check_texture_page();
    pass = pass && check_gpu_fill_bytes();
    pass = pass && check_gpu_queue_sync();
    pass = pass && check_set_queue_alarm();
    pass = pass && check_queue_alarm_expiry();
    pass = pass && check_gpu_driver_reset();
    pass = pass && check_reset_graph();
    pass = pass && check_queue_command();
    disc_close();
    return pass ? 0 : 1;
}
