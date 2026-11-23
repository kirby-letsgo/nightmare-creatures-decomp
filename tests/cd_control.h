#ifndef NC_TESTS_CD_CONTROL_H
#define NC_TESTS_CD_CONTROL_H

#include "jump_context.h"
#include "decomp/cd_control.h"

static bool compare_cd_control(u32 entry, bool (*routine)(CPUState *), enum CdControlMode mode,
                               const CdControlLayout *layout) {
    const bool fast = mode == CD_CONTROL_FAST;
    const bool blocking = mode == CD_CONTROL_BLOCKING;
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
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    if (blocking) {
        /* CD_sync records its mode in the result buffer and returns t3. */
        MEM_W32(layout->sync, 0xaca40000); /* sw a0,0(a1) */
        MEM_W32(layout->sync + 4, 0x03e00008); /* jr ra */
        MEM_W32(layout->sync + 8, 0x01601021); /* addu v0,t3,zero */
    }
    for (unsigned i = 0; i < sizeof writer / sizeof writer[0]; i++)
        MEM_W32(layout->writer + i * 4, writer[i]);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        u32 command = cases[i].command & 0xff;
        MEM_W32(layout->location_table + 4 * command, cases[i].needs_location);
        MEM_W8(layout->callback + 16, cases[i].status);
        MEM_W32(layout->callback, callback);
        for (unsigned j = 0; j < cases[i].calls; j++)
            MEM_W32(trace + 24 * j + 20, (cases[i].failures >> j) & 1);
        CPUState state = arguments(cases[i].command, cases[i].params, location + 8,
                                   i & 1 ? threshold + 1 : 4096);
        state.r[8] = trace;
        state.r[9] = layout->callback;
        state.r[11] = i == 3 ? 5 : 2; /* CdlDiskError or CdlComplete */
        MEM_W32(state.r[6], UINT32_MAX);
        bool sync_called = blocking && cases[i].success;
        bool success = cases[i].success && (!blocking || state.r[11] == 2);
        ok = compare(state, entry, routine, threshold) &&
             MEM_R32(state.r[6]) == (sync_called ? 0u : UINT32_MAX) &&
             nc_cpu.r[2] == success && nc_cpu.r[8] == trace + 24 * cases[i].calls &&
             MEM_R32(layout->callback) == callback;
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
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
