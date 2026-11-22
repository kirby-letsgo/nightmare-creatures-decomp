#include "credits_compare.h"
#include "decomp/credits.h"
#include "port/bios.h"
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

static bool check_cd_setters(void) {
    const struct {
        u32 entry, slot;
        bool (*routine)(CPUState *);
    } setters[] = {
        {0x8001febc, 0x80024768, nc_credits_cd_set_debug},
        {0x8001ff7c, 0x8002475c, nc_credits_cd_sync_callback},
        {0x8001ff94, 0x80024760, nc_credits_cd_ready_callback},
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
    pass = pass && check_gpu_command_chain();
    pass = pass && check_gpu_parameter();
    pass = pass && check_display_x();
    pass = pass && check_set_graph_debug();
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
    pass = pass && check_spu_write_register();
    pass = pass && check_spu_set_address();
    pass = pass && check_spu_read_address();
    pass = pass && check_spu_dma_priority();
    pass = pass && check_spu_wait();
    pass = pass && check_tim_info();
    pass = pass && check_spu_delays();
    pass = pass && check_cd_default_callbacks();
    pass = pass && check_cd_setters();
    pass = pass && check_cd_names();
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
    pass = pass && check_set_queue_alarm();
    pass = pass && check_queue_command();
    disc_close();
    return pass ? 0 : 1;
}
