#ifndef NC_DECOMP_SDK_PUTS_H
#define NC_DECOMP_SDK_PUTS_H

#include "decomp/interrupt_control.h"

/* The SDK puts emits through putchar and uses "<NULL>" for a null pointer. */
static inline bool sdk_puts(CPUState *c, u32 null_string, u32 put_char, u32 resume) {
    if (c->budget <= 9)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 20, R(31));
    c->budget -= 2;
    if (!R(16)) {
        R(16) = null_string;
        c->budget -= 4;
    }
    for (;;) {
        R(4) = MEM_R8(R(16));
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        bool at_end = R(4) == 0;
        R(16)++;
        NC_POLL(c, 2);
        if (at_end)
            break;
        R(31) = resume;
        NC_POLL(c, 2);
        nc_call(c, put_char); /* putchar */
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

#endif
