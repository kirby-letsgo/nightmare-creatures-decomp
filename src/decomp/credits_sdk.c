#include "decomp/credits.h"

/* The credits copy routine advances one byte at a time, even when the two
 * ranges overlap. A null destination or a nonpositive count skips the loop. */
bool nc_credits_copy_bytes(CPUState *c) {
    if (c->budget <= 2)
        return false;
    bool has_destination = R(4) != 0;
    R(2) = 0;
    c->budget -= 2;
    if (has_destination) {
        bool copying = (s32)R(6) > 0;
        R(3) = R(4);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        while (copying) {
            R(2) = MEM_R8(R(5));
            NC_POLL(c, NC_LOAD_CYCLES);
            R(5)++;
            NC_POLL(c, NC_INSN_CYCLES);
            R(6)--;
            NC_POLL(c, NC_INSN_CYCLES);
            MEM_W8(R(4), (u8)R(2));
            NC_POLL(c, NC_INSN_CYCLES);
            copying = (s32)R(6) > 0;
            R(4)++;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
        }
        R(2) = R(3);
        NC_POLL(c, NC_INSN_CYCLES);
    }
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}
