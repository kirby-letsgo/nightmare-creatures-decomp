#ifndef NC_TESTS_CD_INTERRUPT_CALLBACK_H
#define NC_TESTS_CD_INTERRUPT_CALLBACK_H

#include "jump_context.h"
#include "decomp/cd_interrupt_callback.h"

static bool compare_cd_callbacks(const CdCallbackLayout *layout, bool (*routine)(CPUState *)) {
    const u32 getintr = layout->getintr, handler = 0x80061000, trace = 0x80062000;
    const u32 flags = trace + 64, port = trace + 96;
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    MEM_W32(getintr, 0x8f220000);     /* lw v0, 0(t9) */
    MEM_W32(getintr + 4, 0x03e00008);
    MEM_W32(getintr + 8, 0x27390004); /* addiu t9, t9, 4 */
    MEM_W32(handler, 0xaf040000);     /* sw a0, 0(t8) */
    MEM_W32(handler + 4, 0xaf050004); /* sw a1, 4(t8) */
    MEM_W32(handler + 8, 0x03e00008);
    MEM_W32(handler + 12, 0x27180008); /* addiu t8, t8, 8 */
    const u32 events[] = {1, 4, 2, 6, 0};
    for (unsigned i = 0; i < sizeof events / sizeof events[0]; i++)
        MEM_W32(flags + 4 * i, events[i]);
    MEM_W32(layout->ports, port);
    MEM_W8(layout->ports + 24, 2);
    MEM_W8(layout->ports + 25, 5);
    const int threshold = 9 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    bool ok = !saved.error;
    for (unsigned enabled = 0; ok && enabled < 4; enabled++) {
        MEM_W8(port, 0xd3);
        MEM_W32(layout->callbacks + 4, enabled & 1 ? handler : 0);
        MEM_W32(layout->callbacks, enabled & 2 ? handler : 0);
        memset(nc_fast_ptr(trace), 0, 32);
        CPUState state = arguments(0, 0, 0, enabled ? threshold + 1 : 4096);
        state.r[24] = trace;
        state.r[25] = flags;
        ok = compare(state, layout->entry, routine, threshold) &&
             nc_cpu.r[25] == flags + sizeof events && MEM_R8(port) == 3;
        u32 cursor = trace;
        for (unsigned pair = 0; pair < 2; pair++) {
            if (enabled & 1) {
                ok = ok && MEM_R32(cursor) == 5 && MEM_R32(cursor + 4) == layout->results + 8;
                cursor += 8;
            }
            if (enabled & 2) {
                ok = ok && MEM_R32(cursor) == 2 && MEM_R32(cursor + 4) == layout->results;
                cursor += 8;
            }
        }
        ok = ok && nc_cpu.r[24] == cursor;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
