#include "credits_compare.h"
#include "decomp/bios_service.h"
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

static bool check_shared_record(void) {
    ExecInfo info;
    if (!exe_load("cdrom:\\SLUS_005.82;1", &info))
        return false;
    const u32 record = 0x80016400;
    const u32 locations[] = {record, 0xa0016400, SCRATCH_BASE + 32};
    const int budgets[] = {2, 7, 31, 4096};
    for (unsigned i = 0; i < sizeof locations / sizeof locations[0]; i++) {
        u32 base = locations[i];
        for (unsigned j = 0; j < sizeof budgets / sizeof budgets[0]; j++) {
            memset(nc_fast_ptr(base - 1), 0xa5, 42);
            CPUState start = {0};
            start.r[28] = 0x80016300;
            start.r[29] = 0x801fff00;
            start.r[31] = 0x80010000;
            start.budget = budgets[j];
            MEM_W32(start.r[28], base);
            if (!compare(start, 0x8001078c, nc_launcher_init_shared_record, 1) ||
                MEM_R32(base + 4) != 0x08000000 || MEM_R32(base + 0x24) != 0 ||
                MEM_R8(base - 1) != 0xa5 || MEM_R8(base + 0x23) != 0xa5 ||
                MEM_R8(base + 0x28) != 0xa5)
                return false;
        }
    }
    /* The store at offset 0x20 redirects subsequent pointer reads. */
    CPUState start = {0};
    start.r[28] = record + 0x20;
    start.r[29] = 0x801fff00;
    start.r[31] = 0x80010000;
    start.budget = 16;
    MEM_W32(start.r[28], record);
    return compare(start, 0x8001078c, nc_launcher_init_shared_record, 1);
}

static bool run_constructors(CPUState *c) {
    return nc_launcher_global_callbacks(c, true);
}

static bool run_destructors(CPUState *c) {
    return nc_launcher_global_callbacks(c, false);
}

static bool check_global_callbacks(void) {
    const u32 flags[] = {0, 1, 0x80000000};
    for (unsigned ctor = 0; ctor < 2; ctor++)
        for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; i++) {
            CPUState start = {0};
            start.r[1] = 0x3456;
            start.r[8] = 0x1234;
            start.r[16] = 0x6789;
            start.r[17] = 0x9876;
            start.r[29] = 0x801ffe00;
            start.r[31] = 0x80010000;
            start.budget = i ? 4096 : 31;
            MEM_W32(0x800145f4, flags[i]);
            u32 entry = ctor ? 0x800108f8 : 0x80010968;
            if (!compare(start, entry, ctor ? run_constructors : run_destructors, 30) ||
                MEM_R32(0x800145f4) != (ctor && !flags[i] ? 1 : flags[i]))
                return false;
        }
    return true;
}

static u32 a0_service;

static bool call_a0_wrapper(CPUState *c) {
    return bios_service(c, 0xa0, a0_service);
}

static bool check_a0_wrappers(void) {
    const u32 header = 0x80016200, probe = 0x80016300, trace = probe + 64;
    const struct { u32 entry, service, first_arg, result; } cases[] = {
        {0x800109d0, 0x39, header, 0},
        {0x800109e0, 0x42, 0x80010000, 1}, /* original PSX.EXE path */
        {0x800109e0, 0x42, trace, 0},      /* empty path */
        {0x800109f0, 0x43, header, 1},
        {0x80010a00, 0x71, 0, 0},
        {0x80010a18, 0x72, 0, 0},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        CPUState start = {0};
        start.r[4] = cases[i].first_arg;
        start.r[5] = header;
        start.r[6] = 17;
        start.r[16] = trace;
        start.r[29] = 0x801fff00;
        start.r[31] = 0x80010000;
        start.budget = i & 1 ? 4 : 1024;
        a0_service = cases[i].service;
        MEM_W32(trace, 0);
        if (a0_service == 0x43) {
            ExecInfo info = {.pc0 = probe};
            exe_write_info(&info, header);
            MEM_W32(probe, 0xae040000); /* sw a0, 0(s0) */
            MEM_W32(probe + 4, 0x03e00008);
            MEM_W32(probe + 8, 0xae050004); /* sw a1, 4(s0) */
        }
        if (!compare(start, cases[i].entry, call_a0_wrapper, 3) ||
            nc_cpu.r[2] != cases[i].result || nc_cpu.r[9] != a0_service ||
            nc_cpu.r[10] != 0xa0)
            return false;
        if (a0_service == 0x42 && cases[i].result && MEM_R32(header) != 0x80025c70)
            return false;
        if (a0_service == 0x43 && (MEM_R32(trace) != header || MEM_R32(trace + 4) != 17))
            return false;
    }
    return true;
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
    if (!check_shared_record()) {
        fprintf(stderr, "launcher shared record failed\n");
        return 1;
    }
    if (!check_global_callbacks()) {
        fprintf(stderr, "launcher global callbacks failed\n");
        return 1;
    }
    if (!check_a0_wrappers()) {
        fprintf(stderr, "launcher A0 wrappers failed\n");
        return 1;
    }
    for (unsigned selection = 0; selection < 6; selection++)
        for (unsigned missing = 0; missing < 2; missing++)
            if (!check_handoff(selection, missing)) {
                fprintf(stderr, "launcher selector %u, missing %u failed\n", selection, missing);
                return 1;
            }
    disc_close();
    return 0;
}
