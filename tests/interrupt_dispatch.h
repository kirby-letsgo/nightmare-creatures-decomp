#ifndef NC_TESTS_INTERRUPT_DISPATCH_H
#define NC_TESTS_INTERRUPT_DISPATCH_H

#include "jump_context.h"

static bool compare_interrupt_dispatch(u32 trap, bool (*routine)(CPUState *), u32 driver, u32 ports,
                                       u32 print, u32 exception, u32 uninitialized, u32 unhandled) {
    const u32 status = 0x80061000, mask = status + 4, trace = status + 16;
    const u32 first = status + 128, last = first + 32;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* A returning exception stub makes both the early diagnostic and the
     * normally unreachable stack restore observable. */
    MEM_W32(exception, 0x03e00008);
    MEM_W32(exception + 4, 0x25ad0001); /* addiu t5,t5,1 */
    MEM_W32(print, 0x03e00008);
    MEM_W32(print + 4, 0xad040010); /* sw a0,16(t0) */
    for (unsigned i = 0; i < 2; i++) {
        u32 entry = i ? last : first;
        MEM_W32(entry, 0x95220000);      /* lhu v0,0(t1): acknowledged status */
        MEM_W32(entry + 4, 0x00000000);
        MEM_W32(entry + 8, 0xad020000);  /* sw v0,0(t0) */
        MEM_W32(entry + 12, 0x25080004);
        MEM_W32(entry + 16, 0x03e00008);
        MEM_W32(entry + 20, i ? 0xa5200000 : 0xa52a0000); /* set next pending bits */
    }
    const struct {
        u16 initialized, status, enabled, mask;
        u32 counter, final_counter, diagnostic;
    } cases[] = {
        {0, 0, 0, 0, 9, 0, uninitialized},
        {1, 0, 1, 1, 12, 0, 0},
        {1, 1, 1, 1, 0, 0, 0}, /* acknowledge a slot with no callback */
        {1, 0x8004, 0x8404, 0x8404, 0, 0, 0}, /* source 2 posts source 10 */
        {1, 4, 0, 4, 0x800, 0x801, 0},
        {1, 4, 0, 4, 0x801, 0, unhandled},
        {1, 4, 4, 0, 9, 0, 0}, /* hardware mask suppresses the registered source */
    };
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        MEM_W16(driver, cases[i].initialized);
        MEM_W16(driver + 2, 0);
        memset(nc_fast_ptr(driver + 4), 0, 11 * 4);
        MEM_W32(driver + 4 + 2 * 4, first);
        MEM_W32(driver + 4 + 10 * 4, last);
        MEM_W16(driver + 0x30, cases[i].enabled);
        MEM_W32(ports, status);
        MEM_W32(ports + 4, mask);
        MEM_W32(ports + 12, cases[i].counter);
        MEM_W16(status, cases[i].status);
        MEM_W16(mask, cases[i].mask);
        memset(nc_fast_ptr(trace), 0, 32);
        CPUState state = arguments(1, 2, 3, i & 1 ? 10 : 4096);
        state.r[8] = trace;
        state.r[9] = status;
        state.r[10] = 1u << 10;
        state.r[13] = 0;
        ok = compare(state, trap, routine, 9) &&
             MEM_R16(driver + 2) == 0 && MEM_R32(ports + 12) == cases[i].final_counter &&
             MEM_R32(trace + 16) == cases[i].diagnostic &&
             nc_cpu.r[13] == (cases[i].initialized ? 1u : 2u);
        if (i == 3)
            ok = ok && nc_cpu.r[8] == trace + 8 && MEM_R32(trace) == 0xfffb &&
                 MEM_R32(trace + 4) == 0xfbff && MEM_R16(status) == 0;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
