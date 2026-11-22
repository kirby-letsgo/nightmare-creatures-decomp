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

bool nc_credits_draw_sync_callback(CPUState *c) {
    if (c->budget <= 10)
        return false;
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

/* GPU_cw passes the GP0 command word in a0 to BIOS A0 service 0x49. */
bool nc_credits_bios_gpu_command(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(10) = 0xa0;
    R(9) = 0x49;
    c->budget -= 3;
    nc_jump(c, R(10));
    return true;
}

/* DeliverEvent selects BIOS B0 service 7 in the tail jump's delay slot. */
bool nc_credits_deliver_event(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(10) = 0xb0;
    R(9) = 7;
    c->budget -= 3;
    nc_jump(c, R(10));
    return true;
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

/* Load a CD port pointer from the guest table, including the load delay nop. */
static void cd_flush_port(CPUState *c, u32 slot) {
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, slot);
    NC_POLL(c, 1);
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
