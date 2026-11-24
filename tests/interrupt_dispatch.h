#ifndef NC_TESTS_INTERRUPT_DISPATCH_H
#define NC_TESTS_INTERRUPT_DISPATCH_H

#include "cd_sector_dma.h"

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

static bool compare_dma_interrupt_dispatch(u32 entry, bool (*routine)(CPUState *), u32 slots,
                                           u32 status_message, u32 channel_message, u32 print) {
    const u32 trace = 0x80061000, callback = trace + 0x100, dicr = 0x1f8010f4;
    StateIO saved = {0};
    interrupt_start_state(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    const int entry_cycles = 14 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    CPUState short_budget = arguments(1, 2, 3, entry_cycles);
    CPUState before = short_budget;
    bool ok = !saved.error && !routine(&short_budget) &&
              !memcmp(&short_budget, &before, sizeof before);
    for (unsigned scenario = 0; ok && scenario < 5; scenario++) {
        StateIO native = {0}, guest = {0};
        for (unsigned run = 0; ok && run < 2; run++) {
            /* Channel six submits a second zero-length GPU DMA in its delay
             * slot. The handler must resample DICR after its first pass. */
            memset(nc_fast_ptr(slots), 0, 7 * 4);
            MEM_W32(slots + 24, callback);
            if (scenario == 1)
                MEM_W32(slots + 8, callback + 16);
            MEM_W32(callback, 0xad100000);      /* sw s0,0(t0) */
            MEM_W32(callback + 4, 0x25080004);  /* addiu t0,t0,4 */
            MEM_W32(callback + 8, 0x03e00008);  /* jr ra */
            MEM_W32(callback + 12, 0xad2a0000); /* sw t2,0(t1): start DMA2 */
            MEM_W32(callback + 16, 0xad100000);
            MEM_W32(callback + 20, 0x03e00008);
            MEM_W32(callback + 24, 0x25080004);
            MEM_W32(print, 0xad040000); /* record printf format and arguments */
            MEM_W32(print + 4, 0xad050004);
            MEM_W32(print + 8, 0xad060008);
            MEM_W32(print + 12, 0x03e00008);
            MEM_W32(print + 16, 0x2508000c);
            bool diagnostic = scenario >= 3;
            if (diagnostic) {
                MEM_W32(slots - 4, callback + 32);
                MEM_W32(callback + 32, scenario == 3 ? 0x80000000 : 0x8000);
                MEM_W32(slots + 0x20, callback + 64);
                for (unsigned i = 0; i < 7; i++)
                    MEM_W32(callback + 64 + i * 16, 0x12340000 + i);
            } else {
                MEM_W32(slots - 4, dicr);
                MEM_W32(dicr, 0x7fc40000); /* enable channels two and six, clear flags */
                if (scenario) {
                    MEM_W32(0x1f8010a4, 0);
                    MEM_W32(0x1f8010a8, 0x01000200);
                    MEM_W32(0x1f8010e0, 0x62000);
                    MEM_W32(0x1f8010e4, 1);
                    MEM_W32(0x1f8010e8, 0x01000002);
                }
            }
            nc_cpu = arguments(1, 2, 3, scenario & 1 ? entry_cycles + 1 : 4096);
            nc_cpu.r[8] = trace;
            nc_cpu.r[9] = 0x1f8010a8;
            nc_cpu.r[10] = 0x01000200;
            if (run)
                interp_call(&nc_cpu, entry);
            else
                ok = routine(&nc_cpu);
            if (diagnostic) {
                ok = ok && nc_cpu.r[8] == trace + 96 && MEM_R32(trace) == status_message &&
                     MEM_R32(trace + 4) == MEM_R32(callback + 32);
                for (unsigned i = 0; ok && i < 7; i++) {
                    u32 row = trace + 12 + i * 12;
                    ok = MEM_R32(row) == channel_message && MEM_R32(row + 4) == i &&
                         MEM_R32(row + 8) == 0x12340000 + i;
                }
            } else {
                ok = ok && MEM_R32(dicr) == 0xc40000;
                if (scenario == 1)
                    ok = ok && nc_cpu.r[8] == trace + 12 && MEM_R32(trace) == 2 &&
                         MEM_R32(trace + 4) == 6 && MEM_R32(trace + 8) == 2;
                else if (scenario == 2)
                    ok = ok && nc_cpu.r[8] == trace + 4 && MEM_R32(trace) == 6;
                else
                    ok = ok && nc_cpu.r[8] == trace;
            }
            interrupt_start_state(run ? &guest : &native);
            saved.loading = true;
            saved.pos = 0;
            interrupt_start_state(&saved);
        }
        ok = ok && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}


#endif
