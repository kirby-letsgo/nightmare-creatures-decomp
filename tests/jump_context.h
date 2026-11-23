#ifndef NC_TESTS_JUMP_CONTEXT_H
#define NC_TESTS_JUMP_CONTEXT_H

#include "credits_compare.h"

static CPUState arguments(u32 destination, u32 source, u32 count, int budget) {
    CPUState state = {0};
    for (unsigned i = 1; i < 32; i++)
        state.r[i] = 0x468b7u * i;
    state.r[4] = destination;
    state.r[5] = source;
    state.r[6] = count;
    state.r[28] = 0x80024c68;
    state.r[29] = 0x801fef00;
    state.r[31] = 0x80010000;
    state.budget = budget;
    return state;
}

static bool check_restore_jump_context(u32 entry, void (*restore)(CPUState *)) {
    const u32 buffers[] = {0x80061004, 0xa0061004, SCRATCH_BASE + 4};
    const u32 values[] = {0, 1, UINT32_MAX};
    const u32 continuation = 0x80062000, trace = 0x80062100;
    StateIO saved = {0};
    cpu_serialize(&saved);
    /* Observe the restored frame before the continuation rejoins the caller.
     * sw ra,0(a2); sw sp,4(a2); move sp,t0; jr t1; sw v0,8(a2) */
    const u32 program[] = {0xacdf0000, 0xacdd0004, 0x0100e821, 0x01200008, 0xacc20008};
    for (unsigned j = 0; j < sizeof program / sizeof program[0]; j++)
        MEM_W32(continuation + 4 * j, program[j]);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof buffers / sizeof buffers[0]; i++) {
        u32 buffer = buffers[i];
        CPUState state = arguments(buffer, values[i], trace, i == 0 ? 4096 : 1);
        state.r[8] = state.r[29];
        state.r[9] = state.r[31];
        bool jump = i != 0;
        u32 resume = jump ? continuation : state.r[31];
        u32 stack = jump ? state.r[29] + 64 : state.r[29];
        MEM_W32(buffer, resume);
        MEM_W32(buffer + 4, stack);
        for (unsigned word = 2; word < 12; word++)
            MEM_W32(buffer + word * 4, 0x81234000 + word);
        memset(nc_fast_ptr(trace), 0xcc, 12);
        StateIO initial = {0}, native = {0}, guest = {0};
        nc_cpu = state;
        cpu_serialize(&initial);
        restore(&nc_cpu);
        cpu_serialize(&native);
        initial.loading = true;
        cpu_serialize(&initial);
        interp_call(&nc_cpu, entry);
        cpu_serialize(&guest);
        ok = !initial.error && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, native.len) && nc_cpu.r[2] == values[i] &&
             nc_cpu.r[31] == resume && nc_cpu.r[28] == 0x8123400b &&
             nc_cpu.r[30] == 0x81234002 && MEM_R32(trace) == (jump ? resume : 0xcccccccc) &&
             MEM_R32(trace + 4) == (jump ? stack : 0xcccccccc) &&
             MEM_R32(trace + 8) == (jump ? values[i] : 0xcccccccc);
        for (unsigned reg = 16; ok && reg <= 23; reg++)
            ok = nc_cpu.r[reg] == 0x81234003 + reg - 16;
        free(initial.buf);
        free(native.buf);
        free(guest.buf);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    return ok && !saved.error;
}

static bool check_save_irq_context(u32 entry, bool (*save)(CPUState *)) {
    const u32 buffers[] = {0x80061004, 0xa0061004, SCRATCH_BASE + 4};
    const unsigned registers[] = {31, 29, 30, 16, 17, 18, 19, 20, 21, 22, 23, 28};
    for (unsigned i = 0; i < sizeof buffers / sizeof buffers[0]; i++) {
        u32 buffer = buffers[i];
        memset(nc_fast_ptr(buffer - 4), 0xa5, 56);
        CPUState state = arguments(buffer, 0, 0, i == 0 ? 16 : 4096);
        if (!compare(state, entry, save, 15) ||
            nc_cpu.r[2] != 0 || MEM_R32(buffer - 4) != 0xa5a5a5a5 ||
            MEM_R32(buffer + 48) != 0xa5a5a5a5)
            return false;
        for (unsigned word = 0; word < sizeof registers / sizeof registers[0]; word++)
            if (MEM_R32(buffer + 4 * word) != state.r[registers[word]])
                return false;
    }
    return true;
}

#endif
