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

typedef struct {
    u32 entry, state, callback, control;
    u32 fast, puts, flush, lid_message;
    u32 retry_message, timers, frame, callbacks;
    u32 get_sector, mismatch_message, vsync;
} CdRetryFixture;

static bool compare_cd_read_retry(const CdRetryFixture *f, bool (*routine)(CPUState *)) {
    const u32 trace = 0x80061000, control = f->control, fast = f->fast;
    const struct {
        u32 retry, lid, frame, mode, fail_command;
        unsigned commands;
        u8 sequence[4];
    } cases[] = {
        {0, 1, 64, 0, 0, 1, {1}}, {1, 1, 65, 0, 0, 1, {1}},
        {0, 0, 20, 0x80, 0, 1, {6}}, {0, 0, 20, 0, 0, 2, {14, 6}},
        {1, 0, 21, 0x80, 0, 4, {9, 2, 14, 6}},
        {1, 0, 22, 0, 2, 2, {9, 2}}, {0, 0, 23, 0, 14, 1, {14}},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Trace both command wrappers. Only the selected command returns zero. */
    const u32 wrappers[] = {control, fast};
    for (unsigned i = 0; i < 2; i++) {
        u32 entry = wrappers[i];
        MEM_W32(entry, 0xad040000);      /* sw a0,0(t0) */
        MEM_W32(entry + 4, 0xad050004);  /* sw a1,4(t0) */
        MEM_W32(entry + 8, 0x25080008);  /* addiu t0,t0,8 */
        MEM_W32(entry + 12, 0x03e00008);
        MEM_W32(entry + 16, 0x008f1026); /* xor v0,a0,t7 */
    }
    MEM_W32(f->puts, 0x03e00008); /* puts records its string */
    MEM_W32(f->puts + 4, 0xad240000);
    MEM_W32(f->flush, 0x03e00008); /* CdFlush */
    MEM_W32(f->flush + 4, 0);
    MEM_W32(f->timers, trace + 64);
    MEM_W32(f->timers + 4, trace + 68);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        MEM_W32(trace + 32, 0);
        MEM_W32(f->frame, cases[i].frame);
        MEM_W8(f->callbacks + 16, cases[i].lid << 4);
        MEM_W8(f->callbacks + 32, cases[i].mode);
        MEM_W32(f->callbacks + 28, 0x00000300); /* 00:03:00, sector 75 */
        MEM_W32(f->callbacks, trace + 0x100);
        MEM_W32(f->callbacks + 4, trace + 0x100);
        MEM_W32(f->state + 12, 0x180);
        MEM_W32(f->state, 12);
        MEM_W32(f->state + 4, 0x80062000);
        MEM_W32(f->state + 24, 0);
        MEM_W32(f->state + 28, 0);
        CPUState state = arguments(cases[i].retry, 2, 3, i & 1 ? 8 : 4096);
        state.r[8] = trace;
        state.r[9] = trace + 32;
        state.r[15] = cases[i].fail_command;
        bool started = !cases[i].lid && !cases[i].fail_command;
        ok = compare(state, f->entry, routine, 7) &&
             nc_cpu.r[2] == (started ? 12u : UINT32_MAX) &&
             MEM_R32(f->state + 20) == nc_cpu.r[2] && MEM_R32(f->callbacks) == 0 &&
             MEM_R32(f->callbacks + 4) == (started ? f->callback : 0) &&
             nc_cpu.r[8] == trace + cases[i].commands * 8 &&
             MEM_R32(f->state + 24) == (started ? cases[i].frame : 0) &&
             MEM_R32(f->state + 28) == (cases[i].lid ? cases[i].frame : 0);
        if (started)
            ok = ok && MEM_R32(f->state + 32) == 75 && MEM_R32(f->state + 8) == 0x80062000;
        u32 message = cases[i].lid ? (cases[i].frame % 64 == 0 ? f->lid_message : 0)
                                   : (cases[i].retry ? f->retry_message : 0);
        ok = ok && MEM_R32(trace + 32) == message;
        for (unsigned j = 0; ok && j < cases[i].commands; j++) {
            u32 cmd = cases[i].sequence[j];
            u32 param = cmd == 14 ? state.r[29] - 24 : cmd == 2 ? f->callbacks + 28 : 0;
            ok = MEM_R32(trace + 8 * j) == cmd && MEM_R32(trace + 8 * j + 4) == param;
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool compare_sector_ready(const CdRetryFixture *f, bool (*routine)(CPUState *)) {
    const u32 buffer = 0x80061000, trace = 0x80062000, notify = trace + 64;
    const struct {
        u32 event, count, words, expected_sector, first_frame, retry_count;
        bool notify;
    } cases[] = {
        {0x101, 2, 512, 75, 10, 0, true}, /* matching header, more data */
        {1, 1, 512, 75, 10, 0, true},      /* last sector */
        {1, 3, 512, 76, 10, 4, true},      /* mismatch, retry succeeds */
        {5, 2, 512, 75, 1201, 2, true},    /* disk error, deadline exceeded */
        {1, 0, 512, 75, 10, 0, false},     /* already complete */
        {1, 2, 585, 75, 1199, 0, true},    /* deadline crosses on second check */
        {1, UINT32_MAX, 585, 75, 12, 0, true},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    MEM_W32(f->get_sector, 0xad050000); /* CdGetSector records size and writes BCD position */
    MEM_W32(f->get_sector + 4, 0x25080004);
    MEM_W32(f->get_sector + 8, 0x03e00008);
    MEM_W32(f->get_sector + 12, 0xac890000);
    MEM_W32(f->vsync, 0x01401021); /* VSync returns successive frames from t2 */
    MEM_W32(f->vsync + 4, 0x03e00008);
    MEM_W32(f->vsync + 8, 0x254a0001);
    MEM_W32(f->entry, 0x8d620000); /* retry records old count and installs t4 */
    MEM_W32(f->entry + 4, 0);
    MEM_W32(f->entry + 8, 0xada20000);
    MEM_W32(f->entry + 12, 0x03e00008);
    MEM_W32(f->entry + 16, 0xad6c0000);
    MEM_W32(f->puts, 0x03e00008); /* puts */
    MEM_W32(f->puts + 4, 0xada40004);
    MEM_W32(f->control, 0x03e00008); /* CdControl */
    MEM_W32(f->control + 4, 0xada40008);
    MEM_W32(notify, 0xada4000c);
    MEM_W32(notify + 4, 0x03e00008);
    MEM_W32(notify + 8, 0xada50010);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        memset(nc_fast_ptr(trace), 0, 48);
        MEM_W32(f->state - 4, cases[i].notify ? notify : 0);
        MEM_W32(f->state + 8, buffer);
        MEM_W32(f->state + 16, cases[i].words);
        MEM_W32(f->state + 20, cases[i].count);
        MEM_W32(f->state + 28, 0);
        MEM_W32(f->state + 32, cases[i].expected_sector);
        MEM_W32(f->state + 36, notify + 32);
        MEM_W32(f->state + 40, notify + 48);
        MEM_W32(f->callbacks, 0);
        MEM_W32(f->callbacks + 4, f->callback);
        CPUState state = arguments(cases[i].event, buffer + 0x100, 3, i & 1 ? 9 : 4096);
        state.r[8] = trace;
        state.r[9] = 0x300;
        state.r[10] = cases[i].first_frame;
        state.r[11] = f->state + 20;
        state.r[12] = cases[i].retry_count;
        state.r[13] = trace + 16;
        bool collect = (cases[i].event & 255) == 1 && (s32)cases[i].count > 0;
        bool header = collect && cases[i].words == 512;
        bool mismatch = header && cases[i].expected_sector != 75;
        ok = compare(state, f->callback, routine, 8) &&
             MEM_R32(f->state + 24) == cases[i].first_frame &&
             MEM_R32(f->state + 32) == cases[i].expected_sector + collect &&
             MEM_R32(f->state + 8) == buffer + (collect ? 4 * cases[i].words : 0) &&
             nc_cpu.r[8] == trace + 4 * (collect + header) &&
             MEM_R32(trace + 20) == (mismatch ? f->mismatch_message : 0);
        if (collect)
            ok = ok && MEM_R32(trace + 4 * header) == cases[i].words &&
                 (!header || MEM_R32(trace) == 3) && MEM_R32(buffer) == 0x300;
        if (mismatch)
            ok = ok && MEM_R32(trace + 16) == UINT32_MAX - 1;
        const u32 remaining[] = {1, 0, 4, UINT32_MAX, 0, 1, 0};
        bool finished = i != 0 && i != 2;
        ok = ok && MEM_R32(f->state + 20) == remaining[i] &&
             MEM_R32(trace + 24) == (finished ? 9 : 0) &&
             MEM_R32(f->callbacks + 4) == (finished ? notify + 48 : f->callback);
        if (finished && cases[i].notify)
            ok = ok && MEM_R32(trace + 28) == (remaining[i] ? 5 : 2) &&
                 MEM_R32(trace + 32) == state.r[5];
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
