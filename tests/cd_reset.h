#ifndef NC_TESTS_CD_RESET_H
#define NC_TESTS_CD_RESET_H

#include "jump_context.h"

static bool check_cd_reset_modes(u32 entry, bool (*reset)(CPUState *), const u32 drivers[3]) {
    const u32 modes[] = {2, 1, 0, UINT32_MAX};
    const u32 trace = 0x80062000;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Isolate CdReset from the drive. Each callee records its return PC and
     * returns a controlled result from t1, t2 or t3 in the delay slot. */
    for (unsigned i = 0; i < 3; i++) {
        MEM_W32(drivers[i], 0xad1f0000u + 4 * i); /* sw ra, offset(t0) */
        MEM_W32(drivers[i] + 4, 0x03e00008);      /* jr ra */
        MEM_W32(drivers[i] + 8, ((9u + i) << 21) | 0x1021u);
    }
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof modes / sizeof modes[0]; i++) {
        for (unsigned errors = 0; ok && errors < 4; errors++) {
            memset(nc_fast_ptr(trace), 0, 12);
            CPUState state = arguments(modes[i], 0, 0, errors ? 7 : 4096);
            state.r[8] = trace;
            state.r[9] = UINT32_MAX;
            state.r[10] = errors & 1;
            state.r[11] = errors & 2;
            bool interrupts_only = modes[i] == 2;
            bool volume = modes[i] == 1 && !state.r[10];
            bool success = interrupts_only || (!state.r[10] && !(volume && state.r[11]));
            ok = compare(state, entry, reset, 6) &&
                 nc_cpu.r[2] == (u32)success &&
                 MEM_R32(trace) == (interrupts_only ? entry + 0x20 : 0) &&
                 MEM_R32(trace + 4) == (interrupts_only ? 0 : entry + 0x30) &&
                 MEM_R32(trace + 8) == (volume ? entry + 0x4c : 0);
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
