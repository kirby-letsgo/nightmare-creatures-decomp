#ifndef NC_TESTS_CD_READ_H
#define NC_TESTS_CD_READ_H

#include "jump_context.h"

typedef struct {
    u32 entry, state, pause, retry, timer_slots, frame, callbacks;
} CdReadTestLayout;

static bool compare_cd_read(const CdReadTestLayout *layout, bool (*routine)(CPUState *)) {
    const u32 trace = 0x80062000, buffer = 0x80064000;
    const u32 pause = layout->pause, retry = layout->retry;
    const int threshold = 9 * NC_INSN_CYCLES + NC_LOAD_CYCLES;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Capture the pause and retry calls while running the original callback
     * setters, status query and VSync frame query. */
    MEM_W32(pause, 0xaf040000);       /* sw a0, 0(t8) */
    MEM_W32(pause + 4, 0xaf050004);   /* sw a1, 4(t8) */
    MEM_W32(pause + 8, 0x03e00008);
    MEM_W32(pause + 12, 0xaf060008);  /* sw a2, 8(t8) */
    MEM_W32(retry, 0xaf04000c);       /* sw a0, 12(t8) */
    MEM_W32(retry + 4, 0x03e00008);
    MEM_W32(retry + 8, 0x03201021);   /* move v0, t9 */
    MEM_W32(layout->timer_slots, trace + 16);
    MEM_W32(layout->timer_slots + 4, trace + 20);
    MEM_W32(layout->frame, 1234);
    const struct { u32 mode, words, status, result; } cases[] = {
        {0x80, 512, 1, 0}, {0xa0, 585, 0x20, 1},
        {0x90, 582, 0x40, UINT32_MAX}, {0xb0, 582, 0x80, 7},
    };
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        memset(nc_fast_ptr(trace), 0xff, 16);
        MEM_W8(layout->callbacks + 16, cases[i].status);
        MEM_W32(layout->callbacks, 0x80061000);
        MEM_W32(layout->callbacks + 4, 0x80061100);
        CPUState state = arguments(i + 1, buffer, cases[i].mode, i ? threshold + 1 : 4096);
        state.r[24] = trace;
        state.r[25] = cases[i].result;
        ok = compare(state, layout->entry, routine, threshold) &&
             nc_cpu.r[2] == ((s32)cases[i].result > 0) &&
             MEM_R32(layout->state + 16) == cases[i].words && MEM_R32(layout->state + 12) == (cases[i].mode | 0x20) &&
             MEM_R32(layout->state + 4) == buffer && MEM_R32(layout->state) == i + 1 &&
             MEM_R32(layout->state + 28) == 1234 && MEM_R32(layout->callbacks) == 0 && MEM_R32(layout->callbacks + 4) == 0 &&
             MEM_R32(layout->state + 36) == 0x80061000 && MEM_R32(layout->state + 40) == 0x80061100 &&
             MEM_R32(trace + 12) == 0;
        if (cases[i].status & 0xe0)
            ok = ok && MEM_R32(trace) == 9 && MEM_R32(trace + 4) == 0 && MEM_R32(trace + 8) == 0;
        else
            ok = ok && MEM_R32(trace) == UINT32_MAX;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
