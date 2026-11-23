#ifndef NC_TESTS_CD_READ_SYNC_H
#define NC_TESTS_CD_READ_SYNC_H

#include "jump_context.h"

static bool compare_cd_read_sync(u32 entry, bool (*routine)(CPUState *), u32 start_frame,
                                 u32 vsync, u32 retry, u32 ready) {
    const u32 trace = 0x80062000, response = trace + 16;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Return successive frame numbers. Record the retry and ready arguments. */
    MEM_W32(vsync, 0x01201021);      /* move v0, t1 */
    MEM_W32(vsync + 4, 0x03e00008);
    MEM_W32(vsync + 8, 0x25290001);  /* addiu t1, t1, 1 */
    MEM_W32(retry, 0x03e00008);
    MEM_W32(retry + 4, 0xaf040000);  /* sw a0, 0(t8) */
    MEM_W32(ready, 0xaf040004);      /* sw a0, 4(t8) */
    MEM_W32(ready + 4, 0x03e00008);
    MEM_W32(ready + 8, 0xaf050008);  /* sw a1, 8(t8) */
    const struct {
        u32 mode, first_frame, remaining, retry_result, expected, polls;
        bool retried;
    } cases[] = {
        {1, 59, 3, 0, 3, 2, false},       /* exactly at the stall deadline */
        {0, 59, 3, 0, 0, 4, true},        /* wait through the next poll */
        {1, 0, UINT32_MAX, 2, 2, 1, true},
        {0, 1201, 3, 0, UINT32_MAX, 1, false},
        {0, 1, 0, 0, 0, 2, false},
    };
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        MEM_W32(start_frame, 0);
        MEM_W32(start_frame - 4, 0);
        MEM_W32(start_frame - 8, cases[i].remaining);
        MEM_W32(start_frame - 28, cases[i].retry_result);
        memset(nc_fast_ptr(trace), 0, 12);
        CPUState state = arguments(cases[i].mode, response, 0, i ? 11 : 4096);
        state.r[24] = trace;
        state.r[9] = cases[i].first_frame;
        ok = compare(state, entry, routine, 10) &&
             nc_cpu.r[2] == cases[i].expected &&
             nc_cpu.r[9] == cases[i].first_frame + cases[i].polls &&
             MEM_R32(trace) == cases[i].retried && MEM_R32(trace + 4) == 1 &&
             MEM_R32(trace + 8) == response;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
