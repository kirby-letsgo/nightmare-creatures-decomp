#ifndef NC_TESTS_CD_DMA_WAIT_H
#define NC_TESTS_CD_DMA_WAIT_H

#include "jump_context.h"

typedef struct {
    u32 entry, vsync, puts, print, flush;
    u32 dma_slot, names, status, deadline, operation;
} CdDmaWaitFixture;

static bool compare_cd_dma_wait(bool (*routine)(CPUState *), const CdDmaWaitFixture *fixture) {
    const u32 script = 0x80061000, dma = script + 64, trace = dma + 16;
    /* Each VSync call supplies a frame, poll count and DMA status. This
     * reaches the poll-limit boundary without millions of redundant calls. */
    const u32 vsync[] = {
        0x8d020000, /* lw v0,0(t0) */
        0x8d0b0004, /* lw t3,4(t0) */
        0x8d0c0008, /* lw t4,8(t0) */
        0xad2b0000, /* sw t3,0(t1) */
        0xad4c0000, /* sw t4,0(t2) */
        0x03e00008, /* jr ra */
        0x2508000c, /* addiu t0,t0,12 */
    };
    const struct {
        u32 mode, frame, polls, busy, result;
        bool repeats;
    } cases[] = {
        {0, 100, 0, 0, 0, false}, {1, 100, 0, 1, 1, false},
        {0, 100, 0, 1, 0, true}, {1, 1060, 0x3c0000, 1, 1, false},
        {1, 1061, 17, 1, UINT32_MAX, false}, {0, 100, 0x3c0001, 0, UINT32_MAX, false},
        {1, UINT32_MAX, UINT32_MAX, 0, 0, false},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    for (unsigned i = 0; i < sizeof vsync / sizeof vsync[0]; i++)
        MEM_W32(fixture->vsync + i * 4, vsync[i]);
    const u32 reports[] = {fixture->puts, fixture->print, fixture->flush};
    for (unsigned i = 0; i < 3; i++) {
        MEM_W32(reports[i], 0x03e00008); /* jr ra */
        MEM_W32(reports[i] + 4, 0xadbf0000 + i * 4); /* sw ra,offset(t5) */
    }
    MEM_W32(fixture->dma_slot, dma);
    MEM_W8(fixture->names - 3, 3);
    MEM_W8(fixture->status, 1);
    MEM_W8(fixture->status + 1, 2);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        memset(nc_fast_ptr(script), 0, 96);
        MEM_W32(script, 100);
        MEM_W32(script + 12, cases[i].frame);
        MEM_W32(script + 16, cases[i].polls);
        MEM_W32(script + 20, cases[i].busy << 24);
        MEM_W32(script + 24, 101);
        MEM_W32(script + 28, 1);
        CPUState state = arguments(cases[i].mode, 2, 3, i & 1 ? 10 : 4096);
        state.r[8] = script;
        state.r[9] = fixture->deadline + 4;
        state.r[10] = dma;
        state.r[13] = trace;
        bool timed_out = cases[i].result == UINT32_MAX;
        u32 count = cases[i].polls + ((s32)cases[i].frame <= 1060);
        ok = compare(state, fixture->entry, routine, 9) &&
             nc_cpu.r[2] == cases[i].result && MEM_R32(fixture->deadline) == 1060 &&
             MEM_R32(fixture->deadline + 4) == (cases[i].repeats ? 2 : count) &&
             nc_cpu.r[8] == script + (cases[i].repeats ? 36 : 24) &&
             MEM_R32(trace) == (timed_out ? fixture->entry + 0xb8 : 0) &&
             MEM_R32(trace + 4) == (timed_out ? fixture->entry + 0x108 : 0) &&
             MEM_R32(trace + 8) == (timed_out ? fixture->entry + 0x110 : 0);
        if (timed_out)
            ok = ok && nc_cpu.r[5] == fixture->operation && nc_cpu.r[6] == MEM_R32(fixture->names + 12) &&
                 nc_cpu.r[7] == MEM_R32(fixture->names + 0x84) &&
                 MEM_R32(state.r[29] - 32) == MEM_R32(fixture->names + 0x88);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
