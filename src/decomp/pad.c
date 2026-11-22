#include "decomp/pad.h"

enum { PAD_PACKET = 0x8008f1c0u, PACKET_BYTES = 34, PAD_MASK = 0x238, PAD_INDEX = 0x23a };

/* The packet status need only differ from ff. The low nibble of the ID
 * describes packet length and does not participate in pad selection. */
static bool supported_pad(u32 packet, u32 status, int *cycles) {
    if (status == 0xff)
        return false;
    *cycles += 8;
    if (MEM_R8(packet + 1) >> 4 == 4)
        return true;
    *cycles += 8;
    return MEM_R8(packet + 1) >> 4 == 7;
}

bool nc_select_pad(CPUState *c) {
    if (c->budget <= 70)
        return false;
    const u32 gp = R(28);
    const u8 status = MEM_R8(PAD_PACKET);
    MEM_W16(gp + PAD_MASK, 0);
    int cycles = 10 + 9; /* through each status branch and its delay slot */
    if (supported_pad(PAD_PACKET, status, &cycles)) {
        MEM_W16(gp + PAD_MASK, 1);
        ++cycles;
    }
    const u32 second = PAD_PACKET + PACKET_BYTES;
    if (supported_pad(second, MEM_R8(second), &cycles)) {
        MEM_W16(gp + PAD_MASK, MEM_R16(gp + PAD_MASK) | 2);
        cycles += 6;
    }
    const u32 mask = MEM_R16(gp + PAD_MASK);
    R(3) = mask;
    cycles += 8; /* mask test, its delay slot, and return */
    if (mask & 1) {
        R(2) = mask == 3 ? 1 : 2;
        MEM_W16(gp + PAD_INDEX, 0);
        cycles += mask == 3 ? 3 : 5;
    } else {
        R(2) = 1;
        cycles += 4;
        if (mask == 2) {
            MEM_W16(gp + PAD_INDEX, 1);
            ++cycles;
        }
    }
    c->budget -= cycles;
    return true;
}

/* Each signed direction has an open interval for direction + 0x2000,
 * and an inclusive outer threshold for direction alone. The two flag
 * tests differ. The budget follows the branches taken by the guest. */
static void stick_direction(u32 sample, u32 inner, u32 outer, bool increasing, u32 flags, u32 bit,
                            u32 *buttons, int *cycles) {
    if (increasing ? sample > inner : sample < inner) {
        *cycles += 12;
        if (increasing ? sample < outer : sample > outer) {
            *cycles += 7;
            if (!(flags & 0x40100000)) {
                *buttons |= bit | 0x2000;
                ++*cycles;
            }
        }
    }
    if (increasing ? sample >= outer : sample <= outer) {
        *cycles += 7;
        if (!(flags & 0x40000000)) {
            *buttons |= bit;
            ++*cycles;
        }
    }
}

static void right_stick(bool at_edge, u32 flags, u32 bit, u32 *buttons, int *cycles) {
    if (at_edge) {
        *cycles += 11;
        if (!(flags & 0x40000000)) {
            *cycles += 2;
            if (!(*buttons & 0xa0)) {
                *buttons |= 0x100 | bit;
                ++*cycles;
            }
        }
    }
}

/* 800700FC enters with v1 = selected index and v0 = index << 4.
 * Finish at the shared stack epilogue, including port two in bits 16..31. */
bool nc_decode_pad(CPUState *c) {
    if (c->budget <= 550)
        return false;
    const u32 offset = (R(2) + R(3)) * 2;
    const u32 packet = PAD_PACKET + offset;
    const u32 gp = R(28);
    u32 buttons = (u16)~MEM_R16(packet + 2);
    u32 type = MEM_R8(packet + 1) >> 4;
    int cycles = 19; /* through the first type branch */
    R(1) = 0x80090000u + offset;
    R(5) = offset;
    R(3) = type;
    if (type == 4) {
        MEM_W8(gp + 0x410, 0x80);
        MEM_W8(gp + 0x414, 0xff);
        cycles += 6;
    } else if (type == 7) {
        const u32 config = MEM_R32(gp + 0x244);
        MEM_W8(gp + 0x410, MEM_R8(packet + 6));
        MEM_W8(gp + 0x414, MEM_R8(packet + 7));
        const u32 flags = MEM_R32(config);
        const u32 x = MEM_R8(packet + 6), y = MEM_R8(packet + 7);
        /* First comparison includes saving both axis bytes. Each of the
         * remaining seven axis comparisons costs 21 cycles before its arm. */
        cycles += 2 + 28 + 7 * 21;
        stick_direction(x, MEM_R8(config + 0x1a), MEM_R8(config + 0x1e), false, flags, 0x80,
                        &buttons, &cycles);
        stick_direction(x, MEM_R8(config + 0x1c), MEM_R8(config + 0x20), true, flags, 0x20,
                        &buttons, &cycles);
        stick_direction(y, MEM_R8(config + 0x1b), MEM_R8(config + 0x1f), false, flags, 0x10,
                        &buttons, &cycles);
        stick_direction(y, MEM_R8(config + 0x1d), MEM_R8(config + 0x21), true, flags, 0x40,
                        &buttons, &cycles);
        const u32 right_x = MEM_R8(packet + 4);
        cycles += 4 * 16; /* two right-stick and two raw button tests */
        right_stick(right_x < 16, flags, 0x80, &buttons, &cycles);
        right_stick(right_x >= 241, flags, 0x20, &buttons, &cycles);
        const u32 raw = MEM_R16(packet + 2);
        if (!(raw & 2)) {
            cycles += 11;
            if (!(flags & 0x40100000)) {
                buttons = 0x2000;
                ++cycles;
            }
        }
        if (!(raw & 4)) {
            cycles += 11;
            if (!(flags & 0x40000000)) {
                buttons |= 0x200;
                ++cycles;
            }
        }
        R(4) = config;
        R(3) = 0x40000000;
    } else {
        cycles += 4;
    }
    cycles += 7;
    if (MEM_R16(gp + PAD_MASK) & 2) {
        buttons |= (u32)(u16)~MEM_R16(PAD_PACKET + PACKET_BYTES + 2) << 16;
        cycles += 10;
    }
    R(2) = R(6) = buttons;
    c->budget -= cycles;
    return true;
}
