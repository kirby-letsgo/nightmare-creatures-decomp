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

typedef struct {
    u32 entry, ports, callbacks, puts, print;
    u32 reset, install, handler, command, sync;
} CdDriverInitFixture;

static bool compare_cd_driver_init(const CdDriverInitFixture *f, bool (*routine)(CPUState *)) {
    const u32 ports = 0x80061000, trace = ports + 0x100, command = f->command;
    const struct {
        bool pending, lid_open;
        u32 fail_command, sync_result;
    } cases[] = {
        {false, false, 0, 2}, {true, true, 0, 2}, {false, true, 10, 2},
        {true, false, 12, 2}, {false, false, 0, 5},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    const u32 stubs[] = {f->puts, f->print, f->reset, f->install};
    for (unsigned i = 0; i < 4; i++) {
        MEM_W32(stubs[i], 0x03e00008);
        MEM_W32(stubs[i] + 4, 0);
    }
    MEM_W32(f->install, 0xad040020);     /* record IRQ index and handler */
    MEM_W32(f->install + 4, 0x03e00008);
    MEM_W32(f->install + 8, 0xad050024);
    MEM_W32(command, 0xad040000);        /* sw a0,0(t0): command log */
    MEM_W32(command + 4, 0x25080004);
    MEM_W32(command + 8, 0xad2a0000);    /* sw t2,0(t1): controller status */
    MEM_W32(command + 12, 0x008b1026);   /* xor v0,a0,t3 */
    MEM_W32(command + 16, 0x03e00008);
    MEM_W32(command + 20, 0x2c420001);   /* fail the selected command */
    MEM_W32(f->sync, 0x03e00008);
    MEM_W32(f->sync + 4, 0x01801021);     /* return scripted sync result */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        memset(nc_fast_ptr(ports), 0, 0x140);
        MEM_W32(f->ports, ports + 16);
        MEM_W32(f->ports + 12, ports);
        /* Redirect the next status read to a cleared byte when the loop
         * writes the interrupt enable port, modelling a completed ACK. */
        MEM_W32(f->ports + 8, cases[i].pending ? f->ports + 12 : ports + 8);
        MEM_W32(f->ports + 16, ports + 32);
        MEM_W8(ports, cases[i].pending ? 3 : 0);
        MEM_W32(f->ports + 24, UINT32_MAX);
        MEM_W32(f->callbacks + 0x20, UINT32_MAX);
        MEM_W32(f->callbacks + 4, UINT32_MAX);
        MEM_W32(f->callbacks, UINT32_MAX);
        MEM_W32(f->callbacks + 20, UINT32_MAX);
        MEM_W32(f->callbacks + 16, UINT32_MAX);
        CPUState state = arguments(7, 8, 9, i & 1 ? 5 : 4096);
        state.r[8] = trace;
        state.r[9] = f->callbacks + 16;
        state.r[10] = cases[i].lid_open ? 16 : 0;
        state.r[11] = cases[i].fail_command;
        state.r[12] = cases[i].sync_result;
        ok = compare(state, f->entry, routine, 4) &&
             nc_cpu.r[2] == (cases[i].fail_command || cases[i].sync_result != 2 ? UINT32_MAX : 0) &&
             MEM_R32(trace + 32) == 2 && MEM_R32(trace + 36) == f->handler &&
             MEM_R32(ports + 32) == 0x1325 && MEM_R8(ports) == (cases[i].pending ? 7 : 0) &&
             MEM_R32(f->ports + 24) == 0xff000002 && MEM_R32(f->callbacks + 0x20) == 0xffff0000 &&
             MEM_R32(f->callbacks + 4) == 0 && MEM_R32(f->callbacks) == 0 && MEM_R32(f->callbacks + 20) == 0;
        u32 cursor = trace;
        ok = ok && MEM_R32(cursor) == 1;
        cursor += 4;
        if (cases[i].lid_open) {
            ok = ok && MEM_R32(cursor) == 1;
            cursor += 4;
        }
        ok = ok && MEM_R32(cursor) == 10;
        cursor += 4;
        if (cases[i].fail_command != 10) {
            ok = ok && MEM_R32(cursor) == 12;
            cursor += 4;
        }
        ok = ok && nc_cpu.r[8] == cursor;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
