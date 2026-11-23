#ifndef NC_DECOMP_SDK_PRINTF_H
#define NC_DECOMP_SDK_PRINTF_H

#include "decomp/interrupt_control.h"

/* Pack the variadic register arguments into the caller's argument area and
 * call the guest formatter with stdout, the format and the argument cursor. */
static inline bool sdk_printf(CPUState *c, u32 formatter, u32 resume) {
    if (c->budget <= 11)
        return false;
    R(29) -= 0x18;
    R(2) = R(4);
    R(4) = 1;
    MEM_W32(R(29) + 0x1c, R(5));
    R(5) = R(2);
    MEM_W32(R(29) + 0x20, R(6));
    R(6) = R(29) + 0x1c;
    MEM_W32(R(29) + 0x10, R(31));
    MEM_W32(R(29) + 0x18, R(2));
    R(31) = resume;
    MEM_W32(R(29) + 0x24, R(7));
    c->budget -= 11;
    nc_call(c, formatter);
    load_word(c, 31, R(29) + 0x10);
    set_reg(c, 29, R(29) + 0x18);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

#endif
