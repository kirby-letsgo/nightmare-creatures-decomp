#include "credits_compare.h"
#include "credits_stubs.h"
#include "decomp/credits.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include <stdio.h>

typedef struct Calls {
    CPUState searches[3], allocate, read;
    unsigned count;
    u32 stack_args[4];
    unsigned path_ok;
} Calls;
static Calls calls;
static unsigned misses;
static u32 length, buffer;
static u8 initial_ram[RAM_SIZE], result_ram[RAM_SIZE];

static void search(CPUState *c) {
    if (calls.count >= 3) {
        R(2) = 1;
        calls.path_ok = false;
        return;
    }
    calls.searches[calls.count++] = *c;
    calls.path_ok = calls.path_ok && !strcmp((char *)nc_fast_ptr(R(5)), "\\CREDITS.TIM;1");
    if (calls.count <= misses) {
        R(2) = 0;
        return;
    }
    MEM_W32(R(4), 0x123456);
    MEM_W32(R(4) + 4, length);
    for (unsigned i = 0; i < 4; i++)
        MEM_W32(R(4) + 8 + 4 * i, 0x12345678u + i);
    R(2) = R(4);
}
static void allocate(CPUState *c) {
    calls.allocate = *c;
    R(2) = buffer;
}
static void read_sectors(CPUState *c) {
    calls.read = *c;
    for (unsigned i = 0; i < 4; i++)
        calls.stack_args[i] = MEM_R32(R(29) + 0x10 + 4 * i);
    R(2) = 0;
}

typedef struct SectorTrace {
    CPUState position, attempts[11], polls[3];
    unsigned reads, syncs;
    u32 location;
} SectorTrace;
static SectorTrace sectors;
static unsigned rejects, busy_polls;
static bool seek_ok;
static u32 sync_result;

static void set_location(CPUState *c) {
    sectors.position = *c;
    sectors.location = MEM_R32(R(5));
    R(2) = seek_ok;
}

static void cd_read(CPUState *c) {
    if (sectors.reads < 11)
        sectors.attempts[sectors.reads] = *c;
    R(2) = ++sectors.reads > rejects;
}

static void cd_sync(CPUState *c) {
    if (sectors.syncs < 3)
        sectors.polls[sectors.syncs] = *c;
    R(2) = ++sectors.syncs <= busy_polls ? 1 : sync_result;
}

static unsigned roll_entries;
static void enter_roll(CPUState *c) {
    (void)c;
    roll_entries++;
}

static bool check_entry(void) {
    const u32 sizes[][2] = {{0x800000, 0x8000}, {0x200000, 0x10000}, {0, 1}};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        CPUState cpu = {0};
        for (unsigned reg = 1; reg < 32; reg++)
            cpu.r[reg] = reg * 0x11359u;
        cpu.r[31] = 0x80010000;
        cpu.budget = i ? 31 : 4096;
        MEM_W32(0x80024c44, sizes[i][0]);
        MEM_W32(0x80024c40, sizes[i][1]);
        roll_entries = 0;
        if (!compare(cpu, 0x800232f4, nc_credits_entry, 30) || roll_entries != 2 ||
            nc_cpu.r[28] != 0x80024c68 || nc_cpu.r[31] != cpu.r[31] ||
            MEM_R32(0x80024c48) != 0x80028c94 ||
            MEM_R32(0x80024c4c) != sizes[i][0] - sizes[i][1] - 0x28c94u)
            return false;
    }
    return true;
}

const NcFuncEntry credits_exe_table[] = {{0x80018d6c, read_sectors}, {0x80018f94, enter_roll},
                                         {0x80019680, allocate},
                                         {0x80020210, set_location}, {0x80021c34, search},
                                         {0x80022ae0, cd_read},      {0x80022be8, cd_sync}};
const unsigned credits_exe_table_len = sizeof credits_exe_table / sizeof credits_exe_table[0];

static bool check(u32 bytes, unsigned retries) {
    length = bytes;
    misses = retries;
    buffer = retries == 1 ? 0 : 0x80080000;
    CPUState initial = {0};
    for (unsigned i = 1; i < 32; i++)
        initial.r[i] = i * 0x11359u;
    initial.r[4] = 0x80018000;
    initial.r[28] = 0x80024c68;
    initial.r[29] = 0x801fef00;
    initial.r[31] = 0x80010000;
    initial.budget = 11;
    memcpy(initial_ram, nc_ram, sizeof initial_ram);
    CPUState native = initial;
    if (nc_credits_load_file(&native) || memcmp(&native, &initial, sizeof native) ||
        memcmp(initial_ram, nc_ram, sizeof initial_ram))
        return false;
    initial.budget = 4096;
    native = initial;
    calls = (Calls){.path_ok = true};
    if (!nc_credits_load_file(&native) || !calls.path_ok || calls.count != misses + 1 ||
        native.r[2] != buffer || calls.read.r[5] != buffer || calls.read.r[7] != bytes)
        return false;
    Calls expected = calls;
    memcpy(result_ram, nc_ram, sizeof result_ram);
    memcpy(nc_ram, initial_ram, sizeof initial_ram);
    calls = (Calls){.path_ok = true};
    CPUState guest = initial;
    interp_call(&guest, 0x80018e28);
    return !memcmp(&expected, &calls, sizeof calls) && !memcmp(&guest, &native, sizeof guest) &&
           !memcmp(result_ram, nc_ram, sizeof result_ram);
}

static bool check_read(u32 size, unsigned failures, unsigned waits, bool positioned) {
    rejects = failures;
    busy_polls = waits;
    seek_ok = positioned;
    sync_result = (waits & 1) ? 0xffffffff : 0;
    CPUState initial = {0};
    for (unsigned i = 1; i < 32; i++)
        initial.r[i] = i * 0x11359u;
    initial.r[5] = 0x80080000;
    initial.r[6] = 0x123456;
    initial.r[7] = size;
    initial.r[29] = 0x801fef00;
    initial.r[31] = 0x80010000;
    initial.budget = 15;
    CPUState native = initial;
    memcpy(initial_ram, nc_ram, sizeof initial_ram);
    if (nc_credits_read_sectors(&native) || memcmp(&native, &initial, sizeof initial) ||
        memcmp(initial_ram, nc_ram, sizeof initial_ram))
        return false;
    initial.budget = 4096;
    native = initial;
    sectors = (SectorTrace){0};
    if (!nc_credits_read_sectors(&native) || sectors.location != initial.r[6] ||
        native.r[2] != (positioned ? size : 0) ||
        sectors.reads != (positioned ? (failures < 11 ? failures + 1 : 11) : 0) ||
        sectors.syncs != (positioned ? waits + 1 : 0))
        return false;
    SectorTrace expected = sectors;
    memcpy(result_ram, nc_ram, sizeof result_ram);
    memcpy(nc_ram, initial_ram, sizeof initial_ram);
    sectors = (SectorTrace){0};
    CPUState guest = initial;
    interp_call(&guest, 0x80018d6c);
    return !memcmp(&sectors, &expected, sizeof expected) &&
           !memcmp(&native, &guest, sizeof guest) && !memcmp(nc_ram, result_ram, sizeof result_ram);
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
    if (!check_entry())
        return 1;
    const u32 sizes[] = {0,      1,          2047,       2048,       2049,
                         983060, 0x7ffff800, 0x7ffff801, 0x80000000, 0xffffffff};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        for (unsigned retry = 0; retry <= 2; retry++)
            if (!check(sizes[i], retry)) {
                fprintf(stderr, "file loader mismatch for size %08x, retries %u\n", sizes[i],
                        retry);
                return 1;
            }
    const unsigned failures[] = {0, 1, 10, 11};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        if (!check_read(sizes[i], 0, 0, false))
            return 1;
        for (unsigned j = 0; j < sizeof failures / sizeof failures[0]; j++)
            for (unsigned waits = 0; waits <= 2; waits++)
                if (!check_read(sizes[i], failures[j], waits, true)) {
                    fprintf(stderr, "sector read mismatch for size %08x, rejects %u, waits %u\n",
                            sizes[i], failures[j], waits);
                    return 1;
                }
    }
    disc_close();
    return 0;
}
