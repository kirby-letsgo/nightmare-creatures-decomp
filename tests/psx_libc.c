/* PSX.EXE C library and BIOS / memory card stubs (src/decomp/psx_libc.c).
 * Without arguments: rules checks of the native routines alone.
 * With the disc: every routine against the original instructions. */
#include "decomp/psx_libc.h"
#include "decomp/twins/psx_exe.h"
#include "jump_context.h"
#include "port/bios.h"
#include "port/disc.h"
#include "port/exe.h"
#include "sdk_formatter.h"
#include "sdk_putchar.h"
#include <stdio.h>

enum {
    SPRINTF = 0x8001ae6c,
    MEMMOVE = 0x8001b6c8,
    CHAR_CLASS = 0x80025cf1,
    BUFFER = 0x80060000,
    FORMAT = 0x80061000,
    STRINGS = 0x80061800,
    COUNTS = 0x80061f00,
};

static bool upper(CPUState *c) {
    return nc_psx_change_case(c, true);
}
static bool lower(CPUState *c) {
    return nc_psx_change_case(c, false);
}

/* --- equivalence with BIOS state ------------------------------------------------------- */

static void serialize_all(StateIO *io) {
    cpu_serialize(io);
    bios_serialize(io);
}

static void load_all(StateIO *io) {
    io->loading = true;
    io->pos = 0;
    serialize_all(io);
    io->loading = false;
}

static bool same(const StateIO *a, const StateIO *b) {
    return !a->error && !b->error && a->len == b->len && !memcmp(a->buf, b->buf, a->len);
}

/* compare() with the BIOS state (events, files, pad and card) included. The
 * routine must decline at `threshold` without changing anything. */
static bool compare_bios(CPUState start, u32 entry, bool (*routine)(CPUState *), int threshold) {
    StateIO initial = {0}, declined = {0}, native = {0}, guest = {0};
    nc_cpu = start;
    nc_cpu.budget = threshold;
    serialize_all(&initial);
    bool ok = !routine(&nc_cpu);
    serialize_all(&declined);
    ok = ok && same(&initial, &declined);
    load_all(&initial);
    nc_cpu.budget = start.budget;
    ok = routine(&nc_cpu) && ok;
    serialize_all(&native);
    load_all(&initial);
    nc_cpu.budget = start.budget;
    interp_call(&nc_cpu, entry);
    serialize_all(&guest);
    ok = ok && same(&native, &guest);
    free(initial.buf);
    free(declined.buf);
    free(native.buf);
    free(guest.buf);
    return ok;
}

/* --- C library ------------------------------------------------------------------------- */

static bool check_case(void) {
    for (unsigned byte = 0; byte < 256; byte++) {
        CPUState state = arguments(0xabcdef00 | byte, 0, 0, byte & 1 ? 15 : 4096);
        if (!compare(state, 0x8001ad08, upper, 14) || !compare(state, 0x8001ad38, lower, 14))
            return false;
    }
    const u32 table_byte = CHAR_CLASS + '!';
    const u8 original = MEM_R8(table_byte);
    bool ok = true;
    for (unsigned flags = 0; flags < 4 && ok; flags++) {
        MEM_W8(table_byte, (u8)flags);
        CPUState state = arguments('!', 0, 0, 100);
        ok = compare(state, 0x8001ad08, upper, 14) && compare(state, 0x8001ad38, lower, 14);
    }
    MEM_W8(table_byte, original);
    return ok;
}

static bool check_memmove(void) {
    const struct {
        u32 destination, source, count;
    } cases[] = {
        {BUFFER + 8, BUFFER + 3, 9},        {BUFFER + 3, BUFFER + 8, 9},
        {BUFFER + 5, BUFFER + 5, 4},        {BUFFER + 1, BUFFER + 40, 1},
        {BUFFER + 40, BUFFER, 0},           {BUFFER, BUFFER + 9, 0},
        {BUFFER, BUFFER + 9, (u32)-3},      {BUFFER + 9, BUFFER, (u32)-3},
        {SCRATCH_BASE + 2, BUFFER + 1, 17}, {BUFFER + 20, SCRATCH_BASE + 2, 17},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++)
        for (unsigned tight = 0; tight < 3; tight++) {
            for (unsigned byte = 0; byte < 64; byte++) {
                MEM_W8(BUFFER + byte, (u8)(7 * byte + 1));
                MEM_W8(SCRATCH_BASE + byte, (u8)(5 * byte + 2));
            }
            const int budgets[] = {4096, 2, 9};
            CPUState state =
                arguments(cases[i].destination, cases[i].source, cases[i].count, budgets[tight]);
            if (!compare(state, MEMMOVE, nc_psx_move_bytes, 1)) {
                fprintf(stderr, "memmove case %u budget %d\n", i, budgets[tight]);
                return false;
            }
        }
    return true;
}

typedef struct {
    const char *format;
    u32 args[8];
} SprintfCase;

/* sprintf(BUFFER, format, args...): a2/a3 and then the caller's stack slots. */
static CPUState sprintf_state(const SprintfCase *test, int budget) {
    strcpy((char *)nc_fast_ptr(FORMAT), test->format);
    memset(nc_fast_ptr(BUFFER), 0xee, 256);
    CPUState state = arguments(BUFFER, FORMAT, test->args[0], budget);
    state.r[7] = test->args[1];
    for (unsigned i = 2; i < 8; i++)
        MEM_W32(state.r[29] + 0x10 + 4 * (i - 2), test->args[i]);
    return state;
}

static void sprintf_strings(void) {
    strcpy((char *)nc_fast_ptr(STRINGS), "abcdef");
    memcpy(nc_fast_ptr(STRINGS + 16), "\x04wxyz\0", 6);
    memcpy(nc_fast_ptr(STRINGS + 32), "\x09lo\0", 4);
    memset(nc_fast_ptr(COUNTS), 0xff, 16);
}

static const SprintfCase sprintf_cases[] = {
    {"", {0}},
    {"plain text", {0}},
    {"a%%b%%", {0}},
    {"%d|%i|%d", {(u32)-5, 0x80000000, 0}},
    {"%5d|%-5d|%05d|%-05d", {42, 42, (u32)-42, 7}},
    {"%+d|% d|%+ d|% +d", {3, 4, 5, (u32)-6}},
    {"%hd|%hu|%ld|%Ld", {0x1fffe, 0x1fffe, 0x7fffffff, 9}},
    {"%u|%u|%hu", {0, UINT32_MAX, 0x8000}},
    {"%o|%#o|%#o|%#5o|%.4o", {8, 8, 0, 9, 9}},
    {"%x|%X|%#x|%#X|%08x|%#08x", {0xbeef, 0xbeef, 0xff, 0xff, 0x1a, 0x1a}},
    {"%hx|%#hx|%.6x|%-6x|", {0x12345678, 0x12345678, 0xabc, 0xabc}},
    {"%p|%#p|%10p", {0x1234, 0, 0xdeadbeef}},
    {"%c%c%5c|%-3c|", {'A', 0x142, 'z', '!'}},
    {"%s|%8s|%-8s|", {STRINGS, STRINGS + 2, STRINGS + 4}},
    {"%.3s|%.10s|%.0s|%5.2s", {STRINGS, STRINGS, STRINGS, STRINGS}},
    {"%#s|%#.2s|%#6s|%#s", {STRINGS + 16, STRINGS + 16, STRINGS + 16, STRINGS + 32}},
    {"%*d|%*d|%.*d|%*.*d", {6, 1, (u32)-6, 2, 3, 4, 7, 2}},
    {"%.0d|%.0x|%5.3d|%-+8.4d|", {0, 0, (u32)-12, 34}},
    {"%012d|%0*x|%00d", {(u32)-77, 9, 0x55, 3}},
    {"ab%ncd%hnef", {COUNTS, COUNTS + 8}},
    {"%lhn%Lc", {COUNTS + 4, 'q'}},
    {"%5%|%-5%|x", {0}},
    {"left%qright", {0}},
    {"%y and %", {0}},
    {"%Q%A", {0}},
    {"%", {0}},
    {"%5", {0}},
    {"%-#0+ 12.5lX|", {0xc0ffee}},
    {"%s", {0}},
};

static bool check_sprintf(void) {
    sprintf_strings();
    const int budgets[] = {16384, 2, 5, 31, 113};
    for (unsigned i = 0; i < sizeof sprintf_cases / sizeof sprintf_cases[0]; i++)
        for (unsigned b = 0; b < sizeof budgets / sizeof budgets[0]; b++) {
            memset(nc_fast_ptr(COUNTS), 0xff, 16);
            CPUState state = sprintf_state(&sprintf_cases[i], budgets[b]);
            if (!compare(state, SPRINTF, nc_psx_sprintf, 1)) {
                fprintf(stderr, "sprintf case %u (%s) budget %d returned %u: [%s]\n", i,
                        sprintf_cases[i].format, budgets[b], nc_cpu.r[2],
                        (char *)nc_fast_ptr(BUFFER));
                return false;
            }
        }
    return true;
}

/* --- BIOS stubs ------------------------------------------------------------------------ */

typedef struct {
    u32 entry, table, service;
} Stub;

static const Stub *current_stub;
static bool stub(CPUState *c) {
    return nc_psx_bios_service(c, current_stub->table, current_stub->service);
}

static bool call_stub(const Stub *s, u32 a0, u32 a1, u32 a2, u32 a3, int budget) {
    current_stub = s;
    CPUState state = arguments(a0, a1, a2, budget);
    state.r[7] = a3;
    if (!compare_bios(state, s->entry, stub, 3) || nc_cpu.r[9] != s->service ||
        nc_cpu.r[10] != s->table) {
        fprintf(stderr, "BIOS stub %08x (%02x:%02x)\n", s->entry, s->table, s->service);
        return false;
    }
    return true;
}

static const Stub bu_init = {0x8001b734, 0xa0, 0x70}, open_event = {0x8001b744, 0xb0, 0x08},
                  close_event = {0x8001b754, 0xb0, 0x09}, test_event = {0x8001b764, 0xb0, 0x0b},
                  enable_event = {0x8001b774, 0xb0, 0x0c}, disable_event = {0x8001b784, 0xb0, 0x0d},
                  open_file = {0x8001b7c4, 0xb0, 0x32}, read_file = {0x8001b7d4, 0xb0, 0x34},
                  close_file = {0x8001b7f4, 0xb0, 0x36}, first_file = {0x8001b804, 0xb0, 0x42},
                  next_file = {0x8001b814, 0xb0, 0x43}, card_info = {0x800258dc, 0xa0, 0xab},
                  card_load = {0x800258ec, 0xa0, 0xac}, card_read = {0x800258fc, 0xb0, 0x4f},
                  new_card = {0x8002590c, 0xb0, 0x50}, card_write = {0x80025954, 0xb0, 0x4e},
                  init_card = {0x80025a20, 0xb0, 0x4a}, start_card = {0x80025a30, 0xb0, 0x4b},
                  stop_card = {0x80025a40, 0xb0, 0x4c};

/* Run the BIOS service for real (natively) to set up the next case. */
static u32 bios(const Stub *s, u32 a0, u32 a1) {
    CPUState state = arguments(a0, a1, 0, 4096);
    nc_cpu = state;
    nc_cpu.r[9] = s->service;
    bios_call(&nc_cpu, s->table);
    return nc_cpu.r[2];
}

static bool check_bios_stubs(void) {
    StateIO saved = {0};
    serialize_all(&saved);
    bool ok = call_stub(&bu_init, 0, 0, 0, 0, 4) && call_stub(&bu_init, 1, 2, 3, 4, 4096);
    /* Events: open one, then exercise it in each state. */
    ok = ok && call_stub(&open_event, 0xf0000011, 0x20, 0x2000, 0, 4096);
    const u32 event = bios(&open_event, 0xf0000011, 0x20);
    ok = ok && call_stub(&test_event, event, 0, 0, 0, 4);
    ok = ok && call_stub(&enable_event, event, 0, 0, 0, 4096);
    bios(&enable_event, event, 0);
    ok = ok && call_stub(&test_event, event, 0, 0, 0, 4096);
    ok = ok && call_stub(&disable_event, event, 0, 0, 0, 4);
    ok = ok && call_stub(&close_event, event, 0, 0, 0, 4096);
    ok = ok && call_stub(&test_event, 0x12345678, 0, 0, 0, 4096);
    ok = ok && call_stub(&enable_event, 0, 0, 0, 0, 4096);
    /* Files: a disc file, a missing one and the card directory. */
    strcpy((char *)nc_fast_ptr(STRINGS), "cdrom:\\PSX.EXE;1");
    strcpy((char *)nc_fast_ptr(STRINGS + 64), "cdrom:\\NOPE.EXE;1");
    strcpy((char *)nc_fast_ptr(STRINGS + 128), "bu00:*");
    ok = ok && call_stub(&open_file, STRINGS, 1, 0, 0, 4096);
    ok = ok && call_stub(&open_file, STRINGS + 64, 1, 0, 0, 4);
    const u32 fd = bios(&open_file, STRINGS, 1);
    ok = ok && call_stub(&read_file, fd, BUFFER, 0x800, 0, 4096);
    ok = ok && call_stub(&read_file, 99, BUFFER, 0x800, 0, 4096);
    ok = ok && call_stub(&close_file, fd, 0, 0, 0, 4096);
    ok = ok && call_stub(&close_file, 0x40, 0, 0, 0, 4);
    bios(&close_file, fd, 0);
    ok = ok && call_stub(&first_file, STRINGS + 128, BUFFER, 0, 0, 4096);
    ok = ok && call_stub(&first_file, STRINGS + 64, BUFFER, 0, 0, 4);
    ok = ok && call_stub(&next_file, BUFFER, 0, 0, 0, 4096);
    /* Memory card services (no card is inserted). */
    for (u32 port = 0; port <= 0x10 && ok; port += 0x10) {
        ok = call_stub(&card_info, port, 0, 0, 0, 4096) &&
             call_stub(&card_load, port, 0, 0, 0, 4) &&
             call_stub(&card_read, port, 0, BUFFER, 0, 4096) &&
             call_stub(&card_write, port, 0x3f, BUFFER, 0, 4096) &&
             call_stub(&new_card, port, 0, 0, 0, 4);
    }
    ok = ok && call_stub(&init_card, 1, 0, 0, 0, 4096) && call_stub(&start_card, 0, 0, 0, 0, 4) &&
         call_stub(&stop_card, 0, 0, 0, 0, 4096);
    load_all(&saved);
    free(saved.buf);
    return ok;
}

/* --- memory card library --------------------------------------------------------------- */

static bool check_card_library(void) {
    const struct {
        u32 entry;
        bool (*routine)(CPUState *);
        int threshold;
    } cases[] = {
        {0x80025aa8, nc_psx_patch_card, 2}, {0x80025b60, nc_psx_patch_card2, 2},
        {0x80025bf0, nc_psx_exit_card, 2},  {0x80025964, nc_psx_init_card, 4},
        {0x800259b8, nc_psx_start_card, 2}, {0x800259f0, nc_psx_stop_card, 2},
        {0x8002591c, nc_psx_card_clear, 3},
    };
    StateIO saved = {0};
    serialize_all(&saved);
    /* The C0 exception entry holds the lui/ori pair the card patch decodes. */
    const u32 c0_entry = MEM_R32(0x674 + 0x18);
    bool ok = true;
    for (unsigned round = 0; round < 3 && ok; round++) {
        if (round == 1) {
            MEM_W32(c0_entry + 0x70, 0x3c1a0000 | 0x0001);
            MEM_W32(c0_entry + 0x74, 0x375a0000 | 0x2340);
        }
        for (unsigned i = 0; i < sizeof cases / sizeof cases[0] && ok; i++)
            for (unsigned sr = 0; sr < 2 && ok; sr++) {
                const int budgets[] = {4096, cases[i].threshold + 1, 17};
                CPUState state =
                    arguments(round == 2 ? 0x10 : 1, 2, 3, budgets[(i + sr + round) % 3]);
                state.cop0[12] = sr ? 0x40000401 : 0x40000000;
                ok = compare_bios(state, cases[i].entry, cases[i].routine, cases[i].threshold);
                if (!ok)
                    fprintf(stderr, "card routine %08x round %u\n", cases[i].entry, round);
            }
    }
    load_all(&saved);
    free(saved.buf);
    return ok;
}

/* PAD_init and InitPAD are ported twins of the CREDITS.EXE routine (sdk_pad_init.h). */
static bool check_pad_init(void) {
    const struct {
        u32 entry;
        bool (*routine)(CPUState *);
    } cases[] = {
        {0x8001b834, nc_credits_legacy_pad_init__psx_exe_8001B834},
        {0x8001b89c, nc_credits_legacy_pad_init__psx_exe_8001B89C},
    };
    StateIO saved = {0};
    serialize_all(&saved);
    bool ok = true;
    for (unsigned i = 0; i < 2 && ok; i++)
        for (unsigned sr = 0; sr < 2 && ok; sr++) {
            CPUState state = arguments(0x80061000, 34, 0x80061040, sr ? 12 : 4096);
            state.r[7] = 34;
            state.cop0[12] = sr ? 0x401 : 0;
            ok = compare_bios(state, cases[i].entry, cases[i].routine, 11);
        }
    load_all(&saved);
    free(saved.buf);
    return ok;
}

/* --- rules (no disc) ------------------------------------------------------------------- */

static bool rules_memmove(void) {
    u8 expected[64];
    for (unsigned shift = 0; shift < 2; shift++) {
        for (unsigned byte = 0; byte < 64; byte++)
            expected[byte] = (u8)(3 * byte);
        memcpy(nc_fast_ptr(BUFFER), expected, 64);
        u32 destination = shift ? BUFFER + 4 : BUFFER, source = shift ? BUFFER : BUFFER + 4;
        memmove(expected + (destination - BUFFER), expected + (source - BUFFER), 20);
        nc_cpu = arguments(destination, source, 20, 4096);
        if (!nc_psx_move_bytes(&nc_cpu) || memcmp(nc_fast_ptr(BUFFER), expected, 64) ||
            nc_cpu.r[2] != (shift ? destination : destination + 20) || nc_cpu.r[6] != (u32)-1)
            return false;
    }
    nc_cpu = arguments(BUFFER, BUFFER + 1, 5, 1);
    return !nc_psx_move_bytes(&nc_cpu) && nc_cpu.budget == 1;
}

static bool rules_case(void) {
    /* Bit 0: upper case letter, bit 1: lower case letter. */
    for (unsigned byte = 0; byte < 256; byte++)
        MEM_W8(CHAR_CLASS + byte, byte >= 'A' && byte <= 'Z'   ? 1
                                  : byte >= 'a' && byte <= 'z' ? 2
                                                               : 0);
    const struct {
        u32 in, upper_v1, lower_v1;
    } cases[] = {
        {'a', 'A', 'a'},
        {'Z', 'Z', 'z'},
        {'5', '5', '5'},
        {0x1261, 0x1241, 0x1261},
        {0xffffff41, 0xffffff41, 0xffffff61},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        nc_cpu = arguments(cases[i].in, 0, 0, 100);
        if (!upper(&nc_cpu) || nc_cpu.r[3] != cases[i].upper_v1 ||
            nc_cpu.r[2] != (cases[i].upper_v1 & 0xff))
            return false;
        nc_cpu = arguments(cases[i].in, 0, 0, 100);
        if (!lower(&nc_cpu) || nc_cpu.r[3] != cases[i].lower_v1 ||
            nc_cpu.r[2] != (cases[i].lower_v1 & 0xff))
            return false;
    }
    nc_cpu = arguments('a', 0, 0, 14);
    return !upper(&nc_cpu) && nc_cpu.r[3] != 'a';
}

/* sprintf with its tables and a byte-copy loop standing in for memmove. */
static bool rules_sprintf(void) {
    const u32 table = 0x8001835c, digits_upper = 0x80018334, digits_lower = 0x80018348;
    memset(nc_fast_ptr(0x80025d74), 0, 12);
    memcpy(nc_fast_ptr(digits_upper), "0123456789ABCDEF", 16);
    memcpy(nc_fast_ptr(digits_lower), "0123456789abcdef", 16);
    const struct {
        char conversion;
        u32 offset;
    } targets[] = {
        {'L', 0x2e0}, {'X', 0x578}, {'c', 0x680}, {'d', 0x30c}, {'h', 0x2c8},
        {'i', 0x30c}, {'l', 0x2d4}, {'n', 0x724}, {'o', 0x474}, {'p', 0x564},
        {'s', 0x6a0}, {'u', 0x35c}, {'x', 0x588},
    };
    for (unsigned i = 0; i < 45; i++)
        MEM_W32(table + 4 * i, SPRINTF + 0x758);
    for (unsigned i = 0; i < sizeof targets / sizeof targets[0]; i++)
        MEM_W32(table + 4 * (u32)(targets[i].conversion - 'L'), SPRINTF + targets[i].offset);
    /* blez a2,done; nop; lbu t0,0(a1); addiu a1,1; sb t0,0(a0); addiu a2,-1; j loop;
     * addiu a0,1; done: jr ra; move v0,a0 */
    const u32 copy[] = {0x18c00007,
                        0,
                        0x90a80000,
                        0x24a50001,
                        0xa0880000,
                        0x24c6ffff,
                        0x08000000 | ((MEMMOVE >> 2) & 0x3ffffff),
                        0x24840001,
                        0x03e00008,
                        0x00801021};
    for (unsigned i = 0; i < sizeof copy / sizeof copy[0]; i++)
        MEM_W32(MEMMOVE + 4 * i, copy[i]);
    memcpy(nc_fast_ptr(STRINGS), "\x05hello", 6);
    const struct {
        SprintfCase test;
        const char *text;
    } cases[] = {
        {{"", {0}}, ""},
        {{"a%%b", {0}}, "a%b"},
        {{"%d|%i|%u", {(u32)-42, 0x80000000, UINT32_MAX}}, "-42|-2147483648|4294967295"},
        {{"[%5d|%-5d|%05d]", {42, (u32)-3, (u32)-3}}, "[   42|-3   |-0003]"},
        {{"%+d % d %hd %hu", {7, 8, 0x1fffe, 0x1fffe}}, "+7  8 -2 65534"},
        {{"%o %#o %x %X %#x", {8, 8, 0xbeef, 0xbeef, 255}}, "10 010 beef BEEF 0xff"},
        {{"%p|%.4x|%c", {0x1234, 0xab, 'Z'}}, "00001234|00ab|Z"},
        {{"%#s|%#.3s|%*d", {STRINGS, STRINGS, (u32)-4, 9}}, "hello|hel|9   "},
        {{"ab%qcd", {0}}, "ab"},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        nc_cpu = sprintf_state(&cases[i].test, 1 << 20);
        if (!nc_psx_sprintf(&nc_cpu) || nc_cpu.r[2] != strlen(cases[i].text) ||
            strcmp((char *)nc_fast_ptr(BUFFER), cases[i].text) || nc_cpu.r[29] != 0x801fef00) {
            fprintf(stderr, "sprintf rule %u: [%s] returned %u\n", i, (char *)nc_fast_ptr(BUFFER),
                    nc_cpu.r[2]);
            return false;
        }
    }
    nc_cpu = arguments(BUFFER, FORMAT, 0, 1);
    return !nc_psx_sprintf(&nc_cpu);
}

static bool rules_stub(void) {
    const Stub *stubs[] = {&bu_init, &open_event, &card_info, &stop_card};
    for (unsigned i = 0; i < sizeof stubs / sizeof stubs[0]; i++) {
        current_stub = stubs[i];
        nc_cpu = arguments(1, 2, 3, 3);
        if (stub(&nc_cpu) || nc_cpu.r[9] != 0x468b7u * 9)
            return false;
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        bool pass = rules_memmove() && rules_case() && rules_sprintf() && rules_stub();
        if (!pass)
            fprintf(stderr, "psx_libc rules failed\n");
        return pass ? 0 : 1;
    }
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 77;
    fclose(file);
    ExecInfo exe;
    if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX.EXE;1", &exe))
        return 1;
    bool pass = true;
    const struct {
        const char *name;
        bool ok;
    } results[] = {
        {"case", check_case()},
        {"memmove", check_memmove()},
        {"sprintf", check_sprintf()},
        {"prnt", check_format_output(0x8001a67c, 0x8001adb8, nc_psx_format_output)},
        {"putchar", check_sdk_putchar(0x8001adb8, nc_psx_put_char, 0x80027cc8)},
        {"longjmp", check_restore_jump_context(0x8001c34c, nc_psx_longjmp)},
        {"bios stubs", check_bios_stubs()},
        {"card library", check_card_library()},
        {"pad init", check_pad_init()},
    };
    for (unsigned i = 0; i < sizeof results / sizeof results[0]; i++)
        if (!results[i].ok) {
            fprintf(stderr, "psx_libc: %s failed\n", results[i].name);
            pass = false;
        }
    disc_close();
    return pass ? 0 : 1;
}
