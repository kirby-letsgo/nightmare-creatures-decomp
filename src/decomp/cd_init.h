#ifndef NC_DECOMP_CD_INIT_H
#define NC_DECOMP_CD_INIT_H

#include "decomp/interrupt_control.h"

typedef struct {
    u32 entry, reset;
    u32 sync_handler, set_sync;
    u32 ready_handler, set_ready;
    u32 read_handler, set_read;
    u32 message, print;
} CdInitLayout;

/* Materialize a pointer argument before calling an SDK routine. */
static inline void cd_call_with_pointer(CPUState *c, u32 argument, u32 target, u32 resume) {
    set_reg(c, 4, (argument + 0x8000u) & 0xffff0000u);
    set_reg(c, 4, argument);
    R(31) = resume;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, target);
}

/* Try CdReset(1) up to five times before reporting initialization failure. */
static inline bool cd_init(CPUState *c, const CdInitLayout *layout) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = 4;
    c->budget -= 2;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    for (;;) {
        R(31) = layout->entry + 0x18;
        R(4) = 1;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, layout->reset);
        set_reg(c, 3, 1);
        const bool ready = R(2) == R(3);
        R(16)--;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (ready) {
            cd_call_with_pointer(c, layout->sync_handler, layout->set_sync, layout->entry + 0x34); /* sync */
            cd_call_with_pointer(c, layout->ready_handler, layout->set_ready, layout->entry + 0x44); /* ready */
            cd_call_with_pointer(c, layout->read_handler, layout->set_read, layout->entry + 0x54); /* read */
            R(2) = 1;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            break;
        }
        set_reg(c, 2, UINT32_MAX);
        const bool exhausted = R(16) == R(2);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (exhausted) {
            cd_call_with_pointer(c, layout->message, layout->print, layout->entry + 0x78); /* printf */
            set_reg(c, 2, 0);
            break;
        }
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

#endif
