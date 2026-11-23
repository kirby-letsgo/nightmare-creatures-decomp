#ifndef NC_DECOMP_VBLANK_WAIT_H
#define NC_DECOMP_VBLANK_WAIT_H

#include "decomp/interrupt_control.h"

typedef struct {
    u32 entry, frame, message;
    u32 puts_entry, clear_pad, clear_rcnt;
} VBlankWaitLayout;

/* Test the signed VBlank count against the requested frame. */
static inline void compare_vblank_target(CPUState *c, u32 frame) {
    set_reg(c, 2, (frame + 0x8000u) & 0xffff0000u);
    load_word(c, 2, frame);
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, (s32)R(2) < (s32)R(4));
}

static inline bool wait_vblank(CPUState *c, const VBlankWaitLayout *layout) {
    if (c->budget <= 3)
        return false;
    R(29) -= 32;
    R(5) <<= 15;
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(5));
    c->budget--;
    compare_vblank_target(c, layout->frame);
    bool waiting = R(2) != 0;
    MEM_W32(R(29) + 24, R(31));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (waiting)
        set_reg(c, 3, UINT32_MAX);
    while (waiting) {
        load_word(c, 2, R(29) + 16);
        NC_POLL(c, NC_INSN_CYCLES);
        set_reg(c, 2, R(2) - 1);
        MEM_W32(R(29) + 16, R(2));
        NC_POLL(c, NC_INSN_CYCLES);
        load_word(c, 2, R(29) + 16);
        NC_POLL(c, NC_INSN_CYCLES);
        const bool timed_out = R(2) == R(3);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (timed_out) {
            set_reg(c, 4, (layout->message + 0x8000u) & 0xffff0000u);
            set_reg(c, 4, layout->message);
            R(31) = layout->entry + 0x58;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, layout->puts_entry); /* puts */
            R(31) = layout->entry + 0x60;
            R(4) = 0;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, layout->clear_pad); /* ChangeClearPad */
            set_reg(c, 4, 3);
            R(31) = layout->entry + 0x6c;
            R(5) = 0;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, layout->clear_rcnt); /* ChangeClearRCnt */
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            break;
        }
        compare_vblank_target(c, layout->frame);
        waiting = R(2) != 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    }
    load_word(c, 31, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

#endif
