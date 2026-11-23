#ifndef NC_DECOMP_SDK_PUTCHAR_H
#define NC_DECOMP_SDK_PUTCHAR_H

#include "decomp/interrupt_control.h"

/* Expand tabs to eight-column stops and emit a carriage return before a newline. */
static inline bool sdk_putchar(CPUState *c, u32 entry, u32 write_entry, u32 column, u32 classes) {
    const u32 column_hi = (column + 0x8000u) & 0xffff0000u;
    const u32 classes_hi = (classes + 0x8000u) & 0xffff0000u;
    if (c->budget <= 11)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W8(R(29) + 16, (u8)R(4));
    c->budget--;
    R(3) = MEM_R8(R(29) + 16);
    R(2) = 9;
    c->budget -= NC_LOAD_CYCLES + 1;
    bool tab = R(3) == '\t';
    MEM_W32(R(29) + 24, R(31));
    c->budget -= 2;
    if (tab) {
        for (;;) {
            R(31) = entry + 0x44;
            R(4) = ' ';
            NC_POLL(c, 2);
            nc_call(c, entry);
            set_reg(c, 2, column_hi);
            load_word(c, 2, column);
            NC_POLL(c, 1);
            set_reg(c, 2, R(2) & 7);
            bool aligned = R(2) == 0;
            NC_POLL(c, 2);
            if (aligned)
                break;
            NC_POLL(c, 2);
        }
    } else {
        R(2) = '\n';
        c->budget -= 3;
        if (R(3) == '\n') {
            R(31) = entry + 0x2c;
            R(4) = '\r';
            NC_POLL(c, 2);
            nc_call(c, entry);
            set_reg(c, 1, column_hi);
            MEM_W32(column, 0);
            NC_POLL(c, 1);
            R(4) = 1;
            NC_POLL(c, 2);
        } else {
            set_reg(c, 1, classes_hi);
            set_reg(c, 1, R(1) + R(3));
            R(2) = MEM_R8(R(1) + (classes - classes_hi));
            NC_POLL(c, NC_LOAD_CYCLES);
            NC_POLL(c, 1);
            set_reg(c, 2, R(2) & 0x97);
            bool advances = R(2) != 0;
            R(4) = 1;
            NC_POLL(c, 2);
            if (advances) {
                set_reg(c, 2, column_hi);
                load_word(c, 2, column);
                NC_POLL(c, 1);
                set_reg(c, 2, R(2) + 1);
                set_reg(c, 1, column_hi);
                MEM_W32(column, R(2));
                NC_POLL(c, 1);
            }
        }
        set_reg(c, 5, R(29) + 16);
        R(31) = entry + 0xa4;
        R(6) = 1;
        NC_POLL(c, 2);
        nc_call(c, write_entry); /* write(1, &ch, 1) */
    }
    load_word(c, 31, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

#endif
