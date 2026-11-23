#ifndef NC_TESTS_INTERRUPT_START_H
#define NC_TESTS_INTERRUPT_START_H

#include "cd_sector_dma.h"
#include "port/bios.h"

static bool compare_interrupt_start(u32 entry, bool (*routine)(CPUState *), u32 state,
                                    u32 table_slot, u32 vsync_handler, u32 dma_handler) {
    const int threshold = 8 * NC_INSN_CYCLES + NC_LOAD_CYCLES;
    StateIO saved = {0};
    interrupt_start_state(&saved);
    bios_serialize(&saved);
    bool ok = !saved.error;
    for (unsigned active = 0; ok && active < 2; active++) {
        StateIO native = {0}, guest = {0};
        for (unsigned run = 0; ok && run < 2; run++) {
            MEM_W16(state, active);
            nc_cpu = arguments(1, 2, 3, threshold);
            if (!run && routine(&nc_cpu))
                ok = false;
            nc_cpu.budget = active ? 4096 : threshold + 1;
            if (run)
                interp_call(&nc_cpu, entry);
            else
                ok = ok && routine(&nc_cpu);
            ok = ok && nc_cpu.r[2] == (active ? 0 : state);
            if (!active) {
                u32 table = MEM_R32(table_slot);
                ok = ok && MEM_R16(state) == 1 && MEM_R32(state + 0x38) == entry + 0x68 &&
                     MEM_R32(state + 0x3c) == state + 0x1018 && MEM_R32(table + 20) == vsync_handler &&
                     MEM_R32(table + 4) == dma_handler;
            }
            StateIO *result = run ? &guest : &native;
            interrupt_start_state(result);
            bios_serialize(result);
            saved.loading = true;
            saved.pos = 0;
            interrupt_start_state(&saved);
            bios_serialize(&saved);
        }
        ok = ok && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    free(saved.buf);
    return ok && !saved.error;
}

#endif
