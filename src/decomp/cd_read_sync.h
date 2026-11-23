#ifndef NC_DECOMP_CD_READ_SYNC_H
#define NC_DECOMP_CD_READ_SYNC_H

#include "decomp/interrupt_control.h"

typedef struct {
    u32 entry, start_frame;
    u32 vsync, retry, ready;
} CdReadSyncLayout;

/* Compare the current frame with the signed, wrapping SDK deadline in v1. */
static inline bool cd_read_deadline(CPUState *c, u32 vsync, u32 resume, u32 offset, u32 frames) {
    R(31) = resume;
    R(4) = UINT32_MAX;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, vsync); /* VSync(-1) */
    load_word(c, 3, R(17) + offset);
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 3, R(3) + frames);
    set_reg(c, 3, (s32)R(3) < (s32)R(2));
    return R(3) != 0;
}

/* Poll or wait for the sector read, retrying stalled reads within the overall timeout. */
static inline bool cd_read_sync(CPUState *c, const CdReadSyncLayout *layout) {
    if (c->budget <= 10)
        return false;
    R(29) -= 40;
    c->budget--;
    MEM_W32(R(29) + 24, R(18));
    R(18) = R(4); /* wait mode */
    c->budget -= 2;
    MEM_W32(R(29) + 28, R(19));
    R(19) = R(5); /* optional response buffer */
    c->budget -= 2;
    MEM_W32(R(29) + 20, R(17));
    R(17) = layout->start_frame;
    c->budget -= 3;
    MEM_W32(R(29) + 32, R(31));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    for (;;) {
        const bool timed_out = cd_read_deadline(c, layout->vsync, layout->entry + 0x30, 0, 1200);
        R(16) = UINT32_MAX;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (!timed_out) {
            load_word(c, 2, R(17) - 8);
            NC_POLL(c, NC_INSN_CYCLES);
            bool retry = (s32)R(2) < 0;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            if (!retry) {
                retry = cd_read_deadline(c, layout->vsync, layout->entry + 0x60, -4u, 60);
                NC_POLL(c, 2 * NC_INSN_CYCLES);
            }
            if (retry) {
                R(31) = layout->entry + 0x80;
                R(4) = 1;
                NC_POLL(c, 2 * NC_INSN_CYCLES);
                nc_call(c, layout->retry); /* cd_read_retry */
                load_word(c, 16, R(17) - 28);
                NC_POLL(c, 2 * NC_INSN_CYCLES);
            } else {
                load_word(c, 16, R(17) - 8);
            }
        }
        const bool polling = R(18) != 0;
        R(4) = 1;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (polling)
            break;
        const bool pending = (s32)R(16) > 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (!pending)
            break;
    }
    R(31) = layout->entry + 0xa8;
    R(5) = R(19);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, layout->ready); /* CdReady */
    set_reg(c, 2, R(16));
    load_word(c, 31, R(29) + 32);
    load_word(c, 19, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

#endif
