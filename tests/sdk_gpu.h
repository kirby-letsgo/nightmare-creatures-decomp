#ifndef NC_TESTS_SDK_GPU_H
#define NC_TESTS_SDK_GPU_H

/* Equivalence checks for the shared libgpu routines (src/decomp/sdk_gpu.h), written with
 * CREDITS.EXE addresses. The including file defines:
 *   GA(address)       the executable's address for a CREDITS.EXE address
 *   GPU_NATIVE(name)  the executable's handwritten routine (nc_<prefix>_<name>)
 *   GPU_LEAF(name)    the same for the small getters, or NULL to skip their checks */

#include "cd_sector_dma.h" /* interrupt_start_state, hw.h */
#include "jump_context.h"
#include <string.h>

static bool check_draw_mode_words(void) {
    const u32 packets[] = {0x80060400, SCRATCH_BASE + 4, 0x801feee4};
    const u32 pages[] = {0, 0xffff, 0xa5a527ff, 0xffffd800};
    const u8 types[] = {0, 1, 2, 255};
    for (unsigned g = 0; g < sizeof types; g++)
        for (unsigned f = 0; f < 4; f++)
            for (unsigned p = 0; p < sizeof pages / sizeof pages[0]; p++) {
                u32 display = (f & 1) ? 0x80000000 : 0;
                u32 dither = f & 2;
                MEM_W8(GA(0x80024558), types[g]);
                CPUState state = arguments(display, dither, pages[p], 20);
                if (GPU_LEAF(draw_mode_word) &&
                    !compare(state, GA(0x8001d470), GPU_LEAF(draw_mode_word), 19))
                    return false;
                for (unsigned j = 0; j < sizeof packets / sizeof packets[0]; j++) {
                    memset(nc_fast_ptr(packets[j]), 0xab, 8);
                    state = arguments(packets[j], display, dither, 61);
                    state.r[7] = pages[p];
                    if (!compare(state, GA(0x8001ba5c), GPU_NATIVE(draw_tpage), 60))
                        return false;
                }
            }
    return true;
}

static bool check_draw_area(void) {
    const u32 packet = 0x80060500;
    const u32 rects[] = {SCRATCH_BASE + 4, packet, 0xa0060504, packet + 8};
    const s16 fields[][4] = {{0, 0, 1, 1},        {12, 34, 0, 0},       {-3, -8, 6, 12},
                             {1024, 512, 16, 16}, {32767, 32767, 2, 2}, {-32768, 7, -1, -9}};
    for (unsigned graph = 0; graph < 4; graph++)
        for (unsigned i = 0; i < sizeof rects / sizeof rects[0]; i++)
            for (unsigned j = 0; j < sizeof fields / sizeof fields[0]; j++) {
                memset(nc_fast_ptr(packet), 0xa5, 16);
                for (unsigned k = 0; k < 4; k++)
                    MEM_W16(rects[i] + 2 * k, (u16)fields[j][k]);
                MEM_W8(GA(0x80024558), graph);
                MEM_W32(GA(0x8002455c), 0x02000400);
                CPUState state = arguments(packet, rects[i], 0xabcdef12, 162);
                if (!compare(state, GA(0x8001d084), GPU_NATIVE(draw_area_packet), 161))
                    return false;
            }
    return true;
}

static bool check_dump_texture_page(void) {
    const u8 types[] = {0, 1, 2, 255};
    const u32 pages[] = {0, 0x10, 0x60, 0x780, 0x800, 0x0123ffff, 0xfedc0123};
    const u32 callbacks[] = {GA(0x8001935c), GA(0x80019a60)};
    const int budgets[] = {45, 53, 4096};
    u32 previous = MEM_R32(GA(0x80024554));
    for (unsigned g = 0; g < sizeof types; g++)
        for (unsigned p = 0; p < sizeof pages / sizeof pages[0]; p++)
            for (unsigned cb = 0; cb < sizeof callbacks / sizeof callbacks[0]; cb++)
                for (unsigned b = 0; b < sizeof budgets / sizeof budgets[0]; b++) {
                    MEM_W8(GA(0x80024558), types[g]);
                    MEM_W32(GA(0x80024554), callbacks[cb]);
                    CPUState state = arguments(pages[p], 0x1234, 0x5678, budgets[b]);
                    if (!compare(state, GA(0x8001b628), GPU_NATIVE(dump_texture_page), 44))
                        return false;
                }
    MEM_W32(GA(0x80024554), previous);
    return true;
}

static bool check_dump_environment(bool draw, u8 graph) {
    const u32 recorder = 0x80060900, trace = 0x80060a00;
    /* Guest callback: record a0..a3 and the stack argument, then advance t1. */
    const u32 code[] = {0xad240000, 0xad250004, 0xad260008, 0xad27000c, 0x8fa80010,
                        0,          0xad280010, 0x03e00008, 0x25290014};
    const u32 callbacks[] = {recorder, GA(0x80019a60)};
    const u32 envs[] = {0xa0060502, SCRATCH_BASE + 4, 0x801feee0};
    const int budgets[] = {5, 17, 32, 4096};
    u32 previous = MEM_R32(GA(0x80024554));
    for (unsigned i = 0; i < sizeof code / sizeof code[0]; i++)
        MEM_W32(recorder + i * 4, code[i]);
    for (unsigned cb = 0; cb < sizeof callbacks / sizeof callbacks[0]; cb++)
        for (unsigned e = 0; e < sizeof envs / sizeof envs[0]; e++)
            for (unsigned b = 0; b < sizeof budgets / sizeof budgets[0]; b++) {
                MEM_W32(GA(0x80024554), callbacks[cb]);
                MEM_W8(GA(0x80024558), graph);
                for (unsigned j = 0; j < (draw ? 24u : 18u); j++)
                    MEM_W8(envs[e] + j, (u8)(j * 53 + 11));
                memset(nc_fast_ptr(trace), 0xcc, 120);
                CPUState state = arguments(envs[e], 0x1234, 0x5678, budgets[b]);
                state.r[9] = trace;
                if (!compare(state, draw ? GA(0x8001bc14) : GA(0x8001bd94),
                             draw ? GPU_NATIVE(dump_draw) : GPU_NATIVE(dump_display), 4))
                    return false;
            }
    MEM_W32(GA(0x80024554), previous);
    return true;
}

static void defer_graphics_queue(u32 scratch) {
    /* Keep DMA busy so the original driver records the submission in queue slot zero. */
    MEM_W32(GA(0x80023404), scratch);
    MEM_W32(GA(0x800244a8), scratch + 4);
    MEM_W32(GA(0x80024638), scratch + 8);
    MEM_W32(scratch + 8, 1u << 24);
    MEM_W32(GA(0x800244a0), scratch + 16);
    MEM_W32(scratch + 20, GA(0x8001935c));
    MEM_W8(GA(0x80024559), 1);
}

static bool check_image_queue(u32 entry, bool (*routine)(CPUState *), u32 method_offset) {
    const bool clear = method_offset == 0x0c;
    const int threshold = clear ? 14 : 10;
    const u32 scratch = 0x80060c00, rect = scratch + 32, pixels = 0x80062000;
    const u32 record = GA(0x80027488), driver = MEM_R32(GA(0x80024550));
    StateIO saved = {0};
    cpu_serialize(&saved);
    defer_graphics_queue(scratch);
    MEM_W32(GA(0x80024554), GA(0x8001935c)); /* no-op diagnostic */
    MEM_W32(rect, 0x00100020);
    MEM_W32(rect + 4, 0x00020004);
    bool ok = !saved.error;
    for (unsigned verbose = 0; verbose < 2 && ok; verbose++) {
        MEM_W8(GA(0x8002455a), verbose ? 2 : 0);
        MEM_W32(GA(0x8002465c), 0);
        MEM_W32(GA(0x80024660), 0);
        memset(nc_fast_ptr(record), 0xcc, 96);
        CPUState state = arguments(rect, pixels, 3, verbose ? threshold + 1 : 4096);
        u32 queued_argument = pixels;
        if (clear) {
            state.r[5] = 0xff001234;
            state.r[6] = 0xaa005678;
            state.r[7] = 0x11009abc;
            queued_argument = routine == GPU_NATIVE(clear_image2) ? 0x80bc7834u : 0xbc7834u;
        }
        ok = compare(state, entry, routine, threshold) &&
             MEM_R32(record) == MEM_R32(driver + method_offset) &&
             MEM_R32(record + 4) == record + 12 && MEM_R32(record + 8) == queued_argument &&
             !memcmp(nc_fast_ptr(record + 12), nc_fast_ptr(rect), 8) && MEM_R8(record + 20) == 0xcc;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_put_display_env(void) {
    const u32 env = 0x80061000, cache = GA(0x800245c4), driver = env + 64;
    const u32 write = driver + 64, debug = write + 16, trace = debug + 32;
    const struct {
        s16 x, y, w, h, pixels, lines;
        unsigned standard, graph;
        int screen_diff, mode_diff; /* byte offsets in the cached DISPENV */
    } cases[] = {
        {0, 0, 0, 0, 280, 256, 0, 0, -1, -1},        {0, 0, 0, 0, 281, 257, 0, 1, 8, 0},
        {-100, -50, -1, -1, 352, 288, 1, 2, 10, 2},  {400, 400, 200, 200, 353, 289, 1, 1, 12, 4},
        {200, 300, 200, 200, 400, 256, 0, 0, 14, 6}, {10, 10, 100, 100, 401, 240, 0, 2, 8, 16},
        {20, 10, 100, 100, 560, 288, 1, 1, 10, 16},  {20, 10, 0, 0, 561, 289, 2, 3, -1, 16},
        {0, 0, 320, 240, -1, -1, 1, 0, 8, -1},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    MEM_W32(GA(0x80024550), driver);
    MEM_W32(driver + 16, write);
    MEM_W32(GA(0x80024554), debug);
    MEM_W32(write, 0xaf040000); /* sw a0,0(t8) */
    MEM_W32(write + 4, 0x03e00008);
    MEM_W32(write + 8, 0x27180004);
    MEM_W32(debug, 0x03e00008);
    MEM_W32(debug + 4, 0x27390001); /* count debug calls in t9 */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        memset(nc_fast_ptr(env), 0, 20);
        MEM_W16(env, (u16)-17);
        MEM_W16(env + 2, 512);
        MEM_W16(env + 4, (u16)cases[i].pixels);
        MEM_W16(env + 6, (u16)cases[i].lines);
        MEM_W16(env + 8, (u16)cases[i].x);
        MEM_W16(env + 10, (u16)cases[i].y);
        MEM_W16(env + 12, (u16)cases[i].w);
        MEM_W16(env + 14, (u16)cases[i].h);
        MEM_W8(env + 16, i & 1);
        MEM_W8(env + 17, (i >> 1) & 1);
        bool refresh_standard = i == 8;
        MEM_W8(env + 18, refresh_standard ? 0 : cases[i].standard);
        memcpy(nc_fast_ptr(cache), nc_fast_ptr(env), 20);
        if (cases[i].screen_diff >= 0)
            MEM_W8(cache + cases[i].screen_diff, MEM_R8(env + cases[i].screen_diff) ^ 1);
        if (cases[i].mode_diff >= 0)
            MEM_W8(cache + cases[i].mode_diff, MEM_R8(env + cases[i].mode_diff) ^ 1);
        MEM_W32(GA(0x80024504), cases[i].standard);
        MEM_W8(GA(0x80024558), cases[i].graph);
        MEM_W8(GA(0x8002455a), i & 3);
        MEM_W8(GA(0x8002455b), i & 1);
        memset(nc_fast_ptr(trace), 0xcc, 20);
        CPUState state = arguments(env, 2, 3, i & 1 ? 10 : 8192);
        state.r[24] = trace;
        state.r[25] = 0;
        unsigned words = 1;
        ok = compare(state, GA(0x8001cb2c), GPU_NATIVE(put_display_env), 9) && nc_cpu.r[2] == env &&
             !memcmp(nc_fast_ptr(env), nc_fast_ptr(cache), 20) && (MEM_R32(trace) >> 24) == 5 &&
             nc_cpu.r[25] == ((i & 3) >= 2);
        if (cases[i].screen_diff >= 0) {
            int left = 608 + 10 * cases[i].x;
            int right = left + (cases[i].w ? 10 * cases[i].w : 2560);
            int top = cases[i].y + (cases[i].standard ? 19 : 16);
            int bottom = top + (cases[i].h ? cases[i].h : 240);
            int last = cases[i].standard ? 310 : 256;
            left = left < 500 ? 500 : left > 3290 ? 3290 : left;
            right = right < left + 80 ? left + 80 : right > 3290 ? 3290 : right;
            top = top < 16 ? 16 : top > last ? last : top;
            bottom = bottom < top + 2 ? top + 2 : bottom > last + 2 ? last + 2 : bottom;
            ok = ok && MEM_R32(trace + 4) == (0x06000000u | (u32)right << 12 | (u32)left) &&
                 MEM_R32(trace + 8) == (0x07000000u | (u32)bottom << 10 | (u32)top);
            words += 2;
        }
        if (cases[i].mode_diff >= 0 || refresh_standard) {
            int pixels = cases[i].pixels;
            u32 mode = pixels <= 280   ? 0
                       : pixels <= 352 ? 1
                       : pixels <= 400 ? 64
                       : pixels <= 560 ? 2
                                       : 3;
            mode |= cases[i].standard == 1 ? 8 : 0;
            mode |= (i & 1) ? 0xa0 : 0; /* interlace and reverse */
            mode |= (i & 2) ? 0x10 : 0;
            mode |= cases[i].lines > (cases[i].standard ? 288 : 256) ? 0x24 : 0;
            ok = ok && MEM_R32(trace + words * 4) == (0x08000000 | mode) &&
                 MEM_R8(env + 18) == cases[i].standard;
            words++;
        }
        ok = ok && nc_cpu.r[24] == trace + words * 4 && MEM_R32(trace + words * 4) == 0xcccccccc;
        if (!ok)
            fprintf(stderr, "PutDispEnv case %u failed\n", i);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_draw_env_submission(bool linked) {
    const u32 env = 0x80062000, cached = GA(0x80024568), record = GA(0x80027488);
    const u32 ot = 0xa0068000;
    const int threshold = (linked ? 13 : 11) * NC_INSN_CYCLES + NC_LOAD_CYCLES;
    StateIO saved = {0};
    cpu_serialize(&saved);
    defer_graphics_queue(0x80060c00);
    MEM_W32(GA(0x80024554), GA(0x8001935c)); /* no-op diagnostic */
    bool ok = !saved.error;
    for (unsigned background = 0; ok && background < 2; background++) {
        memset(nc_fast_ptr(env), 0, 28);
        MEM_W32(env + 4, 0x00f00140); /* clip width and height */
        MEM_W32(env + 24, 0x12345600 | background);
        memset(nc_fast_ptr(env + 28), 0xab, 64);
        memset(nc_fast_ptr(cached), 0xdd, 92);
        memset(nc_fast_ptr(record), 0xcc, 96);
        MEM_W32(GA(0x8002465c), 0);
        MEM_W32(GA(0x80024660), 0);
        MEM_W8(GA(0x8002455a), background ? 2 : 0);
        CPUState state =
            arguments(linked ? ot : env, linked ? env : 2, 3, background ? threshold + 1 : 4096);
        ok = compare(state, linked ? GA(0x8001ca18) : GA(0x8001c954),
                     linked ? GPU_NATIVE(draw_otag_env) : GPU_NATIVE(put_draw_env), threshold) &&
             nc_cpu.r[2] == (linked ? cached : env) &&
             (MEM_R32(env + 28) & 0xffffff) == (linked ? ot & 0xffffff : 0xffffff) &&
             MEM_R32(record) == MEM_R32(MEM_R32(GA(0x80024550)) + 0x18) &&
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
    const u32 slot = GA(0x80024550), driver = MEM_R32(slot), ot = 0x80062000;
    const u32 noop = GA(0x8001935c), record = GA(0x80027488);
    StateIO saved = {0};
    cpu_serialize(&saved);
    defer_graphics_queue(scratch);
    MEM_W32(GA(0x80024554), diagnostic);
    MEM_W32(diagnostic, 0xad440000);     /* sw a0, 0(t2) */
    MEM_W32(diagnostic + 4, 0x03e00008); /* jr ra */
    MEM_W32(diagnostic + 8, 0xad090000); /* sw t1, 0(t0): replace the driver */
    MEM_W32(alternate + 8, MEM_R32(driver + 8));
    MEM_W32(alternate + 0x18, noop);
    bool ok = !saved.error;
    for (unsigned verbose = 0; verbose < 2 && ok; verbose++)
        for (unsigned tight = 0; tight < 2 && ok; tight++) {
            MEM_W8(GA(0x8002455a), verbose ? 2 : 1);
            MEM_W32(slot, driver);
            MEM_W32(GA(0x8002465c), 0);
            MEM_W32(GA(0x80024660), 0);
            MEM_W32(scratch + 28, 0);
            memset(nc_fast_ptr(record), 0xcc, 96);
            CPUState state = arguments(ot, 2, 3, tight ? 11 : 4096);
            state.r[8] = slot;
            state.r[9] = alternate;
            state.r[10] = scratch + 28;
            ok = compare(state, GA(0x8001c8e0), GPU_NATIVE(draw_otag), 10) &&
                 MEM_R32(record) == (verbose ? noop : MEM_R32(driver + 0x18)) &&
                 MEM_R32(record + 4) == ot && MEM_R32(record + 8) == 0 &&
                 MEM_R32(scratch + 28) == (verbose ? GA(0x80018600u) : 0);
        }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_draw_sync(void) {
    const u32 slot = GA(0x80024550), diagnostic = 0x80060c00, table = 0x80061000;
    const u32 driver = MEM_R32(slot), printer = MEM_R32(GA(0x80024554));
    const u8 debug = MEM_R8(GA(0x8002455a));
    MEM_W8(GA(0x8002455a), 0);
    bool ok = compare(arguments(1, 2, 3, 4096), GA(0x8001c2e0), GPU_NATIVE(draw_sync), 10);
    /* The diagnostic replaces the driver. Its two sync methods return mode and mode + 1. */
    MEM_W32(GA(0x80024554), diagnostic);
    MEM_W32(diagnostic, 0x03e00008);     /* jr ra */
    MEM_W32(diagnostic + 4, 0xad090000); /* sw t1, 0(t0) */
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
            MEM_W8(GA(0x8002455a), verbose ? 2 : 1);
            CPUState state = arguments(modes[i], 2, 3, i ? 11 : 4096);
            state.r[8] = slot;
            state.r[9] = table + 0x80;
            ok = compare(state, GA(0x8001c2e0), GPU_NATIVE(draw_sync), 10) &&
                 nc_cpu.r[2] == modes[i] + verbose;
        }
    MEM_W32(slot, driver);
    MEM_W32(GA(0x80024554), printer);
    MEM_W8(GA(0x8002455a), debug);
    return ok;
}

static bool check_draw_sync_callback(void) {
    const u32 slot = GA(0x80024564), diagnostic = 0x80060c00;
    const u8 levels[] = {0, 1, 2, 255};
    const u32 handlers[] = {0, GA(0x8001935c), 0xa0060400};
    const int budgets[] = {11, 18, 29, 4096};
    const u32 previous_diagnostic = MEM_R32(GA(0x80024554));
    /* The diagnostic changes the registered handler in its return delay slot. */
    MEM_W32(diagnostic, 0x03e00008);     /* jr ra */
    MEM_W32(diagnostic + 4, 0xad090000); /* sw t1, 0(t0) */
    MEM_W32(GA(0x80024554), diagnostic);
    for (unsigned i = 0; i < sizeof levels; i++)
        for (unsigned j = 0; j < sizeof handlers / sizeof handlers[0]; j++)
            for (unsigned k = 0; k < sizeof budgets / sizeof budgets[0]; k++) {
                MEM_W8(GA(0x8002455a), levels[i]);
                MEM_W32(slot, GA(0x800198ec));
                CPUState state = arguments(handlers[j], 0x1234, 0x5678, budgets[k]);
                state.r[8] = slot;
                state.r[9] = GA(0x80019364);
                if (!compare(state, GA(0x8001c1e8), GPU_NATIVE(draw_sync_callback), 10))
                    return false;
            }
    MEM_W32(GA(0x80024554), previous_diagnostic);
    return true;
}

static bool check_rectangle_diagnostics(void) {
    const u32 rect = 0x80060800, callback = 0x80060c00, output = rect + 16;
    /* An exact fit, then each of the SDK's eight rejection branches. */
    const s16 cases[][4] = {{1, 2, 63, 30}, {0, 0, 65, 1}, {2, 0, 63, 1},
                            {0, 33, 1, 1},  {0, 31, 1, 2}, {0, 0, 0, 1},
                            {-1, 0, 1, 1},  {0, -1, 1, 1}, {0, 0, 1, -1}};
    StateIO saved = {0};
    cpu_serialize(&saved);
    MEM_W32(GA(0x80024554), callback);
    MEM_W32(callback, 0xad240000);     /* sw a0,0(t1) */
    MEM_W32(callback + 4, 0x03e00008); /* jr ra */
    MEM_W32(callback + 8, 0x25290004); /* addiu t1,t1,4 */
    MEM_W16(GA(0x8002455c), 64);
    MEM_W16(GA(0x8002455e), 32);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        for (unsigned j = 0; j < 4; j++)
            MEM_W16(rect + j * 2, (u16)cases[i][j]);
        for (unsigned level = 0; ok && level < 4; level++) {
            MEM_W8(GA(0x8002455a), level);
            MEM_W32(output, 0);
            MEM_W32(output + 4, 0);
            CPUState state = arguments(GA(0x800185a8), rect, 0x5678, i & 1 ? 12 : 4096);
            state.r[9] = output;
            bool report = level == 2 || (level == 1 && i != 0);
            ok = compare(state, GA(0x8001c34c), GPU_NATIVE(check_rect), 11) &&
                 MEM_R32(output) == (report ? (level == 1 ? GA(0x8001857c) : GA(0x8001859c)) : 0) &&
                 MEM_R32(output + 4) == (report ? GA(0x80018588) : 0);
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_set_graph_reverse(void) {
    const u32 query = 0x80060c00, control = query + 16, output = control + 32;
    const u32 driver = MEM_R32(GA(0x80024550));
    const int threshold = 11 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    StateIO saved = {0};
    cpu_serialize(&saved);
    MEM_W32(GA(0x80024554), GA(0x8001935c));
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
        MEM_W8(GA(0x8002455b), 0x7f);
        MEM_W8(GA(0x80024558), extra ? 2 : 1);
        MEM_W8(GA(0x8002455a), i & 4 ? 2 : 1);
        MEM_W32(output + 4, 0);
        CPUState state = arguments(reverse ? 0x180 : 0x100, 0, 0, i & 4 ? threshold + 1 : 4096);
        state.r[8] = output;
        ok = compare(state, GA(0x8001bf9c), GPU_NATIVE(set_graph_reverse), threshold) &&
             nc_cpu.r[2] == 0x7f && MEM_R8(GA(0x8002455b)) == (u8)state.r[4] &&
             MEM_R32(output) == (reverse ? 0x0800008b : 0x0800000b) &&
             MEM_R32(output + 4) == (extra ? (reverse ? 0x20000501 : 0x20000504) : 0);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_reset_graph(void) {
    const u32 state_base = GA(0x80024558), trace = 0x80061000, stub = trace + 0x100;
    const u32 driver = trace + 0x200, modes[] = {0, 0x103, 1, 2, 4, 5, 6, 0xfffffff7};
    /* Record each external call by return address and first argument. The
     * real byte-fill routine still runs, including both environment fills. */
    const u32 calls[] = {GA(0x80019a60), GA(0x8001a698), GA(0x8001eca4), GA(0x8001e758)};
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
    MEM_W32(GA(0x80024550), driver);
    MEM_W32(GA(0x80024554), stub);
    MEM_W32(driver + 0x34, stub);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof modes / sizeof modes[0]; i++) {
        u8 type = i & 3, debug = i & 1 ? 2 : 1;
        memset(nc_fast_ptr(state_base), 0xa5, 0x80);
        MEM_W8(state_base + 2, debug);
        MEM_W16(GA(0x800245d8) + type * 4, 1024 + type);
        MEM_W16(GA(0x800245ec) + type * 4, 512 + type);
        CPUState state = arguments(modes[i], 2, 3, i & 1 ? 8 : 4096);
        state.r[8] = trace;
        state.r[11] = 0x100 + type;
        ok = compare(state, GA(0x8001be40), GPU_NATIVE(reset_graph), 7);
        if ((modes[i] & 7) == 0 || (modes[i] & 7) == 3) {
            ok = ok && nc_cpu.r[2] == type && nc_cpu.r[8] == trace + 32 &&
                 MEM_R32(trace) == GA(0x8001be88) && MEM_R32(trace + 8) == GA(0x8001bea0) &&
                 MEM_R32(trace + 16) == GA(0x8001beb8) &&
                 MEM_R32(trace + 20) == (driver & 0xffffff) &&
                 MEM_R32(trace + 24) == GA(0x8001bec0) && MEM_R32(trace + 28) == modes[i] &&
                 MEM_R8(state_base) == type && MEM_R8(state_base + 1) == 1 &&
                 MEM_R16(state_base + 4) == 1024 + type && MEM_R16(state_base + 6) == 512 + type;
            for (unsigned j = 8; ok && j < 0x80; j++)
                ok = MEM_R8(state_base + j) == (j < 16 ? 0 : 0xff);
        } else {
            u32 last = trace + (debug >= 2 ? 8 : 0);
            ok = ok && nc_cpu.r[2] == state.r[11] && nc_cpu.r[8] == last + 8 &&
                 MEM_R32(last) == GA(0x8001bf84) && MEM_R32(last + 4) == 1 &&
                 (debug < 2 || MEM_R32(trace) == GA(0x8001bf68)) && MEM_R8(state_base) == 0xa5;
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_gpu_driver_reset(void) {
    const u32 ports = 0x80061000, first = GA(0x80024d30), second = GA(0x80027488);
    const u32 modes[] = {0, 1, 2, 3, 4, 5, 6, 7, 0x100, 0xfffffff9};
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Return the full mode plus seven from the version probe. */
    MEM_W32(GA(0x8001eb9c), 0x03e00008);
    MEM_W32(GA(0x8001eba0), 0x24820007);
    MEM_W32(GA(0x8002462c), ports);
    MEM_W32(GA(0x80024638), ports + 4);
    MEM_W32(GA(0x80024648), ports + 8);
    MEM_W32(GA(0x800244a8), ports + 12);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof modes / sizeof modes[0]; i++) {
        MEM_W32(ports, 0x12345678);
        MEM_W32(ports + 4, 0x01000201);
        MEM_W32(ports + 8, 0x12345000);
        MEM_W16(ports + 12, 0x145);
        MEM_W32(GA(0x8002465c), 2);
        MEM_W32(GA(0x80024660), 63);
        memset(nc_fast_ptr(first - 1), 0xa5, 0x102);
        memset(nc_fast_ptr(second - 1), 0xa5, 0x1802);
        CPUState state = arguments(modes[i], 2, 3, i & 1 ? 7 : 4096);
        u32 mode = modes[i] & 7;
        bool full = mode == 0, reset = full || mode == 1 || mode == 3;
        ok = compare(state, GA(0x8001e758), GPU_NATIVE(reset_gpu_driver), 6) &&
             nc_cpu.r[2] == (full ? modes[i] + 7 : 0) && MEM_R32(GA(0x8002465c)) == 0 &&
             MEM_R32(GA(0x80024660)) == 0 && MEM_R32(GA(0x8002466c)) == 0x145 &&
             MEM_R16(ports + 12) == 0x145 &&
             MEM_R32(ports) == (full    ? 0
                                : reset ? 0x01000000u
                                        : 0x12345678u) &&
             MEM_R32(ports + 4) == (reset ? 0x401 : 0x01000201) &&
             MEM_R32(ports + 8) == (reset ? 0x12345800 : 0x12345000) && MEM_R8(first - 1) == 0xa5 &&
             MEM_R8(first + 0x100) == 0xa5 && MEM_R8(second - 1) == 0xa5 &&
             MEM_R8(second + 0x1800) == 0xa5;
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
        {7, 7, 0xf0000, false, true},    {7, 7, 0xf0001, true, true},
        {8, 7, 3, true, false},          {0xffffffff, 0, 0xffffffff, false, true},
        {0, 0xffffffff, 2, true, false},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    for (unsigned j = 0; j < sizeof report / sizeof report[0]; j++)
        MEM_W32(GA(0x80019a60) + 4 * j, report[j]);
    MEM_W32(GA(0x8002462c), ports);      /* GPUSTAT / GP1 */
    MEM_W32(GA(0x80024630), ports + 4);  /* MADR */
    MEM_W32(GA(0x80024638), ports + 8);  /* CHCR */
    MEM_W32(GA(0x80024648), ports + 12); /* DPCR */
    MEM_W32(GA(0x800244a8), ports + 16); /* I_MASK */
    MEM_W32(GA(0x80023400), ports);
    MEM_W32(GA(0x80023404), ports + 4);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        MEM_W32(GA(0x800244d4), cases[i].frame);
        MEM_W32(GA(0x80024670), cases[i].deadline);
        MEM_W32(GA(0x80024674), cases[i].count);
        MEM_W32(GA(0x8002465c), 2);
        MEM_W32(GA(0x80024660), 63);
        MEM_W32(ports, 0x12345678);
        MEM_W32(ports + 4, 0x807654);
        MEM_W32(ports + 8, 0x01000201);
        MEM_W32(ports + 12, 0x12345000);
        MEM_W16(ports + 16, 0x145);
        CPUState state = arguments(1, 2, 3, i & 1 ? 5 : 4096);
        state.r[9] = trace;
        ok = compare(state, GA(0x8001ea30), GPU_NATIVE(check_queue_alarm), 4) &&
             nc_cpu.r[2] == (cases[i].expired ? UINT32_MAX : 0) &&
             MEM_R32(GA(0x80024674)) == cases[i].count + cases[i].count_advanced &&
             nc_cpu.r[9] == trace + (cases[i].expired ? 32 : 0) && MEM_R16(ports + 16) == 0x145;
        if (cases[i].expired)
            ok = ok && MEM_R32(trace) == GA(0x80018660) && MEM_R32(trace + 4) == 3 &&
                 MEM_R32(trace + 8) == 0x12345678 && MEM_R32(trace + 12) == 0x01000201 &&
                 MEM_R32(trace + 16) == GA(0x80018694) && MEM_R32(state.r[29] - 16) == 0x807654 &&
                 MEM_R32(GA(0x8002465c)) == 0 && MEM_R32(GA(0x80024660)) == 0 &&
                 MEM_R32(GA(0x8002466c)) == 0x145 && MEM_R32(ports) == 0x01000000 &&
                 MEM_R32(ports + 8) == 0x401 && MEM_R32(ports + 12) == 0x12345800;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_draw_offset(void) {
    const u8 types[] = {0, 1, 2, 255};
    const u32 coords[] = {0, 0x7ff, 0x800, 0xfff, 0x1000, 0x80008000, 0xffffffff};
    const u32 packet = 0x80060400;
    const u32 points[] = {SCRATCH_BASE + 2, 0xa0060400, packet + 2, packet + 4};
    for (unsigned g = 0; g < sizeof types; g++)
        for (unsigned i = 0; i < sizeof coords / sizeof coords[0]; i++) {
            u32 x = coords[i], y = coords[(i + 3) % 7];
            MEM_W8(GA(0x80024558), types[g]);
            if (GPU_LEAF(draw_offset_word) &&
                !compare(arguments(x, y, 0, 19), GA(0x8001d664), GPU_LEAF(draw_offset_word), 18))
                return false;
            for (unsigned j = 0; j < sizeof points / sizeof points[0]; j++) {
                memset(nc_fast_ptr(packet), 0xa5, 16);
                MEM_W16(points[j], (u16)x);
                MEM_W16(points[j] + 2, (u16)y);
                if (!compare(arguments(packet, points[j], 0, 44), GA(0x8001d108),
                             GPU_NATIVE(draw_offset_packet), 43))
                    return false;
            }
        }
    return true;
}

static bool check_set_draw_env(void) {
    const u32 env = 0x80062000;
    const struct {
        s16 x, y, w, h;
        bool clear, alias;
    } cases[] = {
        {0, 0, 320, 240, false, false},    {64, 10, 320, 240, true, false},
        {1, -2, 64, 24, true, false},      {64, 2, 65, 8, true, false},
        {0, 0, -1, -2, true, false},       {0, 32767, 2000, 1000, true, false},
        {-32768, -3, 64, -1, true, false}, {0, 0, 320, 240, true, true},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE); /* run the five GPU command builders from the disc */
    MEM_W16(GA(0x8002455c), 1024);
    MEM_W16(GA(0x8002455e), 512);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        u32 packet = cases[i].alias ? env : env + 28;
        memset(nc_fast_ptr(env), 0xcc, 72);
        MEM_W8(GA(0x80024558), i & 1);
        MEM_W16(env, (u16)cases[i].x);
        MEM_W16(env + 2, (u16)cases[i].y);
        MEM_W16(env + 4, (u16)cases[i].w);
        MEM_W16(env + 6, (u16)cases[i].h);
        MEM_W16(env + 8, 13);
        MEM_W16(env + 10, (u16)-7);
        MEM_W8(env + 24, cases[i].clear);
        MEM_W8(env + 25, 0x12);
        MEM_W8(env + 26, 0x34);
        MEM_W8(env + 27, 0x56);
        u32 link = MEM_R32(packet) & 0xffffff;
        CPUState state = arguments(packet, env, 3, i & 1 ? 7 : 8192);
        /* Overlap lets the E6 command overwrite isbg before the branch. */
        bool clear = cases[i].clear && !cases[i].alias;
        u32 length = clear ? 9 : 6;
        ok = compare(state, GA(0x8001d1cc), GPU_NATIVE(set_draw_env), 6) && nc_cpu.r[2] == length &&
             MEM_R32(packet) == ((length << 24) | link) && MEM_R32(packet + 24) == 0xe6000000;
        if (clear) {
            u32 w = cases[i].w < 0 ? 0 : cases[i].w > 1023 ? 1023 : cases[i].w;
            u32 h = cases[i].h < 0 ? 0 : cases[i].h > 511 ? 511 : cases[i].h;
            bool fill = ((cases[i].x | w) & 63) == 0;
            u16 x = (u16)(cases[i].x - (fill ? 0 : 13));
            u16 y = (u16)(cases[i].y + (fill ? 0 : 7));
            ok = ok && MEM_R32(packet + 28) == ((fill ? 0x02000000u : 0x60000000u) | 0x563412) &&
                 MEM_R32(packet + 32) == ((u32)y << 16 | x) &&
                 MEM_R32(packet + 36) == (h << 16 | w);
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
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
                MEM_W8(GA(0x80024558), types[i]);
                MEM_W32(state.r[29] + 16, rects[j]);
                if (!compare(state, GA(0x8001d174), GPU_NATIVE(draw_mode_packet), 88))
                    return false;
            }
    return true;
}

static bool check_gpu_clear_rectangle(void) {
    const u32 rect = 0x80061000, packet = GA(0x80024cec), gp0 = 0x1f801810;
    const struct {
        s16 x, width, height;
        u16 expected_width, expected_height;
        bool aligned;
    } cases[] = {
        {64, 64, 2, 64, 2, true},   {64, 67, 2, 67, 2, false}, {65, 64, 2, 64, 2, false},
        {0, 200, 10, 128, 8, true}, {9, -2, 3, 0, 3, false},   {9, 3, -1, 3, 0, false},
    };
    StateIO saved = {0};
    interrupt_start_state(&saved);
    gpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        StateIO native = {0}, guest = {0};
        for (unsigned run = 0; ok && run < 2; run++) {
            MEM_W32(0x1f801814, 0);
            MEM_W32(GA(0x80024628), gp0);
            MEM_W32(GA(0x8002462c), 0x1f801814);
            MEM_W32(GA(0x80024630), 0x1f8010a0);
            MEM_W32(GA(0x80024634), 0x1f8010a4);
            MEM_W32(GA(0x80024638), 0x1f8010a8);
            MEM_W32(GA(0x8002455c), 9u << 16 | 129);
            /* Script the GPU info queries. The port currently implements
             * GP0 reads for pixel transfers only. The DMA list runs normally. */
            MEM_W32(GA(0x8001e124), 0x00041080); /* sll v0,a0,2 */
            MEM_W32(GA(0x8001e128), 0x004a1021); /* addu v0,v0,t2 */
            MEM_W32(GA(0x8001e12c), 0x8c420000); /* lw v0,0(v0) */
            MEM_W32(GA(0x8001e130), 0x03e00008);
            MEM_W32(GA(0x8001e134), 0);
            MEM_W32(rect + 32 + 12, 0x402);
            MEM_W32(rect + 32 + 16, 0x32180);
            MEM_W32(rect + 32 + 20, 0x801);
            /* Distinct draw area and offset must survive a rectangle clear. */
            MEM_W32(gp0, 0xe3000402);
            MEM_W32(gp0, 0xe4032180);
            MEM_W32(gp0, 0xe5000801);
            memset(nc_fast_ptr(packet), 0x55, 52);
            MEM_W16(rect, (u16)cases[i].x);
            MEM_W16(rect + 2, 12);
            MEM_W16(rect + 4, (u16)cases[i].width);
            MEM_W16(rect + 6, (u16)cases[i].height);
            u32 color = i & 1 ? 0x800000ff : 0xff;
            nc_cpu = arguments(rect, color, 3, i & 1 ? 6 : 4096);
            nc_cpu.r[10] = rect + 32;
            if (run)
                interp_call(&nc_cpu, GA(0x8001d8ec));
            else
                ok = GPU_NATIVE(clear_gpu_rectangle)(&nc_cpu);
            StateIO *result = run ? &guest : &native;
            interrupt_start_state(result);
            gpu_serialize(result);
            ok = ok && nc_cpu.r[2] == 0 && MEM_R16(rect + 4) == cases[i].expected_width &&
                 MEM_R16(rect + 6) == cases[i].expected_height;
            if (cases[i].aligned)
                ok = ok && MEM_R32(packet) == 0x05ffffff && MEM_R32(packet + 12) == 0x020000ff;
            else
                ok = ok && MEM_R32(packet) == (0x08000000u | (GA(0x80024d10) & 0xffffff)) &&
                     MEM_R32(packet + 24) == 0x600000ff && MEM_R32(packet + 36) == 0x03ffffff &&
                     MEM_R32(packet + 40) == 0xe3000402 && MEM_R32(packet + 44) == 0xe4032180 &&
                     MEM_R32(packet + 48) == 0xe5000801;
            if (ok && cases[i].expected_width && cases[i].expected_height) {
                MEM_W32(gp0, 0xc0000000); /* first cleared pixel */
                MEM_W32(gp0, MEM_R32(rect));
                MEM_W32(gp0, 0x00010001);
                ok = (MEM_R32(gp0) & 0xffff) == 31;
            }
            saved.loading = true;
            saved.pos = 0;
            interrupt_start_state(&saved);
            gpu_serialize(&saved);
        }
        ok = ok && !saved.error && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    nc_set_module(module);
    free(saved.buf);
    return ok;
}

static bool check_gpu_image_transfer(bool readback) {
    const u32 rect = 0x80061000, pixels = rect + 64, status = rect + 512;
    const u32 gp0 = 0x1f801810, gp1 = gp0 + 4;
    const struct {
        s16 width, height, clipped_width, clipped_height;
        bool wait, timeout;
        bool wait_for_data;
    } cases[] = {
        {-1, 8, 0, 8, false, false, false},  {2, -3, 2, 0, false, false, false},
        {3, 3, 3, 3, false, false, false},   /* CPU words, odd pixel count */
        {32, 1, 32, 1, false, false, false}, /* one DMA block */
        {50, 9, 37, 2, false, false, false}, /* clipped, CPU prefix plus DMA */
        {4, 1, 4, 1, true, false, false},    {4, 1, 4, 1, true, true, false},
        {4, 1, 4, 1, true, true, true}, /* timeout after the read command */
    };
    StateIO saved = {0};
    interrupt_start_state(&saved);
    gpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        if (cases[i].wait_for_data && !readback)
            continue;
        StateIO native = {0}, guest = {0};
        for (unsigned run = 0; ok && run < 2; run++) {
            MEM_W32(GA(0x8001e9fc), 0x03e00008); /* set_alarm */
            MEM_W32(GA(0x8001ea00), 0);
            MEM_W32(GA(0x8001ea30), 0xad090000); /* get_alarm makes the GPU ready */
            MEM_W32(GA(0x8001ea34), 0x03e00008);
            MEM_W32(GA(0x8001ea38), 0x01401021); /* return t2 */
            MEM_W32(gp1, 0);                     /* reset command FIFO */
            MEM_W32(GA(0x80024628), gp0);
            MEM_W32(GA(0x8002462c), cases[i].wait ? status : gp1);
            MEM_W32(status, cases[i].wait_for_data ? 1u << 26 : 0);
            MEM_W32(GA(0x80024630), 0x1f8010a0);
            MEM_W32(GA(0x80024634), 0x1f8010a4);
            MEM_W32(GA(0x80024638), 0x1f8010a8);
            MEM_W32(GA(0x8002455c), 2u << 16 | 37);
            if (i < 3)
                MEM_W16(GA(0x8002455e), 8);
            MEM_W32(rect, 10u << 16 | 20);
            MEM_W16(rect + 4, (u16)cases[i].width);
            MEM_W16(rect + 6, (u16)cases[i].height);
            for (unsigned j = 0; j < 80; j++)
                MEM_W16(pixels + j * 2, (u16)(0x1240 + j));
            unsigned count = cases[i].clipped_width * cases[i].clipped_height;
            if (readback && count) {
                MEM_W32(gp0, 0xa0000000); /* seed VRAM with a known rectangle */
                MEM_W32(gp0, MEM_R32(rect));
                MEM_W32(gp0, (u32)cases[i].clipped_height << 16 | (u32)cases[i].clipped_width);
                for (unsigned j = 0; j < count; j += 2)
                    MEM_W32(gp0, MEM_R32(pixels + j * 2));
                memset(nc_fast_ptr(pixels), 0xdd, 160);
            }
            nc_cpu = arguments(rect, pixels, 0, i & 1 ? (readback ? 9 : 10) : 4096);
            nc_cpu.r[8] = status;
            nc_cpu.r[9] = 3u << 26;
            nc_cpu.r[10] = cases[i].timeout;
            if (run)
                interp_call(&nc_cpu, readback ? GA(0x8001ddb0) : GA(0x8001db5c));
            else
                ok = readback ? GPU_NATIVE(read_gpu_image)(&nc_cpu)
                              : GPU_NATIVE(upload_gpu_image)(&nc_cpu);
            StateIO *result = run ? &guest : &native;
            interrupt_start_state(result);
            gpu_serialize(result);
            ok = ok && MEM_R16(rect + 4) == cases[i].clipped_width &&
                 MEM_R16(rect + 6) == cases[i].clipped_height &&
                 nc_cpu.r[2] == (count && !cases[i].timeout ? 0u : UINT32_MAX);
            if (readback && count) {
                for (unsigned j = 0; ok && j < count; j++)
                    ok = MEM_R16(pixels + j * 2) == (cases[i].timeout ? 0xddddu : 0x1240u + j);
                ok = ok && MEM_R32(pixels + 4 * ((count + 1) / 2)) == 0xdddddddd;
            } else if (ok && count && !cases[i].timeout) {
                MEM_W32(gp0, 0xc0000000); /* read the uploaded pixels back from VRAM */
                MEM_W32(gp0, MEM_R32(rect));
                MEM_W32(gp0, MEM_R32(rect + 4));
                for (unsigned j = 0; ok && j < count; j += 2) {
                    u32 pair = MEM_R32(gp0);
                    ok = (u16)pair == 0x1240 + j && (j + 1 == count || pair >> 16 == 0x1241 + j);
                }
            }
            saved.loading = true;
            saved.pos = 0;
            interrupt_start_state(&saved);
            gpu_serialize(&saved);
        }
        ok = ok && !saved.error && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    nc_set_module(module);
    free(saved.buf);
    return ok;
}

static bool check_submit_gpu_command(void) {
    const u32 dma = 0x80061000, status = dma + 4, trace = dma + 32;
    const u32 command = 0x80061200, data = command + 64, ring = GA(0x80027488);
    const struct {
        u32 head, tail, size;
        bool enabled, busy, callback, timeout, wait, queued;
    } cases[] = {
        {4, 2, 8, false, true, true, false, false, false},
        {4, 4, 8, true, false, false, false, true, false},
        {4, 4, 0, true, true, false, false, false, true},
        {63, 63, 11, true, false, true, false, false, true},
        {2, 1, 3, true, false, false, false, false, true},
        {2, 1, UINT32_MAX, true, false, false, false, false, true},
        {63, 0, 4, true, false, false, true, false, false},
        {63, 0, 84, true, false, false, false, false, true},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    gpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* The mask exchange starts a 4-pixel VRAM read for the readiness test.
     * The mock executor advances the tail to t1, making room in a full ring. */
    const u32 mask[] = {0xada40000, 0xadcf0000, 0xadc00000, 0xadcb0000, 0x03e00008, 0x24020045};
    const u32 run[] = {0xaf290000, 0x03e00008, 0x27180001};
    const u32 cmd[] = {0xada40004, 0x03e00008, 0xada50008};
    const u32 callback[] = {0xada4000c, 0x03e00008, 0xada50010};
    for (unsigned j = 0; j < sizeof mask / sizeof mask[0]; j++)
        MEM_W32(GA(0x8001a814) + 4 * j, mask[j]);
    for (unsigned j = 0; j < 3; j++) {
        MEM_W32(GA(0x8001e45c) + 4 * j, run[j]);
        MEM_W32(command + 4 * j, cmd[j]);
        MEM_W32(GA(0x8001a6f8) + 4 * j, callback[j]);
    }
    MEM_W32(GA(0x8001e9fc), 0x03e00008);
    MEM_W32(GA(0x8001ea00), 0);
    MEM_W32(GA(0x8001ea30), 0x03e00008);
    MEM_W32(GA(0x8001ea34), 0x01401021); /* return t2 */
    gpu_gp0(0xa0000000);
    gpu_gp0(0);
    gpu_gp0(0x00010004);
    gpu_gp0(0);
    gpu_gp0(1u << 26);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        u32 head = cases[i].head, tail = cases[i].tail, size = cases[i].size;
        bool full = ((head + 1) & 63) == tail;
        u32 row = ring + 96 * head;
        memset(nc_fast_ptr(row), 0xcc, 96);
        memset(nc_fast_ptr(trace), 0xff, 20);
        for (unsigned j = 0; j < 21; j++)
            MEM_W32(data + 4 * j, 0x11223300 + j);
        MEM_W32(GA(0x8002465c), head);
        MEM_W32(GA(0x80024660), tail);
        MEM_W32(GA(0x80024560), 0);
        MEM_W32(GA(0x80024564), cases[i].callback);
        MEM_W8(GA(0x80024559), cases[i].enabled);
        MEM_W32(GA(0x80024638), dma);
        MEM_W32(dma, cases[i].busy ? 1u << 24 : 0);
        MEM_W32(GA(0x8002462c), cases[i].wait ? 0x1f801810 : status);
        MEM_W32(status, 1u << 26);
        CPUState state = arguments(command, data, size, i & 1 ? 12 : 8192);
        state.r[7] = 0x12345678;
        state.r[25] = GA(0x80024660);
        state.r[9] = full ? (tail + 1) & 63 : tail;
        state.r[10] = cases[i].timeout;
        state.r[11] = 0x00010004;
        state.r[13] = trace;
        state.r[14] = cases[i].wait ? 0x1f801810 : status + 4;
        state.r[15] = 0xc0000000;
        state.r[24] = 0;
        ok = compare(state, GA(0x8001e178), GPU_NATIVE(submit_gpu_command), 11);
        if (cases[i].timeout)
            ok = ok && nc_cpu.r[2] == UINT32_MAX && MEM_R32(GA(0x80024560)) == 0;
        else if (cases[i].queued) {
            u32 next = (head + 1) & 63;
            ok = ok && MEM_R32(row) == command && MEM_R32(row + 4) == (size ? row + 12 : data) &&
                 MEM_R32(row + 8) == state.r[7] && MEM_R32(GA(0x8002465c)) == next &&
                 nc_cpu.r[2] == ((next - state.r[9]) & 63) && nc_cpu.r[24] == (u32)(1 + full) &&
                 MEM_R32(trace + 12) == 2 && MEM_R32(trace + 16) == GA(0x8001e45c);
            for (unsigned j = 0; ok && j < 21; j++)
                ok = MEM_R32(row + 12 + 4 * j) ==
                     ((s32)j < (s32)size / 4 ? 0x11223300 + j : 0xcccccccc);
        } else {
            ok = ok && nc_cpu.r[2] == 0 && MEM_R32(trace + 4) == data &&
                 MEM_R32(trace + 8) == state.r[7] && MEM_R32(GA(0x8002464c)) == command &&
                 MEM_R32(GA(0x80024650)) == data && MEM_R32(GA(0x80024654)) == state.r[7];
        }
        if (!cases[i].timeout)
            ok = ok && MEM_R32(trace) == 0x45 && MEM_R32(GA(0x80024560)) == 1;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    gpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_execute_gpu_queue(void) {
    const u32 mask = 0x80061000, dma = mask + 4, status = mask + 8;
    const u32 command = 0x80061200, done = command + 64, trace = command + 128;
    const struct {
        unsigned head, tail;
        bool busy, start_dma, pending, callback, wait;
        unsigned executed, remaining;
    } cases[] = {
        {2, 0, true, false, false, false, false, 0, 1},
        {7, 7, false, false, false, true, false, 0, 0},
        {7, 7, false, false, true, true, false, 0, 0},
        {1, 63, false, false, true, true, false, 2, 0},
        {2, 63, false, true, true, true, false, 1, 2},
        {1, 0, false, true, true, true, false, 1, 0},
        {1, 0, false, false, true, false, true, 1, 0},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    gpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* SetIntrMask exchanges a RAM-backed mask. Each queued command records
     * both arguments, advances t4's trace pointer, and can start DMA. */
    const u32 mask_stub[] = {0x8d020000, 0xad040000, 0x03e00008, 0};
    const u32 cmd_stub[] = {0xad840000, 0xad850004, 0x258c0008, 0x03e00008, 0xad0a0004};
    for (unsigned j = 0; j < sizeof mask_stub / sizeof mask_stub[0]; j++)
        MEM_W32(GA(0x8001a814) + 4 * j, mask_stub[j]);
    for (unsigned j = 0; j < sizeof cmd_stub / sizeof cmd_stub[0]; j++)
        MEM_W32(command + 4 * j, cmd_stub[j]);
    MEM_W32(done, 0x03e00008);
    MEM_W32(done + 4, 0x25ef0001); /* count completion callbacks in t7 */
    /* DMACallback starts a 4-pixel VRAM read at (0,0). In the wait case
     * GPUREAD supplies a zero status word followed by the ready bit. */
    const u32 detach[] = {0xadcd0000, 0xadc00000, 0xadcb0000, 0x03e00008, 0x27390001};
    for (unsigned j = 0; j < sizeof detach / sizeof detach[0]; j++)
        MEM_W32(GA(0x8001a6f8) + 4 * j, detach[j]);
    gpu_gp0(0xa0000000);
    gpu_gp0(0);
    gpu_gp0(0x00010004);
    gpu_gp0(0);
    gpu_gp0(1u << 26);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        unsigned tail = cases[i].tail, count = cases[i].executed;
        MEM_W32(GA(0x80024638), dma);
        MEM_W32(GA(0x8002462c), cases[i].wait ? 0x1f801810 : status);
        MEM_W32(mask, 0x456);
        MEM_W32(dma, cases[i].busy ? 1u << 24 : 0);
        MEM_W32(status, 1u << 26);
        MEM_W32(GA(0x8002465c), cases[i].head);
        MEM_W32(GA(0x80024660), tail);
        MEM_W32(GA(0x80024560), cases[i].pending);
        MEM_W32(GA(0x80024564), cases[i].callback ? done : 0);
        for (unsigned j = 0; j < 64; j++) {
            u32 row = GA(0x80027488) + 96 * j;
            MEM_W32(row, command);
            MEM_W32(row + 4, 100 + j);
            MEM_W32(row + 8, 200 + j);
        }
        CPUState state = arguments(0, 0, 0, i & 1 ? 2 : 8192);
        state.r[8] = mask;
        state.r[10] = cases[i].start_dma ? 1u << 24 : 0;
        state.r[11] = 0x00010004;
        state.r[12] = trace;
        state.r[13] = 0xc0000000;
        state.r[14] = 0x1f801810;
        state.r[15] = state.r[25] = 0;
        bool notified = cases[i].callback && cases[i].pending && !cases[i].start_dma;
        ok = compare(state, GA(0x8001e45c), GPU_NATIVE(execute_gpu_queue), 1) &&
             nc_cpu.r[2] == cases[i].remaining && MEM_R32(mask) == 0x456 &&
             MEM_R32(GA(0x80024660)) == ((tail + count) & 63) &&
             nc_cpu.r[12] == trace + count * 8 && nc_cpu.r[15] == (u32)notified &&
             nc_cpu.r[25] == (u32)cases[i].wait &&
             MEM_R32(GA(0x80024560)) == (u32)(cases[i].pending && !notified);
        for (unsigned j = 0; ok && j < count; j++) {
            unsigned index = (tail + j) & 63;
            ok = MEM_R32(trace + 8 * j) == 100 + index && MEM_R32(trace + 8 * j + 4) == 200 + index;
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    gpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_gpu_queue_sync(void) {
    const u32 dma = 0x80061000, gpu = dma + 4, tail = GA(0x80024660);
    const u32 start = GA(0x8001e9fc), execute = GA(0x8001e45c), poll = GA(0x8001ea30);
    const struct {
        u32 mode, head, tail, dma_busy, gpu_ready, timeout;
        u32 result, polls, executions;
    } cases[] = {
        {0, 3, 3, 0, 1, 0, 0, 0, 0},          /* idle */
        {0, 4, 3, 0, 1, 0, 0, 1, 1},          /* drain pending entry */
        {0, 4, 3, 0, 1, 1, UINT32_MAX, 1, 1}, /* queue alarm */
        {0, 3, 3, 1, 1, 0, 0, 1, 0},          /* DMA completes */
        {0, 3, 3, 0, 0, 0, 0, 1, 0},          /* GPU becomes ready */
        {0, 3, 3, 1, 0, 1, UINT32_MAX, 1, 0}, /* hardware alarm */
        {1, 2, 63, 0, 1, 0, 3, 0, 1},         /* wrapped queue */
        {1, 2, 63, 1, 0, 0, 3, 0, 1},         {1, 3, 3, 0, 0, 0, 1, 0, 0},
        {2, 3, 3, 1, 1, 0, 1, 0, 0},          {1, 3, 3, 0, 1, 0, 0, 0, 0},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    MEM_W32(GA(0x80024638), dma);
    MEM_W32(GA(0x8002462c), gpu);
    MEM_W32(start, 0x03e00008);     /* jr ra */
    MEM_W32(start + 4, 0x258c0001); /* addiu t4,t4,1 */
    MEM_W32(execute, 0xad090000);   /* sw t1,0(t0): advance tail */
    MEM_W32(execute + 4, 0x03e00008);
    MEM_W32(execute + 8, 0x25ad0001); /* addiu t5,t5,1 */
    MEM_W32(poll, 0xad400000);        /* sw zero,0(t2): DMA complete */
    MEM_W32(poll + 4, 0xad4b0004);    /* sw t3,4(t2): GPU ready */
    MEM_W32(poll + 8, 0x25ce0001);    /* addiu t6,t6,1 */
    MEM_W32(poll + 12, 0x03e00008);
    MEM_W32(poll + 16, 0x01e01021); /* move v0,t7 */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        MEM_W32(dma, cases[i].dma_busy << 24);
        MEM_W32(gpu, cases[i].gpu_ready << 26);
        MEM_W32(GA(0x8002465c), cases[i].head);
        MEM_W32(tail, cases[i].tail);
        CPUState state = arguments(cases[i].mode, 2, 3, i & 1 ? 5 : 4096);
        state.r[8] = tail;
        state.r[9] = cases[i].head;
        state.r[10] = dma;
        state.r[11] = 1u << 26;
        state.r[12] = state.r[13] = state.r[14] = 0;
        state.r[15] = cases[i].timeout;
        ok = compare(state, GA(0x8001e8b4), GPU_NATIVE(sync_gpu_queue), 4) &&
             nc_cpu.r[2] == cases[i].result && nc_cpu.r[12] == (cases[i].mode == 0) &&
             nc_cpu.r[13] == cases[i].executions && nc_cpu.r[14] == cases[i].polls;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
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
            MEM_W8(GA(0x80024558), modes[mode]);
            if (!compare(state, GA(0x8001b548), GPU_NATIVE(texture_page), 62))
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
                MEM_W32(GA(0x80024504), mode >> 1);
                MEM_W8(GA(0x80024558), mode & 1);
                if (!compare(state, GA(0x8001b414), GPU_NATIVE(default_draw), 83))
                    return false;
            }
    return true;
}

#endif
