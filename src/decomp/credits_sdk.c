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

/* Expand tabs to eight-column stops and emit a carriage return before a newline. */
bool nc_credits_put_char(CPUState *c) {
    const u32 column = 0x80024ce8;
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
            R(31) = 0x8001a21c;
            R(4) = ' ';
            NC_POLL(c, 2);
            nc_call(c, 0x8001a1d8);
            set_reg(c, 2, 0x80020000);
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
            R(31) = 0x8001a204;
            R(4) = '\r';
            NC_POLL(c, 2);
            nc_call(c, 0x8001a1d8);
            set_reg(c, 1, 0x80020000);
            MEM_W32(column, 0);
            NC_POLL(c, 1);
            R(4) = 1;
            NC_POLL(c, 2);
        } else {
            set_reg(c, 1, 0x80020000);
            set_reg(c, 1, R(1) + R(3));
            R(2) = MEM_R8(R(1) + 0x3375);
            NC_POLL(c, NC_LOAD_CYCLES);
            NC_POLL(c, 1);
            set_reg(c, 2, R(2) & 0x97);
            bool advances = R(2) != 0;
            R(4) = 1;
            NC_POLL(c, 2);
            if (advances) {
                set_reg(c, 2, 0x80020000);
                load_word(c, 2, column);
                NC_POLL(c, 1);
                set_reg(c, 2, R(2) + 1);
                set_reg(c, 1, 0x80020000);
                MEM_W32(column, R(2));
                NC_POLL(c, 1);
            }
        }
        set_reg(c, 5, R(29) + 16);
        R(31) = 0x8001a27c;
        R(6) = 1;
        NC_POLL(c, 2);
        nc_call(c, 0x8001a29c); /* write(1, &ch, 1) */
    }
    load_word(c, 31, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* In interlaced mode the GPU must also advance to the opposite field. */
static void wait_for_vsync_field(CPUState *c) {
    set_reg(c, 2, 0x80000);
    set_reg(c, 2, R(16) & R(2));
    bool interlaced = R(2) != 0;
    NC_POLL(c, 2);
    if (!interlaced)
        return;
    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x80023400);
    NC_POLL(c, 1);
    load_word(c, 2, R(3));
    NC_POLL(c, 1);
    set_reg(c, 2, R(16) ^ R(2));
    bool same_field = (s32)R(2) >= 0;
    NC_POLL(c, 2);
    if (same_field)
        set_reg(c, 4, 0x80000000);
    while (same_field) {
        load_word(c, 2, R(3));
        NC_POLL(c, 1);
        set_reg(c, 2, R(16) ^ R(2));
        set_reg(c, 2, R(2) & R(4));
        same_field = R(2) == 0;
        NC_POLL(c, 2);
    }
}

static void wait_for_vsync_frames(CPUState *c) {
    bool positive = (s32)R(4) > 0;
    NC_POLL(c, 2);
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x8002340c); /* last synchronized frame */
    if (positive) {
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) - 1);
        R(2) += R(4);
        NC_POLL(c, 2);
    }
    positive = (s32)R(4) > 0;
    R(5) = 0;
    NC_POLL(c, 2);
    if (positive)
        set_reg(c, 5, R(4) - 1);
    R(31) = 0x8001a4ec;
    R(4) = R(2);
    NC_POLL(c, 2);
    nc_call(c, 0x8001a59c); /* v_wait(target, timeout) */

    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80023400);
    NC_POLL(c, 1);
    load_word(c, 16, R(2)); /* sample GPU status before the next VBlank */
    set_reg(c, 4, 0x80020000);
    load_word(c, 4, 0x800244d4);
    set_reg(c, 5, 1);
    R(31) = 0x8001a510;
    R(4)++;
    NC_POLL(c, 2);
    nc_call(c, 0x8001a59c);
    wait_for_vsync_field(c);

    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x800244d4);
    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x80023404);
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x8002340c, R(2));
    NC_POLL(c, 1);
    load_word(c, 3, R(3));
    set_reg(c, 2, R(17));
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x80023408, R(3));
    NC_POLL(c, 1);
}

bool nc_credits_vsync(CPUState *c) {
    if (c->budget <= 1)
        return false;
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80023400);
    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x80023404);
    set_reg(c, 29, R(29) - 32);
    MEM_W32(R(29) + 24, R(31));
    NC_POLL(c, 1);
    MEM_W32(R(29) + 20, R(17));
    NC_POLL(c, 1);
    MEM_W32(R(29) + 16, R(16));
    NC_POLL(c, 1);
    load_word(c, 16, R(2));
    load_word(c, 2, R(3));
    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x80023408);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) - R(3));
    bool query_frame = (s32)R(4) < 0;
    R(17) = R(2) & 0xffff;
    NC_POLL(c, 2);
    if (query_frame) {
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x800244d4);
        NC_POLL(c, 2);
    } else {
        set_reg(c, 2, 1);
        bool query_elapsed = R(4) == R(2);
        R(2) = R(17);
        NC_POLL(c, 2);
        if (!query_elapsed)
            wait_for_vsync_frames(c);
    }
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* Test the signed VBlank count against the requested frame. */
static void compare_vblank_target(CPUState *c) {
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x800244d4);
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, (s32)R(2) < (s32)R(4));
}

bool nc_credits_wait_vblank(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(29) -= 32;
    R(5) <<= 15;
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(5));
    c->budget--;
    compare_vblank_target(c);
    bool waiting = R(2) != 0;
    MEM_W32(R(29) + 24, R(31));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (waiting)
        set_reg(c, 3, UINT32_MAX);
    while (waiting) {
        load_word(c, 2, R(29) + 16);
        NC_POLL(c, NC_INSN_CYCLES);
        set_reg(c, 2, R(2) - 1);
        MEM_W32(R(29) + 16, R(2));
        NC_POLL(c, NC_INSN_CYCLES);
        load_word(c, 2, R(29) + 16);
        NC_POLL(c, NC_INSN_CYCLES);
        const bool timed_out = R(2) == R(3);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (timed_out) {
            set_reg(c, 4, 0x80020000);
            set_reg(c, 4, 0x800182f8);
            R(31) = 0x8001a5f4;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, 0x8001a638); /* puts */
            R(31) = 0x8001a5fc;
            R(4) = 0;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, 0x8001a2ac); /* ChangeClearPad */
            set_reg(c, 4, 3);
            R(31) = 0x8001a608;
            R(5) = 0;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, 0x8001a688); /* ChangeClearRCnt */
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            break;
        }
        compare_vblank_target(c);
        waiting = R(2) != 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    }
    load_word(c, 31, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* The SDK puts emits through putchar and uses "<NULL>" for a null pointer. */
bool nc_credits_put_string(CPUState *c) {
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
        R(16) = 0x80018308;
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
        R(31) = 0x8001a664;
        NC_POLL(c, 2);
        nc_call(c, 0x8001a1d8); /* putchar */
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* The credits memchr takes a signed count and advances a0 even on a match. */
bool nc_credits_find_byte(CPUState *c) {
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
                set_reg(c, 6, R(6) - 1);
                remaining = (s32)R(6) >= 0;
                R(2) = 0;
                NC_POLL(c, 2);
            } while (remaining);
        }
    }
    NC_POLL(c, 2);
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

bool nc_credits_link_packets(CPUState *c) {
    if (c->budget <= 11)
        return false;
    R(6) = 0x00ffffff;
    R(3) = 0xff000000;
    c->budget -= 3;
    u32 tag = MEM_R32(R(4));
    R(5) &= R(6);
    R(2) = (tag & R(3)) | R(5);
    c->budget -= NC_LOAD_CYCLES + 3;
    MEM_W32(R(4), R(2));
    c->budget -= 2;
    return true;
}

/* Both GPU layouts encode the draw-mode command, but move the flag bits. */
static u32 draw_mode_word(bool alternate, bool display, bool dither, u32 page, u32 *opcode) {
    *opcode = 0xe1000000u;
    if (dither)
        *opcode |= alternate ? 0x800 : 0x200;
    u32 bits = page & (alternate ? 0x27ff : 0x9ff);
    if (display)
        bits |= alternate ? 0x1000 : 0x400;
    return *opcode | bits;
}

bool nc_credits_draw_mode_word(CPUState *c) {
    if (c->budget <= 19)
        return false;
    c->budget -= 2;
    u8 graph = MEM_R8(0x80024558);
    bool alternate = graph == 1 || graph == 2;
    bool display = R(4) != 0, dither = R(5) != 0;
    R(2) = draw_mode_word(alternate, display, dither, R(6), &R(3));
    c->budget -= NC_LOAD_CYCLES + 11 + dither + (display ? (alternate ? 2 : 1) : 0);
    return true;
}

bool nc_credits_draw_tpage(CPUState *c) {
    if (c->budget <= 60)
        return false;
    R(29) -= 0x28;
    c->budget--;
    MEM_W32(R(29) + 0x1c, R(19));
    R(19) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 0x18, R(18));
    R(18) = R(5);
    c->budget -= 2;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(6);
    c->budget -= 2;
    MEM_W32(R(29) + 0x14, R(17));
    R(17) = R(7);
    R(2) = 1;
    c->budget -= 3;
    MEM_W32(R(29) + 0x20, R(31));
    c->budget--;
    R(31) = 0x8001ba90;
    MEM_W8(R(19) + 3, 1);
    c->budget -= 2;
    NC_FN_ENTER(c, 0x8001c1c8);
    nc_credits_graph_status(c, false);
    R(3) = 1;
    bool alternate = R(2) == 1;
    c->budget -= 3;
    if (!alternate) {
        R(31) = 0x8001baa4;
        c->budget -= 2;
        NC_FN_ENTER(c, 0x8001c1c8);
        nc_credits_graph_status(c, false);
        R(3) = 2;
        alternate = R(2) == 2;
        c->budget -= 3;
    }

    bool display = R(18) != 0, dither = R(16) != 0;
    R(2) = draw_mode_word(alternate, display, dither, R(17), &R(alternate ? 3 : 7));
    c->budget -= 5 + alternate + display + dither;
    MEM_W32(R(19) + 4, R(2));
    c->budget--;
    R(31) = MEM_R32(R(29) + 0x20);
    c->budget -= NC_LOAD_CYCLES;
    for (int reg = 19; reg >= 16; reg--) {
        R(reg) = MEM_R32(R(29) + 0x10 + (reg - 16) * 4);
        c->budget -= NC_LOAD_CYCLES;
    }
    R(29) += 0x28;
    c->budget -= 3;
    return true;
}

bool nc_credits_draw_corner(CPUState *c, bool bottom_right) {
    if (c->budget <= 55)
        return false;

    s32 x = (s16)R(4), y = (s16)R(5);
    c->budget -= 4;
    if (x < 0) {
        x = 0;
    } else {
        c->budget -= 2;
        s32 max_x = (s16)MEM_R16(0x8002455c) - 1;
        R(6) = (u32)max_x;
        c->budget -= NC_LOAD_CYCLES + 7;
        if (x > max_x) {
            x = max_x;
            c->budget -= 2;
        }
    }
    c->budget -= 5;
    if (y < 0) {
        y = 0;
    } else {
        c->budget -= 2;
        s32 max_y = (s16)MEM_R16(0x8002455e) - 1;
        c->budget -= NC_LOAD_CYCLES + 8;
        if (y > max_y) {
            y = max_y;
            c->budget--;
        }
    }

    c->budget -= 2;
    u8 graph = MEM_R8(0x80024558);
    bool wide = graph == 1 || graph == 2;
    u32 mask = wide ? 0xfff : 0x3ff;
    R(4) = bottom_right ? 0xe4000000u : 0xe3000000u;
    R(5) = (u32)y;
    R(3) = (R(5) & mask) << (wide ? 12 : 10);
    R(2) = R(4) | R(3) | ((u32)x & mask);
    c->budget -= wide ? 14 : 16;
    return true;
}

bool nc_credits_draw_area_packet(CPUState *c) {
    if (c->budget <= 161)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(5);
    R(2) = 2;
    c->budget -= 3;
    MEM_W32(R(29) + 24, R(31));
    c->budget--;
    MEM_W8(R(17) + 3, 2);
    c->budget--;
    R(4) = (u32)(s32)(s16)MEM_R16(R(16));
    c->budget -= NC_LOAD_CYCLES;
    R(5) = (u32)(s32)(s16)MEM_R16(R(16) + 2);
    R(31) = 0x8001d0b4;
    c->budget -= NC_LOAD_CYCLES + 2;
    NC_FN_ENTER(c, 0x8001d4cc);
    nc_credits_draw_corner(c, false);
    MEM_W32(R(17) + 4, R(2));
    c->budget--;

    /* The first command can overlap the rectangle. Reload the fields before
     * computing the inclusive bottom-right corner with 16-bit wraparound. */
    u32 x = MEM_R16(R(16));
    c->budget -= NC_LOAD_CYCLES;
    u32 width = MEM_R16(R(16) + 4);
    c->budget -= NC_LOAD_CYCLES;
    u32 y = MEM_R16(R(16) + 2);
    c->budget -= NC_LOAD_CYCLES + 3;
    u32 height = MEM_R16(R(16) + 6);
    R(4) = (u32)(s32)(s16)(x + width - 1);
    R(5) = (u32)(s32)(s16)(y + height - 1);
    R(31) = 0x8001d0ec;
    c->budget -= NC_LOAD_CYCLES + 6;
    NC_FN_ENTER(c, 0x8001d598);
    nc_credits_draw_corner(c, true);
    MEM_W32(R(17) + 8, R(2));
    c->budget--;
    R(31) = MEM_R32(R(29) + 24);
    c->budget -= NC_LOAD_CYCLES;
    R(17) = MEM_R32(R(29) + 20);
    c->budget -= NC_LOAD_CYCLES;
    R(16) = MEM_R32(R(29) + 16);
    R(29) += 32;
    c->budget -= NC_LOAD_CYCLES + 3;
    return true;
}

bool nc_credits_read_environment(CPUState *c, bool display) {
    /* Eight cycles to reach memcpy, then two for its entry guard. */
    if (c->budget <= 10)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    R(5) = display ? 0x800245c4 : 0x80024568;
    c->budget -= 4;
    MEM_W32(R(29) + 20, R(31));
    R(31) = display ? 0x8001cffc : 0x8001cb14;
    R(6) = display ? 20 : 92;
    c->budget -= 3;
    NC_FN_ENTER(c, 0x80019928);
    nc_credits_copy_bytes(c);

    R(2) = R(16);
    NC_POLL(c, NC_INSN_CYCLES);
    R(31) = MEM_R32(R(29) + 20);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(16) = MEM_R32(R(29) + 16);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(29) += 24;
    NC_POLL(c, NC_INSN_CYCLES);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Probe GPU revision with GP1(10h), then test draw-mode bit 12 on older GPUs. */
bool nc_credits_gpu_version(CPUState *c) {
    if (c->budget <= 58)
        return false;
    bool extended = (R(4) & 8) != 0;
    c->budget -= 2;
    R(2) = MEM_R32(0x8002462c);
    R(3) = 0x10000007;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(2), R(3));
    c->budget -= 2;
    R(5) = MEM_R32(0x80024628);
    c->budget -= NC_LOAD_CYCLES + 1;
    u32 version = MEM_R32(R(5)) & 0xffffff;
    R(3) = 0xe1000000;
    c->budget -= NC_LOAD_CYCLES + 5;

    if (version == 2) {
        R(4) = 0x09000000;
        c->budget -= 3;
        if (extended) {
            R(4) |= 1;
            c->budget -= 2;
            R(3) = MEM_R32(0x8002462c);
            R(2) = 4;
            c->budget -= NC_LOAD_CYCLES + 1;
            MEM_W32(R(3), R(4));
            c->budget -= 3;
        } else {
            R(2) = 3;
            c->budget--;
        }
    } else {
        c->budget--;
        R(2) = MEM_R32(0x8002462c);
        c->budget -= NC_LOAD_CYCLES + 1;
        R(2) = (MEM_R32(R(2)) & 0x3fff) | 0xe1001000;
        c->budget -= NC_LOAD_CYCLES + 3;
        MEM_W32(R(5), R(2));
        c->budget -= 2;
        R(2) = MEM_R32(0x80024628);
        c->budget -= NC_LOAD_CYCLES + 1;
        R(3) = MEM_R32(0x8002462c);
        c->budget -= NC_LOAD_CYCLES;
        (void)MEM_R32(R(2)); /* GPUREAD before the status check */
        c->budget -= NC_LOAD_CYCLES;
        bool supported = (MEM_R32(R(3)) & 0x1000) != 0;
        c->budget -= NC_LOAD_CYCLES + 4;
        if (supported && extended) {
            c->budget -= 3;
            MEM_W32(R(3), 0x20000504);
            R(2) = 2;
            c->budget -= 3;
        } else {
            R(2) = supported;
            c->budget -= supported ? 4 : 2;
        }
    }
    c->budget -= 2;
    return true;
}

bool nc_credits_gpu_parameter(CPUState *c) {
    if (c->budget <= 18)
        return false;

    c->budget -= 2;
    R(3) = MEM_R32(0x8002462c);
    R(4) |= 0x10000000; /* GP1 information request */
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(3), R(4));
    c->budget -= 2;
    R(2) = MEM_R32(0x80024628);
    R(3) = 0x00ff0000;
    c->budget -= NC_LOAD_CYCLES + 1;
    R(2) = MEM_R32(R(2));
    R(3) |= 0xffff;
    R(2) &= R(3);
    c->budget -= NC_LOAD_CYCLES + 3;
    return true;
}

/* The graphics reset path uses this counted byte fill to clear its buffers. */
bool nc_credits_gpu_fill_bytes(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(29) -= 8;
    bool filling = R(6) != 0;
    R(2) = R(6) - 1;
    c->budget -= 3;
    if (filling) {
        set_reg(c, 3, UINT32_MAX);
        do {
            MEM_W8(R(4), (u8)R(5));
            NC_POLL(c, NC_INSN_CYCLES);
            set_reg(c, 2, R(2) - 1);
            filling = R(2) != R(3);
            R(4)++;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
        } while (filling);
    }
    set_reg(c, 29, R(29) + 8);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Read DMA2 CHCR or GPUSTAT through the driver's port pointer. */
static void read_gpu_sync_bit(CPUState *c, u32 port_slot, u32 mask) {
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, port_slot);
    NC_POLL(c, 1);
    load_word(c, 2, R(2));
    set_reg(c, 3, mask);
    set_reg(c, 2, R(2) & R(3));
}

static bool gpu_queue_ready(CPUState *c, bool blocking) {
    read_gpu_sync_bit(c, 0x80024638, 1u << 24);
    bool dma_busy = R(2) != 0;
    NC_POLL(c, 2);
    if (dma_busy)
        return false;
    read_gpu_sync_bit(c, 0x8002462c, 1u << 26);
    bool ready = R(2) != 0;
    if (blocking)
        R(2) = 0; /* delay slot supplies the success result */
    NC_POLL(c, 2);
    return ready;
}

static void gpu_sync_call(CPUState *c, u32 target, u32 resume) {
    R(31) = resume;
    NC_POLL(c, 2);
    nc_call(c, target);
}

static bool gpu_sync_timed_out(CPUState *c, u32 resume) {
    gpu_sync_call(c, 0x8001ea30, resume); /* get_alarm */
    bool expired = R(2) != 0;
    R(2) = UINT32_MAX;
    NC_POLL(c, 2);
    return expired;
}

static void drain_gpu_queue(CPUState *c) {
    gpu_sync_call(c, 0x8001e9fc, 0x8001e8cc); /* set_alarm */
    NC_POLL(c, 2);
    for (;;) {
        set_reg(c, 3, 0x80020000);
        load_word(c, 3, 0x8002465c);
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024660);
        NC_POLL(c, 1);
        bool empty = R(3) == R(2);
        NC_POLL(c, 2);
        if (empty)
            break;
        NC_POLL(c, 2);
        gpu_sync_call(c, 0x8001e45c, 0x8001e8dc); /* _exeque */
        if (gpu_sync_timed_out(c, 0x8001e8e4))
            return;
    }
    while (!gpu_queue_ready(c, true)) {
        if (gpu_sync_timed_out(c, 0x8001e918))
            return;
    }
    NC_POLL(c, 2);
}

bool nc_credits_sync_gpu_queue(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    bool blocking = R(4) == 0;
    MEM_W32(R(29) + 16, R(16));
    c->budget -= 2;
    if (blocking) {
        drain_gpu_queue(c);
    } else {
        /* Report the original depth even if _exeque advances the queue. */
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x8002465c);
        set_reg(c, 3, 0x80020000);
        load_word(c, 3, 0x80024660);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) - R(3));
        set_reg(c, 16, R(2) & 63);
        bool queued = R(16) != 0;
        NC_POLL(c, 2);
        if (queued)
            gpu_sync_call(c, 0x8001e45c, 0x8001e994);
        if (gpu_queue_ready(c, false)) {
            set_reg(c, 2, R(16));
        } else {
            bool empty = R(16) == 0;
            R(2) = R(16);
            NC_POLL(c, 2);
            if (empty) {
                R(2) = 1;
                NC_POLL(c, 2);
            }
        }
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_set_queue_alarm(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(31) = 0x8001ea0c;
    R(4) = 0xffffffff; /* VSync(-1) reads the current vertical blank count. */
    c->budget -= 3;
    nc_call(c, 0x8001a454);

    set_reg(c, 2, R(2) + 240);
    set_reg(c, 1, 0x80020000);
    MEM_W32(R(1) + 0x4670, R(2)); /* queue deadline */
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 1, 0x80020000);
    MEM_W32(R(1) + 0x4674, 0); /* polls since the alarm was set */
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* The GPU driver addresses these globals through the same upper halfword. */
static void load_gpu_global(CPUState *c, unsigned reg, u32 address) {
    set_reg(c, reg, 0x80020000);
    load_word(c, reg, address);
}

static void store_gpu_global(CPUState *c, u32 address, unsigned reg) {
    set_reg(c, 1, 0x80020000);
    MEM_W32(address, R(reg));
    NC_POLL(c, 1);
}

static void report_gpu_timeout(CPUState *c) {
    load_gpu_global(c, 3, 0x8002462c);
    set_reg(c, 4, 0x80020000);
    set_reg(c, 4, 0x80018660);
    load_word(c, 2, R(3)); /* GPUSTAT is read twice by the original. */
    load_gpu_global(c, 5, 0x8002465c);
    load_gpu_global(c, 2, 0x80024630);
    load_gpu_global(c, 8, 0x80024660);
    load_word(c, 2, R(2)); /* DMA2 address is the fifth printf argument */
    set_reg(c, 5, R(5) - R(8));
    MEM_W32(R(29) + 16, R(2));
    NC_POLL(c, 1);
    load_gpu_global(c, 2, 0x80024638);
    load_word(c, 6, R(3));
    load_word(c, 7, R(2));
    R(31) = 0x8001ead0;
    R(5) &= 63;
    NC_POLL(c, 2);
    nc_call(c, 0x80019a60);

    set_reg(c, 2, 0x80020000);
    set_reg(c, 2, 0x8002464c);
    load_word(c, 5, R(2));
    load_gpu_global(c, 6, 0x80024650);
    load_gpu_global(c, 7, 0x80024654);
    set_reg(c, 4, 0x80020000);
    set_reg(c, 4, 0x80018694);
    gpu_sync_call(c, 0x80019a60, 0x8001eafc);
}

static void reset_gpu_command_buffer(CPUState *c) {
    load_gpu_global(c, 3, 0x80024638);
    set_reg(c, 2, 0x401);
    MEM_W32(R(3), R(2)); /* stop DMA2 */
    NC_POLL(c, 1);
    load_gpu_global(c, 3, 0x80024648);
    NC_POLL(c, 1);
    load_word(c, 2, R(3));
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) | 0x800);
    MEM_W32(R(3), R(2)); /* enable DMA2 in DPCR */
    NC_POLL(c, 1);
    /* Acknowledge the GPU interrupt, then reset its command buffer. */
    for (unsigned command = 2; command > 0; command--) {
        load_gpu_global(c, 3, 0x8002462c);
        set_reg(c, 2, command << 24);
        MEM_W32(R(3), R(2));
        NC_POLL(c, 1);
    }
}

static void recover_gpu_queue(CPUState *c) {
    R(31) = 0x8001eb04;
    R(4) = 0;
    NC_POLL(c, 2);
    nc_call(c, 0x8001a814); /* SetIntrMask(0) */
    store_gpu_global(c, 0x80024660, 0);
    load_gpu_global(c, 3, 0x80024660);
    store_gpu_global(c, 0x8002466c, 2);
    store_gpu_global(c, 0x8002465c, 3);

    reset_gpu_command_buffer(c);
    load_gpu_global(c, 4, 0x8002466c);
    gpu_sync_call(c, 0x8001a814, 0x8001eb80);
    R(2) = UINT32_MAX;
    NC_POLL(c, 2);
}

/* Mode zero also resets GP1 and clears both driver buffers. */
static void reset_gpu_buffers(CPUState *c) {
    load_gpu_global(c, 3, 0x80024638);
    NC_POLL(c, 1);
    MEM_W32(R(3), R(2)); /* DMA2 CHCR = 0x401 */
    NC_POLL(c, 1);
    load_gpu_global(c, 3, 0x80024648);
    set_reg(c, 4, 0x80020000);
    set_reg(c, 4, 0x80024d30);
    load_word(c, 2, R(3));
    set_reg(c, 5, 0);
    set_reg(c, 2, R(2) | 0x800);
    MEM_W32(R(3), R(2));
    NC_POLL(c, 1);
    load_gpu_global(c, 2, 0x8002462c);
    set_reg(c, 6, 0x100);
    MEM_W32(R(2), 0);
    NC_POLL(c, 1);
    gpu_sync_call(c, 0x8001ec78, 0x8001e814);
    set_reg(c, 4, 0x80020000);
    set_reg(c, 4, 0x80027488);
    set_reg(c, 5, 0);
    R(31) = 0x8001e828;
    R(6) = 0x1800;
    NC_POLL(c, 2);
    nc_call(c, 0x8001ec78);
    NC_POLL(c, 2);
}

bool nc_credits_reset_gpu_driver(CPUState *c) {
    if (c->budget <= 6)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 20, R(31));
    R(31) = 0x8001e770;
    R(4) = 0;
    c->budget -= 3;
    nc_call(c, 0x8001a814); /* suspend interrupts while resetting the queue */
    store_gpu_global(c, 0x80024660, 0);
    load_gpu_global(c, 3, 0x80024660);
    store_gpu_global(c, 0x8002466c, 2);
    set_reg(c, 2, 1);
    store_gpu_global(c, 0x8002465c, 3);
    set_reg(c, 3, R(16) & 7);
    bool command_reset = R(3) == R(2);
    R(2) = (s32)R(3) < 2;
    NC_POLL(c, 2);
    if (!command_reset) {
        bool low_mode = R(2) != 0;
        NC_POLL(c, 2);
        if (low_mode) {
            bool full_reset = R(3) == 0;
            R(2) = 0x401;
            NC_POLL(c, 2);
            if (full_reset)
                reset_gpu_buffers(c);
            else
                NC_POLL(c, 2);
        } else {
            set_reg(c, 2, 3);
            command_reset = R(3) == R(2);
            NC_POLL(c, 2);
            if (!command_reset)
                NC_POLL(c, 2);
        }
    }
    if (command_reset)
        reset_gpu_command_buffer(c);

    load_gpu_global(c, 4, 0x8002466c);
    gpu_sync_call(c, 0x8001a814, 0x8001e88c);
    set_reg(c, 2, R(16) & 7);
    bool query_version = R(2) == 0;
    R(2) = 0;
    NC_POLL(c, 2);
    if (query_version) {
        R(31) = 0x8001e8a0;
        R(4) = R(16);
        NC_POLL(c, 2);
        nc_call(c, 0x8001eb9c);
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_check_queue_alarm(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 24, R(31));
    R(31) = 0x8001ea40;
    R(4) = UINT32_MAX;
    c->budget -= 3;
    nc_call(c, 0x8001a454); /* VSync(-1) */

    load_gpu_global(c, 3, 0x80024670);
    NC_POLL(c, 1);
    set_reg(c, 3, (s32)R(3) < (s32)R(2));
    bool expired = R(3) != 0;
    NC_POLL(c, 2);
    if (!expired) {
        load_gpu_global(c, 3, 0x80024674);
        NC_POLL(c, 1);
        set_reg(c, 2, R(3) + 1);
        store_gpu_global(c, 0x80024674, 2);
        set_reg(c, 2, 0xf0000);
        set_reg(c, 2, (s32)R(2) < (s32)R(3));
        expired = R(2) != 0;
        NC_POLL(c, 2);
    }
    if (expired) {
        report_gpu_timeout(c);
        recover_gpu_queue(c);
    } else {
        set_reg(c, 2, 0);
    }
    load_word(c, 31, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_queue_command(CPUState *c) {
    if (c->budget <= 5)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    /* _addque2 takes (command, data, copy size, argument). A zero size
     * keeps the caller's data pointer in the queue entry. */
    R(7) = R(6);
    R(31) = 0x8001e168;
    R(6) = 0;
    c->budget -= 4;
    nc_call(c, 0x8001e178);
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_credits_gpu_command_chain(CPUState *c) {
    if (c->budget <= 27)
        return false;

    /* Set GP1 DMA direction to CPU-to-GPU before programming channel 2. */
    c->budget -= 2;
    R(2) = MEM_R32(0x8002462c);
    R(3) = 0x04000002;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(2), R(3));
    c->budget -= 2;
    R(2) = MEM_R32(0x80024630);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(2), R(4)); /* list head */
    c->budget -= 2;
    R(2) = MEM_R32(0x80024634);
    R(3) = 0x01000000;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(2), 0); /* block count is unused in linked-list mode */
    c->budget -= 2;
    R(2) = MEM_R32(0x80024638);
    R(3) |= 0x401; /* start linked-list transfer from RAM */
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(2), R(3));
    c->budget -= 3;
    return true;
}

bool nc_credits_gpu_command_block(CPUState *c) {
    if (c->budget <= 10)
        return false;
    R(29) -= 8;
    R(6) = R(5) - 1;
    c->budget -= 3;
    R(3) = MEM_R32(0x8002462c);
    R(2) = 0x04000000; /* disable DMA for direct GP0 writes */
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(3), R(2));
    bool has_words = R(5) != 0;
    R(2) = 0;
    c->budget -= 3;

    if (has_words) {
        set_reg(c, 5, 0xffffffff);
        bool more;
        do {
            load_word(c, 3, R(4));
            set_reg(c, 4, R(4) + 4);
            set_reg(c, 2, 0x80020000);
            load_word(c, 2, R(2) + 0x4628);
            set_reg(c, 6, R(6) - 1);
            MEM_W32(R(2), R(3));
            NC_POLL(c, NC_INSN_CYCLES);
            more = R(6) != R(5);
            R(2) = 0;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
        } while (more);
    }
    set_reg(c, 29, R(29) + 8);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_credits_gpu_control(CPUState *c) {
    if (c->budget <= 12)
        return false;
    c->budget--;
    R(2) = MEM_R32(0x8002462c);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(2), R(4));
    R(2) = R(4) >> 24; /* GP1 opcode */
    R(1) = 0x80020000 + R(2);
    c->budget -= 4;
    MEM_W8(R(1) + 0x4d30, (u8)R(4));
    c->budget -= 3;
    return true;
}

bool nc_credits_gpu_control_byte(CPUState *c) {
    if (c->budget <= 7)
        return false;
    R(1) = 0x80020000 + R(4);
    c->budget -= 2;
    R(2) = MEM_R8(R(1) + 0x4d30);
    c->budget -= NC_LOAD_CYCLES + 2;
    return true;
}

/* Clear an ordering table backwards with DMA channel 6. */
bool nc_credits_clear_ot_dma(CPUState *c) {
    if (c->budget <= 9 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(5); /* number of entries */
    c->budget -= 3;
    R(5) = MEM_R32(0x80024648);
    c->budget -= NC_LOAD_CYCLES;
    MEM_W32(R(29) + 24, R(31));
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    R(2) = MEM_R32(R(5));
    R(3) = 0x08000000; /* channel 6 enable in DPCR */
    R(2) |= R(3);
    c->budget -= NC_LOAD_CYCLES + 2;
    MEM_W32(R(5), R(2));
    c->budget--;
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024644);
    NC_POLL(c, NC_INSN_CYCLES);
    MEM_W32(R(2), 0);
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, R(16) << 2);
    set_reg(c, 2, R(2) - 4);
    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x8002463c);
    set_reg(c, 4, R(4) + R(2));
    MEM_W32(R(3), R(4)); /* last tag in the table */
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024640);
    set_reg(c, 3, 0x11000000);
    MEM_W32(R(2), R(16));
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024644);
    set_reg(c, 3, R(3) | 2);
    MEM_W32(R(2), R(3)); /* start a descending, manually triggered transfer */
    NC_POLL(c, NC_INSN_CYCLES);
    R(31) = 0x8001d880;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001e9fc); /* set_alarm */

    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024644);
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 2, R(2));
    set_reg(c, 3, 1u << 24);
    set_reg(c, 2, R(2) & R(3));
    bool busy = R(2) != 0;
    R(2) = R(16);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (busy)
        set_reg(c, 17, 1u << 24);
    while (busy) {
        R(31) = 0x8001d8ac;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8001ea30); /* get_alarm */
        const bool timed_out = R(2) != 0;
        R(2) = UINT32_MAX;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (timed_out)
            break;
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024644);
        NC_POLL(c, NC_INSN_CYCLES);
        load_word(c, 2, R(2));
        NC_POLL(c, NC_INSN_CYCLES);
        set_reg(c, 2, R(2) & R(17));
        busy = R(2) != 0;
        R(2) = R(16);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    }
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_credits_graphics_status(CPUState *c) {
    if (c->budget <= 10)
        return false;
    c->budget--;
    R(2) = MEM_R32(0x8002462c); /* GPU status register address */
    c->budget -= NC_LOAD_CYCLES + 1;
    R(2) = MEM_R32(R(2));
    c->budget -= NC_LOAD_CYCLES + 2;
    return true;
}

bool nc_credits_display_field(CPUState *c) {
    if (c->budget <= 12)
        return false;
    c->budget--;
    R(2) = MEM_R32(0x80024550); /* active graphics driver */
    R(29) -= 24;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 16, R(31));
    c->budget--;
    R(2) = MEM_R32(R(2) + 0x38);
    R(31) = 0x8001d034;
    c->budget -= NC_LOAD_CYCLES + 3;
    nc_call(c, R(2));
    set_reg(c, 2, R(2) >> 31);
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_credits_display_x(CPUState *c) {
    if (c->budget <= 37)
        return false;
    const u32 rect = R(4);
    c->budget -= 2;
    R(3) = MEM_R8(0x80024558);
    c->budget -= NC_LOAD_CYCLES + 5;
    bool half_width = R(3) == 2;
    if (R(3) != 1)
        c->budget -= 2;

    bool reverse = false;
    if (R(3) == 1 || half_width) {
        c->budget--;
        reverse = MEM_R8(0x8002455b) != 0;
        c->budget -= NC_LOAD_CYCLES + 3;
    } else {
        c->budget -= 2;
    }

    if (reverse) {
        s32 width = (s16)MEM_R16(rect + 4);
        c->budget -= NC_LOAD_CYCLES;
        R(4) = (u32)(s32)(s16)MEM_R16(rect);
        R(3) = (u32)(half_width ? width / 2 : width);
        R(2) = 1024u - R(3) - R(4);
        c->budget -= NC_LOAD_CYCLES + (half_width ? 10 : 3);
    } else {
        s32 x = (s16)MEM_R16(rect);
        R(2) = (u32)(half_width ? x / 2 : x);
        if (half_width) {
            /* The guest leaves the rounding adjustment in v1. */
            R(3) = (u32)(x + (x < 0));
            c->budget -= 7;
        }
        c->budget -= NC_LOAD_CYCLES;
    }
    c->budget -= 2;
    return true;
}

bool nc_credits_draw_offset_word(CPUState *c) {
    if (c->budget <= 18)
        return false;
    c->budget -= 2;
    u8 graph = MEM_R8(0x80024558);
    bool wide = graph == 1 || graph == 2;
    u32 mask = wide ? 0xfff : 0x7ff;
    R(3) = (R(5) & mask) << (wide ? 12 : 11);
    u32 x = R(4) & mask;
    R(4) = 0xe5000000u;
    R(2) = R(4) | R(3) | x;
    c->budget -= wide ? 14 : 16;
    return true;
}

bool nc_credits_draw_offset_packet(CPUState *c) {
    if (c->budget <= 43)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    R(2) = 2;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    /* Write the tag first: its length byte may overlap the input coordinates. */
    MEM_W8(R(16) + 3, 2);
    c->budget--;
    R(4) = (u32)(s32)(s16)MEM_R16(R(5));
    c->budget -= NC_LOAD_CYCLES;
    R(5) = (u32)(s32)(s16)MEM_R16(R(5) + 2);
    R(31) = 0x8001d130;
    c->budget -= NC_LOAD_CYCLES + 2;
    NC_FN_ENTER(c, 0x8001d664);
    nc_credits_draw_offset_word(c);
    MEM_W32(R(16) + 4, R(2));
    c->budget--;
    MEM_W32(R(16) + 8, 0);
    c->budget--;
    R(31) = MEM_R32(R(29) + 20);
    c->budget -= NC_LOAD_CYCLES;
    R(16) = MEM_R32(R(29) + 16);
    R(29) += 24;
    c->budget -= NC_LOAD_CYCLES + 3;
    return true;
}

bool nc_credits_texture_window_word(CPUState *c) {
    const u32 rect = R(4);
    if (c->budget <= (rect ? 39 : 7))
        return false;

    R(29) -= 16;
    c->budget -= 2;
    if (rect) {
        /* The four five-bit fields are also spilled into the guest stack.
         * Read each rectangle member in order in case it overlaps a spill. */
        u32 x = MEM_R8(rect) >> 3;
        c->budget -= NC_LOAD_CYCLES + 2;
        MEM_W32(R(29), x);
        c->budget--;
        R(6) = ((0u - MEM_R16(rect + 4)) & 0xff) >> 3;
        c->budget -= NC_LOAD_CYCLES + 4;
        MEM_W32(R(29) + 8, R(6));
        c->budget--;
        u32 y = MEM_R8(rect + 2) >> 3;
        c->budget -= NC_LOAD_CYCLES + 2;
        MEM_W32(R(29) + 4, y);
        c->budget -= 2;
        R(3) = ((0u - MEM_R16(rect + 6)) & 0xff) >> 3;
        R(5) = 0xe2000000u | (x << 10);
        R(4) = R(3) << 5;
        R(2) = R(5) | (y << 15) | R(4) | R(6);
        c->budget -= NC_LOAD_CYCLES + 9;
        MEM_W32(R(29) + 12, R(3));
        c->budget--;
    } else {
        R(2) = 0;
        c->budget -= 2;
    }
    R(29) += 16;
    c->budget -= 3;
    return true;
}

bool nc_credits_texture_window_packet(CPUState *c) {
    if (c->budget <= (R(5) ? 58 : 26))
        return false;

    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    R(2) = 2;
    R(4) = R(5);
    c->budget -= 4;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    R(31) = 0x8001d068;
    MEM_W8(R(16) + 3, 2);
    c->budget -= 2;
    NC_FN_ENTER(c, 0x8001d6ac);
    nc_credits_texture_window_word(c);
    MEM_W32(R(16) + 4, R(2));
    c->budget--;
    MEM_W32(R(16) + 8, 0);
    c->budget--;
    R(31) = MEM_R32(R(29) + 20);
    c->budget -= NC_LOAD_CYCLES;
    R(16) = MEM_R32(R(29) + 16);
    R(29) += 24;
    c->budget -= NC_LOAD_CYCLES + 3;
    return true;
}

bool nc_credits_draw_mode_packet(CPUState *c) {
    /* Both command builders can run to completion within this budget. */
    if (c->budget <= 88)
        return false;

    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    R(4) = R(5);
    R(2) = 2;
    R(5) = R(6);
    c->budget -= 5;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    R(17) = MEM_R32(R(29) + 48); /* fifth argument: texture-window rectangle */
    R(6) = (u16)R(7);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 24, R(31));
    c->budget--;
    R(31) = 0x8001d1a4;
    MEM_W8(R(16) + 3, 2);
    c->budget -= 2;
    NC_FN_ENTER(c, 0x8001d470);
    nc_credits_draw_mode_word(c);
    MEM_W32(R(16) + 4, R(2));
    R(31) = 0x8001d1b0;
    R(4) = R(17);
    c->budget -= 3;
    NC_FN_ENTER(c, 0x8001d6ac);
    nc_credits_texture_window_word(c);
    MEM_W32(R(16) + 8, R(2));
    c->budget--;
    R(31) = MEM_R32(R(29) + 24);
    c->budget -= NC_LOAD_CYCLES;
    R(17) = MEM_R32(R(29) + 20);
    c->budget -= NC_LOAD_CYCLES;
    R(16) = MEM_R32(R(29) + 16);
    R(29) += 32;
    c->budget -= NC_LOAD_CYCLES + 3;
    return true;
}

bool nc_credits_draw_move(CPUState *c) {
    if (c->budget <= 32)
        return false;

    const u32 packet = R(8) = R(4);
    const u32 rect = R(5);
    c->budget--;
    bool nonempty = MEM_R16(rect + 4) != 0;
    c->budget -= NC_LOAD_CYCLES + 3;
    if (nonempty) {
        nonempty = MEM_R16(rect + 6) != 0;
        c->budget -= NC_LOAD_CYCLES + 3;
    }
    R(4) = nonempty ? 5 : 0;
    if (!nonempty)
        c->budget -= 2;

    MEM_W32(packet + 4, 0x01000000); /* clear texture cache */
    c->budget -= 2;
    MEM_W32(packet + 8, 0x80000000); /* VRAM-to-VRAM copy */
    R(3) = (u16)R(6);
    c->budget -= 3;
    MEM_W8(packet + 3, (u8)R(4));
    c->budget--;
    R(4) = MEM_R32(rect);
    const u32 destination = (R(7) << 16) | R(3);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(packet + 16, destination);
    c->budget--;
    MEM_W32(packet + 12, R(4));
    c->budget--;
    R(2) = MEM_R32(rect + 4);
    c->budget -= NC_LOAD_CYCLES;
    MEM_W32(packet + 20, R(2));
    c->budget -= 2;
    return true;
}

bool nc_credits_draw_load(CPUState *c) {
    if (c->budget <= 35)
        return false;

    const u32 packet = R(4), rect = R(5);
    s16 width = (s16)MEM_R16(rect + 4);
    c->budget -= NC_LOAD_CYCLES;
    s16 height = (s16)MEM_R16(rect + 6);
    nc_mult(c, (u32)(s32)width, (u32)(s32)height);
    /* Two 16-bit pixels per word, with space for at most twelve words. */
    u32 pixel_words = (u32)(((s32)c->lo + 1) / 2);
    u32 length = pixel_words <= 12 ? pixel_words + 4 : 0;
    c->budget -= NC_LOAD_CYCLES + 11 + (length == 0);

    MEM_W8(packet + 3, (u8)length);
    c->budget--;
    MEM_W32(packet + 4, 0xa0000000); /* CPU-to-VRAM copy */
    c->budget--;
    for (unsigned off = 0; off <= 4; off += 4) {
        u32 word = MEM_R32(rect + off);
        c->budget -= NC_LOAD_CYCLES + 1;
        MEM_W32(packet + 8 + off, word);
        c->budget--;
    }
    R(2) = packet + length * 4;
    R(3) = 0x01000000; /* clear texture cache after the pixel data */
    c->budget -= 3;
    MEM_W32(R(2), R(3));
    c->budget -= 2;
    return true;
}

bool nc_credits_merge_primitives(CPUState *c) {
    if (c->budget <= 17)
        return false;

    u32 first_words = MEM_R8(R(4) + 3);
    c->budget -= NC_LOAD_CYCLES;
    u32 second_words = MEM_R8(R(5) + 3);
    /* The second tag becomes a word in the combined packet. */
    R(3) = first_words + 1 + second_words;
    R(2) = 0;
    c->budget -= NC_LOAD_CYCLES + 6;
    if (R(3) <= 16) {
        MEM_W8(R(4) + 3, (u8)R(3));
        c->budget--;
        MEM_W32(R(5), 0);
        c->budget -= 2;
    } else {
        R(2) = (u32)-1;
        c->budget--;
    }
    c->budget -= 2;
    return true;
}

bool nc_credits_end_packet(CPUState *c) {
    if (c->budget <= 8)
        return false;
    c->budget--;
    u32 tag = MEM_R32(R(4));
    R(3) = 0x00ffffff;
    R(2) = tag | R(3);
    c->budget -= NC_LOAD_CYCLES + 2;
    MEM_W32(R(4), R(2));
    c->budget -= 2;
    return true;
}

bool nc_credits_dump_texture_page(CPUState *c) {
    if (c->budget <= 44)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 24, R(16));
    c->budget--;
    MEM_W32(R(29) + 28, R(31));
    R(16) = R(4);
    R(31) = 0x8001b63c;
    c->budget -= 3;
    NC_FN_ENTER(c, 0x8001c1c8);
    nc_credits_graph_status(c, false);
    bool alternate = R(2) == 1;
    c->budget -= 3;
    if (!alternate) {
        R(31) = 0x8001b650;
        c->budget -= 2;
        NC_FN_ENTER(c, 0x8001c1c8);
        nc_credits_graph_status(c, false);
        alternate = R(2) == 2;
        c->budget -= 3;
    }

    const u32 page = (u16)R(16);
    R(4) = 0x800183a8;
    R(5) = (page >> (alternate ? 9 : 7)) & 3; /* depth */
    R(6) = (page >> (alternate ? 7 : 5)) & 3; /* blend mode */
    R(7) = (page << 6) & 0x7c0;               /* VRAM x */
    c->budget -= alternate ? 11 : 13;
    u32 callback = MEM_R32(0x80024554);
    c->budget -= NC_LOAD_CYCLES + 1;
    u32 y;
    if (alternate) {
        R(3) = callback;
        y = R(2) = (page << 3) & 0x300;
        R(31) = 0x8001b698;
    } else {
        R(8) = callback;
        R(2) = (page >> 2) & 0x200;
        y = R(3) = ((page << 4) & 0x100) | R(2);
        R(31) = 0x8001b6e4;
    }
    MEM_W32(R(29) + 16, y);
    c->budget -= 2;
    nc_call(c, callback);
    if (alternate)
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    load_word(c, 31, R(29) + 28);
    load_word(c, 16, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_credits_dump_palette(CPUState *c) {
    if (c->budget <= 14)
        return false;
    const u32 palette = (u16)R(4);
    R(29) -= 24;
    R(4) = 0x800183c0; /* diagnostic format string */
    R(5) = (palette & 0x3f) << 4;
    c->budget -= 8;
    R(2) = MEM_R32(0x80024554);
    c->budget -= NC_LOAD_CYCLES;
    MEM_W32(R(29) + 16, R(31));
    R(31) = 0x8001b728;
    R(6) = palette >> 6;
    c->budget -= 3;
    nc_call(c, R(2));
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_credits_palette_id(CPUState *c) {
    if (c->budget <= 6)
        return false;
    R(4) = (R(4) >> 4) & 0x3f;
    R(2) = (u16)((R(5) << 6) | R(4));
    c->budget -= 6;
    return true;
}

bool nc_credits_video_mode(CPUState *c, bool replace) {
    if (c->budget <= (replace ? 8 : 6))
        return false;
    R(2) = MEM_R32(0x80024504);
    if (replace) {
        R(1) = 0x80020000;
        MEM_W32(R(1) + 0x4504, R(4));
    }
    c->budget -= replace ? 8 : 6;
    return true;
}

bool nc_credits_default_display(CPUState *c) {
    if (c->budget <= 17)
        return false;
    R(3) = MEM_R32(R(29) + 0x10); /* fifth argument: height */
    R(2) = R(4);
    c->budget -= NC_LOAD_CYCLES + 1;
    for (unsigned i = 0; i < 3; i++) {
        MEM_W16(R(2) + i * 2, (u16)R(5 + i));
        c->budget--;
    }
    /* Clear the screen rectangle and mode flags, in their guest store order. */
    for (unsigned off = 8; off <= 14; off += 2) {
        MEM_W16(R(2) + off, 0);
        c->budget--;
    }
    const unsigned offsets[] = {0x11, 0x10, 0x13, 0x12};
    for (unsigned i = 0; i < 4; i++) {
        MEM_W8(R(2) + offsets[i], 0);
        c->budget--;
    }
    MEM_W16(R(2) + 6, (u16)R(3));
    c->budget -= 2;
    return true;
}

bool nc_credits_priority_packet(CPUState *c) {
    bool check_mask = R(5) != 0;
    if (c->budget <= (check_mask ? 10 : 9))
        return false;

    R(2) = 2;
    c->budget--;
    MEM_W8(R(4) + 3, 2); /* Two command words follow the DMA tag. */
    R(3) = 0xe6000000u | (check_mask ? 2u : 0u);
    c->budget -= check_mask ? 4 : 3;
    R(2) = R(3) | (R(6) != 0); /* Set the mask bit on written pixels. */
    c->budget -= 2;
    MEM_W32(R(4) + 4, R(2));
    c->budget--;
    MEM_W32(R(4) + 8, 0);
    c->budget -= 2;
    return true;
}

/* Both environment diagnostics reload the callback before every report. */
static void report_fields(CPUState *c, u32 format, u32 resume, bool rectangle) {
    unsigned reg = rectangle ? 3 : 2;
    set_reg(c, reg, 0x80020000);
    load_word(c, reg, R(reg) + 0x4554);
    set_reg(c, 4, 0x80020000);
    set_reg(c, 4, format);
    u32 callback = R(reg);
    R(31) = resume;
    if (rectangle)
        MEM_W32(R(29) + 16, R(2));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, callback);
}

static void report_rectangle(CPUState *c, unsigned offset, u32 format, u32 resume) {
    for (unsigned j = 0; j < 4; j++) {
        R(j == 3 ? 2 : 5 + j) = (u32)(s32)(s16)MEM_R16(R(16) + offset + j * 2);
        NC_POLL(c, NC_LOAD_CYCLES);
    }
    report_fields(c, format, resume, true);
}

static void report_flag(CPUState *c, unsigned offset, u32 format, u32 resume) {
    R(5) = MEM_R8(R(16) + offset);
    NC_POLL(c, NC_LOAD_CYCLES);
    report_fields(c, format, resume, false);
}

static void report_draw_page(CPUState *c) {
    R(31) = 0x8001bcd4;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001c1c8);
    set_reg(c, 3, 1);
    bool alternate = R(2) == 1;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (!alternate) {
        R(31) = 0x8001bce8;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8001c1c8);
        set_reg(c, 3, 2);
        alternate = R(2) == 2;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    }

    set_reg(c, 4, 0x80020000);
    set_reg(c, 4, 0x800183a8);
    R(2) = MEM_R16(R(16) + 20);
    NC_POLL(c, NC_LOAD_CYCLES);
    unsigned reg = alternate ? 3 : 8;
    set_reg(c, reg, 0x80020000);
    load_word(c, reg, R(reg) + 0x4554);
    set_reg(c, 5, R(2) >> (alternate ? 9 : 7));
    set_reg(c, 5, R(5) & 3);
    set_reg(c, 6, R(2) >> (alternate ? 7 : 5));
    set_reg(c, 6, R(6) & 3);
    set_reg(c, 7, R(2) << 6);
    set_reg(c, 7, R(7) & 0x7c0);
    if (alternate) {
        set_reg(c, 2, R(2) << 3);
        set_reg(c, 2, R(2) & 0x300);
    } else {
        set_reg(c, 3, R(2) << 4);
        set_reg(c, 3, R(3) & 0x100);
        set_reg(c, 2, R(2) >> 2);
        set_reg(c, 2, R(2) & 0x200);
        set_reg(c, 3, R(3) + R(2));
    }
    u32 callback = R(reg);
    R(31) = alternate ? 0x8001bd30 : 0x8001bd80;
    MEM_W32(R(29) + 16, R(alternate ? 2 : 3)); /* VRAM y */
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, callback);
    if (alternate)
        NC_POLL(c, 2 * NC_INSN_CYCLES);
}

static bool dump_environment(CPUState *c, bool draw) {
    if (c->budget <= 4)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 24, R(16));
    R(16) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 28, R(31));
    c->budget--;

    if (draw) {
        report_rectangle(c, 0, 0x800183d0, 0x8001bc4c); /* clipping rectangle */
        for (unsigned j = 0; j < 2; j++) {
            R(5 + j) = (u32)(s32)(s16)MEM_R16(R(16) + 8 + j * 2);
            NC_POLL(c, NC_LOAD_CYCLES);
        }
        report_fields(c, 0x800183e8, 0x8001bc6c, false); /* drawing offset */
        report_rectangle(c, 12, 0x800183f8, 0x8001bc94); /* texture window */
        report_flag(c, 22, 0x80018410, 0x8001bcb0); /* dithering */
        report_flag(c, 23, 0x8001841c, 0x8001bccc); /* draw to display */
        report_draw_page(c);
    } else {
        report_rectangle(c, 0, 0x80018428, 0x8001bdcc);
        report_rectangle(c, 8, 0x80018444, 0x8001bdf4);
        report_flag(c, 16, 0x80018460, 0x8001be10); /* interlace */
        report_flag(c, 17, 0x8001846c, 0x8001be2c); /* RGB24 */
    }
    load_word(c, 31, R(29) + 28);
    load_word(c, 16, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_credits_dump_draw(CPUState *c) {
    return dump_environment(c, true);
}

bool nc_credits_dump_display(CPUState *c) {
    return dump_environment(c, false);
}

/* SetDispMask invalidates the cached display environment when blanking the screen. */
bool nc_credits_set_display_mask(CPUState *c) {
    if (c->budget <= 13)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    R(17) = 0x8002455a;
    c->budget -= 3;
    MEM_W32(R(29) + 24, R(31));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    R(2) = MEM_R8(R(17)) < 2;
    R(16) = R(4);
    c->budget -= NC_LOAD_CYCLES + 4;
    if (!R(2)) {
        set_reg(c, 4, 0x80020000);
        set_reg(c, 4, 0x80018554);
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024554);
        NC_POLL(c, 1);
        u32 diagnostic = R(2);
        R(31) = 0x8001c28c;
        R(5) = R(16);
        NC_POLL(c, 2);
        nc_call(c, diagnostic);
    }
    bool blank = R(16) == 0;
    R(4) = R(17) + 0x6a;
    NC_POLL(c, 2);
    if (blank) {
        set_reg(c, 5, UINT32_MAX);
        R(31) = 0x8001c2a0;
        R(6) = 20;
        NC_POLL(c, 2);
        nc_call(c, 0x8001ec78); /* fill the display environment with 0xff */
    }
    set_reg(c, 4, 0x03000000);
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024550);
    blank = R(16) == 0;
    R(4) |= 1;
    NC_POLL(c, 2);
    if (!blank)
        set_reg(c, 4, 0x03000000);
    load_word(c, 2, R(2) + 16);
    NC_POLL(c, 1);
    u32 control = R(2);
    R(31) = 0x8001c2c8;
    NC_POLL(c, 2);
    nc_call(c, control); /* GP1 display enable */
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* Build the VRAM rectangle in 16-bit words, upload it, then encode the texture page. */
bool nc_credits_load_tpage(CPUState *c) {
    if (c->budget <= 38)
        return false;
    R(29) -= 48;
    c->budget--;
    MEM_W32(R(29) + 36, R(19));
    c->budget--;
    R(19) = MEM_R32(R(29) + 64); /* y */
    c->budget -= NC_LOAD_CYCLES;
    R(3) = MEM_R32(R(29) + 68); /* width in pixels */
    c->budget -= NC_LOAD_CYCLES;
    R(2) = MEM_R32(R(29) + 72); /* height */
    R(8) = R(4);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 24, R(16));
    R(16) = R(5); /* texture depth */
    c->budget -= 2;
    MEM_W32(R(29) + 32, R(18));
    R(18) = R(6); /* blend mode */
    c->budget -= 2;
    MEM_W32(R(29) + 28, R(17));
    R(17) = R(7); /* x */
    c->budget -= 2;
    MEM_W32(R(29) + 40, R(31));
    c->budget--;
    MEM_W16(R(29) + 16, R(17));
    c->budget--;
    MEM_W16(R(29) + 22, R(2));
    R(2) = 1;
    c->budget -= 2;
    MEM_W16(R(29) + 18, R(19));
    c->budget -= 2;

    const s32 width = (s32)R(3);
    switch ((s32)R(16)) {
    case 0: /* four 4-bit pixels per word */
        R(2) = (u32)(width / 4);
        c->budget -= 8 + (width < 0);
        MEM_W16(R(29) + 20, R(2));
        c->budget -= 3;
        break;
    case 1: /* two 8-bit pixels per word */
        R(2) = (u32)(width / 2);
        c->budget -= 3;
        MEM_W16(R(29) + 20, R(2));
        c->budget -= 3;
        break;
    case 2:
        R(2) = 2;
        c->budget -= 6;
        MEM_W16(R(29) + 20, R(3));
        c->budget -= 2;
        break;
    default: /* Other modes leave the stack rectangle's width untouched. */
        R(2) = (s32)R(16) < 0 ? 1 : 2;
        c->budget -= (s32)R(16) < 0 ? 7 : 8;
        break;
    }
    R(4) = R(29) + 16;
    R(31) = 0x8001b30c;
    R(5) = R(8);
    c->budget -= 2;
    nc_call(c, 0x8001c5a4); /* LoadImage */

    set_reg(c, 4, R(16));
    set_reg(c, 5, R(18));
    set_reg(c, 6, R(17));
    R(31) = 0x8001b320;
    R(7) = R(19);
    NC_POLL(c, 2);
    nc_call(c, 0x8001b548); /* GetTPage */
    set_reg(c, 2, R(2) & 0xffff);
    load_word(c, 31, R(29) + 40);
    load_word(c, 19, R(29) + 36);
    load_word(c, 18, R(29) + 32);
    load_word(c, 17, R(29) + 28);
    load_word(c, 16, R(29) + 24);
    set_reg(c, 29, R(29) + 48);
    NC_POLL(c, 2);
    return true;
}

/* Upload the palette as a single VRAM row and return its CLUT identifier. */
static bool upload_clut(CPUState *c, u32 width, u32 entry) {
    if (c->budget <= 16)
        return false;
    R(29) -= 40;
    R(2) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 24, R(16));
    R(16) = R(5);
    c->budget -= 2;
    MEM_W32(R(29) + 28, R(17));
    R(17) = R(6);
    R(4) = R(29) + 16;
    R(5) = R(2);
    R(2) = width;
    c->budget -= 5;
    MEM_W16(R(29) + 20, R(2));
    R(2) = 1;
    c->budget -= 2;
    MEM_W32(R(29) + 32, R(31));
    c->budget--;
    MEM_W16(R(29) + 16, R(16));
    c->budget--;
    MEM_W16(R(29) + 18, R(17));
    c->budget--;
    R(31) = entry + 0x40;
    MEM_W16(R(29) + 22, R(2));
    c->budget -= 2;
    nc_call(c, 0x8001c5a4); /* LoadImage */

    set_reg(c, 4, R(16));
    R(31) = entry + 0x4c;
    R(5) = R(17);
    NC_POLL(c, 2);
    nc_call(c, 0x8001b610); /* GetClut */
    set_reg(c, 2, R(2) & 0xffff);
    load_word(c, 31, R(29) + 32);
    load_word(c, 17, R(29) + 28);
    load_word(c, 16, R(29) + 24);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_load_clut(CPUState *c) {
    return upload_clut(c, 256, 0x8001b344);
}

bool nc_credits_load_clut2(CPUState *c) {
    return upload_clut(c, 16, 0x8001b3ac);
}

/* Sample each condition before polling for the branch and its delay slot. */
static bool rectangle_branch(CPUState *c, bool condition) {
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return condition;
}

/* Level one checks the rectangle against the signed VRAM limits in SDK order. */
static bool rectangle_out_of_bounds(CPUState *c) {
    R(5) = (s16)MEM_R16(R(16) + 4); /* width */
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 3, 0x80020000);
    R(3) = (s16)MEM_R16(0x8002455c);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, (s32)R(3) < (s32)R(5));
    if (rectangle_branch(c, R(2)))
        return true;

    R(7) = (s16)MEM_R16(R(16)); /* x */
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, R(5) + R(7));
    set_reg(c, 2, (s32)R(3) < (s32)R(2));
    if (rectangle_branch(c, R(2)))
        return true;

    R(3) = (s16)MEM_R16(R(16) + 2); /* y */
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 4, 0x80020000);
    R(4) = (s16)MEM_R16(0x8002455e);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, (s32)R(4) < (s32)R(3));
    if (rectangle_branch(c, R(2)))
        return true;

    R(6) = (s16)MEM_R16(R(16) + 6); /* height */
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, R(3) + R(6));
    set_reg(c, 2, (s32)R(4) < (s32)R(2));
    if (rectangle_branch(c, R(2)))
        return true;
    if (rectangle_branch(c, (s32)R(5) <= 0))
        return true;
    if (rectangle_branch(c, (s32)R(7) < 0))
        return true;
    if (rectangle_branch(c, (s32)R(3) < 0))
        return true;
    return rectangle_branch(c, (s32)R(6) <= 0);
}

bool nc_credits_check_rect(CPUState *c) {
    if (c->budget <= 8 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    R(29) -= 32;
    R(8) = R(4); /* caller's operation name */
    c->budget -= 2;
    MEM_W32(R(29) + 24, R(16));
    R(16) = R(5);
    c->budget -= 3;
    R(3) = MEM_R8(0x8002455a);
    R(2) = 1;
    c->budget -= NC_LOAD_CYCLES + 1;
    bool validate = R(3) == 1;
    MEM_W32(R(29) + 28, R(31));
    NC_POLL(c, 2);

    bool report;
    if (validate) {
        report = rectangle_out_of_bounds(c);
    } else {
        set_reg(c, 2, 2);
        report = R(3) == 2;
        NC_POLL(c, 2);
        if (!report)
            NC_POLL(c, 2);
    }
    if (report) {
        set_reg(c, 4, 0x80020000);
        set_reg(c, 4, validate ? 0x8001857c : 0x8001859c);
        if (validate)
            NC_POLL(c, 2);
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024554);
        NC_POLL(c, 1);
        R(31) = 0x8001c438;
        R(5) = R(8);
        NC_POLL(c, 2);
        nc_call(c, R(2));
        report_rectangle(c, 0, 0x80018588, 0x8001c460);
    }
    load_word(c, 31, R(29) + 28);
    load_word(c, 16, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* Both fills pack RGB from the low argument bytes. ClearImage2 also sets bit 31. */
static bool queue_image_fill(CPUState *c, bool high_bit) {
    const unsigned green = high_bit ? 16 : 17;
    const unsigned blue = high_bit ? 17 : 16;
    const unsigned driver = high_bit ? 3 : 2;
    if (c->budget <= 14)
        return false;
    R(29) -= 40;
    c->budget--;
    MEM_W32(R(29) + 28, R(19));
    R(19) = R(4);
    R(4) = 0x800185a0; /* "ClearImage" */
    c->budget -= 4;
    MEM_W32(R(29) + 24, R(18));
    R(18) = R(5);
    R(5) = R(19);
    c->budget -= 3;
    MEM_W32(R(29) + (high_bit ? 16 : 20), R(green));
    R(green) = R(6);
    c->budget -= 2;
    MEM_W32(R(29) + (high_bit ? 20 : 16), R(blue));
    c->budget--;
    MEM_W32(R(29) + 32, R(31));
    R(31) = high_bit ? 0x8001c540 : 0x8001c4ac;
    R(blue) = R(7);
    c->budget -= 3;
    nc_call(c, 0x8001c34c); /* checkRECT */

    set_reg(c, 5, R(19));
    set_reg(c, blue, R(blue) & 0xff);
    set_reg(c, blue, R(blue) << 16);
    set_reg(c, green, R(green) & 0xff);
    set_reg(c, green, R(green) << 8);
    if (high_bit) {
        set_reg(c, 2, 0x80000000);
        set_reg(c, green, R(green) | R(2));
    }
    set_reg(c, blue, R(blue) | R(green));
    set_reg(c, 18, R(18) & 0xff);
    set_reg(c, driver, 0x80020000);
    load_word(c, driver, 0x80024550);
    set_reg(c, 6, 8);
    load_word(c, 4, R(driver) + 0x0c);
    load_word(c, 2, R(driver) + 8);
    NC_POLL(c, 1);
    u32 enqueue = R(2);
    R(31) = high_bit ? 0x8001c584 : 0x8001c4e8;
    R(7) = R(blue) | R(18);
    NC_POLL(c, 2);
    nc_call(c, enqueue);

    load_word(c, 31, R(29) + 32);
    load_word(c, 19, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_clear_image(CPUState *c) {
    return queue_image_fill(c, false);
}

bool nc_credits_clear_image2(CPUState *c) {
    return queue_image_fill(c, true);
}

/* Validate the VRAM rectangle, then queue its eight bytes and the pixel buffer. */
static bool queue_image_transfer(CPUState *c, bool store) {
    if (c->budget <= 10)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(5);
    R(4) = store ? 0x800185b8 : 0x800185ac; /* name passed to checkRECT */
    c->budget -= 4;
    MEM_W32(R(29) + 24, R(31));
    R(31) = store ? 0x8001c630 : 0x8001c5cc;
    R(5) = R(16);
    c->budget -= 3;
    nc_call(c, 0x8001c34c); /* checkRECT */

    set_reg(c, 5, R(16));
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024550);
    set_reg(c, 6, 8);
    load_word(c, 4, R(2) + (store ? 0x1c : 0x20));
    load_word(c, 2, R(2) + 8);
    NC_POLL(c, 1);
    u32 enqueue = R(2);
    R(31) = store ? 0x8001c654 : 0x8001c5f0;
    R(7) = R(17);
    NC_POLL(c, 2);
    nc_call(c, enqueue);
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_load_image(CPUState *c) {
    return queue_image_transfer(c, false);
}

bool nc_credits_store_image(CPUState *c) {
    return queue_image_transfer(c, true);
}

/* MoveImage fills in the VRAM copy packet and submits it through the active driver. */
bool nc_credits_move_image(CPUState *c) {
    if (c->budget <= 12)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 24, R(18));
    R(18) = R(5);
    c->budget -= 2;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(6);
    R(4) = 0x800185c4; /* "MoveImage" */
    c->budget -= 4;
    MEM_W32(R(29) + 28, R(31));
    R(31) = 0x8001c69c;
    R(5) = R(16);
    c->budget -= 3;
    nc_call(c, 0x8001c34c); /* checkRECT */

    R(2) = (s32)(s16)MEM_R16(R(16) + 4);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    bool has_width = R(2) != 0;
    R(2) = UINT32_MAX;
    NC_POLL(c, 2);
    if (has_width) {
        R(2) = (s32)(s16)MEM_R16(R(16) + 6);
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        bool has_height = R(2) != 0;
        R(2) = R(17) << 16;
        NC_POLL(c, 2);
        if (has_height) {
            set_reg(c, 3, R(18) & 0xffff);
            set_reg(c, 2, R(2) | R(3));
            set_reg(c, 5, 0x80020000);
            set_reg(c, 5, 0x80024608);
            load_word(c, 4, R(16)); /* source XY */
            set_reg(c, 3, 0x80020000);
            load_word(c, 3, 0x80024550);
            set_reg(c, 6, 20); /* packet size */
            set_reg(c, 1, 0x80020000);
            MEM_W32(0x8002460c, R(2)); /* destination XY */
            NC_POLL(c, 1);
            MEM_W32(R(5), R(4));
            NC_POLL(c, 1);
            load_word(c, 2, R(16) + 4);
            set_reg(c, 7, 0);
            set_reg(c, 1, 0x80020000);
            MEM_W32(0x80024610, R(2)); /* width and height */
            NC_POLL(c, 1);
            load_word(c, 4, R(3) + 0x18);
            load_word(c, 2, R(3) + 8);
            NC_POLL(c, 1);
            u32 enqueue = R(2);
            R(31) = 0x8001c714;
            R(5) -= 8;
            NC_POLL(c, 2);
            nc_call(c, enqueue);
        } else {
            R(2) = UINT32_MAX;
            NC_POLL(c, 2);
        }
    }
    load_word(c, 31, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

static void ordering_table_prologue(CPUState *c) {
    c->budget--;
    R(2) = MEM_R8(0x8002455a);
    R(29) -= 32;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(5);
    R(2) = R(2) < 2;
    c->budget -= 3;
    MEM_W32(R(29) + 24, R(31));
    c->budget -= 2;
}

/* Link each tag to the next and return the address of the final tag. */
bool nc_credits_clear_otag(CPUState *c) {
    if (c->budget <= 12)
        return false;
    ordering_table_prologue(c);
    if (!R(2)) {
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024554);
        set_reg(c, 4, 0x80020000);
        set_reg(c, 4, 0x800185d0);
        set_reg(c, 5, R(16));
        u32 diagnostic = R(2);
        R(31) = 0x8001c774;
        R(6) = R(17);
        NC_POLL(c, 2);
        nc_call(c, diagnostic);
    }

    set_reg(c, 17, R(17) - 1);
    R(3) = 0xff0000;
    NC_POLL(c, 2);
    if (R(17)) {
        set_reg(c, 5, 0xff0000);
        set_reg(c, 5, 0xffffff);
        set_reg(c, 6, 0xff000000);
        do {
            set_reg(c, 17, R(17) - 1);
            set_reg(c, 4, R(16) + 4);
            MEM_W8(R(16) + 3, 0); /* zero packet length before updating its link */
            NC_POLL(c, 1);
            load_word(c, 2, R(16));
            set_reg(c, 3, R(4) & R(5));
            set_reg(c, 2, R(2) & R(6));
            set_reg(c, 2, R(2) | R(3));
            MEM_W32(R(16), R(2));
            NC_POLL(c, 1);
            R(16) = R(4);
            NC_POLL(c, 2);
        } while (R(17));
        set_reg(c, 3, 0xff0000);
    }
    set_reg(c, 3, R(3) | 0xffff);
    set_reg(c, 2, 0x80020000);
    set_reg(c, 2, 0x80024614);
    set_reg(c, 2, R(2) & R(3));
    MEM_W32(R(16), R(2));
    NC_POLL(c, 1);
    set_reg(c, 2, R(16));
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* Build a reverse ordering table through the driver, then link its first tag
 * to the SDK terminator and return the original table address. */
bool nc_credits_clear_otag_reverse(CPUState *c) {
    if (c->budget <= 12)
        return false;
    ordering_table_prologue(c);

    if (!R(2)) {
        set_reg(c, 4, 0x80020000);
        set_reg(c, 4, 0x800185e8);
        set_reg(c, 5, R(16));
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024554);
        NC_POLL(c, 1);
        u32 diagnostic = R(2);
        R(31) = 0x8001c830;
        R(6) = R(17);
        NC_POLL(c, 2);
        nc_call(c, diagnostic);
    }
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024550);
    set_reg(c, 4, R(16));
    load_word(c, 2, R(2) + 0x2c);
    NC_POLL(c, 1);
    u32 clear_table = R(2);
    R(31) = 0x8001c84c;
    R(5) = R(17);
    NC_POLL(c, 2);
    nc_call(c, clear_table);

    set_reg(c, 4, 0xff0000);
    set_reg(c, 4, 0xffffff);
    set_reg(c, 2, R(16));
    set_reg(c, 3, 0x80020000);
    set_reg(c, 3, 0x80024614);
    set_reg(c, 3, R(3) & R(4));
    MEM_W32(R(2), R(3));
    NC_POLL(c, 1);
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* DrawPrim saves the packet length before synchronizing, then submits its payload. */
bool nc_credits_draw_primitive(CPUState *c) {
    if (c->budget <= 17)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    R(2) = 0x80020000;
    c->budget -= 3;
    R(2) = MEM_R32(0x80024550);
    c->budget -= NC_LOAD_CYCLES;
    MEM_W32(R(29) + 24, R(31));
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    R(2) = MEM_R32(R(2) + 0x3c);
    c->budget -= NC_LOAD_CYCLES;
    R(17) = MEM_R8(R(16) + 3);
    u32 sync = R(2);
    R(31) = 0x8001c8ac;
    R(4) = 0;
    c->budget -= NC_LOAD_CYCLES + 2;
    nc_call(c, sync);

    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024550);
    set_reg(c, 4, R(16) + 4);
    load_word(c, 2, R(2) + 0x14);
    NC_POLL(c, 1);
    u32 write_commands = R(2);
    R(31) = 0x8001c8c8;
    R(5) = R(17);
    NC_POLL(c, 2);
    nc_call(c, write_commands);
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* Save the graphics call argument and test the SDK debug level. */
static void graphics_debug_prologue(CPUState *c) {
    c->budget--;
    R(2) = MEM_R8(0x8002455a);
    R(29) -= 24;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    R(2) = R(2) < 2;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget -= 2;
}

static void graphics_diagnostic(CPUState *c, u32 format, u32 return_pc, unsigned argument_reg) {
    if (!R(2)) {
        set_reg(c, 4, 0x80020000);
        set_reg(c, 4, format);
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024554);
        NC_POLL(c, 1);
        u32 diagnostic = R(2);
        R(31) = return_pc;
        R(5) = R(argument_reg);
        NC_POLL(c, 2);
        nc_call(c, diagnostic);
    }
}

static void build_draw_env_packet(CPUState *c, u32 resume) {
    set_reg(c, 16, R(17) + 28); /* embedded packet */
    set_reg(c, 4, R(16));
    R(31) = resume;
    R(5) = R(17);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001d1cc); /* SetDrawEnv */

    set_reg(c, 4, 0x00ff0000);
    set_reg(c, 4, 0x00ffffff);
    set_reg(c, 5, R(16));
    set_reg(c, 6, 64); /* packet bytes copied by the queue */
}

/* Merge the link address into the packet header, enqueue it and cache the DRAWENV. */
static void submit_draw_env(CPUState *c, unsigned debug_reg, u32 queue_resume, u32 copy_resume) {
    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x80024550);
    set_reg(c, 2, R(2) | R(4));
    MEM_W32(R(17) + 28, R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 4, R(3) + 0x18);
    load_word(c, 2, R(3) + 8);
    NC_POLL(c, NC_INSN_CYCLES);
    u32 enqueue = R(2);
    R(31) = queue_resume;
    R(7) = 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, enqueue);

    set_reg(c, 4, R(debug_reg) + 14);
    set_reg(c, 5, R(17));
    R(31) = copy_resume;
    R(6) = 92; /* full DRAWENV, including the packet */
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x80019928); /* memcpy */
}

/* Build the embedded draw packet, submit it and save the active DRAWENV. */
bool nc_credits_put_draw_env(CPUState *c) {
    if (c->budget <= 11 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 24, R(18));
    R(18) = 0x8002455a;
    c->budget -= 3;
    MEM_W32(R(29) + 28, R(31));
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    R(2) = MEM_R8(R(18)) < 2;
    R(17) = R(4); /* environment */
    c->budget -= NC_LOAD_CYCLES + 4;
    graphics_diagnostic(c, 0x80018614, 0x8001c9a0, 17);

    build_draw_env_packet(c, 0x8001c9b0);
    load_word(c, 2, R(17) + 28);
    submit_draw_env(c, 18, 0x8001c9e8, 0x8001c9f8);
    set_reg(c, 2, R(17));
    load_word(c, 31, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Prepend the draw environment packet to the supplied ordering table. */
bool nc_credits_draw_otag_env(CPUState *c) {
    if (c->budget <= 13 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    R(29) -= 40;
    c->budget--;
    MEM_W32(R(29) + 24, R(18));
    R(18) = R(4); /* ordering table */
    c->budget -= 2;
    MEM_W32(R(29) + 28, R(19));
    R(19) = 0x8002455a;
    c->budget -= 3;
    MEM_W32(R(29) + 32, R(31));
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    R(2) = MEM_R8(R(19)) < 2;
    R(17) = R(5); /* environment */
    c->budget -= NC_LOAD_CYCLES + 4;
    if (!R(2)) {
        set_reg(c, 4, 0x80020000);
        set_reg(c, 4, 0x8001862c);
        set_reg(c, 5, R(18));
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024554);
        NC_POLL(c, NC_INSN_CYCLES);
        u32 diagnostic = R(2);
        R(31) = 0x8001ca70;
        R(6) = R(17);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, diagnostic);
    }
    build_draw_env_packet(c, 0x8001ca80);
    set_reg(c, 3, 0xff000000);
    load_word(c, 2, R(17) + 28);
    set_reg(c, 4, R(18) & R(4)); /* 24-bit ordering table address */
    set_reg(c, 2, R(2) & R(3)); /* retain the packet length */
    submit_draw_env(c, 19, 0x8001cac4, 0x8001cad4);
    load_word(c, 31, R(29) + 32);
    load_word(c, 19, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* DrawOTag queues the active driver's DMA-chain method with the ordering table. */
bool nc_credits_draw_otag(CPUState *c) {
    if (c->budget <= 10)
        return false;
    graphics_debug_prologue(c);
    graphics_diagnostic(c, 0x80018600, 0x8001c91c, 16);
    set_reg(c, 5, R(16));
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024550);
    set_reg(c, 6, 0);
    load_word(c, 4, R(2) + 0x18);
    load_word(c, 2, R(2) + 8);
    NC_POLL(c, 1);
    u32 enqueue = R(2);
    R(31) = 0x8001c940;
    R(7) = 0;
    NC_POLL(c, 2);
    nc_call(c, enqueue);

    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_draw_sync(CPUState *c) {
    if (c->budget <= 10)
        return false;
    graphics_debug_prologue(c);
    graphics_diagnostic(c, 0x80018568, 0x8001c31c, 16);

    /* The diagnostic can replace the active driver before this lookup. */
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024550);
    NC_POLL(c, 1);
    load_word(c, 2, R(2) + 0x3c);
    NC_POLL(c, 1);
    u32 sync = R(2);
    R(31) = 0x8001c338;
    R(4) = R(16);
    NC_POLL(c, 2);
    nc_call(c, sync);
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_draw_sync_callback(CPUState *c) {
    if (c->budget <= 10)
        return false;
    graphics_debug_prologue(c);
    if (!R(2)) {
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, R(2) + 0x4554);
        set_reg(c, 4, 0x80020000);
        set_reg(c, 4, 0x80018538);
        u32 diagnostic = R(2);
        R(31) = 0x8001c220;
        R(5) = R(16);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, diagnostic);
    }

    /* Read the previous handler after the diagnostic returns. */
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, R(2) + 0x4564);
    set_reg(c, 1, 0x80020000);
    MEM_W32(R(1) + 0x4564, R(16));
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

static void fill_graphics_state(CPUState *c, u32 length, u32 resume) {
    R(31) = resume;
    R(6) = length;
    NC_POLL(c, 2);
    nc_call(c, 0x8001ec78);
}

static void initialize_graphics_state(CPUState *c) {
    set_reg(c, 4, 0x80020000);
    set_reg(c, 4, 0x800184ac);
    set_reg(c, 5, 0x80020000);
    set_reg(c, 5, 0x80024510);
    set_reg(c, 16, 0x80020000);
    set_reg(c, 16, 0x80024558);
    R(31) = 0x8001be88;
    R(6) = R(16);
    NC_POLL(c, 2);
    nc_call(c, 0x80019a60); /* printf */
    set_reg(c, 4, R(16));
    set_reg(c, 5, 0);
    fill_graphics_state(c, 0x80, 0x8001be98);
    gpu_sync_call(c, 0x8001a698, 0x8001bea0); /* ResetCallback */
    set_reg(c, 2, 0x00ff0000);
    load_gpu_global(c, 4, 0x80024550);
    set_reg(c, 2, R(2) | 0xffff);
    R(31) = 0x8001beb8;
    R(4) &= R(2);
    NC_POLL(c, 2);
    nc_call(c, 0x8001eca4); /* GPU_cw */
    R(31) = 0x8001bec0;
    R(4) = R(17);
    NC_POLL(c, 2);
    nc_call(c, 0x8001e758);

    /* The reset result selects the VRAM width and height tables by low byte. */
    set_reg(c, 4, R(16) + 16);
    MEM_W8(R(16), (u8)R(2));
    NC_POLL(c, 1);
    R(2) = MEM_R8(R(16));
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 3, 1);
    set_reg(c, 1, 0x80020000);
    MEM_W8(0x80024559, (u8)R(3));
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) << 2);
    set_reg(c, 1, 0x80020000);
    set_reg(c, 1, R(1) + R(2));
    R(3) = MEM_R16(R(1) + 0x45d8);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(2) = MEM_R8(R(16));
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 5, UINT32_MAX);
    set_reg(c, 2, R(2) << 2);
    set_reg(c, 1, 0x80020000);
    MEM_W16(0x8002455c, (u16)R(3));
    NC_POLL(c, 1);
    set_reg(c, 1, 0x80020000);
    set_reg(c, 1, R(1) + R(2));
    R(2) = MEM_R16(R(1) + 0x45ec);
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 1, 0x80020000);
    MEM_W16(0x8002455e, (u16)R(2));
    NC_POLL(c, 1);
    fill_graphics_state(c, 0x5c, 0x8001bf18);
    set_reg(c, 4, R(16) + 0x6c);
    set_reg(c, 5, UINT32_MAX);
    fill_graphics_state(c, 0x14, 0x8001bf28);
    R(2) = MEM_R8(R(16));
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 2);
}

bool nc_credits_reset_graph(CPUState *c) {
    if (c->budget <= 7)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(4);
    R(3) = R(17) & 7;
    c->budget -= 3;
    MEM_W32(R(29) + 24, R(31));
    c->budget--;
    bool initialize = R(3) == 0;
    MEM_W32(R(29) + 16, R(16));
    c->budget -= 2;
    if (!initialize) {
        set_reg(c, 2, 3);
        initialize = R(3) == R(2);
        NC_POLL(c, 2);
    }
    if (initialize) {
        initialize_graphics_state(c);
    } else {
        set_reg(c, 2, 0x80020000);
        R(2) = MEM_R8(0x8002455a);
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) < 2);
        NC_POLL(c, 2);
        graphics_diagnostic(c, 0x800184cc, 0x8001bf68, 17);
        load_gpu_global(c, 2, 0x80024550);
        NC_POLL(c, 1);
        load_word(c, 2, R(2) + 0x34);
        NC_POLL(c, 1);
        u32 reset = R(2);
        R(31) = 0x8001bf84;
        R(4) = 1;
        NC_POLL(c, 2);
        nc_call(c, reset);
    }
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* Set the GPU display-mode reverse bit and return the previous flag byte. */
bool nc_credits_set_graph_reverse(CPUState *c) {
    const int entry_cycles = 11 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    if (c->budget <= entry_cycles)
        return false;
    c->budget--;
    R(2) = MEM_R8(0x8002455a);
    R(29) -= 32;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = 0x8002455b;
    c->budget -= 3;
    MEM_W32(R(29) + 28, R(31));
    c->budget--;
    MEM_W32(R(29) + 24, R(18));
    c->budget--;
    R(18) = MEM_R8(R(16));
    R(2) = R(2) < 2;
    R(17) = R(4);
    c->budget -= NC_LOAD_CYCLES + 3;
    graphics_diagnostic(c, 0x800184e0, 0x8001bfec, 17);

    /* Query GPU parameter 8, then combine it with the current reverse flag. */
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024550);
    MEM_W8(R(16), (u8)R(17));
    NC_POLL(c, 1);
    load_word(c, 2, R(2) + 0x28);
    NC_POLL(c, 1);
    R(31) = 0x8001c008;
    R(4) = 8;
    NC_POLL(c, 2);
    nc_call(c, R(2));
    R(3) = MEM_R8(R(16));
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    bool reverse = R(3) != 0;
    R(4) = R(2);
    NC_POLL(c, 2);
    set_reg(c, 2, 0x08000000);
    if (reverse) {
        R(2) |= 0x80;
        NC_POLL(c, 2);
    }
    set_reg(c, 4, R(4) | R(2));
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024550);
    NC_POLL(c, 1);
    load_word(c, 2, R(2) + 0x10);
    NC_POLL(c, 1);
    R(31) = 0x8001c048;
    NC_POLL(c, 2);
    nc_call(c, R(2));

    /* Graphics type 2 needs a second control command. */
    set_reg(c, 3, 0x80020000);
    R(3) = MEM_R8(0x80024558);
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 2, 2);
    bool extra_command = R(3) == R(2);
    R(2) = R(18);
    NC_POLL(c, 2);
    if (extra_command) {
        set_reg(c, 4, 0x20000000);
        set_reg(c, 2, 0x80020000);
        R(2) = MEM_R8(0x8002455b);
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, 3, 0x80020000);
        load_word(c, 3, 0x80024550);
        reverse = R(2) != 0;
        R(4) = 0x20000504;
        NC_POLL(c, 2);
        if (reverse) {
            set_reg(c, 4, 0x20000000);
            set_reg(c, 4, 0x20000501);
        }
        load_word(c, 2, R(3) + 0x10);
        NC_POLL(c, 1);
        R(31) = 0x8001c090;
        NC_POLL(c, 2);
        nc_call(c, R(2));
        set_reg(c, 2, R(18));
    }
    load_word(c, 31, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* Change GPU queue mode and detach the GPU DMA callback when the mode changes. */
bool nc_credits_set_graph_queue(CPUState *c) {
    const int entry_cycles = 11 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    if (c->budget <= entry_cycles)
        return false;
    c->budget--;
    R(2) = MEM_R8(0x8002455a);
    R(29) -= 32;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    R(17) = 0x80024559;
    c->budget -= 3;
    MEM_W32(R(29) + 28, R(31));
    c->budget--;
    MEM_W32(R(29) + 24, R(18));
    c->budget--;
    R(18) = MEM_R8(R(17));
    R(2) = R(2) < 2;
    R(16) = R(4);
    c->budget -= NC_LOAD_CYCLES + 3;
    if (!R(2)) {
        set_reg(c, 4, 0x80020000);
        set_reg(c, 4, 0x80018524);
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024554);
        NC_POLL(c, NC_INSN_CYCLES);
        R(31) = 0x8001c16c;
        R(5) = R(16);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, R(2));
    }

    /* The diagnostic may have changed the mode since the first load. */
    R(2) = MEM_R8(R(17));
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, NC_INSN_CYCLES);
    const bool changed = R(16) != R(2);
    R(2) = R(18);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (changed) {
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024550);
        NC_POLL(c, NC_INSN_CYCLES);
        load_word(c, 2, R(2) + 0x34);
        NC_POLL(c, NC_INSN_CYCLES);
        R(31) = 0x8001c198;
        R(4) = 1;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, R(2));

        set_reg(c, 4, 2);
        set_reg(c, 5, 0);
        R(31) = 0x8001c1a8;
        MEM_W8(R(17), (u8)R(16));
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8001a6f8); /* DMACallback(2, NULL) */
        set_reg(c, 2, R(18));
    }
    load_word(c, 31, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_credits_set_graph_debug(CPUState *c) {
    if (c->budget <= 31)
        return false;
    R(29) -= 24;
    R(3) = 0x8002455a;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    R(16) = MEM_R8(R(3));
    c->budget -= NC_LOAD_CYCLES;
    MEM_W8(R(3), (u8)R(4));
    R(4) &= 0xff;
    R(2) = R(16);
    c->budget -= 4;
    if (R(4)) {
        /* Report the new level, graphics type, and reverse-display flag. */
        c->budget--;
        R(2) = MEM_R32(0x80024554);
        c->budget -= NC_LOAD_CYCLES;
        R(5) = MEM_R8(R(3));
        c->budget -= NC_LOAD_CYCLES + 1;
        R(6) = MEM_R8(0x80024558);
        c->budget -= NC_LOAD_CYCLES + 1;
        R(7) = MEM_R8(0x8002455b);
        R(4) = 0x800184f8;
        R(31) = 0x8001c104;
        c->budget -= NC_LOAD_CYCLES + 4;
        nc_call(c, R(2));
        set_reg(c, 2, R(16));
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Copy the VRAM rectangle and point the descriptor at the following TIM data.
 * The original stores the pointer before the height, including for aliased buffers. */
static u32 tim_rectangle(CPUState *c, u32 source, u32 dest) {
    for (unsigned offset = 0; offset < 6; offset += 2) {
        u16 field = MEM_R16(source + offset);
        c->budget -= NC_LOAD_CYCLES + 1;
        MEM_W16(dest + offset, field);
        c->budget--;
    }
    u16 height = MEM_R16(source + 6);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(dest + 8, source + 8);
    c->budget--;
    MEM_W16(dest + 6, height);
    c->budget--;
    return height;
}

bool nc_credits_tim_info(CPUState *c) {
    if (c->budget <= 63)
        return false;
    u32 flags = MEM_R32(R(4));
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(5), flags);
    c->budget -= 5;
    if (flags & 8) {
        u32 palette = R(4) + 4;
        c->budget--;
        u32 length = MEM_R32(palette);
        u32 pixels = palette + (length & ~3u) + 4;
        c->budget -= NC_LOAD_CYCLES + 5;
        tim_rectangle(c, palette + 4, R(5) + 16);
        R(4) = palette + 12;
        R(3) = tim_rectangle(c, pixels, R(5) + 4);
        R(2) = pixels + 8;
        c->budget--; /* jump with the height store in its delay slot */
    } else {
        u32 pixels = R(4) + 8;
        c->budget--;
        R(2) = tim_rectangle(c, pixels, R(5) + 4);
        R(4) = pixels + 8;
    }
    c->budget -= 2;
    return true;
}

/* Read the two DMA mode bits from SPUCNT. */
static void read_spu_dma_mode(CPUState *c) {
    R(2) = MEM_R16(R(4) + 0x1aa);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, R(2) & 0x30);
}

bool nc_credits_spu_dma_complete(CPUState *c) {
    if (c->budget <= 4 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    c->budget--;
    R(2) = MEM_R32(0x800246c8);
    R(29) -= 24;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 16, R(31));
    c->budget -= 2;
    if (!R(2)) {
        /* A write transfer needs three settling delays before clearing SPUCNT. */
        for (u32 resume = 0x8001f5a8; resume <= 0x8001f5b8; resume += 8) {
            R(31) = resume;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, 0x8001fc78);
        }
    }
    set_reg(c, 4, 0x80020000);
    load_word(c, 4, 0x80024678);
    NC_POLL(c, NC_INSN_CYCLES);
    R(2) = MEM_R16(R(4) + 0x1aa);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, R(2) & 0xffcf);
    MEM_W16(R(4) + 0x1aa, (u16)R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    read_spu_dma_mode(c);
    bool busy = R(2) != 0;
    R(3) = 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (busy) {
        set_reg(c, 3, R(3) + 1);
        do {
            set_reg(c, 2, R(3) < 0xf01);
            const bool exhausted = !R(2);
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            if (exhausted)
                break;
            read_spu_dma_mode(c);
            busy = R(2) != 0;
            R(3)++;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            if (!busy)
                set_reg(c, 3, R(3) - 1);
        } while (busy);
    }

    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x800246b0);
    NC_POLL(c, NC_INSN_CYCLES);
    const bool has_callback = R(2) != 0;
    R(4) = 0xf0000000;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (has_callback) {
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x800246b0);
        NC_POLL(c, NC_INSN_CYCLES);
        u32 callback = R(2);
        R(31) = 0x8001f638;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, callback);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } else {
        set_reg(c, 4, R(4) | 9);
        R(31) = 0x8001f64c;
        R(5) = 0x20;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8001fcd8); /* DeliverEvent */
    }
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* _spu_r_ starts a DMA read using an SPU address and a count of 16-word blocks. */
bool nc_credits_spu_read_blocks(CPUState *c) {
    if (c->budget <= 9 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    c->budget--;
    R(2) = MEM_R32(0x80024678);
    R(29) -= 32;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(4); /* RAM destination */
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    MEM_W32(R(29) + 24, R(31));
    c->budget--;
    MEM_W16(R(2) + 0x1a6, (u16)R(5));
    R(31) = 0x8001f684;
    R(16) = R(6); /* DMA block count */
    c->budget -= 3;
    nc_call(c, 0x8001fc78); /* _spu_FwaitFs */
    R(31) = 0x8001f68c;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001fc78);

    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x80024678);
    NC_POLL(c, NC_INSN_CYCLES);
    R(2) = MEM_R16(R(3) + 0x1aa);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, R(2) | 0x30); /* SPU DMA read mode */
    MEM_W16(R(3) + 0x1aa, (u16)R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    R(31) = 0x8001f6b0;
    R(16) <<= 16;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001fc78);
    R(31) = 0x8001f6b8;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001fc78);
    R(31) = 0x8001f6c0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001fc4c); /* _spu_FsetDelayR */

    set_reg(c, 4, 0x01000000);
    set_reg(c, 4, 0x01000200);
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x8002467c);
    NC_POLL(c, NC_INSN_CYCLES);
    MEM_W32(R(2), R(17)); /* DMA base address */
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024680);
    set_reg(c, 16, R(16) | 16);
    MEM_W32(R(2), R(16)); /* block count and words per block */
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x80024684);
    set_reg(c, 2, 1);
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x800246c8, R(2)); /* record read direction */
    NC_POLL(c, NC_INSN_CYCLES);
    MEM_W32(R(3), R(4)); /* start DMA */
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Send the caller's bytes to the SPU using programmed I/O or DMA. */
bool nc_credits_spu_write(CPUState *c) {
    if (c->budget <= 8 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    c->budget--;
    R(2) = MEM_R32(0x80024694);
    R(29) -= 32;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(4); /* source */
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(5); /* byte count */
    c->budget -= 2;
    MEM_W32(R(29) + 24, R(31));
    c->budget -= 2;
    if (R(2)) {
        set_reg(c, 4, R(17));
        R(31) = 0x8001fa14;
        R(5) = R(16);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8001f3c0); /* _spu_writeByIO */
        set_reg(c, 2, R(16));
    } else {
        set_reg(c, 2, 0x80020000);
        R(2) = MEM_R16(0x80024690);
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, 5, 0x80020000);
        load_word(c, 5, 0x800246a0);
        set_reg(c, 4, 2); /* set transfer address */
        R(31) = 0x8001f9e8;
        R(5) = R(2) << (R(5) & 31);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8001f718);

        R(31) = 0x8001f9f0;
        R(4) = 1; /* select write mode */
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8001f718);

        set_reg(c, 4, 3); /* start DMA */
        set_reg(c, 5, R(17));
        R(31) = 0x8001fa00;
        R(6) = R(16);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8001f718);
        R(2) = R(16);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    }
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Start an SPU-to-RAM DMA transfer and return the requested byte count. */
bool nc_credits_spu_read(CPUState *c) {
    if (c->budget <= 11 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(4); /* destination */
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(5); /* byte count */
    c->budget -= 3;
    R(2) = MEM_R16(0x80024690);
    c->budget -= NC_LOAD_CYCLES + 1;
    R(5) = MEM_R32(0x800246a0);
    R(4) = 2; /* set transfer address */
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 24, R(31));
    R(31) = 0x8001fa64;
    R(5) = R(2) << (R(5) & 31);
    c->budget -= 3;
    nc_call(c, 0x8001f718);

    R(31) = 0x8001fa6c;
    R(4) = 0; /* select read mode */
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001f718);

    set_reg(c, 4, 3); /* start DMA */
    set_reg(c, 5, R(17));
    R(31) = 0x8001fa7c;
    R(6) = R(16);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001f718);

    set_reg(c, 2, R(16));
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* _spu_FsetRXX writes an indexed halfword, optionally in SPU address units. */
bool nc_credits_spu_write_register(CPUState *c) {
    if (c->budget <= 15)
        return false;
    R(2) = R(4) * 2;
    c->budget -= 3; /* branch, delay slot and base-address lui */
    u32 base = MEM_R32(0x80024678);
    if (R(6) == 0) {
        R(3) = base;
        R(2) += base;
        c->budget -= NC_LOAD_CYCLES + 2;
        MEM_W16(R(2), (u16)R(5));
        c->budget -= 3; /* store and jump to the return */
    } else {
        R(4) = base;
        c->budget -= NC_LOAD_CYCLES + 1;
        u32 shift = MEM_R32(0x800246a0);
        R(2) += base;
        R(3) = R(5) >> (shift & 31);
        c->budget -= NC_LOAD_CYCLES + 2;
        MEM_W16(R(2), (u16)R(3));
        c->budget--;
    }
    c->budget -= 2;
    return true;
}

/* _spu_FsetRXXa can also return either address representation without a store. */
bool nc_credits_spu_set_address(CPUState *c) {
    if (c->budget <= 47)
        return false;
    c->budget--;
    u32 align = MEM_R32(0x8002469c);
    R(6) = R(4); /* register index, or -1/-2 for conversion only */
    c->budget -= NC_LOAD_CYCLES + 3;
    if (align) {
        c->budget--;
        R(4) = MEM_R32(0x800246a4);
        nc_divu(c, R(5), R(4));
        c->budget -= NC_LOAD_CYCLES + 4;
        if (R(4) == 0) {
            nc_break(c, 0x8001fb0c, 0x1c00);
            c->budget--;
        }
        c->budget -= 4; /* test the remainder in HI */
        if (c->hi != 0) {
            c->budget--;
            u32 mask = MEM_R32(0x800246a8);
            R(5) = (R(5) + R(4)) & ~mask;
            c->budget -= NC_LOAD_CYCLES + 3;
        }
    }
    c->budget--;
    u32 shift = MEM_R32(0x800246a0);
    R(7) = R(5) >> (shift & 31);
    R(2) = R(5);
    c->budget -= NC_LOAD_CYCLES + 5;
    if (R(6) == (u32)-2) {
        c->budget -= 2;
    } else {
        c->budget -= 2;
        if (R(6) == (u32)-1) {
            R(2) = (u16)R(7);
            c->budget -= 2;
        } else {
            c->budget--;
            R(4) = MEM_R32(0x80024678);
            R(3) = R(4) + R(6) * 2;
            c->budget -= NC_LOAD_CYCLES + 2;
            MEM_W16(R(3), (u16)R(7));
            c->budget--;
        }
    }
    c->budget -= 2;
    return true;
}

/* _spu_FgetRXXa reads a raw SPU register or expands it to a byte address. */
bool nc_credits_spu_read_address(CPUState *c) {
    if (c->budget <= 20)
        return false;
    c->budget--;
    u32 base = MEM_R32(0x80024678);
    R(4) = base + R(4) * 2;
    R(2) = UINT32_MAX;
    c->budget -= NC_LOAD_CYCLES + 3;
    R(4) = MEM_R16(R(4));
    c->budget -= NC_LOAD_CYCLES + 2;
    if (R(5) == UINT32_MAX) {
        R(2) = R(4);
        c->budget--;
    } else {
        c->budget--;
        u32 shift = MEM_R32(0x800246a0);
        R(2) = R(4) << (shift & 31);
        c->budget -= NC_LOAD_CYCLES + 2;
    }
    c->budget -= 2;
    return true;
}

/* _spu_FsetPCR selects the SPU DMA channel priority in DPCR bits 16..18. */
bool nc_credits_spu_set_dma_priority(CPUState *c) {
    if (c->budget <= 27)
        return false;
    c->budget--;
    R(5) = MEM_R32(0x80024688);
    R(3) = 0xfff80000;
    c->budget -= NC_LOAD_CYCLES + 1;
    R(2) = MEM_R32(R(5)) & 0xfff8ffff;
    R(3) = 0xfff8ffff;
    c->budget -= NC_LOAD_CYCLES + 2;
    MEM_W32(R(5), R(2));

    bool priority_three = R(4) != 0;
    c->budget -= 4; /* store, branch with delay slot, then lui */
    R(2) = MEM_R32(0x80024688);
    c->budget -= NC_LOAD_CYCLES + 1;
    R(3) = MEM_R32(R(2));
    R(4) = priority_three ? 0x30000 : 0x50000;
    R(3) |= R(4);
    c->budget -= NC_LOAD_CYCLES + 2 + priority_three;
    MEM_W32(R(2), R(3));
    c->budget -= 3;
    return true;
}

/* SPU initialization and PIO use this 240-iteration delay. The two stack
 * words remain visible to the guest throughout the wait. */
bool nc_credits_spu_wait(CPUState *c) {
    if (c->budget <= 6)
        return false;
    R(29) -= 8;
    R(2) = 13;
    c->budget -= 2;
    MEM_W32(R(29) + 4, R(2));
    c->budget--;
    MEM_W32(R(29), 0);
    c->budget -= 3;

    for (;;) {
        load_word(c, 2, R(29));
        NC_POLL(c, 1);
        set_reg(c, 2, (s32)R(2) < 240);
        bool again = R(2) != 0;
        NC_POLL(c, 2);
        if (!again)
            break;

        load_word(c, 3, R(29) + 4);
        NC_POLL(c, 1);
        set_reg(c, 2, R(3) * 2);
        set_reg(c, 2, R(2) + R(3));
        MEM_W32(R(29) + 4, R(2));
        NC_POLL(c, 1);
        load_word(c, 2, R(29));
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) + 1);
        MEM_W32(R(29), R(2));
        NC_POLL(c, 1);
        load_word(c, 2, R(29)); /* the loop test performs a second load */
    }
    set_reg(c, 29, R(29) + 8);
    NC_POLL(c, 2);
    return true;
}

/* _spu_FsetDelayW and _spu_FsetDelayR update the SPU bus delay register. */
bool nc_credits_spu_set_delay(CPUState *c, bool reading) {
    if (c->budget <= 15)
        return false;
    c->budget--;
    R(4) = MEM_R32(0x8002468c);
    R(3) = 0xf0ff0000;
    c->budget -= NC_LOAD_CYCLES + 1;
    R(2) = MEM_R32(R(4));
    R(3) = reading ? 0x22000000 : 0x20000000;
    R(2) = (R(2) & 0xf0ffffff) | R(3);
    c->budget -= NC_LOAD_CYCLES + 4;
    MEM_W32(R(4), R(2));
    c->budget -= 3;
    return true;
}

/* The guest selects the BIOS service in the tail jump's delay slot. */
static bool bios_service(CPUState *c, u32 table, u32 service) {
    if (c->budget <= 3)
        return false;
    R(10) = table;
    R(9) = service;
    c->budget -= 3;
    nc_jump(c, R(10));
    return true;
}

/* GPU_cw passes the GP0 command word in a0 to BIOS A0 service 0x49. */
bool nc_credits_bios_gpu_command(CPUState *c) {
    return bios_service(c, 0xa0, 0x49);
}

bool nc_credits_flush_cache(CPUState *c) {
    return bios_service(c, 0xa0, 0x44);
}

/* Replace eleven words in the BIOS pad handler with NOPs while IRQs are disabled. */
bool nc_credits_patch_pad(CPUState *c) {
    if (c->budget <= 71)
        return false;
    R(1) = 0x80020000;
    c->budget--;
    MEM_W32(0x80024c80, R(31));
    R(31) = 0x8001a3f4;
    c->budget -= 3;
    nc_call(c, 0x8001a444); /* EnterCriticalSection */
    R(10) = 0xb0;
    R(31) = 0x8001a400;
    R(9) = 0x57;
    c->budget -= 3;
    nc_call(c, R(10)); /* GetB0Table */

    R(2) = MEM_R32(R(2) + 0x16c);
    R(9) = 11;
    c->budget -= NC_LOAD_CYCLES + 1;
    do {
        R(9)--;
        c->budget--;
        MEM_W32(R(2) + 0x594, 0);
        R(2) += 4;
        c->budget -= 3;
    } while (R(9));
    R(31) = 0x8001a420;
    c->budget -= 2;
    nc_call(c, 0x8001a434); /* FlushCache */
    c->budget--;
    R(31) = MEM_R32(0x80024c80);
    c->budget -= NC_LOAD_CYCLES + 3;
    return true;
}

bool nc_credits_bios_init_pad(CPUState *c) {
    return bios_service(c, 0xb0, 0x12);
}

/* Preserve both buffer/size pairs across the SDK patch and ChangeClearPad. */
static bool sdk_pad_init(CPUState *c, u32 entry, u32 bios_stub) {
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
    nc_call(c, 0x8001a3e4); /* _patch_pad */
    R(31) = entry + 0x34;
    R(4) = 0;
    NC_POLL(c, 2);
    nc_call(c, 0x8001a2ac); /* ChangeClearPad */
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

bool nc_credits_sdk_init_pad(CPUState *c) {
    return sdk_pad_init(c, 0x8001a324, 0x8001a3b4); /* BIOS InitPAD */
}

bool nc_credits_legacy_pad_init(CPUState *c) {
    return sdk_pad_init(c, 0x8001a2bc, 0x8001a3d4); /* OutdatedPadInitAndStart */
}

bool nc_credits_bios_pad_init_and_start(CPUState *c) {
    return bios_service(c, 0xb0, 0x15);
}

bool nc_credits_bios_init_heap(CPUState *c) {
    return bios_service(c, 0xa0, 0x39);
}

bool nc_credits_bios_write(CPUState *c) {
    return bios_service(c, 0xb0, 0x35);
}

bool nc_credits_bios_stop_pad(CPUState *c) {
    return bios_service(c, 0xb0, 0x14);
}

bool nc_credits_bios_start_pad(CPUState *c) {
    return bios_service(c, 0xb0, 0x13);
}

/* Start the BIOS pad driver, then turn off automatic pad interrupt clearing. */
bool nc_credits_start_pad(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(31) = 0x8001a39c;
    c->budget -= 3;
    nc_call(c, 0x8001a3c4); /* BIOS StartPAD */
    R(31) = 0x8001a3a4;
    R(4) = 0;
    NC_POLL(c, 2);
    nc_call(c, 0x8001a2ac); /* ChangeClearPad */
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_change_clear_pad(CPUState *c) {
    return bios_service(c, 0xb0, 0x5b);
}

bool nc_credits_change_clear_counter(CPUState *c) {
    return bios_service(c, 0xc0, 0x0a);
}

bool nc_credits_deliver_event(CPUState *c) {
    return bios_service(c, 0xb0, 7);
}

bool nc_credits_bios_96_remove(CPUState *c) {
    return bios_service(c, 0xa0, 0x72);
}

bool nc_credits_return_from_exception(CPUState *c) {
    return bios_service(c, 0xb0, 0x17);
}

bool nc_credits_reset_irq_entry(CPUState *c) {
    return bios_service(c, 0xb0, 0x18);
}

bool nc_credits_hook_irq_entry(CPUState *c) {
    return bios_service(c, 0xb0, 0x19);
}

/* The default callbacks notify the BIOS CD event class through DeliverEvent. */
static bool cd_default_event(CPUState *c, u32 spec, u32 resume) {
    if (c->budget <= 6)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(4) = 0xf0000003;
    R(31) = resume;
    R(5) = spec;
    c->budget -= 5;
    nc_call(c, 0x8001fcd8); /* DeliverEvent */
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_cd_default_sync(CPUState *c) {
    return cd_default_event(c, 0x20, 0x8001fd90);
}

bool nc_credits_cd_default_ready(CPUState *c) {
    return cd_default_event(c, 0x40, 0x8001fdb8);
}

bool nc_credits_cd_default_read(CPUState *c) {
    return cd_default_event(c, 0x40, 0x8001fde0);
}

static void cd_mix_control(CPUState *c, u32 slot, u8 value) {
    c->budget--;
    R(3) = MEM_R32(slot);
    R(2) = value;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W8(R(3), value);
    c->budget--;
}

static void cd_mix_channel(CPUState *c, u32 slot, u32 coefficient) {
    c->budget--;
    R(3) = MEM_R32(slot);
    c->budget -= NC_LOAD_CYCLES;
    R(2) = MEM_R8(coefficient);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W8(R(3), (u8)R(2));
    c->budget--;
}

/* CD_vol programs four CD audio mix coefficients through register banks 2 and 3. */
bool nc_credits_cd_volume(CPUState *c) {
    if (c->budget <= 56)
        return false;
    cd_mix_control(c, 0x80024a20, 2);
    cd_mix_channel(c, 0x80024a28, R(4));
    cd_mix_channel(c, 0x80024a2c, R(4) + 1);
    cd_mix_control(c, 0x80024a20, 3);
    cd_mix_channel(c, 0x80024a24, R(4) + 2);
    cd_mix_channel(c, 0x80024a28, R(4) + 3);
    cd_mix_control(c, 0x80024a2c, 0x20); /* apply the mix */
    R(2) = 0;
    c->budget -= 2;
    return true;
}

/* ResetCallback and InterruptCallback dispatch through the active IRQ driver. */
static bool interrupt_driver_call(CPUState *c, u32 offset, u32 resume) {
    if (c->budget <= 12)
        return false;
    c->budget--;
    R(2) = MEM_R32(0x800244a0);
    R(29) -= 24;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 16, R(31));
    c->budget--;
    R(2) = MEM_R32(R(2) + offset);
    R(31) = resume;
    c->budget -= NC_LOAD_CYCLES + 3;
    nc_call(c, R(2));
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_reset_callback(CPUState *c) {
    return interrupt_driver_call(c, 12, 0x8001a6b8);
}

bool nc_credits_interrupt_callback(CPUState *c) {
    return interrupt_driver_call(c, 8, 0x8001a6e8);
}

bool nc_credits_dma_callback(CPUState *c) {
    return interrupt_driver_call(c, 4, 0x8001a718);
}

/* VSyncCallback(handler) installs it in slot zero. VSyncCallbacks takes a slot. */
bool nc_credits_vsync_callback(CPUState *c) {
    if (c->budget <= 13)
        return false;
    R(29) -= 24;
    c->budget -= 2;
    R(2) = MEM_R32(0x800244a0);
    R(5) = R(4);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 16, R(31));
    c->budget--;
    R(2) = MEM_R32(R(2) + 20);
    R(31) = 0x8001a74c;
    R(4) = 0;
    c->budget -= NC_LOAD_CYCLES + 3;
    nc_call(c, R(2));
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_vsync_callbacks(CPUState *c) {
    return interrupt_driver_call(c, 20, 0x8001a77c);
}

bool nc_credits_stop_callback(CPUState *c) {
    return interrupt_driver_call(c, 16, 0x8001a7ac);
}

bool nc_credits_restart_callback(CPUState *c) {
    return interrupt_driver_call(c, 24, 0x8001a7dc);
}

/* The credits interrupt driver selects the BIOS critical-section syscall in a0. */
static bool critical_section_syscall(CPUState *c, u32 selector) {
    if (c->budget <= 4)
        return false;
    R(4) = selector;
    c->budget--;
    nc_syscall(c);
    c->budget -= 3;
    return true;
}

bool nc_credits_enter_critical_section(CPUState *c) {
    return critical_section_syscall(c, 1);
}

bool nc_credits_exit_critical_section(CPUState *c) {
    return critical_section_syscall(c, 2);
}

static bool leave_interrupt_start(CPUState *c) {
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* The BIOS interrupt hook re-enters startIntr at the return address of setjmp. */
bool nc_credits_resume_interrupt_start(CPUState *c) {
    const bool from_exception = R(2) != 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (from_exception) {
        R(31) = 0x8001a8a8;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8001a90c); /* trapIntr */
    }
    set_reg(c, 16, 0x80020000);
    set_reg(c, 16, 0x80023454);
    set_reg(c, 4, R(16) - 4); /* saved jump buffer */
    set_reg(c, 2, R(16) + 0xfdc);
    R(31) = 0x8001a8c0;
    MEM_W32(R(16), R(2)); /* interrupt stack pointer */
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001ae68); /* HookEntryInt */
    set_reg(c, 2, 1);
    R(31) = 0x8001a8cc;
    MEM_W16(R(16) - 0x3c, (u16)R(2));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001ae88); /* startIntrVSync */
    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x800244a0);
    R(31) = 0x8001a8dc;
    MEM_W32(R(3) + 20, R(2));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001afb0); /* startIntrDMA */
    set_reg(c, 4, 0x80020000);
    load_word(c, 4, 0x800244a0);
    R(31) = 0x8001a8ec;
    MEM_W32(R(4) + 4, R(2));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001ae30); /* _96_remove */
    R(31) = 0x8001a8f4;
    R(16) -= 0x3c;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001ae78);
    set_reg(c, 2, R(16));
    return leave_interrupt_start(c);
}

bool nc_credits_start_interrupts(CPUState *c) {
    if (c->budget <= 8 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = 0x80023418;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    R(2) = MEM_R16(R(16));
    const bool active = R(2) != 0;
    R(2) = 0;
    c->budget -= NC_LOAD_CYCLES + 3;
    if (active)
        return leave_interrupt_start(c);

    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x800244a4);
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x800244a8);
    set_reg(c, 5, 0x33330000);
    MEM_W16(R(2), 0); /* mask IRQs before clearing pending requests */
    NC_POLL(c, NC_INSN_CYCLES);
    R(2) = MEM_R16(R(2));
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 5, R(5) | 0x3333);
    MEM_W16(R(3), (u16)R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x800244ac);
    set_reg(c, 4, R(16));
    MEM_W32(R(2), R(5)); /* reset DMA priorities */
    NC_POLL(c, NC_INSN_CYCLES);
    R(31) = 0x8001a890;
    R(5) = 0x41a;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001ad7c); /* memclr */
    R(31) = 0x8001a898;
    R(4) = R(16) + 0x38;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001ada8); /* setjmp */
    return nc_credits_resume_interrupt_start(c);
}

/* VBlank and the three root counters have BIOS auto-acknowledge settings. */
static void update_interrupt_clear_mode(CPUState *c) {
    bool vblank = R(17) == 0;
    R(2) = 4;
    NC_POLL(c, 2);
    if (vblank) {
        set_reg(c, 16, R(18) == 0);
        R(31) = 0x8001abc0;
        R(4) = R(16);
        NC_POLL(c, 2);
        nc_call(c, 0x8001a2ac); /* ChangeClearPad */
        set_reg(c, 4, 3);
        R(31) = 0x8001abcc;
        R(5) = R(16);
        NC_POLL(c, 2);
        nc_call(c, 0x8001a688); /* ChangeClearRCnt */
        set_reg(c, 2, 4);
    }
    for (unsigned counter = 0; counter < 3; counter++) {
        bool matches = R(17) == R(2);
        if (counter == 2)
            R(4) = 2;
        else
            R(2) = 5 + counter;
        NC_POLL(c, 2);
        if (matches) {
            if (counter != 2)
                set_reg(c, 4, counter);
            const u32 resume[] = {0x8001abe4, 0x8001abfc, 0x8001ac10};
            R(31) = resume[counter];
            R(5) = R(18) == 0;
            NC_POLL(c, 2);
            nc_call(c, 0x8001a688);
            if (counter != 2)
                set_reg(c, 2, 5 + counter);
        }
    }
}

static void replace_interrupt_handler(CPUState *c) {
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x800244a8); /* I_MASK */
    NC_POLL(c, 1);
    R(3) = MEM_R16(R(2));
    NC_POLL(c, NC_LOAD_CYCLES);
    MEM_W16(R(2), 0);
    NC_POLL(c, 1);
    bool install = R(18) != 0;
    R(19) = R(3) & 0xffff;
    NC_POLL(c, 2);
    if (install) {
        set_reg(c, 3, 1);
        set_reg(c, 3, R(3) << (R(17) & 31));
        MEM_W32(R(4), R(18));
        NC_POLL(c, 1);
        R(2) = MEM_R16(R(5) + 44);
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, 19, R(19) | R(3));
        set_reg(c, 2, R(2) | R(3));
        MEM_W16(R(5) + 44, (u16)R(2));
        NC_POLL(c, 2);
    } else {
        set_reg(c, 2, 1);
        set_reg(c, 2, R(2) << (R(17) & 31));
        set_reg(c, 2, ~R(2));
        MEM_W32(R(4), 0);
        NC_POLL(c, 1);
        set_reg(c, 3, 0x80020000);
        R(3) = MEM_R16(0x80023448);
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, 19, R(19) & R(2));
        set_reg(c, 3, R(3) & R(2));
        set_reg(c, 1, 0x80020000);
        MEM_W16(0x80023448, (u16)R(3));
        NC_POLL(c, 1);
    }
    update_interrupt_clear_mode(c);
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x800244a8);
    NC_POLL(c, 1);
    MEM_W16(R(2), (u16)R(19));
    NC_POLL(c, 1);
    set_reg(c, 2, R(20));
}

/* Return the previous handler, leaving an inactive driver untouched. */
bool nc_credits_set_interrupt_handler(CPUState *c) {
    if (c->budget <= 16 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    R(29) -= 40;
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 24, R(18));
    R(18) = R(5);
    R(5) = 0x8002341c;
    R(2) = R(17) << 2;
    R(4) = R(2) + R(5);
    c->budget -= 6;
    MEM_W32(R(29) + 36, R(31));
    c->budget--;
    MEM_W32(R(29) + 32, R(20));
    c->budget--;
    MEM_W32(R(29) + 28, R(19));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    R(20) = MEM_R32(R(4));
    bool changed = R(18) != R(20);
    R(2) = R(20);
    c->budget -= NC_LOAD_CYCLES + 3;
    if (changed) {
        R(2) = MEM_R16(R(5) - 4);
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        bool active = R(2) != 0;
        R(2) = R(20);
        NC_POLL(c, 2);
        if (active)
            replace_interrupt_handler(c);
    }
    load_word(c, 31, R(29) + 36);
    for (unsigned reg = 20; reg >= 16; reg--)
        load_word(c, reg, R(29) + 16 + (reg - 16) * 4);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2);
    return true;
}

/* Save the IRQ mask and DMA priorities before disabling the interrupt driver. */
bool nc_credits_stop_interrupts(CPUState *c) {
    if (c->budget <= 11)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = 0x80023418;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    bool active = MEM_R16(R(16)) != 0;
    R(2) = 0;
    c->budget -= NC_LOAD_CYCLES + 3;
    if (active) {
        R(31) = 0x8001ac74;
        NC_POLL(c, 2);
        nc_call(c, 0x8001a444); /* EnterCriticalSection */
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x800244a8); /* I_MASK */
        set_reg(c, 4, 0x80020000);
        load_word(c, 4, 0x800244ac); /* DPCR */
        R(3) = MEM_R16(R(2));
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, 1, 0x80020000);
        MEM_W16(0x8002344a, (u16)R(3));
        NC_POLL(c, 1);
        load_word(c, 3, R(4));
        set_reg(c, 4, 0x80020000);
        load_word(c, 4, 0x800244a4); /* I_STAT */
        set_reg(c, 1, 0x80020000);
        MEM_W32(0x8002344c, R(3));
        NC_POLL(c, 1);
        MEM_W16(R(2), 0);
        NC_POLL(c, 1);
        R(2) = MEM_R16(R(2));
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        MEM_W16(R(4), (u16)R(2));
        NC_POLL(c, 1);
        set_reg(c, 4, 0x80020000);
        load_word(c, 4, 0x800244ac);
        set_reg(c, 3, 0x77770000);
        load_word(c, 2, R(4));
        set_reg(c, 3, 0x77777777);
        set_reg(c, 2, R(2) & R(3));
        MEM_W32(R(4), R(2));
        NC_POLL(c, 1);
        R(31) = 0x8001acd8;
        NC_POLL(c, 2);
        nc_call(c, 0x8001ae58); /* ResetEntryInt */
        set_reg(c, 2, R(16));
        MEM_W16(R(2), 0);
        NC_POLL(c, 1);
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* Reinstall the BIOS hook and restore the saved IRQ mask and DMA priorities. */
bool nc_credits_restart_interrupts(CPUState *c) {
    if (c->budget <= 11)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = 0x80023418;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    R(2) = MEM_R16(R(16));
    c->budget -= NC_LOAD_CYCLES + 3;
    if (R(2) == 0) {
        R(31) = 0x8001ad20;
        R(4) = R(16) + 0x38;
        NC_POLL(c, 2);
        nc_call(c, 0x8001ae68); /* HookEntryInt */
        set_reg(c, 4, 0x80020000);
        load_word(c, 4, 0x800244a8);
        set_reg(c, 3, 0x80020000);
        R(3) = MEM_R16(0x8002344a);
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, 2, 1);
        MEM_W16(R(16), (u16)R(2));
        NC_POLL(c, 1);
        MEM_W16(R(4), (u16)R(3));
        NC_POLL(c, 1);
        set_reg(c, 3, 0x80020000);
        load_word(c, 3, 0x800244ac);
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x8002344c);
        NC_POLL(c, 1);
        MEM_W32(R(3), R(2));
        NC_POLL(c, 1);
        R(31) = 0x8001ad5c;
        NC_POLL(c, 2);
        nc_call(c, 0x8001ae78); /* ExitCriticalSection */
        R(2) = R(16);
        NC_POLL(c, 2);
    } else {
        set_reg(c, 2, 0);
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* startIntr uses setjmp to build the BIOS interrupt entry context. */
bool nc_credits_save_irq_context(CPUState *c) {
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

/* Change one DMA callback and update its DICR enable without acknowledging pending IRQs. */
bool nc_credits_set_dma_slot(CPUState *c) {
    if (c->budget <= 34)
        return false;
    R(6) = R(4);
    R(2) = R(6) << 2;
    R(3) = 0x800244e0u + R(2);
    c->budget -= 5;
    R(7) = MEM_R32(R(3));
    R(4) = R(5);
    c->budget -= NC_LOAD_CYCLES + 3;
    if (R(4) != R(7)) {
        R(2) = 0x00ff0000;
        R(5) = 0x80020000;
        c->budget -= 3;
        R(5) = MEM_R32(0x800244dc);
        R(2) = 0x00ffffff;
        c->budget -= NC_LOAD_CYCLES + 1;
        MEM_W32(R(3), R(4));
        c->budget--;
        u32 control = MEM_R32(R(5)) & 0x00ffffff;
        u32 shift = R(6) + 16;
        u32 enable = 1u << (shift & 31);
        if (R(4)) {
            R(3) = 0x00800000;
            R(2) = enable | R(3);
            R(4) = control | R(2);
            c->budget -= NC_LOAD_CYCLES + 7;
            MEM_W32(R(5), R(4));
            c->budget -= 3;
        } else {
            R(4) = shift;
            R(2) = ~enable;
            R(3) = (control | 0x00800000) & R(2);
            c->budget -= NC_LOAD_CYCLES + 8;
            MEM_W32(R(5), R(3));
            c->budget--;
        }
    }
    R(2) = R(7);
    c->budget -= 2;
    return true;
}

/* Clear the DMA callback table and DICR, then register the channel IRQ handler. */
bool nc_credits_start_dma(CPUState *c) {
    if (c->budget <= 6)
        return false;
    R(29) -= 24;
    R(4) = 0x800244e0;
    c->budget -= 3;
    MEM_W32(R(29) + 16, R(31));
    R(31) = 0x8001afc8;
    R(5) = 8;
    c->budget -= 3;
    nc_call(c, 0x8001b22c); /* clear eight callback slots */

    set_reg(c, 4, 3); /* DMA IRQ */
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x800244dc);
    set_reg(c, 5, 0x80020000);
    set_reg(c, 5, 0x8001b000); /* trapIntrDMA */
    MEM_W32(R(2), 0);
    NC_POLL(c, 1);
    R(31) = 0x8001afe8;
    NC_POLL(c, 2);
    nc_call(c, 0x8001a6c8); /* InterruptCallback */
    set_reg(c, 2, 0x80020000);
    set_reg(c, 2, 0x8001b184); /* setIntrDMA */
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* Initialize timer 1 and the VBlank callback table, then return its slot setter. */
bool nc_credits_start_vsync(CPUState *c) {
    if (c->budget <= 14)
        return false;
    R(29) -= 24;
    R(4) = 0x800244b4;
    c->budget -= 4;
    R(3) = MEM_R32(0x800244d8);
    R(2) = 0x107;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 16, R(31));
    c->budget--;
    MEM_W32(R(3), R(2));
    R(1) = 0x80020000;
    c->budget -= 2;
    MEM_W32(0x800244d4, 0); /* VBlank count */
    R(31) = 0x8001aeb8;
    R(5) = 8;
    c->budget -= 3;
    nc_call(c, 0x8001af84); /* clear the callback slots */

    set_reg(c, 5, 0x80020000);
    set_reg(c, 5, 0x8001aee0); /* trapIntrVSync */
    R(31) = 0x8001aec8;
    R(4) = 0;
    NC_POLL(c, 2);
    nc_call(c, 0x8001a6c8); /* InterruptCallback */
    set_reg(c, 2, 0x80020000);
    set_reg(c, 2, 0x8001af58); /* setIntrVSync */
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* Count this VBlank and visit the live table so callbacks can change later slots. */
bool nc_credits_dispatch_vblank(CPUState *c) {
    if (c->budget <= 18)
        return false;
    c->budget--;
    R(2) = MEM_R32(0x800244d4);
    R(29) -= 32;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 20, R(17));
    R(17) = 0;
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(16));
    R(16) = 0x800244b4;
    c->budget -= 3;
    MEM_W32(R(29) + 24, R(31));
    R(2)++;
    R(1) = 0x80020000;
    c->budget -= 3;
    MEM_W32(0x800244d4, R(2));
    c->budget -= 2;
    load_word(c, 2, 0x800244d4);

    bool more;
    do {
        load_word(c, 2, R(16));
        NC_POLL(c, 1);
        bool occupied = R(2) != 0;
        R(16) += 4;
        NC_POLL(c, 2);
        if (occupied) {
            u32 callback = R(2);
            R(31) = 0x8001af30;
            NC_POLL(c, 2);
            nc_call(c, callback);
        }
        set_reg(c, 17, R(17) + 1);
        set_reg(c, 2, (s32)R(17) < 8);
        more = R(2) != 0;
        NC_POLL(c, 2);
    } while (more);
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* setIntrVSync returns the previous callback, including when it skips the store. */
bool nc_credits_set_vsync_slot(CPUState *c) {
    if (c->budget <= NC_LOAD_CYCLES + 10)
        return false;
    R(4) = 0x800244b4u + 4u * R(4);
    R(2) = 0x800244b4u;
    c->budget -= 4;
    R(2) = MEM_R32(R(4));
    c->budget -= NC_LOAD_CYCLES + 3;
    if (R(5) != R(2)) {
        MEM_W32(R(4), R(5));
        c->budget--;
    }
    c->budget -= 2;
    return true;
}

/* The IRQ, VSync and DMA initializers use identical word-clear loops. */
bool nc_credits_clear_irq_words(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(29) -= 8;
    bool more = R(5) != 0;
    R(2) = R(5) - 1;
    c->budget -= 3;
    if (more) {
        set_reg(c, 3, UINT32_MAX);
        do {
            MEM_W32(R(4), 0);
            NC_POLL(c, 1);
            set_reg(c, 2, R(2) - 1);
            more = R(2) != R(3);
            R(4) += 4;
            NC_POLL(c, 2);
        } while (more);
    }
    set_reg(c, 29, R(29) + 8);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_check_callback(CPUState *c) {
    if (c->budget <= 6)
        return false;
    c->budget--;
    R(2) = MEM_R16(0x8002341a);
    c->budget -= NC_LOAD_CYCLES + 2;
    return true;
}

/* Both mask accessors return the old unsigned halfword. SetIntrMask also
 * leaves the port pointer in v1 and stores the low half of a0. */
bool nc_credits_interrupt_mask(CPUState *c, bool write) {
    if (c->budget <= (write ? 11 : 10))
        return false;
    c->budget--;
    u32 port = MEM_R32(0x800244a8);
    c->budget -= NC_LOAD_CYCLES + 1;
    R(2) = MEM_R16(port);
    c->budget -= NC_LOAD_CYCLES;
    if (write) {
        R(3) = port;
        MEM_W16(port, (u16)R(4));
        c->budget--;
    }
    c->budget -= 2;
    return true;
}

/* Clear the CD callbacks and cached status, then install the IRQ 2 handler. */
bool nc_credits_cd_init_interrupt(CPUState *c) {
    if (c->budget <= 12)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(1) = 0x80020000;
    c->budget -= 2;
    MEM_W32(0x80024760, 0); /* ready callback */
    c->budget -= 2;
    MEM_W32(0x8002475c, 0); /* sync callback */
    c->budget -= 2;
    MEM_W32(0x80024770, 0);
    c->budget -= 2;
    MEM_W32(0x8002476c, 0);
    R(31) = 0x800216cc;
    c->budget -= 3;
    nc_call(c, 0x8001a698); /* ResetCallback */

    set_reg(c, 5, 0x80020000);
    set_reg(c, 5, 0x80021b54);
    R(31) = 0x800216dc;
    R(4) = 2;
    NC_POLL(c, 2);
    nc_call(c, 0x8001a6c8); /* InterruptCallback */
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* CD_initvol installs the default stereo mix and enables CD input on the SPU. */
bool nc_credits_cd_init_volume(CPUState *c) {
    if (c->budget <= 91)
        return false;
    c->budget--;
    R(3) = MEM_R32(0x80024a34);
    c->budget -= NC_LOAD_CYCLES + 1;
    u16 left = MEM_R16(R(3) + 0x1b8);
    R(29) -= 8;
    c->budget -= NC_LOAD_CYCLES + 3;
    if (!left) {
        u16 right = MEM_R16(R(3) + 0x1ba);
        R(2) = 0x3fff;
        c->budget -= NC_LOAD_CYCLES + 3;
        if (!right) {
            MEM_W16(R(3) + 0x180, R(2));
            c->budget--;
            MEM_W16(R(3) + 0x182, R(2));
            c->budget -= 2;
            R(3) = MEM_R32(0x80024a34);
            c->budget -= NC_LOAD_CYCLES + 1;
        }
    } else {
        R(2) = 0x3fff;
        c->budget--;
    }
    MEM_W16(R(3) + 0x1b0, R(2));
    c->budget--;
    MEM_W16(R(3) + 0x1b2, R(2));
    c->budget -= 2;
    MEM_W16(R(3) + 0x1aa, 0xc001);
    c->budget -= 2;
    R(3) = MEM_R32(0x80024a20);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W8(R(29) + 2, 0x80);
    c->budget--;
    MEM_W8(R(29), 0x80);
    c->budget -= 2;
    MEM_W8(R(29) + 3, 0);
    c->budget--;
    MEM_W8(R(29) + 1, 0);
    c->budget--;
    MEM_W8(R(3), 2);
    c->budget--;
    cd_mix_channel(c, 0x80024a28, R(29));
    cd_mix_channel(c, 0x80024a2c, R(29) + 1);
    cd_mix_control(c, 0x80024a20, 3);
    cd_mix_channel(c, 0x80024a24, R(29) + 2);
    cd_mix_channel(c, 0x80024a28, R(29) + 3);
    cd_mix_control(c, 0x80024a2c, 0x20);
    R(2) = 0;
    R(29) += 8;
    c->budget -= 4;
    return true;
}

bool nc_credits_cd_mix(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(31) = 0x80020364;
    c->budget -= 3;
    nc_call(c, 0x80021440);
    set_reg(c, 2, 1);
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* The SDK loads each CD or DMA register address through its pointer table. */
static void load_cd_port(CPUState *c, unsigned reg, u32 slot) {
    set_reg(c, reg, 0x80020000);
    load_word(c, reg, slot);
}

/* Request the CD data FIFO, then transfer the requested words through DMA 3. */
bool nc_credits_cd_sector_dma(CPUState *c) {
    if (c->budget <= 3 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    c->budget--;
    R(2) = MEM_R32(0x80024a20);
    R(6) = 0x20000;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W8(R(2), 0); /* CD register bank zero */
    c->budget--;
    load_cd_port(c, 3, 0x80024a2c);
    set_reg(c, 2, 0x80);
    MEM_W8(R(3), (u8)R(2)); /* request sector data */
    NC_POLL(c, NC_INSN_CYCLES);
    load_cd_port(c, 2, 0x80024a54);
    set_reg(c, 6, R(6) | 0x943);
    MEM_W32(R(2), R(6));
    NC_POLL(c, NC_INSN_CYCLES);
    load_cd_port(c, 3, 0x80024a30);
    set_reg(c, 2, 0x1323);
    MEM_W32(R(3), R(2));
    NC_POLL(c, NC_INSN_CYCLES);

    load_cd_port(c, 3, 0x80024a58);
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 2, R(3));
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, R(2) | 0x8000); /* enable DMA channel 3 */
    MEM_W32(R(3), R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    load_cd_port(c, 2, 0x80024a5c);
    NC_POLL(c, NC_INSN_CYCLES);
    MEM_W32(R(2), R(4)); /* destination */
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, 0x10000);
    load_cd_port(c, 3, 0x80024a60);
    set_reg(c, 5, R(5) | R(2)); /* one block */
    MEM_W32(R(3), R(5));
    NC_POLL(c, NC_INSN_CYCLES);

    load_cd_port(c, 3, 0x80024a20);
    bool ready;
    do {
        NC_POLL(c, NC_INSN_CYCLES);
        R(2) = MEM_R8(R(3));
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, NC_INSN_CYCLES);
        set_reg(c, 2, R(2) & 0x40);
        ready = R(2) != 0;
        R(2) = 0x11000000;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } while (!ready);
    load_cd_port(c, 3, 0x80024a64);
    NC_POLL(c, NC_INSN_CYCLES);
    MEM_W32(R(3), R(2)); /* trigger transfer */
    NC_POLL(c, NC_INSN_CYCLES);
    load_cd_port(c, 4, 0x80024a64);
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 2, R(4));
    set_reg(c, 3, 1u << 24);
    set_reg(c, 2, R(2) & R(3));
    bool busy = R(2) != 0;
    R(3) = R(4);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (busy)
        set_reg(c, 4, 1u << 24);
    while (busy) {
        load_word(c, 2, R(3));
        NC_POLL(c, NC_INSN_CYCLES);
        set_reg(c, 2, R(2) & R(4));
        busy = R(2) != 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    }
    load_cd_port(c, 3, 0x80024a30);
    set_reg(c, 2, 0x1325);
    MEM_W32(R(3), R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    R(2) = 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Read the requested number of words and convert the driver's zero result to success. */
bool nc_credits_cd_get_sector(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(31) = 0x80020388;
    c->budget -= 3;
    nc_call(c, 0x80021a44); /* CD_getsector */
    set_reg(c, 2, R(2) == 0);
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* Register the CD data handler on DMA channel 3 and return its predecessor. */
bool nc_credits_cd_data_callback(CPUState *c) {
    if (c->budget <= 5)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(5) = R(4);
    R(31) = 0x800203b0;
    R(4) = 3;
    c->budget -= 4;
    nc_call(c, 0x8001a6f8); /* DMACallback */
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* Forward the caller's arguments and return the CD driver status. */
static bool call_cd_status_driver(CPUState *c, u32 driver, u32 return_pc) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(31) = return_pc;
    c->budget -= 3;
    nc_call(c, driver);
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_cd_sync(CPUState *c) {
    return call_cd_status_driver(c, 0x80020adc, 0x8001ff4c);
}

bool nc_credits_cd_ready(CPUState *c) {
    return call_cd_status_driver(c, 0x80020d5c, 0x8001ff6c);
}

bool nc_credits_cd_data_sync(CPUState *c) {
    return call_cd_status_driver(c, 0x800218d8, 0x800203d0);
}

/* Load a CD port pointer from the guest table, including the load delay nop. */
static void cd_flush_port(CPUState *c, u32 slot) {
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, slot);
    NC_POLL(c, 1);
}

static void report_cd_dma_timeout(CPUState *c) {
    set_reg(c, 4, 0x80020000);
    set_reg(c, 4, 0x80018890);
    R(31) = 0x80021990;
    NC_POLL(c, 2);
    nc_call(c, 0x8001a638); /* puts */
    R(4) = MEM_R8(R(17));
    NC_POLL(c, NC_LOAD_CYCLES);
    R(2) = MEM_R8(R(17) + 1);
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 5, 0x80020000);
    load_word(c, 5, 0x80024e50);
    set_reg(c, 2, R(2) << 2);
    set_reg(c, 2, R(2) + R(16));
    set_reg(c, 4, R(4) << 2);
    load_word(c, 3, R(2)); /* second interrupt name, passed on the stack */
    set_reg(c, 2, 0x80020000);
    R(2) = MEM_R8(0x8002477d);
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 4, R(4) + R(16));
    set_reg(c, 2, R(2) << 2);
    set_reg(c, 2, R(2) + R(19));
    MEM_W32(R(29) + 16, R(3));
    NC_POLL(c, 1);
    load_word(c, 6, R(2)); /* command name */
    load_word(c, 7, R(4)); /* first interrupt name */
    set_reg(c, 4, 0x80020000);
    set_reg(c, 4, 0x800188a0);
    R(31) = 0x800219e0;
    NC_POLL(c, 2);
    nc_call(c, 0x80019a60); /* printf */
    R(31) = 0x800219e8;
    NC_POLL(c, 2);
    nc_call(c, 0x800214c8); /* CD_flush */
    R(2) = UINT32_MAX;
    NC_POLL(c, 2);
}

static bool cd_dma_wait_expired(CPUState *c) {
    R(31) = 0x80021940;
    R(4) = UINT32_MAX;
    NC_POLL(c, 2);
    nc_call(c, 0x8001a454); /* VSync(-1) */
    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x80024e48);
    NC_POLL(c, 1);
    set_reg(c, 3, (s32)R(3) < (s32)R(2));
    bool expired = R(3) != 0;
    NC_POLL(c, 2);
    if (!expired) {
        set_reg(c, 3, 0x80020000);
        load_word(c, 3, 0x80024e4c);
        NC_POLL(c, 1);
        set_reg(c, 2, R(3) + 1);
        set_reg(c, 1, 0x80020000);
        MEM_W32(0x80024e4c, R(2));
        NC_POLL(c, 1);
        set_reg(c, 2, 0x3c0000);
        set_reg(c, 2, (s32)R(2) < (s32)R(3));
        expired = R(2) != 0;
        NC_POLL(c, 2);
    }
    if (expired)
        report_cd_dma_timeout(c);
    else
        set_reg(c, 2, 0);
    expired = R(2) != 0;
    R(2) = UINT32_MAX;
    NC_POLL(c, 2);
    return expired;
}

bool nc_credits_sync_cd_dma(CPUState *c) {
    if (c->budget <= 9)
        return false;
    R(29) -= 48;
    c->budget--;
    MEM_W32(R(29) + 32, R(18));
    R(18) = R(4);
    R(4) = UINT32_MAX;
    c->budget -= 3;
    MEM_W32(R(29) + 40, R(31));
    c->budget--;
    MEM_W32(R(29) + 36, R(19));
    c->budget--;
    MEM_W32(R(29) + 28, R(17));
    c->budget--;
    R(31) = 0x800218fc;
    MEM_W32(R(29) + 24, R(16));
    c->budget -= 2;
    nc_call(c, 0x8001a454);
    set_reg(c, 19, 0x80020000);
    set_reg(c, 19, 0x80024780);
    set_reg(c, 17, 0x80020000);
    set_reg(c, 17, 0x80024a38);
    set_reg(c, 16, 0x80020000);
    set_reg(c, 16, 0x80024800);
    set_reg(c, 2, R(2) + 960);
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x80024e48, R(2)); /* frame deadline */
    NC_POLL(c, 1);
    set_reg(c, 2, 0x80020000);
    set_reg(c, 2, 0x80018998);
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x80024e4c, 0); /* poll count */
    NC_POLL(c, 1);
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x80024e50, R(2));
    NC_POLL(c, 1);
    while (!cd_dma_wait_expired(c)) {
        cd_flush_port(c, 0x80024a64);
        load_word(c, 2, R(2));
        set_reg(c, 3, 1u << 24);
        set_reg(c, 2, R(2) & R(3));
        bool busy = R(2) != 0;
        R(2) = 0;
        NC_POLL(c, 2);
        if (!busy)
            break;
        bool blocking = R(18) == 0;
        R(2) = 1;
        NC_POLL(c, 2);
        if (!blocking)
            break;
    }
    load_word(c, 31, R(29) + 40);
    for (unsigned reg = 19; reg >= 16; reg--)
        load_word(c, reg, R(29) + 24 + (reg - 16) * 4);
    set_reg(c, 29, R(29) + 48);
    NC_POLL(c, 2);
    return true;
}

static bool cd_flush_pending(CPUState *c, bool first) {
    cd_flush_port(c, 0x80024a2c);
    R(2) = MEM_R8(R(2));
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) & 7);
    bool pending = R(2) != 0;
    if (first)
        R(3) = 7;
    NC_POLL(c, 2);
    return pending;
}

bool nc_credits_cd_flush_controller(CPUState *c) {
    if (c->budget <= 6)
        return false;
    c->budget--;
    R(3) = MEM_R32(0x80024a20);
    R(2) = 1;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W8(R(3), 1); /* select the interrupt bank */
    c->budget--;
    if (cd_flush_pending(c, true)) {
        set_reg(c, 4, 1);
        do {
            cd_flush_port(c, 0x80024a20);
            MEM_W8(R(2), (u8)R(4));
            NC_POLL(c, 1);
            cd_flush_port(c, 0x80024a2c);
            MEM_W8(R(2), (u8)R(3)); /* acknowledge pending interrupts */
            NC_POLL(c, 1);
            cd_flush_port(c, 0x80024a28);
            MEM_W8(R(2), (u8)R(3)); /* enable the next interrupt */
            NC_POLL(c, 1);
        } while (cd_flush_pending(c, false));
    }
    set_reg(c, 1, 0x80020000);
    MEM_W8(0x80024a3a, 0);
    NC_POLL(c, 1);
    set_reg(c, 3, 0x80020000);
    R(3) = MEM_R8(0x80024a3a);
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 2, 0x80020000);
    set_reg(c, 2, 0x80024a39);
    MEM_W8(R(2), (u8)R(3));
    NC_POLL(c, 1);
    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x80024a20);
    set_reg(c, 2, 2);
    set_reg(c, 1, 0x80020000);
    MEM_W8(0x80024a38, (u8)R(2));
    NC_POLL(c, 1);
    MEM_W8(R(3), 0);
    NC_POLL(c, 1);
    cd_flush_port(c, 0x80024a2c);
    MEM_W8(R(2), 0); /* clear the data request */
    NC_POLL(c, 1);
    set_reg(c, 3, 0x80020000);
    load_word(c, 3, 0x80024a30);
    set_reg(c, 2, 0x1325);
    MEM_W32(R(3), R(2));
    NC_POLL(c, 1);
    NC_POLL(c, 2);
    return true;
}

/* Handle a CD audio interrupt and resume playback when the status allows it. */
bool nc_credits_cd_audio_ready(CPUState *c) {
    if (c->budget <= 7)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(5); /* drive response */
    R(4) &= 0xff;
    R(2) = 4;
    c->budget -= 4;
    MEM_W32(R(29) + 20, R(31));
    c->budget -= 2;
    bool resume = false;
    if (R(4) == 4) { /* CdlDataEnd */
        R(31) = 0x80022ee0;
        R(4) = UINT32_MAX;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8001a454); /* VSync(-1) */
        set_reg(c, 4, 0x80020000);
        set_reg(c, 4, 0x80018bfc);
        set_reg(c, 5, 0x80020000);
        load_word(c, 5, 0x80024c34);
        R(31) = 0x80022ef8;
        R(6) = R(2);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x80019a60); /* report track and frame */
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024c34);
        NC_POLL(c, NC_INSN_CYCLES);
        set_reg(c, 2, R(2) + 1);
        set_reg(c, 1, 0x80020000);
        MEM_W32(0x80024c34, R(2));
        NC_POLL(c, NC_INSN_CYCLES);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        resume = true;
    } else {
        R(2) = MEM_R8(R(16));
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, NC_INSN_CYCLES);
        set_reg(c, 2, R(2) & 1);
        const bool error = R(2) != 0;
        R(2) = 1;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (!error) {
            set_reg(c, 1, 0x80020000);
            MEM_W32(0x80024c38, R(2));
            NC_POLL(c, NC_INSN_CYCLES);
            R(31) = 0x80022f3c;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, 0x8001ff08); /* CdIntstr */
            set_reg(c, 4, 0x80020000);
            set_reg(c, 4, 0x80018c28);
            R(6) = MEM_R8(R(16));
            NC_POLL(c, NC_LOAD_CYCLES);
            R(7) = MEM_R8(R(16) + 1);
            NC_POLL(c, NC_LOAD_CYCLES);
            R(31) = 0x80022f54;
            R(5) = R(2);
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, 0x80019a60);
            R(2) = MEM_R8(R(16));
            NC_POLL(c, NC_LOAD_CYCLES);
            NC_POLL(c, NC_INSN_CYCLES);
            set_reg(c, 2, R(2) & 0x11); /* error or shell open */
            resume = R(2) == 0;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
        }
    }
    if (resume) {
        R(31) = 0x80022f70;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x80022f84); /* play */
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Select a playlist entry and issue CdlPlay using its table-of-contents position. */
bool nc_credits_cd_play_track(CPUState *c) {
    const u32 index = 0x80024c34, playlist = 0x80024aa4, last_track = 0x800273e4;
    if (c->budget <= 11 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES)
        return false;
    c->budget--;
    R(2) = MEM_R32(index);
    R(29) -= 32;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 28, R(31));
    c->budget--;
    MEM_W32(R(29) + 24, R(16));
    R(2) <<= 2;
    R(1) = 0x80020000 + R(2);
    c->budget -= 4;
    R(2) = MEM_R32(R(1) + 0x4aa4);
    R(1) = 0x80020000;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(0x80024c38, 0);
    c->budget -= 3;
    if (!R(2)) {
        /* At the terminator, restart at zero or mark playback stopped. */
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024c3c);
        NC_POLL(c, NC_INSN_CYCLES);
        const bool repeat = R(2) != 0;
        R(2) = UINT32_MAX;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        set_reg(c, 1, 0x80020000);
        MEM_W32(index, repeat ? 0 : R(2));
        NC_POLL(c, NC_INSN_CYCLES);
        if (repeat)
            NC_POLL(c, 2 * NC_INSN_CYCLES);
    }
    set_reg(c, 3, 0x80020000);
    load_word(c, 3, index);
    NC_POLL(c, NC_INSN_CYCLES);
    const bool active = (s32)R(3) >= 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (active) {
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, 0x80024aa0);
        NC_POLL(c, NC_INSN_CYCLES);
        set_reg(c, 2, R(2) + 1);
        set_reg(c, 1, 0x80020000);
        MEM_W32(0x80024aa0, R(2));
        NC_POLL(c, NC_INSN_CYCLES);
        set_reg(c, 2, R(3) << 2);
        set_reg(c, 1, 0x80020000);
        set_reg(c, 1, R(1) + R(2));
        load_word(c, 5, R(1) + 0x4aa4);
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, last_track);
        set_reg(c, 16, 0x80020000);
        set_reg(c, 16, playlist);
        set_reg(c, 2, (s32)R(2) < (s32)R(5));
        const bool beyond_disc = R(2) != 0;
        R(4) = 3;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (beyond_disc) {
            set_reg(c, 4, 0x80020000);
            set_reg(c, 4, 0x80018c38);
            R(31) = 0x8002304c;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, 0x80019a60); /* printf */
            set_reg(c, 2, 0x80020000);
            load_word(c, 2, index);
            set_reg(c, 3, 0x80020000);
            load_word(c, 3, last_track);
            set_reg(c, 2, R(2) << 2);
            set_reg(c, 2, R(2) + R(16));
            MEM_W32(R(2), R(3));
            NC_POLL(c, NC_INSN_CYCLES);
            set_reg(c, 4, 3);
        }
        set_reg(c, 2, 0x80020000);
        load_word(c, 2, index);
        set_reg(c, 6, 0);
        set_reg(c, 2, R(2) << 2);
        set_reg(c, 2, R(2) + R(16));
        load_word(c, 5, R(2));
        set_reg(c, 2, 0x80020000);
        set_reg(c, 2, 0x80027254);
        set_reg(c, 5, R(5) << 2);
        R(31) = 0x80023098;
        R(5) += R(2);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8001ffac); /* CdControl(CdlPlay, position, NULL) */
    }
    load_word(c, 31, R(29) + 28);
    load_word(c, 16, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* CdGetToc forwards the caller's track-position buffer to CdGetToc2(1, buffer). */
bool nc_credits_cd_get_toc(CPUState *c) {
    if (c->budget <= 5)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(5) = R(4);
    R(31) = 0x800230c0;
    R(4) = 1;
    c->budget -= 4;
    nc_call(c, 0x800230d0);
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Each CdInit callback setter and the error diagnostic take one pointer. */
static void cd_init_call(CPUState *c, u32 argument, u32 target, u32 resume) {
    set_reg(c, 4, 0x80020000);
    set_reg(c, 4, argument);
    R(31) = resume;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, target);
}

/* Try CdReset(1) up to five times before reporting initialization failure. */
bool nc_credits_cd_init(CPUState *c) {
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
        R(31) = 0x8001fd00;
        R(4) = 1;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8001fe30);
        set_reg(c, 3, 1);
        const bool ready = R(2) == R(3);
        R(16)--;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (ready) {
            cd_init_call(c, 0x8001fd78, 0x8001ff7c, 0x8001fd1c); /* sync */
            cd_init_call(c, 0x8001fda0, 0x8001ff94, 0x8001fd2c); /* ready */
            cd_init_call(c, 0x8001fdc8, 0x80022cb4, 0x8001fd3c); /* read */
            R(2) = 1;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            break;
        }
        set_reg(c, 2, UINT32_MAX);
        const bool exhausted = R(16) == R(2);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (exhausted) {
            cd_init_call(c, 0x80018734, 0x80019a60, 0x8001fd60); /* printf */
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

/* Mode 2 only reinstalls interrupts. Mode 1 also initializes CD audio volume. */
bool nc_credits_cd_reset(CPUState *c) {
    if (c->budget <= 6)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    R(2) = 2;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget -= 2;
    if (R(16) == 2) {
        R(31) = 0x8001fe50;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x8002169c); /* CD_initintr */
        R(2) = 1;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } else {
        R(31) = 0x8001fe60;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x800216ec); /* CD_init */
        const bool initialized = R(2) == 0;
        R(2) = 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (initialized) {
            set_reg(c, 2, 1);
            const bool init_volume = R(16) == 1;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            if (init_volume) {
                R(31) = 0x8001fe7c;
                NC_POLL(c, 2 * NC_INSN_CYCLES);
                nc_call(c, 0x800215a8); /* CD_initvol */
                const bool volume_ready = R(2) == 0;
                R(2) = 0;
                NC_POLL(c, 2 * NC_INSN_CYCLES);
                if (volume_ready)
                    set_reg(c, 2, 1);
            }
        }
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_credits_cd_flush(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(31) = 0x8001feac;
    c->budget -= 3;
    nc_call(c, 0x800214c8);
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* CdStatus, CdMode and CdLastCom each return one cached unsigned byte. */
static bool cd_state_byte(CPUState *c, u32 address) {
    if (c->budget <= 6)
        return false;
    c->budget--; /* lui */
    R(2) = MEM_R8(address);
    c->budget -= NC_LOAD_CYCLES + 2; /* lbu, jr and its delay slot */
    return true;
}

bool nc_credits_cd_status(CPUState *c) {
    return cd_state_byte(c, 0x8002476c);
}

bool nc_credits_cd_mode(CPUState *c) {
    return cd_state_byte(c, 0x8002477c);
}

bool nc_credits_cd_last_command(CPUState *c) {
    return cd_state_byte(c, 0x8002477d);
}

/* These CD setters return the previous word before installing a0. */
static bool exchange_cd_word(CPUState *c, u32 address) {
    if (c->budget <= 8)
        return false;
    c->budget--; /* lui */
    R(2) = MEM_R32(address);
    R(1) = 0x80020000;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(address, R(4));
    c->budget -= 3; /* sw, jr and its delay slot */
    return true;
}

bool nc_credits_cd_set_debug(CPUState *c) {
    return exchange_cd_word(c, 0x80024768);
}

bool nc_credits_cd_sync_callback(CPUState *c) {
    return exchange_cd_word(c, 0x8002475c);
}

bool nc_credits_cd_ready_callback(CPUState *c) {
    return exchange_cd_word(c, 0x80024760);
}

bool nc_credits_cd_read_callback(CPUState *c) {
    return exchange_cd_word(c, 0x80024a70);
}

/* CdComstr and CdIntstr return guest pointers into the CD diagnostic strings. */
static bool cd_lookup_name(CPUState *c, u32 table, u32 count) {
    if (c->budget <= 13)
        return false;
    R(4) &= 0xff;
    c->budget -= 4; /* mask, range check, branch and delay slot */
    if (R(4) < count) {
        u32 offset = R(4) * 4;
        R(1) = 0x80020000 + offset;
        c->budget -= 2;
        R(2) = MEM_R32(table + offset);
        c->budget -= NC_LOAD_CYCLES + 2; /* load and jump to the return */
    } else {
        R(2) = 0x8001874c;
        c->budget -= 2;
    }
    c->budget -= 2; /* jr and delay slot */
    return true;
}

/* CD_cw(command, parameters, result, async). The async flag is in the delay slot. */
static void issue_cd_control(CPUState *c, u32 parameters, unsigned result_reg, bool async, u32 resume) {
    set_reg(c, 5, parameters);
    set_reg(c, 6, result_reg ? R(result_reg) : 0);
    R(31) = resume;
    R(7) = async;
    NC_POLL(c, 2);
    nc_call(c, 0x80021028);
}

static void set_cd_control_callback(CPUState *c, u32 callback) {
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x8002475c, callback);
    NC_POLL(c, 1);
}

enum CdControlMode { CD_CONTROL, CD_CONTROL_FAST, CD_CONTROL_BLOCKING };

/* The CdControl variants retry the command up to four times. Keep the sync callback
 * disabled for the status and Setloc commands preceding the requested one. */
static bool cd_control(CPUState *c, enum CdControlMode mode) {
    const bool fast = mode == CD_CONTROL_FAST;
    const bool blocking = mode == CD_CONTROL_BLOCKING;
    /* Guest loop PCs share the same command-call offsets. */
    const u32 loop = fast ? 0x80020134 : (blocking ? 0x80020260 : 0x80020004);
    /* CdControl keeps its result pointer in s2. The other saved registers
     * move up one slot compared with CdControlF. */
    const unsigned result_reg = fast ? 0 : 18;
    const unsigned command_byte = fast ? 18 : 19;
    const unsigned command = command_byte + 1;
    const unsigned callback = command + 1;
    const unsigned location_entry = callback + 1;
    const unsigned status = location_entry + 1;
    const unsigned last_saved = blocking ? location_entry : status;
    const unsigned frame = mode == CD_CONTROL ? 56 : 48;
    const unsigned ra_offset = mode == CD_CONTROL ? 48 : 44;
    const unsigned saved_base = fast ? 24 : 28; /* stack slot for command_byte */
    if (c->budget <= (mode == CD_CONTROL ? 21 : 19) * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    R(29) -= frame;
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(5);
    c->budget -= 2;
    if (!fast) {
        MEM_W32(R(29) + 24, R(18));
        R(18) = R(6);
        c->budget -= 2;
    }
    MEM_W32(R(29) + saved_base + 4, R(command));
    R(command) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(16));
    R(16) = 3;
    c->budget -= 2;
    MEM_W32(R(29) + saved_base, R(command_byte));
    R(command_byte) = R(command) & 0xff;
    R(3) = 0x800246d4;
    c->budget -= 4;
    MEM_W32(R(29) + saved_base + 8, R(callback));
    c->budget -= 2;
    R(callback) = MEM_R32(0x8002475c);
    R(2) = R(command_byte) << 2;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + saved_base + 12, R(location_entry));
    R(location_entry) = R(2) + R(3);
    c->budget -= 2;
    if (!blocking) {
        MEM_W32(R(29) + saved_base + 16, R(status));
        R(status) = 0;
        c->budget -= 2;
    }
    MEM_W32(R(29) + ra_offset, R(31));
    c->budget--;

    for (;;) {
        set_cd_control_callback(c, 0);
        set_reg(c, 2, 1);
        bool check_status = R(command_byte) != R(2);
        NC_POLL(c, 2);
        if (check_status) {
            set_reg(c, 2, 0x80020000);
            R(2) = MEM_R8(0x8002476c);
            NC_POLL(c, NC_LOAD_CYCLES);
            NC_POLL(c, 1);
            set_reg(c, 2, R(2) & 0x10);
            bool shell_open = R(2) != 0;
            R(4) = 1;
            NC_POLL(c, 2);
            if (shell_open)
                issue_cd_control(c, 0, 0, false, loop + 0x3c);
        }

        bool has_parameters = R(17) != 0;
        NC_POLL(c, 2);
        bool location_failed = false;
        if (has_parameters) {
            load_word(c, 2, R(location_entry));
            NC_POLL(c, 1);
            bool needs_location = R(2) != 0;
            R(4) = 2;
            NC_POLL(c, 2);
            if (needs_location) {
                issue_cd_control(c, R(17), result_reg, false, loop + 0x64);
                location_failed = R(2) != 0;
                NC_POLL(c, 2);
            }
        }
        if (!location_failed) {
            set_cd_control_callback(c, R(callback));
            set_reg(c, 4, R(command) & 0xff);
            issue_cd_control(c, R(17), result_reg, fast, loop + 0x88);
            bool success = R(2) == 0;
            R(2) = blocking ? 0 : R(status) + 1;
            NC_POLL(c, 2);
            if (success)
                break;
        }

        set_reg(c, 16, R(16) - 1);
        set_reg(c, 2, UINT32_MAX);
        bool retry = R(16) != R(2);
        NC_POLL(c, 2);
        if (!retry) {
            set_cd_control_callback(c, R(callback));
            if (!blocking) {
                set_reg(c, status, UINT32_MAX);
                set_reg(c, 2, R(status) + 1);
            }
            break;
        }
    }

    if (blocking) {
        bool issued = R(2) == 0;
        R(4) = 0;
        NC_POLL(c, 2);
        if (issued) {
            R(31) = 0x80020318;
            R(5) = R(result_reg);
            NC_POLL(c, 2);
            nc_call(c, 0x80020adc); /* CD_sync(0, result) */
            set_reg(c, 2, R(2) ^ 2);
            R(2) = R(2) == 0; /* CdlComplete */
            NC_POLL(c, 2);
        } else {
            set_reg(c, 2, 0);
        }
    }
    load_word(c, 31, R(29) + ra_offset);
    for (unsigned reg = last_saved; reg >= 16; reg--)
        load_word(c, reg, R(29) + 16 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + frame);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_cd_control(CPUState *c) {
    return cd_control(c, CD_CONTROL);
}

bool nc_credits_cd_control_f(CPUState *c) {
    return cd_control(c, CD_CONTROL_FAST);
}

bool nc_credits_cd_control_b(CPUState *c) {
    return cd_control(c, CD_CONTROL_BLOCKING);
}

bool nc_credits_cd_command_name(CPUState *c) {
    return cd_lookup_name(c, 0x80024780, 28);
}

bool nc_credits_cd_interrupt_name(CPUState *c) {
    return cd_lookup_name(c, 0x80024800, 7);
}

/* CdIntToPos adds the lead-in before splitting the signed frame count.
 * The guest leaves the intermediate BCD digits and last product in registers. */
bool nc_credits_cd_sector_to_position(CPUState *c) {
    if (c->budget <= 65)
        return false;
    s32 frames = (s32)(R(4) + 150u);
    s32 frame = frames % 75;
    s32 second = (frames / 75) % 60;
    s32 minute = frames / 4500;

    R(2) = R(5); /* returned position address */
    R(8) = frame / 10;
    R(7) = (frame / 10) * 16 + frame % 10;
    R(5) = second / 10;
    R(6) = second % 10;
    R(4) = (second / 10) * 16 + second % 10;
    nc_mult(c, (u32)minute, 0x66666667); /* guest's final divide-by-ten product */
    c->budget -= 51;
    MEM_W8(R(2) + 1, (u8)R(4));
    R(4) = minute < 0 ? UINT32_MAX : 0;
    c->budget -= 2;
    MEM_W8(R(2) + 2, (u8)R(7));

    R(3) = minute / 10;
    R(4) = (minute / 10) * 10;
    R(9) = minute % 10;
    R(5) = (minute / 10) * 16 + minute % 10;
    c->budget -= 10;
    MEM_W8(R(2), (u8)R(5)); /* return delay slot */
    c->budget -= 2;
    return true;
}

/* Drain CD interrupts and dispatch ready before sync when both flags are set. */
bool nc_credits_cd_interrupt_callback(CPUState *c) {
    if (c->budget <= 9 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES)
        return false;
    c->budget--;
    R(2) = MEM_R32(0x80024a20);
    R(29) -= 32;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 28, R(31));
    c->budget--;
    MEM_W32(R(29) + 24, R(18));
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    R(2) = MEM_R8(R(2));
    R(18) = 0x80024a39;
    R(17) = R(2) & 3; /* original CD register bank */
    c->budget -= NC_LOAD_CYCLES + 3;

    for (;;) {
        R(31) = 0x80021b88;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x80020564); /* getintr */
        set_reg(c, 16, R(2));
        const bool pending = R(16) != 0;
        R(2) = R(16) & 4;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (!pending)
            break;
        const bool ready = R(2) != 0;
        R(2) = R(16) & 2;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (ready) {
            set_reg(c, 2, 0x80020000);
            load_word(c, 2, 0x80024760);
            NC_POLL(c, NC_INSN_CYCLES);
            const bool registered = R(2) != 0;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            if (registered) {
                R(4) = MEM_R8(R(18));
                NC_POLL(c, NC_LOAD_CYCLES);
                set_reg(c, 5, 0x80020000);
                set_reg(c, 5, 0x80024e38);
                u32 handler = R(2);
                R(31) = 0x80021bc4;
                NC_POLL(c, 2 * NC_INSN_CYCLES);
                nc_call(c, handler);
            }
            set_reg(c, 2, R(16) & 2);
        }
        const bool sync = R(2) != 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (!sync)
            continue;
        set_reg(c, 3, 0x80020000);
        load_word(c, 3, 0x8002475c);
        NC_POLL(c, NC_INSN_CYCLES);
        const bool registered = R(3) != 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (registered) {
            set_reg(c, 2, 0x80020000);
            set_reg(c, 2, 0x80024a38);
            R(4) = MEM_R8(R(2));
            NC_POLL(c, NC_LOAD_CYCLES);
            set_reg(c, 5, 0x80020000);
            set_reg(c, 5, 0x80024e30);
            u32 handler = R(3);
            R(31) = 0x80021c00;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            nc_call(c, handler);
            NC_POLL(c, 2 * NC_INSN_CYCLES);
        }
    }
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, 0x80024a20);
    NC_POLL(c, NC_INSN_CYCLES);
    MEM_W8(R(2), (u8)R(17));
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 31, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Start a sector read, saving the current callbacks for the retry handler. */
bool nc_credits_cd_read(CPUState *c) {
    const u32 mode = 0x80024a80, sector_words = 0x80024a84;
    if (c->budget <= 9 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    R(29) -= 24;
    R(2) = mode;
    c->budget -= 3;
    MEM_W32(R(29) + 16, R(31));
    c->budget--;
    MEM_W32(R(2), R(6));
    c->budget--;
    R(2) = MEM_R32(R(2));
    R(3) = R(2) & 0x30;
    R(7) = R(4); /* sector count */
    c->budget -= NC_LOAD_CYCLES + 4;

    const bool normal_size = R(3) == 0;
    bool full_sector = false;
    if (!normal_size) {
        set_reg(c, 2, 0x20);
        full_sector = R(3) == R(2);
        R(2) = 585;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    }
    if (normal_size || full_sector) {
        if (normal_size)
            set_reg(c, 2, 512);
        set_reg(c, 1, 0x80020000);
        MEM_W32(sector_words, R(2));
        NC_POLL(c, NC_INSN_CYCLES);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } else {
        R(2) = 582;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        set_reg(c, 3, 0x80020000);
        set_reg(c, 3, sector_words);
        MEM_W32(R(3), R(2));
        NC_POLL(c, NC_INSN_CYCLES);
    }
    set_reg(c, 3, 0x80020000);
    set_reg(c, 3, mode);
    load_word(c, 2, R(3));
    set_reg(c, 4, 0);
    set_reg(c, 2, R(2) | 0x20);
    MEM_W32(R(3), R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x80024a78, R(5)); /* destination */
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x80024a74, R(7));
    NC_POLL(c, NC_INSN_CYCLES);
    R(31) = 0x80022b7c;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001ff7c); /* CdSyncCallback(NULL) */
    set_reg(c, 4, 0);
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x80024a98, R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    R(31) = 0x80022b90;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001ff94); /* CdReadyCallback(NULL) */
    set_reg(c, 4, UINT32_MAX);
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x80024a9c, R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    R(31) = 0x80022ba4;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001a454); /* VSync(-1) */
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x80024a90, R(2)); /* starting frame for CdReadSync */
    NC_POLL(c, NC_INSN_CYCLES);
    R(31) = 0x80022bb4;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001fdf0); /* CdStatus */
    set_reg(c, 2, R(2) & 0xe0);
    const bool pause_drive = R(2) != 0;
    R(4) = 9;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (pause_drive) {
        set_reg(c, 5, 0);
        R(31) = 0x80022bcc;
        R(6) = 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x80020210); /* CdControlB(CdlPause, NULL, NULL) */
    }
    R(31) = 0x80022bd4;
    R(4) = 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x80022908); /* cd_read_retry */
    set_reg(c, 2, (s32)R(2) > 0);
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Compare the current frame with the signed, wrapping SDK deadline in v1. */
static bool cd_read_deadline(CPUState *c, u32 resume, u32 offset, u32 frames) {
    R(31) = resume;
    R(4) = UINT32_MAX;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001a454); /* VSync(-1) */
    load_word(c, 3, R(17) + offset);
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 3, R(3) + frames);
    set_reg(c, 3, (s32)R(3) < (s32)R(2));
    return R(3) != 0;
}

/* Poll or wait for the sector read, retrying stalled reads within the overall timeout. */
bool nc_credits_cd_read_sync(CPUState *c) {
    if (c->budget <= 10)
        return false;
    R(29) -= 40;
    c->budget--;
    MEM_W32(R(29) + 24, R(18));
    R(18) = R(4); /* wait mode */
    c->budget -= 2;
    MEM_W32(R(29) + 28, R(19));
    R(19) = R(5); /* optional response buffer */
    c->budget -= 2;
    MEM_W32(R(29) + 20, R(17));
    R(17) = 0x80024a90;
    c->budget -= 3;
    MEM_W32(R(29) + 32, R(31));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    for (;;) {
        const bool timed_out = cd_read_deadline(c, 0x80022c18, 0, 1200);
        R(16) = UINT32_MAX;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (!timed_out) {
            load_word(c, 2, R(17) - 8);
            NC_POLL(c, NC_INSN_CYCLES);
            bool retry = (s32)R(2) < 0;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            if (!retry) {
                retry = cd_read_deadline(c, 0x80022c48, -4u, 60);
                NC_POLL(c, 2 * NC_INSN_CYCLES);
            }
            if (retry) {
                R(31) = 0x80022c68;
                R(4) = 1;
                NC_POLL(c, 2 * NC_INSN_CYCLES);
                nc_call(c, 0x80022908); /* cd_read_retry */
                load_word(c, 16, R(17) - 28);
                NC_POLL(c, 2 * NC_INSN_CYCLES);
            } else {
                load_word(c, 16, R(17) - 8);
            }
        }
        const bool polling = R(18) != 0;
        R(4) = 1;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (polling)
            break;
        const bool pending = (s32)R(16) > 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (!pending)
            break;
    }
    R(31) = 0x80022c90;
    R(5) = R(19);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001ff5c); /* CdReady */
    set_reg(c, 2, R(16));
    load_word(c, 31, R(29) + 32);
    load_word(c, 19, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Seek to a sector, request a read, then wait for it to finish. */
bool nc_credits_cd_read_sectors(CPUState *c) {
    if (c->budget <= 9)
        return false;
    R(29) -= 40;
    c->budget--;
    MEM_W32(R(29) + 28, R(17));
    R(17) = R(4); /* sector count */
    R(4) = R(5);  /* starting sector */
    R(5) = R(29) + 16;
    c->budget -= 4;
    MEM_W32(R(29) + 24, R(16));
    c->budget--;
    MEM_W32(R(29) + 32, R(31));
    R(31) = 0x80022584;
    R(16) = R(6); /* destination */
    c->budget -= 3;
    nc_call(c, 0x800203e0); /* CdIntToPos */

    set_reg(c, 4, 2); /* CdlSetloc */
    set_reg(c, 5, R(29) + 16);
    R(31) = 0x80022594;
    R(6) = 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001ffac); /* CdControl */

    set_reg(c, 4, R(17));
    set_reg(c, 5, R(16));
    R(31) = 0x800225a4;
    R(6) = 0x80; /* double speed */
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x80022ae0); /* CdRead */

    set_reg(c, 4, 0);
    R(31) = 0x800225b0;
    R(5) = 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x80022be8); /* CdReadSync */
    set_reg(c, 2, R(2) == 0);
    load_word(c, 31, R(29) + 32);
    load_word(c, 17, R(29) + 28);
    load_word(c, 16, R(29) + 24);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Find a named child of the requested parent in the cached CD path table. */
bool nc_credits_cd_search_directory(CPUState *c) {
    if (c->budget <= 13)
        return false;
    R(29) -= 40;
    c->budget--;
    MEM_W32(R(29) + 28, R(19));
    R(19) = R(4); /* parent directory number */
    c->budget -= 2;
    MEM_W32(R(29) + 32, R(20));
    R(20) = R(5); /* name to find */
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(16));
    R(16) = 0;
    c->budget -= 2;
    MEM_W32(R(29) + 24, R(18));
    R(18) = 0x80025460; /* name in the first record */
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(17));
    R(17) = 0;
    c->budget -= 2;
    MEM_W32(R(29) + 36, R(31));
    c->budget--;

    for (;;) {
        set_reg(c, 1, 0x80020000);
        set_reg(c, 1, R(1) + R(17));
        load_word(c, 2, R(1) + 0x5458);
        NC_POLL(c, 1);
        bool end = R(2) == 0;
        NC_POLL(c, 2);
        if (end)
            break;

        bool same_parent = R(2) == R(19);
        R(4) = R(20);
        NC_POLL(c, 2);
        if (same_parent) {
            R(31) = 0x80022270;
            R(5) = R(18);
            NC_POLL(c, 2);
            nc_call(c, 0x80022600); /* strcmp */
            bool match = R(2) == 0;
            R(18) += 44;
            NC_POLL(c, 2);
            if (match) {
                R(2) = R(16) + 1; /* directory numbers start at one */
                NC_POLL(c, 2);
                goto restore;
            }
        } else {
            set_reg(c, 18, R(18) + 44);
        }
        set_reg(c, 16, R(16) + 1);
        set_reg(c, 2, (s32)R(16) < 128);
        bool more = R(2) != 0;
        R(17) += 44;
        NC_POLL(c, 2);
        if (!more)
            break;
    }
    set_reg(c, 2, UINT32_MAX);
restore:
    load_word(c, 31, R(29) + 36);
    load_word(c, 20, R(29) + 32);
    load_word(c, 19, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2);
    return true;
}

/* The CD directory readers copy exactly the identifier length from the record. */
bool nc_credits_cd_copy_identifier(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(29) -= 8;
    bool copying = R(6) != 0;
    R(3) = R(6) - 1;
    c->budget -= 3;
    if (copying) {
        set_reg(c, 6, UINT32_MAX);
        do {
            R(2) = MEM_R8(R(5));
            NC_POLL(c, NC_LOAD_CYCLES);
            set_reg(c, 5, R(5) + 1);
            set_reg(c, 3, R(3) - 1);
            MEM_W8(R(4), (u8)R(2));
            NC_POLL(c, 1);
            copying = R(3) != R(6);
            R(4)++;
            NC_POLL(c, 2);
        } while (copying);
    }
    set_reg(c, 29, R(29) + 8);
    NC_POLL(c, 2);
    return true;
}

bool nc_credits_strcmp(CPUState *c) {
    if (c->budget <= 2)
        return false;
    bool both_present = R(4) != 0;
    c->budget -= 2;
    if (both_present) {
        both_present = R(5) != 0;
        NC_POLL(c, 2);
    }
    if (!both_present) {
        bool equal = R(4) == R(5);
        NC_POLL(c, 2);
        if (equal)
            goto equal_strings;
        bool left_present = R(4) != 0;
        R(2) = 1;
        NC_POLL(c, 2);
        if (!left_present) {
            R(2) = UINT32_MAX;
            NC_POLL(c, 2);
        }
        goto done;
    }

    for (;;) {
        R(3) = MEM_R8(R(4));
        NC_POLL(c, NC_LOAD_CYCLES);
        R(2) = MEM_R8(R(5));
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        bool different = R(3) != R(2);
        R(5)++;
        NC_POLL(c, 2);
        if (different)
            break;
        bool terminator = R(3) == 0;
        R(4)++;
        NC_POLL(c, 2);
        if (terminator)
            goto equal_strings;
    }

    /* Both unsigned bytes are read again on the mismatch path. */
    R(3) = MEM_R8(R(4));
    NC_POLL(c, NC_LOAD_CYCLES);
    R(2) = MEM_R8(R(5) - 1);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, R(3) - R(2));
    goto done;
equal_strings:
    R(2) = 0;
    NC_POLL(c, 2);
done:
    NC_POLL(c, 2);
    return true;
}

/* The credits strncmp orders null pointers before checking the byte limit. */
bool nc_credits_strncmp(CPUState *c) {
    if (c->budget <= 2)
        return false;
    bool left = R(4) != 0;
    c->budget -= 2;
    bool right = false;
    if (left) {
        right = R(5) != 0;
        NC_POLL(c, 2);
    }
    if (!left || !right) {
        bool equal = R(4) == R(5);
        NC_POLL(c, 2);
        if (equal)
            goto equal_strings;
        left = R(4) != 0;
        R(2) = 1;
        NC_POLL(c, 2);
        if (!left) {
            R(2) = UINT32_MAX;
            NC_POLL(c, 2);
        }
        goto done;
    }

    set_reg(c, 6, R(6) - 1);
    bool exhausted = (s32)R(6) < 0;
    R(2) = 0;
    NC_POLL(c, 2);
    if (exhausted)
        goto done;

    for (;;) {
        R(3) = MEM_R8(R(4));
        NC_POLL(c, NC_LOAD_CYCLES);
        R(2) = MEM_R8(R(5));
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        bool different = R(3) != R(2);
        R(5)++;
        NC_POLL(c, 2);
        if (different)
            break;
        bool terminator = R(3) == 0;
        R(4)++;
        NC_POLL(c, 2);
        if (terminator)
            goto equal_strings;
        set_reg(c, 6, R(6) - 1);
        exhausted = (s32)R(6) < 0;
        NC_POLL(c, 2);
        if (exhausted)
            break;
    }

    exhausted = (s32)R(6) < 0;
    NC_POLL(c, 2);
    if (!exhausted) {
        /* The original rereads both bytes to compute their unsigned-byte difference. */
        R(3) = MEM_R8(R(4));
        NC_POLL(c, NC_LOAD_CYCLES);
        R(2) = MEM_R8(R(5) - 1);
        NC_POLL(c, NC_LOAD_CYCLES);
        R(2) = R(3) - R(2);
        NC_POLL(c, 2);
        goto done;
    }
equal_strings:
    set_reg(c, 2, 0);
done:
    NC_POLL(c, 2);
    return true;
}

/* CdSearchFile uses _cmp to compare the first twelve bytes of two names. */
bool nc_credits_cd_names_equal(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(31) = 0x80021f24;
    R(6) = 12;
    c->budget -= 3;
    nc_call(c, 0x80022664); /* strncmp */
    set_reg(c, 2, R(2) == 0);
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* CdPosToInt converts a packed BCD minute:second:frame position to an LBA. */
bool nc_credits_cd_position_to_sector(CPUState *c) {
    if (c->budget <= 38)
        return false;
    u32 position = R(4);
    u32 minute = MEM_R8(position);
    c->budget -= NC_LOAD_CYCLES;
    u32 second = MEM_R8(position + 1);
    c->budget -= NC_LOAD_CYCLES + 19;
    u32 frame = MEM_R8(position + 2);
    u32 minutes = (minute >> 4) * 10 + (minute & 15);
    R(6) = second & 15;
    u32 seconds = (second >> 4) * 10 + R(6);
    R(4) = frame >> 4;
    R(5) = frame & 15;
    R(3) = R(4) * 10 + R(5);
    R(2) = (minutes * 60 + seconds) * 75 + R(3) - 150;
    c->budget -= NC_LOAD_CYCLES + 10;
    return true;
}

bool nc_credits_cd_set_test_parameter_count(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(1) = 0x80020000;
    c->budget--;
    MEM_W32(R(1) + 0x4a04, R(4));
    c->budget -= 3; /* store, return and delay slot */
    return true;
}

bool nc_credits_cd_last_position(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(2) = 0x80024778; /* Address of the cached CD position. */
    c->budget -= 4;
    return true;
}

bool nc_credits_graph_status(CPUState *c, bool debug_level) {
    if (c->budget <= 6)
        return false;
    c->budget--; /* lui */
    R(2) = MEM_R8(debug_level ? 0x8002455a : 0x80024558);
    c->budget -= NC_LOAD_CYCLES + 2; /* lbu, jr, delay slot */
    return true;
}

bool nc_credits_texture_page(CPUState *c) {
    /* The longest path reads the graph type twice and uses the standard layout. */
    if (c->budget <= 62)
        return false;

    R(29) -= 0x28;
    c->budget--;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 0x18, R(18));
    R(18) = R(5);
    c->budget -= 2;
    MEM_W32(R(29) + 0x1c, R(19));
    R(19) = R(6);
    c->budget -= 2;
    MEM_W32(R(29) + 0x14, R(17));
    c->budget--;
    MEM_W32(R(29) + 0x20, R(31));
    R(31) = 0x8001b574;
    R(17) = R(7);
    c->budget -= 3;

    NC_FN_ENTER(c, 0x8001c1c8);
    nc_credits_graph_status(c, false);
    bool alternate_layout = R(2) == 1;
    c->budget -= 3;
    if (!alternate_layout) {
        R(31) = 0x8001b588;
        c->budget -= 2;
        NC_FN_ENTER(c, 0x8001c1c8);
        nc_credits_graph_status(c, false);
        alternate_layout = R(2) == 2;
        c->budget -= 3;
    }

    const u32 depth = R(16) & 3, blend = R(18) & 3;
    const u32 page_x = (R(19) & 0x3ff) >> 6, y = R(17);
    if (alternate_layout) {
        R(3) = (depth << 9) | (blend << 7) | ((y & 0x300) >> 3);
        R(2) = R(3) | page_x;
        c->budget -= 11;
    } else {
        R(3) = (depth << 7) | (blend << 5) | ((y & 0x100) >> 4) | page_x;
        R(2) = R(3) | ((y & 0x200) << 2);
        c->budget -= 13;
    }

    R(31) = MEM_R32(R(29) + 0x20);
    c->budget -= NC_LOAD_CYCLES;
    for (int reg = 19; reg >= 16; reg--) {
        R(reg) = MEM_R32(R(29) + 0x10 + (reg - 16) * 4);
        c->budget -= NC_LOAD_CYCLES;
    }
    R(29) += 0x28;
    c->budget -= 3;
    return true;
}

bool nc_credits_default_draw(CPUState *c) {
    if (c->budget <= 83)
        return false;

    R(29) -= 0x28;
    c->budget--;
    MEM_W32(R(29) + 0x18, R(18));
    c->budget--;
    R(18) = MEM_R32(R(29) + 0x38); /* height, the fifth argument */
    c->budget -= NC_LOAD_CYCLES;
    MEM_W32(R(29) + 0x14, R(17));
    R(17) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 0x1c, R(19));
    R(19) = R(5);
    c->budget -= 2;
    MEM_W32(R(29) + 0x20, R(20));
    R(20) = R(6);
    c->budget -= 2;
    MEM_W32(R(29) + 0x10, R(16));
    c->budget--;
    MEM_W32(R(29) + 0x24, R(31));
    R(31) = 0x8001b448;
    R(16) = R(7);
    c->budget -= 3;
    NC_FN_ENTER(c, 0x8001b538);
    nc_credits_video_mode(c, false);

    const u32 env = R(17);
    bool pal = R(2) != 0;
    R(3) = 1;
    c->budget--;
    MEM_W16(env, (u16)R(19));
    c->budget--;
    MEM_W16(env + 2, (u16)R(20));
    c->budget--;
    MEM_W16(env + 4, (u16)R(16));
    c->budget--;
    /* Empty texture window and black background. */
    for (unsigned off = 0x0c; off <= 0x12; off += 2) {
        MEM_W16(env + off, 0);
        c->budget--;
    }
    for (unsigned off = 0x19; off <= 0x1b; off++) {
        MEM_W8(env + off, 0);
        c->budget--;
    }
    MEM_W8(env + 0x16, 1); /* dithering */
    c->budget--;
    MEM_W16(env + 6, (u16)R(18));
    R(2) = (s32)R(18) <= (pal ? 288 : 256);
    c->budget -= pal ? 4 : 3;
    MEM_W8(env + 0x17, (u8)R(2)); /* allow drawing to the displayed area */
    c->budget--;
    MEM_W16(env + 8, (u16)R(19));
    c->budget--;
    R(31) = 0x8001b49c;
    MEM_W16(env + 10, (u16)R(20));
    c->budget -= 2;
    NC_FN_ENTER(c, 0x8001c1c8);
    nc_credits_graph_status(c, false);
    bool read_again = R(2) != 1;
    R(2) = env;
    c->budget -= 3;
    if (read_again) {
        R(31) = 0x8001b4b0;
        c->budget -= 2;
        NC_FN_ENTER(c, 0x8001c1c8);
        nc_credits_graph_status(c, false);
        R(2) = env;
        c->budget--;
    }

    R(3) = 10;
    c->budget--;
    MEM_W16(env + 0x14, (u16)R(3)); /* texture page */
    c->budget--;
    MEM_W8(env + 0x18, 0); /* disable background clear */
    c->budget--;
    R(31) = MEM_R32(R(29) + 0x24);
    c->budget -= NC_LOAD_CYCLES;
    for (int reg = 20; reg >= 16; reg--) {
        R(reg) = MEM_R32(R(29) + 0x10 + (reg - 16) * 4);
        c->budget -= NC_LOAD_CYCLES;
    }
    R(29) += 0x28;
    c->budget -= 3;
    return true;
}
