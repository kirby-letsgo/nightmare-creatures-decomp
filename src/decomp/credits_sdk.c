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

/* A nonpositive count returns zero; a positive count returns the starting
 * address and leaves a0 just beyond the filled bytes. */
bool nc_credits_fill_bytes(CPUState *c) {
    if (c->budget <= 2)
        return false;
    bool has_destination = R(4) != 0;
    R(2) = 0;
    c->budget -= 2;
    if (has_destination) {
        bool filling = (s32)R(6) > 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (filling) {
            R(2) = R(4);
            NC_POLL(c, NC_INSN_CYCLES);
            do {
                MEM_W8(R(4), (u8)R(5));
                NC_POLL(c, NC_INSN_CYCLES);
                R(6)--;
                NC_POLL(c, NC_INSN_CYCLES);
                filling = (s32)R(6) > 0;
                R(4)++;
                NC_POLL(c, 2 * NC_INSN_CYCLES);
            } while (filling);
        }
    }
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_credits_string_length(CPUState *c) {
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

static void set_reg(CPUState *c, unsigned reg, u32 value) {
    R(reg) = value;
    NC_POLL(c, NC_INSN_CYCLES);
}
static void load_word(CPUState *c, unsigned reg, u32 addr) {
    R(reg) = MEM_R32(addr);
    NC_POLL(c, NC_LOAD_CYCLES);
}

/* Both lengths are measured first. Strings with the same end address are
 * rejected. On success the destination length is scanned a second time. */
bool nc_credits_append_string(CPUState *c) {
    if (c->budget <= 8)
        return false;
    R(29) -= 0x20;
    MEM_W32(R(29) + 0x14, R(17));
    R(17) = R(4);
    MEM_W32(R(29) + 0x18, R(18));
    R(18) = R(5);
    MEM_W32(R(29) + 0x1c, R(31));
    MEM_W32(R(29) + 0x10, R(16));
    c->budget -= 8;
    if (!R(17)) {
        set_reg(c, 2, 0);
    } else {
        bool has_source = R(18) != 0;
        R(2) = 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (has_source) {
            R(31) = 0x800199b8;
            R(4) = R(17);
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, 0x80019a30);
            set_reg(c, 4, R(18));
            R(31) = 0x800199c4;
            R(16) = R(17) + R(2);
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, 0x80019a30);
            set_reg(c, 2, R(18) + R(2));
            bool same_end = R(16) == R(2);
            R(3) = R(17);
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            if (same_end) {
                set_reg(c, 2, 0);
            } else {
                bool more;
                do {
                    R(2) = MEM_R8(R(17));
                    NC_POLL(c, NC_LOAD_CYCLES);
                    NC_POLL(c, NC_INSN_CYCLES);
                    more = R(2) != 0;
                    R(17)++;
                    NC_POLL(c, 2 * NC_INSN_CYCLES);
                } while (more);
                set_reg(c, 17, R(17) - 1);
                do {
                    R(2) = MEM_R8(R(18));
                    NC_POLL(c, NC_LOAD_CYCLES);
                    set_reg(c, 18, R(18) + 1);
                    MEM_W8(R(17), (u8)R(2));
                    NC_POLL(c, NC_INSN_CYCLES);
                    more = R(2) != 0;
                    R(17)++;
                    NC_POLL(c, 2 * NC_INSN_CYCLES);
                } while (more);
                R(2) = R(3);
                NC_POLL(c, 2 * NC_INSN_CYCLES);
            }
        }
    }
    load_word(c, 31, R(29) + 0x1c);
    for (int reg = 18; reg >= 16; reg--)
        load_word(c, reg, R(29) + 0x10 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + 0x20);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Pack the variadic register arguments into the caller's argument area and
 * call the guest formatter with stdout, the format and the argument cursor. */
bool nc_credits_printf(CPUState *c) {
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
    R(31) = 0x80019a8c;
    MEM_W32(R(29) + 0x24, R(7));
    c->budget -= 11;
    nc_call(c, 0x80019a9c);
    load_word(c, 31, R(29) + 0x10);
    set_reg(c, 29, R(29) + 0x18);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}
