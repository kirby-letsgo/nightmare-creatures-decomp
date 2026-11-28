#ifndef NC_DECOMP_SDK_PAD_INIT_H
#define NC_DECOMP_SDK_PAD_INIT_H

#include "decomp/interrupt_control.h"

/* InitPAD and PAD_init preserve both buffer/size pairs across _patch_pad and
 * ChangeClearPad(0), then pass them to their BIOS stub. */
static inline bool sdk_pad_init(CPUState *c, u32 entry, u32 patch_pad, u32 clear_pad,
                                u32 bios_stub) {
    if (c->budget <= 11)
        return false;
    R(29) -= 40;
    c->budget--;
    for (unsigned i = 0; i < 3; i++) {
        MEM_W32(R(29) + 16 + 4 * i, R(16 + i));
        R(16 + i) = R(4 + i);
        c->budget -= 2;
    }
    MEM_W32(R(29) + 28, R(19));
    c->budget--;
    MEM_W32(R(29) + 32, R(31));
    R(31) = entry + 0x2c;
    R(19) = R(7);
    c->budget -= 3;
    nc_call(c, patch_pad);
    R(31) = entry + 0x34;
    R(4) = 0;
    NC_POLL(c, 2);
    nc_call(c, clear_pad);
    for (unsigned i = 0; i < 3; i++)
        set_reg(c, 4 + i, R(16 + i));
    R(31) = entry + 0x48;
    R(7) = R(19);
    NC_POLL(c, 2);
    nc_call(c, bios_stub);
    load_word(c, 31, R(29) + 32);
    for (int i = 3; i >= 0; i--)
        load_word(c, 16 + i, R(29) + 16 + 4 * i);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2);
    return true;
}

#endif
