#ifndef NC_TESTS_VSYNC_H
#define NC_TESTS_VSYNC_H

#include "jump_context.h"

static void advance_vsync_field(void) {
    MEM_W32(0x80060800, 0x80080000);
}

static bool compare_vsync(u32 entry, bool (*routine)(CPUState *), u32 slots, u32 frame, u32 wait_entry) {
    const u32 status = 0x80060800, timer = status + 4, trace = status + 16;
    const u32 wait_stub[] = {
        0xad040000, /* sw a0,0(t0) */
        0xad050004, /* sw a1,4(t0) */
        0x8d0a0008, /* lw t2,8(t0) */
        0x2508000c, /* addiu t0,t0,12 */
        0x03e00008, /* jr ra */
        0xad2a0000, /* sw t2,0(t1) */
    };
    const struct {
        s32 mode;
        u32 before_field, after_field;
        bool advance_on_vblank;
    } cases[] = {
        {-1, 0, 0, false}, {1, 0, 0, false}, {0, 0, 0, false},
        {4, 0x80000, 0x80080000, false}, {2, 0x80000, 0x80000, true},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    void (*saved_hook)(void) = nc_frame_hook;
    for (unsigned i = 0; i < sizeof wait_stub / sizeof wait_stub[0]; i++)
        MEM_W32(wait_entry + i * 4, wait_stub[i]);
    MEM_W32(slots, status);
    MEM_W32(slots + 4, timer);
    MEM_W32(timer, 3);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        bool waits = cases[i].mode == 0 || cases[i].mode > 1;
        MEM_W32(frame, 9);
        MEM_W32(slots + 12, 7);
        MEM_W32(slots + 8, 0xfffe);
        MEM_W32(status, 0);
        MEM_W32(trace + 8, cases[i].before_field);
        MEM_W32(trace + 20, cases[i].after_field);
        nc_frame_hook = cases[i].advance_on_vblank ? advance_vsync_field : NULL;
        CPUState state = arguments((u32)cases[i].mode, 0, 0, i & 1 ? 2 : 4096);
        state.r[8] = trace;
        state.r[9] = status;
        ok = compare(state, entry, routine, 1) &&
             nc_cpu.r[2] == (cases[i].mode < 0 ? 9 : 5) &&
             MEM_R32(slots + 12) == (waits ? 9 : 7) &&
             MEM_R32(slots + 8) == (waits ? 3 : 0xfffe) &&
             nc_cpu.r[8] == trace + (waits ? 24 : 0);
        if (waits) {
            u32 skip = cases[i].mode > 0 ? (u32)cases[i].mode - 1 : 0;
            ok = ok && MEM_R32(trace) == 7 + skip && MEM_R32(trace + 4) == skip &&
                 MEM_R32(trace + 12) == 10 && MEM_R32(trace + 16) == 1;
        }
    }
    nc_frame_hook = saved_hook;
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
