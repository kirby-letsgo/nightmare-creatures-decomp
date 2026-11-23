#ifndef NC_DECOMP_JUMP_CONTEXT_H
#define NC_DECOMP_JUMP_CONTEXT_H

#include "decomp/interrupt_control.h"
#include "port/interp.h"

/* startIntr uses setjmp to build the BIOS interrupt entry context. */
static inline bool save_irq_context(CPUState *c) {
    if (c->budget <= 15)
        return false;
    u32 buffer = R(4);
    MEM_W32(buffer, R(31));
    c->budget--;
    MEM_W32(buffer + 44, R(28));
    c->budget--;
    MEM_W32(buffer + 4, R(29));
    c->budget--;
    MEM_W32(buffer + 8, R(30));
    c->budget--;
    for (unsigned i = 0; i < 8; i++) {
        MEM_W32(buffer + 12 + 4 * i, R(16 + i));
        c->budget--;
    }
    R(2) = 0;
    c->budget -= 3;
    return true;
}

/* Psy-Q longjmp accepts zero as a return value. The restored ra is the
 * continuation PC, which can differ from the return address of this call. */
static inline void restore_jump_context(CPUState *c) {
    u32 return_pc = R(31), return_sp = R(29);
    u32 buffer = R(4);
    load_word(c, 31, buffer);
    load_word(c, 28, buffer + 44);
    load_word(c, 29, buffer + 4);
    load_word(c, 30, buffer + 8);
    for (unsigned reg = 16; reg <= 23; reg++)
        load_word(c, reg, buffer + 12 + 4 * (reg - 16));
    set_reg(c, 2, R(5));
    u32 resume_pc = R(31);
    NC_POLL(c, 2); /* jr ra and its nop delay slot */
    interp_continue_call(c, resume_pc, return_pc, return_sp);
}

#endif
