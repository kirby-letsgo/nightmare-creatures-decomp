#ifndef NC_TESTS_VBLANK_WAIT_H
#define NC_TESTS_VBLANK_WAIT_H

#include "jump_context.h"

static bool compare_vblank_wait(u32 entry, bool (*routine)(CPUState *), u32 frame,
                                u32 message, const u32 drivers[3]) {
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
        ok = compare(state, entry, routine, 3) &&
             MEM_R32(state.r[29] - 16) == cases[i].remaining &&
             MEM_R32(trace) == (cases[i].timed_out ? message : UINT32_MAX) &&
             MEM_R32(trace + 4) == (cases[i].timed_out ? 0 : UINT32_MAX) &&
             MEM_R32(trace + 8) == (cases[i].timed_out ? 3 : UINT32_MAX);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
