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

const NcFuncEntry credits_exe_table[] = {
    {0x80018d6c, read_sectors}, {0x80019680, allocate}, {0x80021c34, search}};
const unsigned credits_exe_table_len = 3;

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
    const u32 sizes[] = {0,      1,          2047,       2048,       2049,
                         983060, 0x7ffff800, 0x7ffff801, 0x80000000, 0xffffffff};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        for (unsigned retry = 0; retry <= 2; retry++)
            if (!check(sizes[i], retry)) {
                fprintf(stderr, "file loader mismatch for size %08x, retries %u\n", sizes[i],
                        retry);
                return 1;
            }
    disc_close();
    return 0;
}
