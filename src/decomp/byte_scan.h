#ifndef NC_DECOMP_BYTE_SCAN_H
#define NC_DECOMP_BYTE_SCAN_H

#include "port/recomp.h"

static inline bool sdk_string_length(CPUState *c) {
    if (c->budget <= 2)
        return false;
    const bool has_string = R(4) != 0;
    R(3) = 0;
    c->budget -= 2;
    if (has_string) {
        for (;;) {
            R(2) = MEM_R8(R(4));
            NC_POLL(c, NC_LOAD_CYCLES);
            NC_POLL(c, NC_INSN_CYCLES);
            const bool more = R(2) != 0;
            R(4)++; /* Includes the terminator. */
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            if (!more)
                break;
            R(3)++;
            NC_POLL(c, NC_INSN_CYCLES);
        }
        R(2) = R(3);
        NC_POLL(c, NC_INSN_CYCLES);
    } else {
        R(2) = 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    }
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* The SDK takes a signed count and advances a0 even on a match. */
static inline bool sdk_find_byte(CPUState *c) {
    if (c->budget <= 9)
        return false;
    R(2) = 0;
    c->budget -= 2;
    if (R(4)) {
        c->budget -= 2;
        if ((s32)R(6) > 0) {
            R(6)--;
            R(5) &= 0xff;
            c->budget -= 5;
            bool remaining;
            do {
                R(2) = MEM_R8(R(4));
                NC_POLL(c, NC_LOAD_CYCLES);
                NC_POLL(c, 1);
                bool matched = R(2) == R(5);
                R(4)++;
                NC_POLL(c, 2);
                if (matched) {
                    R(2) = R(4) - 1;
                    NC_POLL(c, 2);
                    break;
                }
                R(6)--;
                NC_POLL(c, NC_INSN_CYCLES);
                remaining = (s32)R(6) >= 0;
                R(2) = 0;
                NC_POLL(c, 2);
            } while (remaining);
        }
    }
    NC_POLL(c, 2);
    return true;
}

#endif
