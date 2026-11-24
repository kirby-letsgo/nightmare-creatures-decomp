#ifndef NC_TESTS_CD_EVENTS_H
#define NC_TESTS_CD_EVENTS_H

#include "jump_context.h"

typedef struct {
    u32 vsync, check_callback, getintr, puts, print, flush;
    u32 ports, callbacks, results, deadline, status, ready, sync, writer;
} CdEventFixture;

static void install_cd_wait_stubs(const CdEventFixture *f, u32 handler) {
    /* VSync supplies a frame, poll counter and controller state per call. */
    const u32 vsync[] = {
        0x8d020000, 0x8d0b0004, 0x8d0c0008, 0xad2b0000,
        0xa14c0000, 0x03e00008, 0x2508000c,
    };
    for (unsigned j = 0; j < sizeof vsync / sizeof vsync[0]; j++)
        MEM_W32(f->vsync + j * 4, vsync[j]);
    MEM_W32(f->check_callback, 0x03e00008);
    MEM_W32(f->check_callback + 4, 0x01c01021); /* CheckCallback returns t6 */
    MEM_W32(f->getintr, 0x8f220000);
    MEM_W32(f->getintr + 4, 0x03e00008);
    MEM_W32(f->getintr + 8, 0x27390004); /* getintr consumes a flag word */
    MEM_W32(handler, 0xaf040000);
    MEM_W32(handler + 4, 0xaf050004);
    MEM_W32(handler + 8, 0x03e00008);
    MEM_W32(handler + 12, 0x27180008);
    const u32 reports[] = {f->puts, f->print, f->flush};
    for (unsigned j = 0; j < 3; j++) {
        MEM_W32(reports[j], 0x03e00008);
        MEM_W32(reports[j] + 4, 0xadbf0000 + 4 * j);
    }
}

static bool check_cd_ready_driver(const CdEventFixture *f, bool (*routine)(CPUState *)) {
    const u32 script = 0x80061000, dest = script + 64, port = dest + 32;
    const u32 events = port + 16, trace = events + 32, handler = trace + 64;
    const struct {
        u32 mode, status, next_status, frame, polls, expected;
        bool drain, copy;
    } cases[] = {
        {1, 0, 0, 100, 0, 0, false, true},
        {0, 0, 0x0100, 100, 0, 1, false, true}, /* blocking, second poll */
        {1, 0x050102, 0, 1060, 0x3c0000, 5, true, true}, /* buffered wins */
        {1, 0x0400, 0, 100, 0, 4, false, false},
        {1, 0x010000, 0, 100, 0, 1, false, false},
        {0, 0x0102, 0, 1061, 0, UINT32_MAX, false, true},
        {1, 0x0102, 0, 100, 0x3c0001, UINT32_MAX, false, false},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    install_cd_wait_stubs(f, handler);
    MEM_W32(f->vsync + 16, 0xad4c0000); /* VSync publishes all three status bytes */
    MEM_W32(f->ports, port);
    MEM_W32(events, 6);
    MEM_W32(events + 4, 0);
    MEM_W32(f->callbacks + 4, handler);
    MEM_W32(f->callbacks, handler);
    for (unsigned j = 0; j < 16; j++)
        MEM_W8(f->results + 8 + j, (u8)(0x31 + j));
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        memset(nc_fast_ptr(script), 0, 48);
        MEM_W32(script, 100);
        MEM_W32(script + 12, cases[i].frame);
        MEM_W32(script + 16, cases[i].polls);
        MEM_W32(script + 20, cases[i].status);
        MEM_W32(script + 24, 101);
        MEM_W32(script + 32, cases[i].next_status);
        MEM_W8(port, 0xe3);
        memset(nc_fast_ptr(trace), 0, 48);
        memset(nc_fast_ptr(dest), 0xcc, 12);
        CPUState state = arguments(cases[i].mode, cases[i].copy ? dest : 0, 3, i & 1 ? 13 : 4096);
        state.r[8] = script;
        state.r[9] = f->deadline + 4;
        state.r[10] = f->status;
        state.r[13] = trace + 32;
        state.r[14] = cases[i].drain;
        state.r[24] = trace;
        state.r[25] = events;
        bool expired = cases[i].expected == UINT32_MAX;
        bool received = cases[i].expected != 0 && !expired;
        u32 status = cases[i].next_status ? cases[i].next_status : cases[i].status;
        bool buffered = (status & 0xff0000) != 0;
        u32 remaining = received ? status & ~(buffered ? 0xff0000u : 0xff00u) : status;
        ok = compare(state, f->ready, routine, 12) &&
             nc_cpu.r[2] == cases[i].expected && MEM_R32(f->status) == remaining &&
             MEM_R8(port) == (cases[i].drain ? 3 : 0xe3) &&
             MEM_R32(trace + 40) == (expired ? f->ready + 0x128 : 0) && MEM_R8(dest + 8) == 0xcc;
        for (unsigned j = 0; ok && j < 8; j++)
            ok = MEM_R8(dest + j) == (received && cases[i].copy ? (buffered ? 0x39 : 0x31) + j : 0xcc);
        if (cases[i].drain)
            ok = ok && nc_cpu.r[24] == trace + 16 && MEM_R32(trace) == 1 &&
                 MEM_R32(trace + 4) == f->results + 8 && MEM_R32(trace + 8) == 2 &&
                 MEM_R32(trace + 12) == f->results;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_command_sync(const CdEventFixture *f, bool (*routine)(CPUState *)) {
    const u32 script = 0x80061000, dest = script + 64, port = dest + 32;
    const u32 events = port + 16, trace = events + 32, handler = trace + 64;
    const struct {
        u32 mode, status, next_status, frame, polls, callbacks, expected;
        bool drain, copy;
    } cases[] = {
        {1, 1, 0, 100, 0, 0, 0, false, true}, /* still pending */
        {0, 1, 2, 100, 0, 0, 2, false, true}, /* completes on second poll */
        {1, 5, 0, 1060, 0x3c0000, 3, 5, true, true}, /* both limits inclusive */
        {1, 2, 0, 100, 0, 0, 2, true, false},
        {0, 1, 0, 1061, 0, 0, UINT32_MAX, false, true},
        {1, 1, 0, 100, 0x3c0001, 0, UINT32_MAX, false, false},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    install_cd_wait_stubs(f, handler);
    MEM_W32(f->ports, port);
    MEM_W8(f->status + 1, 1);
    MEM_W32(events, 6);
    MEM_W32(events + 4, 1);
    MEM_W32(events + 8, 0);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        MEM_W32(script, 100);
        MEM_W32(script + 12, cases[i].frame);
        MEM_W32(script + 16, cases[i].polls);
        MEM_W32(script + 20, cases[i].status);
        MEM_W32(script + 24, 101);
        MEM_W32(script + 28, 1);
        MEM_W32(script + 32, cases[i].next_status);
        MEM_W32(f->callbacks + 4, cases[i].callbacks & 1 ? handler : 0);
        MEM_W32(f->callbacks, cases[i].callbacks & 2 ? handler : 0);
        MEM_W8(port, 0xe3);
        memset(nc_fast_ptr(trace), 0, 48);
        memset(nc_fast_ptr(dest), 0xcc, 12);
        for (unsigned j = 0; j < 8; j++)
            MEM_W8(f->results + j, (u8)(0x31 + j));
        CPUState state = arguments(cases[i].mode, cases[i].copy ? dest : 0, 3, i & 1 ? 12 : 4096);
        state.r[8] = script;
        state.r[9] = f->deadline + 4;
        state.r[10] = f->status;
        state.r[13] = trace + 32;
        state.r[14] = cases[i].drain;
        state.r[24] = trace;
        state.r[25] = events;
        bool expired = cases[i].expected == UINT32_MAX;
        bool complete = cases[i].expected == 2 || cases[i].expected == 5;
        ok = compare(state, f->sync, routine, 11) &&
             nc_cpu.r[2] == cases[i].expected && MEM_R32(f->deadline) == 1060 &&
             MEM_R8(f->status) == (complete ? 2u : cases[i].status) &&
             MEM_R8(port) == (cases[i].drain ? 3 : 0xe3) &&
             MEM_R32(trace + 40) == (expired ? f->sync + 0x120 : 0) && MEM_R8(dest + 8) == 0xcc;
        for (unsigned j = 0; ok && j < 8; j++)
            ok = MEM_R8(dest + j) == (complete && cases[i].copy ? 0x31 + j : 0xcc);
        if (cases[i].callbacks)
            ok = ok && nc_cpu.r[24] == trace + 16 && MEM_R32(trace) == 1 &&
                 MEM_R32(trace + 4) == f->results + 8 && MEM_R32(trace + 8) == 5 &&
                 MEM_R32(trace + 12) == f->results;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_write_cd_command(const CdEventFixture *f, bool (*routine)(CPUState *)) {
    const u32 script = 0x80061000, params = script + 64, dest = params + 16;
    const u32 port = dest + 16, events = port + 16, trace = events + 32, handler = trace + 64;
    enum { ASYNC, ALREADY_ACKED, CALLBACK_ACK, DISK_ERROR, FRAME_TIMEOUT, POLL_TIMEOUT };
    const struct { u32 command, debug; unsigned path; bool params, copy; } cases[] = {
        {2, 0, ASYNC, false, false}, {14, 2, ASYNC, false, false},
        {0x102, 2, ASYNC, true, true}, {14, 0, ASYNC, true, false},
        {3, 0, ALREADY_ACKED, false, true}, {2, 0, CALLBACK_ACK, true, true},
        {1, 0, DISK_ERROR, false, false}, {1, 0, FRAME_TIMEOUT, false, true},
        {1, 0, POLL_TIMEOUT, false, true},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    install_cd_wait_stubs(f, handler);
    MEM_W32(f->sync, 0x03e00008); /* CD_sync counts submissions in t7 */
    MEM_W32(f->sync + 4, 0x25ef0001);
    MEM_W32(f->ports, port);
    MEM_W32(f->ports + 4, port + 1);
    MEM_W32(f->ports + 8, port + 2);
    MEM_W32(f->callbacks + 4, handler);
    MEM_W32(f->callbacks, handler);
    MEM_W32(events, 6);
    MEM_W32(events + 4, 0);
    MEM_W32(params, 0x78563412);
    for (unsigned j = 0; j < 8; j++)
        MEM_W8(f->results + j, (u8)(0x41 + j));
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        unsigned path = cases[i].path, cmd = cases[i].command & 0xff;
        bool invalid = !cases[i].params && (cmd == 2 || cmd == 14);
        bool timeout = path == FRAME_TIMEOUT || path == POLL_TIMEOUT;
        bool received = !invalid && !timeout && path != ASYNC;
        memset(nc_fast_ptr(script), 0, 36);
        MEM_W32(script, 100);
        MEM_W32(script + 8, path == ALREADY_ACKED ? 3 : 0);
        MEM_W32(script + 12, path == FRAME_TIMEOUT ? 1061 : 100);
        MEM_W32(script + 16, path == POLL_TIMEOUT ? 0x3c0001 : 0);
        MEM_W32(script + 20, path == DISK_ERROR ? 5 : 3);
        MEM_W32(f->callbacks + 12, cases[i].debug);
        MEM_W32(f->callbacks + 28, 0xffffffff);
        MEM_W8(f->callbacks + 32, 0xee);
        MEM_W8(f->callbacks + 33, 0xee);
        MEM_W8(f->status + 1, 1);
        memset(nc_fast_ptr(port), 0xee, 4);
        memset(nc_fast_ptr(dest), 0xcc, 12);
        memset(nc_fast_ptr(trace), 0, 48);
        CPUState state = arguments(cases[i].command, cases[i].params ? params : 0,
                                   cases[i].copy ? dest : 0, i & 1 ? 2 : 8192);
        state.r[7] = path == ASYNC;
        state.r[8] = script;
        state.r[9] = f->deadline + 4;
        state.r[10] = f->status;
        state.r[13] = trace + 32;
        state.r[14] = path == CALLBACK_ACK;
        state.r[15] = 0;
        state.r[24] = trace;
        state.r[25] = events;
        u32 expected = invalid ? (u32)-2 : timeout || path == DISK_ERROR ? UINT32_MAX : 0;
        ok = compare(state, f->writer, routine, 1) &&
             nc_cpu.r[2] == expected && nc_cpu.r[15] == (u32)!invalid &&
             MEM_R8(port + 1) == (invalid ? 0xee : cmd) &&
             MEM_R8(f->callbacks + 33) == (invalid ? 0xee : cmd) &&
             MEM_R32(f->callbacks + 28) == (cmd == 2 && !invalid ? 0x78563412u : UINT32_MAX) &&
             MEM_R8(f->callbacks + 32) == (cmd == 14 && !invalid ? 0x12 : 0xee) &&
             MEM_R8(port + 2) == (invalid ? 0xee : cmd == 2 ? 0x56 : cmd == 14 ? 0x12 : 0xee) &&
             MEM_R8(f->status + 1) == (cmd == 3 ? 0 : 1) &&
             MEM_R32(trace + 40) == (timeout ? f->writer + 0x2c8 : 0);
        for (unsigned j = 0; ok && j < 9; j++)
            ok = MEM_R8(dest + j) == (received && cases[i].copy && j < 8 ? 0x41 + j : 0xcc);
        if (path == CALLBACK_ACK)
            ok = ok && nc_cpu.r[24] == trace + 16 && MEM_R32(trace) == 1 &&
                 MEM_R32(trace + 4) == f->results + 8 && MEM_R32(trace + 8) == 3 &&
                 MEM_R32(trace + 12) == f->results;
        if (!ok)
            fprintf(stderr, "CD_cw case %u failed\n", i);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
