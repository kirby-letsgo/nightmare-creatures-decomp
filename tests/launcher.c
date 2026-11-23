#include "credits_compare.h"
#include "decomp/launcher.h"
#include "port/disc.h"
#include "port/exe.h"

#include <stdio.h>

static void install_recorder(u32 entry, u32 body, const u32 *actions, unsigned count) {
    /* Jump to a guest stub, log the caller's ra at s1, then perform its actions. */
    MEM_W32(entry, 0x08000000 | ((body >> 2) & 0x03ffffff));
    MEM_W32(entry + 4, 0xae3f0000); /* sw ra, 0(s1) */
    for (unsigned i = 0; i < count; i++)
        MEM_W32(body + 4 * i, actions[i]);
    MEM_W32(body + 4 * count, 0x03e00008);
    MEM_W32(body + 4 * count + 4, 0x26310004); /* advance trace */
}

static bool check_handoff(unsigned selection, bool missing) {
    const u32 trace = 0x80016200, shared = trace + 128, args = shared + 64;
    const u32 path_offsets[] = {0, 0x14, 0x2c, 0x40, 0x58, 0x70};
    const NcModule modules[] = {NC_MOD_PSX, NC_MOD_STREAM1, NC_MOD_PSX2,
                                NC_MOD_STREAM2, NC_MOD_STREAM3, NC_MOD_CREDITS};
    ExecInfo info;
    if (!exe_load("cdrom:\\SLUS_005.82;1", &info))
        return false;
    nc_set_module(NC_MOD_NONE);
    /* Use the real stack switch, Load and Exec. Replace the loaded program's
     * entry with a returning probe so the resident loop runs for one handoff. */
    const u32 probe = 0x800160f0;
    MEM_W32(probe, 0xae440000);     /* sw a0, 0(s2) */
    MEM_W32(probe + 4, 0xae450004); /* sw a1, 4(s2) */
    MEM_W32(probe + 8, 0x03e00008);
    MEM_W32(probe + 12, 0x25080001); /* count executions in t0 */
    install_recorder(0x800108f8, 0x80016000, NULL, 0);
    install_recorder(0x80010a50, 0x80016010, NULL, 0);
    install_recorder(0x80011e7c, 0x80016020, NULL, 0);
    install_recorder(0x8001078c, 0x80016030, NULL, 0);
    install_recorder(0x80010a00, 0x80016040, NULL, 0);
    const u32 finish[] = {0xad8d0000}; /* next selection uses the original epilogue */
    install_recorder(0x80010a18, 0x80016050, finish, 1);
    const u32 redirect[] = {0xae0e0000}; /* sw t6, 0(s0): replace exec_header.pc0 */
    install_recorder(0x80010a30, 0x80016060, redirect, 1);
    install_recorder(0x80010b44, 0x80016070, NULL, 0);

    CPUState start = {0};
    start.r[12] = 0x80010088 + 4 * selection;
    start.r[13] = 0x80010778;
    start.r[14] = probe;
    start.r[16] = 0x1234;
    start.r[17] = trace;
    start.r[18] = args;
    start.r[28] = shared + 48;
    start.r[29] = 0x801fff18;
    start.r[31] = 0x80010000;
    start.budget = selection & 1 ? 2 : 4096;
    MEM_W32(start.r[28], shared);
    MEM_W32(shared + 0x24, selection);
    MEM_W32(args, UINT32_MAX);
    MEM_W32(args + 4, UINT32_MAX);
    memset(nc_fast_ptr(trace), 0, 128);
    if (missing)
        MEM_W8(0x80010000 + path_offsets[selection] + 7, '!');

    const u32 arm = MEM_R32(start.r[12]);
    if (!compare(start, 0x80010618, nc_launcher_run_modules, 1))
        return false;
    const u32 expected[] = {0x80010628, 0x8001063c, 0x80010644, 0x8001064c, arm + 8,
                            missing ? 0x80010770 : 0x80010738, 0x80010740, 0x80010758, 0x80010760};
    const unsigned calls = missing ? 6 : 9;
    for (unsigned i = 0; i < calls; i++)
        if (MEM_R32(trace + 4 * i) != expected[i])
            return false;
    return nc_cpu.r[17] == trace + 4 * calls && nc_cpu.r[8] == (missing ? 0u : 1u) &&
           nc_active_module() == (missing ? NC_MOD_NONE : modules[selection]) &&
           MEM_R32(args) == (missing ? UINT32_MAX : 0) &&
           MEM_R32(args + 4) == (missing ? UINT32_MAX : 0);
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 77;
    fclose(file);
    if (!disc_open(argv[1]))
        return 1;
    for (unsigned selection = 0; selection < 6; selection++)
        for (unsigned missing = 0; missing < 2; missing++)
            if (!check_handoff(selection, missing)) {
                fprintf(stderr, "launcher selector %u, missing %u failed\n", selection, missing);
                return 1;
            }
    disc_close();
    return 0;
}
