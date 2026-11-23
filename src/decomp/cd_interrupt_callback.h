#ifndef NC_DECOMP_CD_INTERRUPT_CALLBACK_H
#define NC_DECOMP_CD_INTERRUPT_CALLBACK_H

#include "decomp/interrupt_control.h"

typedef struct {
    u32 entry, getintr, ports, callbacks, results;
} CdCallbackLayout;

/* Drain CD interrupts and dispatch ready before sync when both flags are set. */
static inline bool cd_interrupt_callback(CPUState *c, const CdCallbackLayout *layout) {
    const u32 upper = (layout->ports + 0x8000u) & 0xffff0000u;
    if (c->budget <= 9 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES)
        return false;
    c->budget--;
    R(2) = MEM_R32(layout->ports);
    R(29) -= 32;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 28, R(31));
    c->budget--;
    MEM_W32(R(29) + 24, R(18));
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    R(2) = MEM_R8(R(2));
    R(18) = layout->ports + 25;
    R(17) = R(2) & 3; /* original CD register bank */
    c->budget -= NC_LOAD_CYCLES + 3;

    for (;;) {
        R(31) = layout->entry + 0x34;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, layout->getintr); /* getintr */
        set_reg(c, 16, R(2));
        const bool pending = R(16) != 0;
        R(2) = R(16) & 4;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (!pending)
            break;
        const bool ready = R(2) != 0;
        R(2) = R(16) & 2;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (ready) {
            set_reg(c, 2, upper);
            load_word(c, 2, layout->callbacks + 4);
            NC_POLL(c, NC_INSN_CYCLES);
            const bool registered = R(2) != 0;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            if (registered) {
                R(4) = MEM_R8(R(18));
                NC_POLL(c, NC_LOAD_CYCLES);
                set_reg(c, 5, upper);
                set_reg(c, 5, layout->results + 8);
                u32 handler = R(2);
                R(31) = layout->entry + 0x70;
                NC_POLL(c, 2 * NC_INSN_CYCLES);
                nc_call(c, handler);
            }
            set_reg(c, 2, R(16) & 2);
        }
        const bool sync = R(2) != 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (!sync)
            continue;
        set_reg(c, 3, upper);
        load_word(c, 3, layout->callbacks);
        NC_POLL(c, NC_INSN_CYCLES);
        const bool registered = R(3) != 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (registered) {
            set_reg(c, 2, upper);
            set_reg(c, 2, layout->ports + 24);
            R(4) = MEM_R8(R(2));
            NC_POLL(c, NC_LOAD_CYCLES);
            set_reg(c, 5, upper);
            set_reg(c, 5, layout->results);
            u32 handler = R(3);
            R(31) = layout->entry + 0xac;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, handler);
            NC_POLL(c, 2 * NC_INSN_CYCLES);
        }
    }
    set_reg(c, 2, upper);
    load_word(c, 2, layout->ports);
    NC_POLL(c, NC_INSN_CYCLES);
    MEM_W8(R(2), (u8)R(17));
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 31, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

#endif
