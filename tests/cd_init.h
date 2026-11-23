#ifndef NC_TESTS_CD_INIT_H
#define NC_TESTS_CD_INIT_H

#include "jump_context.h"

static bool check_cd_init_retries(u32 entry, bool (*routine)(CPUState *), u32 reset,
                                  u32 diagnostic, u32 message, const u32 slots[3],
                                  const u32 handlers[3]) {
    const u32 responses = 0x80062000;
    const unsigned attempts[] = {1, 5, 0}; /* zero means all resets fail */
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Consume one reset response per call and record the diagnostic argument.
     * The callback setters themselves run from the disc image. */
    MEM_W32(reset, 0x8d220000);     /* lw v0, 0(t1) */
    MEM_W32(reset + 4, 0x03e00008);
    MEM_W32(reset + 8, 0x25290004); /* addiu t1, t1, 4 */
    MEM_W32(diagnostic, 0x03e00008);
    MEM_W32(diagnostic + 4, 0xad440000); /* sw a0, 0(t2) */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof attempts / sizeof attempts[0]; i++) {
        for (unsigned j = 0; j < 5; j++)
            MEM_W32(responses + 4 * j, j + 1 == attempts[i] ? 1 : 2);
        for (unsigned j = 0; j < 3; j++)
            MEM_W32(slots[j], UINT32_MAX);
        MEM_W32(responses + 20, 0);
        CPUState state = arguments(0, 0, 0, i ? 5 : 4096);
        state.r[9] = responses;
        state.r[10] = responses + 20;
        ok = compare(state, entry, routine, 4) &&
             nc_cpu.r[2] == (attempts[i] != 0) &&
             nc_cpu.r[9] == responses + 4 * (attempts[i] ? attempts[i] : 5) &&
             MEM_R32(responses + 20) == (attempts[i] ? 0 : message);
        for (unsigned j = 0; ok && j < 3; j++)
            ok = MEM_R32(slots[j]) == (attempts[i] ? handlers[j] : UINT32_MAX);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
