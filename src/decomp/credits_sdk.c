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

/* The SDK indexes its writable character-class table with the low byte.
 * v1 retains the full input (possibly adjusted); v0 returns only its low byte. */
bool nc_credits_change_case(CPUState *c, bool uppercase) {
    if (c->budget <= 14)
        return false;
    R(3) = R(4);
    R(1) = 0x80020000 + (R(4) & 0xff);
    const u8 flags = MEM_R8(R(1) + 0x3375);
    const bool change = (flags & (uppercase ? 2 : 1)) != 0;
    if (change)
        R(3) += uppercase ? (u32)-32 : 32;
    R(2) = (u8)R(3);
    c->budget -= change ? 14 : 13;
    return true;
}

/* Psy-Q packet constructors write the DMA word count and GP0 opcode.
 * A polyline also needs a terminator in its last word. */
bool nc_credits_init_primitive(CPUState *c, NcCreditsPrimitive primitive) {
    static const struct {
        u8 words, opcode;
    } packets[] = {
        [NC_POLY_F3] = {4, 0x20},    [NC_POLY_FT3] = {7, 0x24},  [NC_POLY_G3] = {6, 0x30},
        [NC_POLY_GT3] = {9, 0x34},   [NC_POLY_F4] = {5, 0x28},   [NC_POLY_FT4] = {9, 0x2c},
        [NC_POLY_G4] = {8, 0x38},    [NC_POLY_GT4] = {12, 0x3c}, [NC_SPRT8] = {3, 0x74},
        [NC_SPRT16] = {3, 0x7c},     [NC_SPRT] = {4, 0x64},      [NC_TILE1] = {2, 0x68},
        [NC_TILE8] = {2, 0x70},      [NC_TILE16] = {2, 0x78},    [NC_TILE] = {3, 0x60},
        [NC_LINE_F2] = {3, 0x40},    [NC_LINE_G2] = {4, 0x50},   [NC_LINE_F3] = {5, 0x48},
        [NC_LINE_G3] = {7, 0x58},    [NC_LINE_F4] = {6, 0x4c},   [NC_LINE_G4] = {9, 0x5c},
        [NC_BLOCK_FILL] = {3, 0x02},
    };
    const u8 opcode = packets[primitive].opcode, words = packets[primitive].words;
    const bool polyline = (opcode & 0xe8) == 0x48;
    if (c->budget <= (polyline ? 8 : 5))
        return false;
    if (polyline) {
        R(3) = 0x55555555;
        c->budget -= 2;
    }
    R(2) = words;
    c->budget--;
    MEM_W8(R(4) + 3, words);
    c->budget--;
    R(2) = opcode;
    c->budget--;
    MEM_W8(R(4) + 7, opcode);
    if (polyline) {
        c->budget--;
        MEM_W32(R(4) + 4 * words, R(3));
    }
    c->budget -= 2;
    return true;
}

/* SetSemiTrans and SetShadeTex change a single bit of the GP0 opcode. */
bool nc_credits_packet_flag(CPUState *c, u8 mask) {
    if (c->budget <= 9)
        return false;
    const bool enable = R(5) != 0;
    c->budget -= 2;
    u8 opcode = MEM_R8(R(4) + 7);
    c->budget -= NC_LOAD_CYCLES;
    R(2) = enable ? (opcode | mask) : (opcode & (u8)~mask);
    c->budget -= 2;
    MEM_W8(R(4) + 7, (u8)R(2));
    c->budget -= 2;
    return true;
}

bool nc_credits_prepend_packets(CPUState *c, bool range) {
    if (c->budget <= 21)
        return false;
    const u32 address_mask = 0x00ffffff, size_mask = 0xff000000;
    const u32 last = range ? R(6) : R(5);
    R(range ? 7 : 6) = address_mask;
    R(range ? 8 : 7) = size_mask;
    c->budget -= 3;
    R(3) = MEM_R32(last);
    c->budget -= NC_LOAD_CYCLES;
    R(2) = MEM_R32(R(4));
    c->budget -= NC_LOAD_CYCLES;
    R(3) = (R(3) & size_mask) | (R(2) & address_mask);
    c->budget -= 3;
    MEM_W32(last, R(3));
    c->budget--;
    /* Read the head again: the tail can alias it. */
    R(2) = MEM_R32(R(4));
    c->budget -= NC_LOAD_CYCLES;
    R(5) &= address_mask;
    R(2) = (R(2) & size_mask) | R(5);
    c->budget -= 3;
    MEM_W32(R(4), R(2));
    c->budget -= 2;
    return true;
}

bool nc_credits_packet_link(CPUState *c, bool check_end) {
    if (c->budget <= 9)
        return false;
    c->budget--;
    const u32 link = MEM_R32(R(4)) & 0x00ffffff;
    R(3) = check_end ? 0x00ffffff : 0x80000000;
    R(2) = check_end ? link == 0x00ffffff : link | R(3);
    c->budget -= 8;
    return true;
}
