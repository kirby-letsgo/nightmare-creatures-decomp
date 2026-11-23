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

static bool compare_set_interrupt_handler(u32 entry, bool (*routine)(CPUState *), u32 state) {
    const struct {
        unsigned irq;
        u32 old_handler, new_handler;
        bool active;
    } cases[] = {
        {0, 0, 0x8001935c, true}, {0, 0x8001935c, 0, true},
        {4, 0, 0x8001935c, true}, {5, 0x8001935c, 0, true},
        {6, 0x8001935c, 0x800198ec, true}, {2, 0, 0x8001935c, true},
        {2, 0x8001935c, 0x8001935c, true}, {2, 0x8001935c, 0, false},
    };
    StateIO saved = {0};
    interrupt_start_state(&saved);
    bios_serialize(&saved);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        StateIO native = {0}, guest = {0};
        u32 slot = state + 4 + 4 * cases[i].irq;
        u16 bit = 1u << cases[i].irq;
        u16 hardware = 0x280 | (cases[i].old_handler ? bit : 0);
        u16 registered = 0x108 | (cases[i].old_handler ? bit : 0);
        bool change = cases[i].active && cases[i].old_handler != cases[i].new_handler;
        for (unsigned run = 0; ok && run < 2; run++) {
            MEM_W16(state, cases[i].active);
            MEM_W32(slot, cases[i].old_handler);
            MEM_W16(state + 0x30, registered);
            MEM_W16(0x1f801074, hardware);
            nc_cpu = arguments(cases[i].irq, cases[i].new_handler, 3, 19);
            if (!run && routine(&nc_cpu))
                ok = false;
            nc_cpu.budget = i & 1 ? 4096 : 20;
            if (run)
                interp_call(&nc_cpu, entry);
            else
                ok = ok && routine(&nc_cpu);
            u16 enabled = cases[i].new_handler ? bit : 0;
            ok = ok && nc_cpu.r[2] == cases[i].old_handler &&
                 MEM_R32(slot) == (change ? cases[i].new_handler : cases[i].old_handler) &&
                 MEM_R16(state + 0x30) == (change ? ((registered & ~bit) | enabled) : registered) &&
                 MEM_R16(0x1f801074) == (change ? ((hardware & ~bit) | enabled) : hardware);
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
