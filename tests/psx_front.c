/* PSX.EXE front end, memory card, display, heap and TIM/TMD routines (src/decomp/psx_front.c,
 * psx_tmd.c). Without arguments: rules checks on synthetic guest code. With the disc: every
 * routine against the original instructions, comparing the whole machine. */
#include <stdio.h>
#include <stdlib.h>

#include "decomp/psx_front.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include "port/runtime.h"
#include "port/savestate.h"

enum {
    GP = 0x80027be0,
    STACK = 0x801fe000,
    RETURN = 0x80010000,
    /* Test scratch: stub bodies, their value lists and test data, all above PSX.EXE's BSS. */
    TRACE = 0x80100000, /* pointer to the next trace word */
    SLOTS = 0x80100010, /* per-stub value cursors */
    TRACE_BUF = 0x80101000,
    VALUES = 0x80108000, /* 0x100 bytes per stub */
    BODIES = 0x80110000, /* 0x100 bytes per stub */
    DATA = 0x80140000,
    HEAP = 0x80160000,
    STUB_COUNT = 64,
};

/* --- machine snapshots --------------------------------------------------------------------- */

static void serialize_machine(StateIO *io) {
    cpu_serialize(io);
    bios_serialize(io);
    irq_serialize(io);
    timers_serialize(io);
    dma_serialize(io);
    gpu_serialize(io);
    spu_serialize(io);
    cdrom_serialize(io);
    mdec_serialize(io);
}

static void restore_machine(StateIO *io) {
    io->loading = true;
    io->pos = 0;
    serialize_machine(io);
    io->loading = false;
}

static bool same_machine(const StateIO *a, const StateIO *b, const char *name, int budget) {
    if (!a->error && !b->error && a->len == b->len && !memcmp(a->buf, b->buf, a->len))
        return true;
    size_t at = 0;
    while (at < a->len && at < b->len && a->buf[at] == b->buf[at])
        at++;
    fprintf(stderr, "%s, budget %d: state differs at byte %zu of %zu/%zu", name, budget, at, a->len,
            b->len);
    if (at < sizeof(CPUState))
        fprintf(stderr, " (CPUState offset %zu: register %zu)", at, at / 4);
    else if (at < sizeof(CPUState) + RAM_SIZE)
        fprintf(stderr, " (RAM %08x)", (unsigned)(0x80000000u + at - sizeof(CPUState)));
    fprintf(stderr, "\n");
    return false;
}

static const char *current = "";
static bool failed(const char *what) {
    fprintf(stderr, "%s: %s\n", current, what);
    return false;
}

/* Run the routine on nc_cpu at every budget. The lowest must decline without changing anything;
 * the others must match the original instructions exactly. The machine is left in the
 * interpreter's state for the last budget, so a test can chain calls. */
static bool check_budgets(u32 entry, bool (*native)(CPUState *), int low, const int *budgets,
                          unsigned count) {
    StateIO base = {0};
    serialize_machine(&base);
    bool ok = !base.error;
    if (ok && low > 0) {
        StateIO before = {0}, after = {0};
        restore_machine(&base);
        nc_cpu.budget = low;
        serialize_machine(&before);
        if (native(&nc_cpu))
            ok = failed("did not decline");
        serialize_machine(&after);
        ok = ok && same_machine(&before, &after, current, low);
        free(before.buf);
        free(after.buf);
    }
    for (unsigned i = 0; ok && i < count; i++) {
        StateIO native_run = {0}, guest_run = {0};
        restore_machine(&base);
        nc_cpu.budget = budgets[i];
        if (!native(&nc_cpu))
            ok = failed("declined");
        serialize_machine(&native_run);
        restore_machine(&base);
        nc_cpu.budget = budgets[i];
        interp_call(&nc_cpu, entry);
        serialize_machine(&guest_run);
        ok = ok && same_machine(&native_run, &guest_run, current, budgets[i]);
        free(native_run.buf);
        free(guest_run.buf);
    }
    free(base.buf);
    return ok;
}

/* Budgets that move the next event through the routine, plus one far away. */
static const int sweep[] = {1,  2,  3,  4,  5,  6,  7,   8,   9,    10,   11,
                            12, 13, 14, 15, 16, 17, 18,  19,  20,   23,   29,
                            31, 37, 41, 53, 67, 97, 131, 257, 1000, 4096, 100000};
#define SWEEP sweep, sizeof sweep / sizeof sweep[0]

static bool check(const char *name, u32 entry, bool (*native)(CPUState *), int low) {
    current = name;
    if (getenv("NC_TEST_VERBOSE"))
        fprintf(stderr, "check %s\n", name);
    /* Budgets below the decline threshold make no sense as comparisons. */
    int budgets[sizeof sweep / sizeof sweep[0]];
    unsigned count = 0;
    for (unsigned i = 0; i < sizeof sweep / sizeof sweep[0]; i++)
        if (sweep[i] > low)
            budgets[count++] = sweep[i];
    return check_budgets(entry, native, low, budgets, count);
}

/* --- guest code for callees ---------------------------------------------------------------- */

enum { AT = 1, V0 = 2, A0 = 4, A1 = 5, A2 = 6, T0 = 8, T1 = 9, RA = 31 };
static u32 op_i(u32 op, u32 rs, u32 rt, u32 imm) {
    return op << 26 | rs << 21 | rt << 16 | (imm & 0xffff);
}
static u32 lui(u32 rt, u32 address) {
    return op_i(15, 0, rt, (address + 0x8000) >> 16);
}
static u32 lo(u32 address) {
    return address & 0xffff;
}
#define LW(rt, off, base) op_i(0x23, base, rt, off)
#define SW(rt, off, base) op_i(0x2b, base, rt, off)
#define LHU(rt, off, base) op_i(0x25, base, rt, off)
#define SH(rt, off, base) op_i(0x29, base, rt, off)
#define SB(rt, off, base) op_i(0x28, base, rt, off)
#define ADDIU(rt, rs, imm) op_i(9, rs, rt, imm)
#define JR(rs) ((rs) << 21 | 8)
#define J(target) (2u << 26 | (((target) >> 2) & 0x03ffffff))

typedef enum {
    STUB_PLAIN = 0,
    STUB_HALF_A2 = 1, /* store the next value's low halfword at 0(a2) (card sector) */
    STUB_FILE_A0 = 2, /* copy six words of the stub's data to 0(a0) (CdlFILE) */
    STUB_BYTE_A1 = 4, /* store the next value of the following stub's list at 3(a1) */
} StubKind;

typedef struct {
    u32 address, saved[2];
} Patch;
static Patch patches[STUB_COUNT];
static unsigned patch_count;

static u32 stub_values(unsigned index) {
    return VALUES + 0x100 * index;
}

/* Replace the routine at `address` with a stub that logs its return address and returns the
 * next value of its list (the last value repeats). */
static unsigned stub(u32 address, const u32 *values, unsigned count, StubKind kind) {
    const unsigned index = patch_count++;
    const u32 slot = SLOTS + 4 * index, list = stub_values(index);
    const u32 body = BODIES + 0x100 * index;
    patches[index] = (Patch){address, {MEM_R32(address), MEM_R32(address + 4)}};
    for (unsigned i = 0; i < 0x40; i++)
        MEM_W32(list + 4 * i, count ? values[i < count ? i : count - 1] : 0);
    MEM_W32(slot, list);
    u32 code[64];
    unsigned n = 0;
    code[n++] = lui(AT, TRACE);
    code[n++] = LW(T0, lo(TRACE), AT);
    code[n++] = 0;
    code[n++] = SW(RA, 0, T0);
    code[n++] = ADDIU(T0, T0, 4);
    code[n++] = SW(T0, lo(TRACE), AT);
    code[n++] = lui(AT, slot);
    code[n++] = LW(T0, lo(slot), AT);
    code[n++] = 0;
    code[n++] = LW(V0, 0, T0);
    code[n++] = ADDIU(T0, T0, 4);
    code[n++] = SW(T0, lo(slot), AT);
    if (kind & STUB_HALF_A2)
        code[n++] = SH(V0, 0, A2);
    if (kind & STUB_FILE_A0) {
        const u32 file = list + 0x80;
        for (unsigned i = 0; i < 6; i++) {
            code[n++] = lui(AT, file + 4 * i);
            code[n++] = LW(T1, lo(file + 4 * i), AT);
            code[n++] = 0;
            code[n++] = SW(T1, 4 * i, A0);
        }
    }
    if (kind & STUB_BYTE_A1) {
        const u32 next = slot + 4;
        code[n++] = lui(AT, next);
        code[n++] = LW(T0, lo(next), AT);
        code[n++] = 0;
        code[n++] = LW(T1, 0, T0);
        code[n++] = ADDIU(T0, T0, 4);
        code[n++] = SW(T0, lo(next), AT);
        code[n++] = SB(T1, 3, A1);
    }
    code[n++] = JR(RA);
    code[n++] = 0;
    for (unsigned i = 0; i < n; i++)
        MEM_W32(body + 4 * i, code[i]);
    MEM_W32(address, J(body));
    MEM_W32(address + 4, 0);
    return index;
}

static unsigned stub_const(u32 address, u32 value) {
    return stub(address, &value, 1, STUB_PLAIN);
}

/* A value list for a stub that is not installed (the second list of STUB_BYTE_A1). */
static void stub_list(const u32 *values, unsigned count) {
    const unsigned index = patch_count++;
    patches[index] = (Patch){0, {0, 0}};
    for (unsigned i = 0; i < 0x40; i++)
        MEM_W32(stub_values(index) + 4 * i, count ? values[i < count ? i : count - 1] : 0);
    MEM_W32(SLOTS + 4 * index, stub_values(index));
}

static void unstub_all(void) {
    while (patch_count) {
        const Patch *p = &patches[--patch_count];
        if (p->address) {
            MEM_W32(p->address, p->saved[0]);
            MEM_W32(p->address + 4, p->saved[1]);
        }
    }
    MEM_W32(TRACE, TRACE_BUF);
}

static void fresh_cpu(void) {
    CPUState cpu = {0};
    for (unsigned i = 1; i < 32; i++)
        cpu.r[i] = 0x01010101u * i;
    cpu.r[28] = GP;
    cpu.r[29] = STACK;
    cpu.r[31] = RETURN;
    cpu.cop0[12] = nc_cpu.cop0[12];
    cpu.budget = 4096;
    nc_cpu = cpu;
    MEM_W32(TRACE, TRACE_BUF);
}

static void args(u32 a0, u32 a1, u32 a2, u32 a3) {
    fresh_cpu();
    nc_cpu.r[4] = a0;
    nc_cpu.r[5] = a1;
    nc_cpu.r[6] = a2;
    nc_cpu.r[7] = a3;
}

static void put_string(u32 address, const char *text) {
    for (size_t i = 0; i <= strlen(text); i++)
        MEM_W8(address + (u32)i, (u8)text[i]);
}

/* --- memory card ----------------------------------------------------------------------------- */

static bool card_events(void) {
    unstub_all();
    for (unsigned i = 0; i < 8; i++)
        MEM_W32(GP + 0x84 + 4 * i, 0);
    fresh_cpu();
    if (!check("open events", 0x80018de4, nc_psx_card_open_events, 4))
        return false;
    for (unsigned i = 0; i < 3; i++) {
        static const struct {
            const char *name;
            u32 entry;
            bool (*native)(CPUState *);
        } tests[] = {{"test events", 0x8001905c, nc_psx_card_test_events},
                     {"test sw events", 0x800190d4, nc_psx_card_test_sw_events},
                     {"test hw events", 0x8001911c, nc_psx_card_test_hw_events}};
        fresh_cpu();
        if (!check(tests[i].name, tests[i].entry, tests[i].native, 7))
            return false;
    }

    /* TestEvent fires on the n-th poll: every event of both groups, over two rounds. */
    for (unsigned fire = 1; fire <= 9; fire++) {
        for (unsigned hw = 0; hw < 2; hw++) {
            u32 values[16] = {0};
            values[fire - 1] = 1;
            unstub_all();
            stub(0x8001b764, values, 16, STUB_PLAIN);
            fresh_cpu();
            if (!check(hw ? "wait hw event" : "wait sw event", hw ? 0x800191cc : 0x80019164,
                       hw ? nc_psx_card_wait_hw_event : nc_psx_card_wait_sw_event, 2))
                return false;
        }
    }

    /* The real BIOS: no card is inserted, so the probe times out. */
    unstub_all();
    args(0, 0, 0, 0);
    if (!check("probe (no card)", 0x80019234, nc_psx_card_probe, 8))
        return false;
    fresh_cpu();
    if (!check("close events", 0x80018f64, nc_psx_card_close_events, 4))
        return false;
    return true;
}

static bool card_probe_paths(void) {
    for (u32 first = 0; first < 5; first++)
        for (u32 second = 0; second < 5; second++)
            for (u32 formatted = 0; formatted < 2; formatted++) {
                if (first != 3 && second) /* the second probe follows a card change only */
                    continue;
                if (first != 0 && formatted)
                    continue;
                unstub_all();
                const u32 waits[] = {first, second};
                stub(0x80019164, waits, 2, STUB_PLAIN);
                stub_const(0x80019354, formatted);
                stub_const(0x8001905c, 0);
                stub_const(0x800190d4, 0);
                stub_const(0x8001911c, 0);
                stub_const(0x800191cc, 0);
                stub_const(0x800258dc, 1);
                stub_const(0x8002591c, 1);
                stub_const(0x800258ec, 1);
                args(0x10, 0, 0, 0);
                if (!check("probe", 0x80019234, nc_psx_card_probe, 8))
                    return false;
            }
    return true;
}

static bool card_formatted_paths(void) {
    static const u32 headers[] = {0x434d, 0x584d, 0x4358, 0}; /* "MC", "MX", "XC" */
    for (u32 wait = 0; wait < 2; wait++)
        for (unsigned h = 0; h < 4; h++) {
            unstub_all();
            stub_const(0x8001905c, 0);
            stub_const(0x8002590c, 1);
            stub(0x800258fc, &headers[h], 1, STUB_HALF_A2);
            stub_const(0x800191cc, wait);
            args(0, 0, 0, 0);
            if (!check("card formatted", 0x80019354, nc_psx_card_formatted, 8))
                return false;
        }
    return true;
}

static bool card_file_checks(void) {
    put_string(DATA, "BASLUS-00582NIGHT");
    for (unsigned found = 0; found < 2; found++) {
        unstub_all();
        stub_const(0x8001b7c4, found ? 3 : UINT32_MAX);
        stub_const(0x8001b7f4, 1);
        args(0x10, DATA, 0, 0);
        if (!check("file exists", 0x800193e4, nc_psx_card_file_exists, 8))
            return false;
    }

    /* Directory scans: no file, two good files, an open failure, a short read. */
    static const struct {
        u32 files, open_fail_at, short_at;
    } scans[] = {{0, 0, 0}, {2, 0, 0}, {3, 2, 0}, {3, 0, 2}, {1, 1, 0}};
    for (unsigned s = 0; s < sizeof scans / sizeof scans[0]; s++) {
        unstub_all();
        stub_const(0x8001b804, scans[s].files != 0);
        u32 next[8] = {0}, opens[8], reads[8], sizes[8];
        for (unsigned i = 0; i < 8; i++) {
            next[i] = i + 1 < scans[s].files;
            opens[i] = scans[s].open_fail_at == i + 1 ? UINT32_MAX : 4 + i;
            reads[i] = scans[s].short_at == i + 1 ? 0x40 : 0x80;
            sizes[i] = 1 + 2 * i;
        }
        stub(0x8001b814, next, 8, STUB_PLAIN);
        stub(0x8001b7c4, opens, 8, STUB_PLAIN);
        stub(0x8001b7d4, reads, 8, STUB_BYTE_A1);
        stub_list(sizes, 8);
        stub_const(0x8001b7f4, 1);
        args(0, 0, 0, 0);
        if (!check("free blocks", 0x8001943c, nc_psx_card_free_blocks, 13))
            return false;
    }
    return true;
}

/* --- display --------------------------------------------------------------------------------- */

static const u32 settings = DATA + 0x800;

static bool display_paths(void) {
    unstub_all();
    MEM_W32(GP + 0x30, settings);
    static const u32 sizes[][3] = {{640, 256, 250}, {320, 480, 512}, {0xfffffffd, 0x1e1, 1}};
    for (unsigned i = 0; i < 3; i++) {
        args(sizes[i][0], sizes[i][1], sizes[i][2], 0);
        if (!check("init display buffers", 0x80019a18, nc_psx_init_display_buffers, 19))
            return false;
    }
    for (unsigned interlaced = 0; interlaced < 2; interlaced++) {
        MEM_W32(settings, interlaced ? 0x20000000 : 0xdfffffff);
        MEM_W8(settings + 0x18, 10 + 7 * interlaced);
        MEM_W8(settings + 0x19, 3);
        args(320, 240, 0, 300);
        MEM_W32(STACK + 0x10, 0x11);
        MEM_W32(STACK + 0x14, 0x22);
        MEM_W32(STACK + 0x18, 0x133);
        MEM_W32(STACK + 0x1c, 0x8002a25c);
        if (!check("setup display", 0x80019b40, nc_psx_setup_display, 33))
            return false;
    }
    static const u8 offsets[][2] = {{10, 5}, {0, 0}, {255, 200}};
    for (unsigned i = 0; i < 3; i++) {
        MEM_W8(settings + 0x18, offsets[i][0]);
        MEM_W8(settings + 0x19, offsets[i][1]);
        fresh_cpu();
        if (!check("apply screen offset", 0x80019c04, nc_psx_apply_screen_offset, 2))
            return false;
    }
    fresh_cpu();
    if (!check("clear display", 0x80019c4c, nc_psx_clear_display, 4))
        return false;

    /* Again with every callee replaced by a stub that logs its return address. */
    static const u32 callees[] = {0x80021a38, 0x80021a58, 0x8001c96c, 0x8001ca3c, 0x8001d838,
                                  0x8001dd40, 0x8001d9cc, 0x80019a18, 0x80019c04};
    for (unsigned i = 0; i < sizeof callees / sizeof callees[0]; i++)
        stub_const(callees[i], i);
    args(640, 480, 300, 0);
    if (!check("clear display (stubs)", 0x80019c4c, nc_psx_clear_display, 4))
        return false;
    for (unsigned i = 0; i < 2; i++) {
        MEM_W32(settings, i ? 0x20000000 : 0);
        args(256, 240, 0, 300);
        MEM_W32(STACK + 0x1c, 0x8002a25c);
        if (!check("setup display (stubs)", 0x80019b40, nc_psx_setup_display, 33))
            return false;
    }
    unstub_all();
    for (unsigned i = 0; i < 7; i++)
        stub_const(callees[i], i);
    args(640, 480, 300, 0);
    if (!check("init display buffers (stubs)", 0x80019a18, nc_psx_init_display_buffers, 19))
        return false;
    unstub_all();
    fresh_cpu();
    nc_cpu.r[4] = 0x155;
    if (!check("set geom screen", 0x80021a58, nc_psx_set_geom_screen, 3))
        return false;
    stub_const(0x80021a68, 0);
    fresh_cpu();
    if (!check("init geom", 0x800219b0, nc_psx_init_geom, 4))
        return false;
    unstub_all();
    return true;
}

/* --- heap ------------------------------------------------------------------------------------ */

static NcHeapWarning warning_case;
static bool heap_warning(CPUState *c) {
    return nc_psx_heap_warning(c, warning_case);
}

static bool heap_call(const char *name, u32 entry, bool (*native)(CPUState *), int low, u32 a0,
                      u32 a1) {
    args(a0, a1, 0, 0);
    return check(name, entry, native, low);
}

static bool heap_paths(void) {
    unstub_all();
    for (unsigned misalign = 0; misalign < 4; misalign += 3)
        if (!heap_call("init heap", 0x80019edc, nc_psx_init_heap, 18, HEAP + misalign, 0x3000))
            return false;
    const u32 sizes[] = {0x10, 0x200, 1, 0x7f, 0};
    u32 blocks[5];
    for (unsigned i = 0; i < 5; i++) {
        if (!heap_call("allocate", 0x8001a108, nc_psx_allocate, 4, sizes[i], 0))
            return false;
        blocks[i] = nc_cpu.r[2];
    }
    if (!heap_call("allocate (no room)", 0x8001a108, nc_psx_allocate, 4, 0x10000, 0) ||
        !heap_call("allocation size", 0x8001a374, nc_psx_allocation_size, 3, blocks[1], 0) ||
        !heap_call("allocation size (null)", 0x8001a374, nc_psx_allocation_size, 3, 0, 0) ||
        !heap_call("allocation size (unknown)", 0x8001a374, nc_psx_allocation_size, 3, 4, 0) ||
        !heap_call("find allocation", 0x8001a338, nc_psx_find_allocation, 20, blocks[2], 0) ||
        !heap_call("free", 0x8001a268, nc_psx_free, 3, blocks[1], 0) ||
        !heap_call("free (null)", 0x8001a268, nc_psx_free, 3, 0, 0) ||
        !heap_call("free (unknown)", 0x8001a268, nc_psx_free, 3, 8, 0) ||
        !heap_call("resize (grow in place)", 0x80019f24, nc_psx_resize, 10, blocks[0], 0x40) ||
        !heap_call("resize (shrink)", 0x80019f24, nc_psx_resize, 10, blocks[0], 0x8) ||
        !heap_call("resize (same)", 0x80019f24, nc_psx_resize, 10, blocks[0], 0x8) ||
        !heap_call("resize (move)", 0x80019f24, nc_psx_resize, 10, blocks[2], 0x400) ||
        !heap_call("resize (new)", 0x80019f24, nc_psx_resize, 10, 0, 0x20) ||
        !heap_call("resize (to zero)", 0x80019f24, nc_psx_resize, 10, blocks[3], 0) ||
        !heap_call("resize (unknown)", 0x80019f24, nc_psx_resize, 10, 12, 0x20) ||
        !heap_call("resize (no room)", 0x80019f24, nc_psx_resize, 10, blocks[0], 0x100000))
        return false;
    static const u32 entries[] = {0x80019e3c, 0x80019e64, 0x80019e8c, 0x80019eb4};
    for (unsigned w = 0; w < 4; w++) {
        warning_case = (NcHeapWarning)w;
        args(0, 0, 0, 0);
        if (!check("heap warning", entries[w], heap_warning, 6))
            return false;
    }
    return true;
}

/* --- TIM and TMD ----------------------------------------------------------------------------- */

static const u32 tim = DATA + 0x1000, info = DATA + 0x2000, tmd = DATA + 0x3000;
static const u32 graph_debug = 0x80026ee2;

/* A TIM with an optional 16-colour palette and a 16 x 4 pixel block, then a second TIM. */
static u32 build_tim(u32 at, bool palette, u32 magic) {
    MEM_W32(at, magic);
    MEM_W32(at + 4, palette ? 8 : 2);
    u32 p = at + 8;
    if (palette) {
        MEM_W32(p, 12 + 32);
        MEM_W32(p + 4, 0x01e00000); /* x 0, y 480 */
        MEM_W32(p + 8, 0x00010010); /* 16 x 1 */
        for (unsigned i = 0; i < 8; i++)
            MEM_W32(p + 12 + 4 * i, 0x7fff0000u + i);
        p += 12 + 32;
    }
    MEM_W32(p, 12 + 64);
    MEM_W32(p + 4, 0x00000140); /* x 320 */
    MEM_W32(p + 8, 0x00040008); /* 8 x 4 */
    for (unsigned i = 0; i < 16; i++)
        MEM_W32(p + 12 + 4 * i, 0x12345678u * i);
    return p + 12 + 64;
}

static bool tim_paths(void) {
    unstub_all();
    for (unsigned debug = 0; debug < 3; debug += 2) {
        MEM_W8(graph_debug, (u8)debug);
        for (unsigned palette = 0; palette < 2; palette++) {
            const u32 second = build_tim(tim, palette, 0x10);
            build_tim(second, !palette, 0x11); /* not a TIM */
            args(tim, 0, 0, 0);
            if (!check("open tim", 0x8002020c, nc_psx_open_tim, 4))
                return false;
            for (unsigned i = 0; i < 2; i++) {
                args(info, 0, 0, 0);
                if (!check("read tim", 0x8002021c, nc_psx_read_tim, 10))
                    return false;
            }
            args(second, info, 0, 0);
            if (!check("tim info (not a tim)", 0x80020570, nc_psx_tim_info, 13))
                return false;
        }
    }
    MEM_W8(graph_debug, 0);

    /* Stubbed GetGraphDebug (each trace on or off) and printf, to log every return address. */
    for (unsigned mask = 0; mask < 8; mask++) {
        unstub_all();
        const u32 levels[] = {mask & 1 ? 2 : 0, mask & 2 ? 2 : 1, mask & 4 ? 2 : 3};
        stub(0x8001d730, levels, 3, STUB_PLAIN);
        stub_const(0x8001a640, 0);
        build_tim(tim, mask & 1, 0x10);
        args(tim, info, 0, 0);
        if (!check("tim info (stubs)", 0x80020570, nc_psx_tim_info, 13))
            return false;
    }
    for (unsigned result = 0; result < 3; result++) {
        unstub_all();
        stub_const(0x80020570, result == 2 ? UINT32_MAX : 5 * result);
        MEM_W32(0x80027e10, tim);
        args(info, 0, 0, 0);
        if (!check("read tim (stubs)", 0x8002021c, nc_psx_read_tim, 10))
            return false;
    }
    unstub_all();
    return true;
}

static bool tmd_paths(void) {
    unstub_all();
    /* Header and two objects: vertex, normal and primitive tables relative to the object
     * table at +0xC. */
    MEM_W32(tmd, 0x41);
    MEM_W32(tmd + 4, 0);
    MEM_W32(tmd + 8, 2);
    for (unsigned o = 0; o < 2; o++) {
        const u32 object = tmd + 0xc + 28 * o;
        MEM_W32(object + 0x00, 0x100 + 0x40 * o);
        MEM_W32(object + 0x04, 8);
        MEM_W32(object + 0x08, 0x200 + 0x40 * o);
        MEM_W32(object + 0x0c, 8);
        MEM_W32(object + 0x10, 0x300 + 0x40 * o);
        MEM_W32(object + 0x14, 5 + o);
        MEM_W32(object + 0x18, 0);
    }
    for (unsigned i = 0; i < 0x80; i++)
        MEM_W32(tmd + 0x100 + 4 * i, 0x00010001u * (i + 1));
    for (unsigned debug = 0; debug < 3; debug += 2) {
        MEM_W8(graph_debug, (u8)debug);
        for (unsigned o = 0; o < 2; o++) {
            args(tmd, o, 0, 0);
            if (!check("open tmd", 0x80020284, nc_psx_open_tmd, 10))
                return false;
            args(tmd, o, DATA + 0x4000, DATA + 0x4004);
            MEM_W32(STACK + 0x10, DATA + 0x4008);
            if (!check("tmd info", 0x80020690, nc_psx_tmd_info, 17))
                return false;
        }
    }
    MEM_W8(graph_debug, 0);
    for (unsigned mask = 0; mask < 32; mask++) {
        unstub_all();
        u32 levels[5];
        for (unsigned i = 0; i < 5; i++)
            levels[i] = mask & (1u << i) ? 2 : i;
        stub(0x8001d730, levels, 5, STUB_PLAIN);
        stub_const(0x8001a640, 0);
        args(tmd, mask & 1, DATA + 0x4000, DATA + 0x4004);
        MEM_W32(STACK + 0x10, DATA + 0x4008);
        if (!check("tmd info (stubs)", 0x80020690, nc_psx_tmd_info, 17))
            return false;
    }
    unstub_all();
    stub_const(0x80020690, 7);
    args(tmd, 1, 0, 0);
    if (!check("open tmd (stubs)", 0x80020284, nc_psx_open_tmd, 10))
        return false;
    unstub_all();

    /* ReadTMD after unpack_packet filled in the vertex and normal indices. */
    const u32 packet = DATA + 0x5000;
    const u32 sizes[] = {4, 7, UINT32_MAX};
    stub(0x80020828, sizes, 3, STUB_PLAIN);
    for (unsigned i = 0; i < 3; i++) {
        for (unsigned j = 0; j < 8; j++)
            MEM_W16(packet + 0x68 + 2 * j, (u16)(3 * j + i));
        args(packet, 0, 0, 0);
        if (!check("read tmd", 0x800202c4, nc_psx_read_tmd, 10))
            return false;
    }
    unstub_all();
    return true;
}

/* --- front end ------------------------------------------------------------------------------- */

static bool front_end_paths(void) {
    static const u32 callees[] = {0x8001bc00, 0x8001d398, 0x8001d608, 0x800228f8, 0x800219b0,
                                  0x80018de4, 0x800199a8, 0x80019edc, 0x8001d79c, 0x80019b40,
                                  0x8001986c, 0x8001a568, 0x800199f8, 0x80018f64, 0x8001bcf4};
    static const struct {
        u32 probe, free_blocks, exists;
    } cards[] = {{1, 0, 0}, {1, 0, 1}, {1, 3, 0},    {4, 0, 0},    {4, 255, 1},
                 {2, 0, 0}, {8, 0, 1}, {0x10, 0, 0}, {0x101, 0, 0}};
    MEM_W32(0x80027bd8, 0x80053cbc);
    MEM_W32(0x80027bdc, 0x100);
    MEM_W32(GP + 0x30, settings);
    MEM_W32(settings, 0x12345678);
    MEM_W32(GP, 0x4f534e49);
    MEM_W32(GP + 4, 0xfff4ff9a);
    for (unsigned i = 0; i < sizeof cards / sizeof cards[0]; i++) {
        unstub_all();
        for (unsigned j = 0; j < sizeof callees / sizeof callees[0]; j++)
            stub_const(callees[j], 0);
        stub_const(0x80019234, cards[i].probe);
        stub_const(0x8001943c, cards[i].free_blocks);
        stub_const(0x800193e4, cards[i].exists);
        fresh_cpu();
        if (!check("front end", 0x8001952c, nc_psx_front_end, 5))
            return false;
    }
    unstub_all();
    stub_const(0x80025cd8, 0);
    stub_const(0x8001952c, 0);
    fresh_cpu();
    if (!check("main", 0x800196cc, nc_psx_main, 4))
        return false;

    /* The warning screen: a missing file, no frames, a full countdown, a skip. */
    static const struct {
        u32 image, frames, skippable, skip_at;
    } screens[] = {{0, 5, 1, 0},          {DATA, 0, 1, 0}, {DATA, 1, 0, 0},
                   {DATA, 4, 0x10000, 0}, {DATA, 4, 1, 3}, {DATA, 3, 0x1ffff, 1}};
    for (unsigned i = 0; i < sizeof screens / sizeof screens[0]; i++) {
        unstub_all();
        u32 skips[8] = {0};
        if (screens[i].skip_at)
            skips[screens[i].skip_at - 1] = 0x101;
        stub_const(0x800197b0, screens[i].image);
        stub_const(0x80019d4c, 0);
        stub_const(0x8001e084, 0);
        stub_const(0x8001d79c, 0);
        stub(0x80019938, skips, 8, STUB_PLAIN);
        stub_const(0x8001b9bc, 0);
        stub_const(0x8001a268, 0);
        args(0x80018028, screens[i].frames, 640, 256);
        MEM_W32(STACK + 0x10, screens[i].skippable);
        if (!check("show tim screen", 0x8001986c, nc_psx_show_tim_screen, 15))
            return false;
    }

    static const struct {
        const char *name;
        u32 entry;
        bool (*native)(CPUState *);
        int low;
        u32 callees[8];
    } simple[] = {
        {"load screen tim",
         0x80019d4c,
         nc_psx_load_screen_tim,
         5,
         {0x80019c4c, 0x8002020c, 0x8002021c, 0x8001dafc, 0x8001d838}},
        {"restart display",
         0x80019d9c,
         nc_psx_restart_display,
         4,
         {0x8001bc00, 0x8001d398, 0x800219b0, 0x800199a8, 0x80021b08, 0x8001bc90}},
        {"shutdown display",
         0x80019dec,
         nc_psx_shutdown_display,
         4,
         {0x80019c4c, 0x8001bc90, 0x8002287c, 0x800199f8, 0x8001d398, 0x8001bc00, 0x8001bcf4}},
    };
    for (unsigned i = 0; i < sizeof simple / sizeof simple[0]; i++) {
        unstub_all();
        for (unsigned j = 0; j < 8 && simple[i].callees[j]; j++)
            stub_const(simple[i].callees[j], j);
        args(DATA, 0, 0, 0);
        if (!check(simple[i].name, simple[i].entry, simple[i].native, simple[i].low))
            return false;
    }
    unstub_all();
    return true;
}

static bool load_file_paths(void) {
    static const u32 lengths[] = {0, 1, 2048, 2049, 0x7ffff801, 0xfffff000};
    put_string(DATA + 0x100, "CARDWARN.TIM");
    MEM_W16(GP + 8, '\\');
    for (unsigned i = 0; i < sizeof lengths / sizeof lengths[0]; i++) {
        unstub_all();
        const u32 found[] = {0, 0, 1};
        const unsigned search = stub(0x80024844, found, 3, STUB_FILE_A0);
        const u32 file = stub_values(search) + 0x80;
        const u32 words[] = {0x00021602, lengths[i], 0x52414300 + i,
                             0x4e524157, 0x4d49542e, 0x00313b};
        for (unsigned w = 0; w < 6; w++)
            MEM_W32(file + 4 * w, words[w]);
        stub_const(0x8001a108, HEAP + 0x40 * i);
        stub_const(0x800196f4, lengths[i]);
        args(DATA + 0x100, 0, 0, 0);
        if (!check("load file", 0x800197b0, nc_psx_load_file, 11))
            return false;
    }
    unstub_all();
    return true;
}

static bool small_paths(void) {
    unstub_all();
    for (unsigned i = 0; i < 3; i++) {
        MEM_W32(GP + 0x28, 0xfffffffe + i);
        MEM_W32(GP + 0x20, 7 * i);
        fresh_cpu();
        if (!check("count vblank", 0x8001a3b0, nc_psx_count_vblank, 12))
            return false;
    }
    for (unsigned mode = 0; mode < 8; mode++) {
        MEM_W32(GP + 0x28, 1000);
        MEM_W32(GP + 0x24, 900);
        MEM_W32(GP + 0x78, 33);
        args(mode & 1, mode & 2, mode & 4, 0);
        if (!check("frame timer", 0x8001a3d0, nc_psx_frame_timer, 2))
            return false;
    }
    MEM_W32(GP + 0x30, settings);
    fresh_cpu();
    if (!check("init shared record", 0x8001a444, nc_psx_init_shared_record, 49 + 26))
        return false;

    /* The card driver's wait: interrupt disabled, or enabled with the line idle. */
    stub_const(0x80020000, 0x5a5a);
    MEM_W32(0x80027c38, 0x80020000);
    for (unsigned enabled = 0; enabled < 2; enabled++) {
        MEM_W32(DATA + 0x1074, enabled ? 0x80 : 0x7f);
        MEM_W32(DATA + 0x1044, 0x7f);
        fresh_cpu();
        nc_cpu.r[3] = DATA;
        if (!check("card wait ack", 0x80025a64, nc_psx_card_wait_ack, 2))
            return false;
        fresh_cpu();
        nc_cpu.r[3] = DATA;
        if (!check("card patch jump", 0x80025a50, nc_psx_card_patch_jump, 4))
            return false;
    }
    unstub_all();

    stub_const(0x800196cc, 0);
    MEM_W32(0x80027bbc, 0x200000);
    MEM_W32(0x80027bb8, 0x8000);
    fresh_cpu();
    if (!check("start", 0x80025c70, nc_psx_start, 30))
        return false;
    unstub_all();
    return true;
}

/* --- rules (no disc) ------------------------------------------------------------------------- */

/* Fill the module slot with "jr ra; ori v0, zero, 0" so any callee returns 0. */
static void blank_module(void) {
    for (u32 a = 0x80018000; a < 0x80060000; a += 8) {
        MEM_W32(a, JR(RA));
        MEM_W32(a + 4, op_i(13, 0, V0, 0));
    }
}

static bool rules(void) {
    blank_module();
    current = "rules";
    /* Decline without effects below each prologue's budget. */
    static const struct {
        bool (*native)(CPUState *);
        int low;
    } declines[] = {
        {nc_psx_card_open_events, 4},
        {nc_psx_card_close_events, 4},
        {nc_psx_card_test_events, 7},
        {nc_psx_card_wait_sw_event, 2},
        {nc_psx_card_probe, 8},
        {nc_psx_card_formatted, 8},
        {nc_psx_card_file_exists, 8},
        {nc_psx_card_free_blocks, 13},
        {nc_psx_front_end, 5},
        {nc_psx_main, 4},
        {nc_psx_show_tim_screen, 15},
        {nc_psx_load_screen_tim, 5},
        {nc_psx_init_display_buffers, 19},
        {nc_psx_setup_display, 33},
        {nc_psx_clear_display, 4},
        {nc_psx_init_heap, 18},
        {nc_psx_allocate, 4},
        {nc_psx_resize, 10},
        {nc_psx_open_tim, 4},
        {nc_psx_read_tim, 10},
        {nc_psx_tim_info, 13},
        {nc_psx_open_tmd, 10},
        {nc_psx_tmd_info, 17},
        {nc_psx_read_tmd, 10},
        {nc_psx_start, 30},
        {nc_psx_count_vblank, 12},
        {nc_psx_init_geom, 4},
        {nc_psx_set_geom_screen, 3},
    };
    for (unsigned i = 0; i < sizeof declines / sizeof declines[0]; i++) {
        fresh_cpu();
        nc_cpu.budget = declines[i].low;
        CPUState before = nc_cpu;
        static u8 ram[RAM_SIZE];
        memcpy(ram, nc_ram, RAM_SIZE);
        if (declines[i].native(&nc_cpu) || memcmp(&before, &nc_cpu, sizeof before) ||
            memcmp(ram, nc_ram, RAM_SIZE)) {
            fprintf(stderr, "rules: routine %u did not decline cleanly\n", i);
            return false;
        }
    }

    /* get_tim_addr describes a TIM (GetGraphDebug returns 0 here, so nothing is printed). */
    const u32 second = build_tim(tim, true, 0x10);
    const u32 third = build_tim(second, false, 0x10);
    MEM_W32(third, 0x12);
    args(tim, info, 0, 0);
    if (!nc_psx_tim_info(&nc_cpu) || nc_cpu.r[2] != 32 || MEM_R32(info) != 8 ||
        MEM_R32(info + 4) != tim + 12 || MEM_R32(info + 8) != tim + 20 ||
        MEM_R32(info + 12) != tim + 8 + 44 + 4 || MEM_R32(info + 16) != tim + 8 + 44 + 12)
        return failed("get_tim_addr with a palette");
    args(second, info, 0, 0);
    if (!nc_psx_tim_info(&nc_cpu) || nc_cpu.r[2] != 21 || MEM_R32(info + 4) != 0 ||
        MEM_R32(info + 8) != 0 || MEM_R32(info + 12) != second + 12)
        return failed("get_tim_addr without a palette");
    args(third, info, 0, 0);
    if (!nc_psx_tim_info(&nc_cpu) || nc_cpu.r[2] != UINT32_MAX)
        return failed("get_tim_addr past the end");

    /* OpenTIM and ReadTIM keep the cursor; get_tim_addr is a stub returning 32 words, then -1. */
    args(tim, 0, 0, 0);
    if (!nc_psx_open_tim(&nc_cpu) || MEM_R32(0x80027e10) != tim || nc_cpu.r[2] != 0)
        return failed("OpenTIM");
    const u32 words[] = {32, UINT32_MAX};
    stub(0x80020570, words, 2, STUB_PLAIN);
    args(info, 0, 0, 0);
    if (!nc_psx_read_tim(&nc_cpu) || nc_cpu.r[2] != info || MEM_R32(0x80027e10) != tim + 128)
        return failed("ReadTIM");
    args(info, 0, 0, 0);
    if (!nc_psx_read_tim(&nc_cpu) || nc_cpu.r[2] != 0 || MEM_R32(0x80027e10) != tim + 128)
        return failed("ReadTIM past the end");
    unstub_all();

    /* get_tmd_addr converts the object's offsets to addresses. */
    MEM_W32(tmd + 8, 1);
    MEM_W32(tmd + 0xc, 0x100);
    MEM_W32(tmd + 0x14, 0x200);
    MEM_W32(tmd + 0x1c, 0x300);
    MEM_W32(tmd + 0x20, 9);
    args(tmd, 0, DATA + 0x4000, DATA + 0x4004);
    MEM_W32(STACK + 0x10, DATA + 0x4008);
    if (!nc_psx_tmd_info(&nc_cpu) || nc_cpu.r[2] != 9 || nc_cpu.r[29] != STACK ||
        MEM_R32(DATA + 0x4004) != tmd + 0xc + 0x100 ||
        MEM_R32(DATA + 0x4008) != tmd + 0xc + 0x200 || MEM_R32(DATA + 0x4000) != tmd + 0xc + 0x300)
        return failed("get_tmd_addr");

    /* The heap hands out aligned payloads after 28-byte records. */
    args(HEAP + 1, 0x1000, 0, 0);
    if (!nc_psx_init_heap(&nc_cpu) || MEM_R32(GP + 0x80) != HEAP + 4 ||
        MEM_R32(GP + 0x18) != 0x1000 || MEM_R32(HEAP + 4 + 0x0c) != HEAP + 4 + 28)
        return failed("init heap");
    args(10, 0, 0, 0);
    if (!nc_psx_allocate(&nc_cpu) || nc_cpu.r[2] != HEAP + 4 + 28 + 28 || MEM_R32(GP + 0x10) != 1 ||
        MEM_R32(GP + 0x14) != 10 || MEM_R32(GP + 0x18) != 0x1000 - 10)
        return failed("allocate");
    const u32 block = nc_cpu.r[2];
    args(block, 0, 0, 0);
    if (!nc_psx_find_allocation(&nc_cpu) || nc_cpu.r[2] != block - 28)
        return failed("find allocation");
    args(block + 4, 0, 0, 0);
    if (!nc_psx_find_allocation(&nc_cpu) || nc_cpu.r[2] != 0)
        return failed("find allocation (unknown)");

    /* Small leaf routines. */
    MEM_W32(GP + 0x28, 41);
    MEM_W32(GP + 0x20, 9);
    fresh_cpu();
    if (!nc_psx_count_vblank(&nc_cpu) || MEM_R32(GP + 0x28) != 42 || MEM_R32(GP + 0x20) != 10)
        return failed("count vblank");
    MEM_W32(GP + 0x24, 30);
    args(1, 0, 0, 0);
    if (!nc_psx_frame_timer(&nc_cpu) || MEM_R32(GP + 0x78) != 12 || MEM_R32(GP + 0x24) != 42 ||
        MEM_R32(GP + 0x1c) != 42 * 20)
        return failed("frame timer restart");
    args(0, 1, 1, 0);
    if (!nc_psx_frame_timer(&nc_cpu) || MEM_R32(GP + 0x24) != 54 || MEM_R32(GP + 0x1c) != 840)
        return failed("frame timer resume");
    MEM_W32(GP + 0x30, settings);
    fresh_cpu();
    if (!nc_psx_init_shared_record(&nc_cpu) || MEM_R8(settings + 0x18) != 10 ||
        MEM_R8(settings + 0x19) != 5 || MEM_R16(settings + 8) != 128 ||
        !(MEM_R32(settings + 4) & 0x08000000))
        return failed("init shared record");
    MEM_W8(settings + 0x18, 13);
    MEM_W8(settings + 0x19, 1);
    fresh_cpu();
    if (!nc_psx_apply_screen_offset(&nc_cpu) || MEM_R16(0x8002a25c + 0x64) != 3 ||
        MEM_R16(0x8003e38c + 0x64) != 3 || MEM_R16(0x8002a25c + 0x66) != 0xfffc)
        return failed("apply screen offset");
    MEM_W32(0x80027bbc, 0x200000);
    MEM_W32(0x80027bb8, 0x8000);
    fresh_cpu();
    if (!nc_psx_start(&nc_cpu) || nc_cpu.r[28] != GP || MEM_R32(0x80027bc0) != 0x80053cbc ||
        MEM_R32(0x80027bc4) != 0x200000 - 0x8000 - 0x53cbc)
        return failed("start");

    /* Memory card waits return the index of the first event that fired. */
    for (unsigned i = 0; i < 8; i++)
        MEM_W32(GP + 0x84 + 4 * i, i);
    for (unsigned fire = 1; fire <= 6; fire++) {
        u32 values[8] = {0};
        values[fire - 1] = 1;
        unstub_all();
        stub(0x8001b764, values, 8, STUB_PLAIN);
        fresh_cpu();
        if (!nc_psx_card_wait_hw_event(&nc_cpu) || nc_cpu.r[2] != (fire - 1) % 4 ||
            MEM_R32(TRACE) != TRACE_BUF + 4 * fire)
            return failed("card wait");
    }
    unstub_all();
    return true;
}

/* --- driver ---------------------------------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc > 2)
        return 1;
    MEM_W32(TRACE, TRACE_BUF);
    if (argc == 1)
        return rules() ? 0 : 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 77;
    fclose(file);
    ExecInfo exe;
    if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX.EXE;1", &exe))
        return 1;
    nc_set_module(NC_MOD_PSX);
    bool ok = card_events() && card_probe_paths() && card_formatted_paths() && card_file_checks() &&
              display_paths() && heap_paths() && tim_paths() && tmd_paths() && front_end_paths() &&
              load_file_paths() && small_paths();
    return ok ? 0 : 1;
}
