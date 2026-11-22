#include "decomp/pad.h"

enum { PAD_PACKET = 0x8008f1c0u, PACKET_BYTES = 34, PAD_MASK = 0x238, PAD_INDEX = 0x23a };

/* Save the player-control frame before deciding whether to read input.
 * v0 retains the blocking bits and v1 retains the full flags for the selector. */
bool nc_enter_player_input(CPUState *c) {
    if (c->budget <= 16)
        return false;
    R(29) -= 0x20;
    MEM_W32(R(29) + 0x10, R(16));
    R(3) = MEM_R32(R(28) + 0x244);
    MEM_W32(R(29) + 0x1c, R(31));
    MEM_W32(R(29) + 0x18, R(18));
    MEM_W32(R(29) + 0x14, R(17));
    R(3) = MEM_R32(R(3));
    R(2) = R(3) & 0x80009800;
    R(16) = R(4);
    c->budget -= 16;
    return true;
}

/* The early exit skips the button-word store and shares the frame restore. */
bool nc_leave_player_input(CPUState *c, bool store_buttons) {
    int cycles = store_buttons ? 16 : 15;
    if (c->budget <= cycles)
        return false;
    if (store_buttons)
        MEM_W32(R(16) + 0x2f8, R(17));
    R(31) = MEM_R32(R(29) + 0x1c);
    R(18) = MEM_R32(R(29) + 0x18);
    R(17) = MEM_R32(R(29) + 0x14);
    R(16) = MEM_R32(R(29) + 0x10);
    R(29) += 0x20;
    c->budget -= cycles;
    return true;
}

/* Player control has already loaded the flags into v1. A live read
 * resumes at the player_input hook with the full button word in v0.
 * Sequence playback sets s1 itself and joins the caller after the hook. */
u32 nc_player_input_source(CPUState *c) {
    if (c->budget <= 7)
        return 0x80044754;
    R(2) = R(3) & 0x30;
    if (R(2)) {
        R(31) = 0x80044768;
        c->budget -= 5;
        nc_call(c, 0x80062d2c);
        R(17) = R(2) & 0xffff;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        return 0x80044784;
    }
    R(4) = 0x800d7c04;
    R(5) = 1;
    R(31) = 0x80044780;
    c->budget -= 7;
    nc_call(c, 0x80070050);
    return 0x80044780;
}

/* The PSX2 BIOS stubs tail-jump to B0, setting t1 in the delay slot.
 * With three cycles or less, the generated path polls at each boundary. */
bool nc_pad_bios_call(CPUState *c, NcPadBiosService service) {
    if (c->budget <= 3)
        return false;
    R(10) = 0xb0;
    R(9) = service;
    c->budget -= 3;
    nc_jump(c, R(10));
    return true;
}

static void return_from_pad_driver(CPUState *c) {
    R(31) = MEM_R32(R(29) + 0x10);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(29) += 0x18;
    NC_POLL(c, NC_INSN_CYCLES);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
}

/* The SDK calls, in particular VSync, can service an event. Keep the
 * original polling boundaries after each call and its delay slot. */
static void pad_driver_call(CPUState *c, u32 target, u32 return_address) {
    R(31) = return_address;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, target);
}

/* InitPAD's SDK patch clears eleven instruction words relative to an entry
 * in the guest B0 table. The runtime supplies a RAM-backed table. */
bool nc_sdk_patch_pad(CPUState *c) {
    /* Includes EnterCriticalSection (4 cycles) and the FlushCache stub (3). */
    if (c->budget <= 71)
        return false;
    R(1) = 0x80090000;
    MEM_W32(0x8008efe8, R(31));
    R(31) = 0x80073f84;
    c->budget -= 4;
    nc_call(c, 0x80073d8c); /* EnterCriticalSection */

    R(10) = 0xb0;
    R(31) = 0x80073f90;
    R(9) = 0x57; /* GetB0Table */
    c->budget -= 3;
    nc_call(c, R(10));

    R(2) = MEM_R32(R(2) + 0x16c);
    R(9) = 11;
    while (R(9)) {
        --R(9);
        MEM_W32(R(2) + 0x594, 0);
        R(2) += 4;
    }
    R(31) = 0x80073fb0;
    c->budget -= 4 + 11 * 4 + 2;
    nc_call(c, 0x80073fc4); /* FlushCache */
    R(31) = MEM_R32(0x8008efe8);
    c->budget -= 7;
    return true;
}

/* Psy-Q InitPAD preserves both buffer/length pairs across _patch_pad and
 * ChangeClearPad before passing them on to BIOS B0(12h). */
bool nc_sdk_init_pad(CPUState *c) {
    if (c->budget <= 11)
        return false;
    R(29) -= 0x28;
    for (unsigned i = 0; i < 4; ++i) {
        MEM_W32(R(29) + 0x10 + 4 * i, R(16 + i));
        R(16 + i) = R(4 + i);
    }
    MEM_W32(R(29) + 0x20, R(31));
    R(31) = 0x80073ee0;
    c->budget -= 11;
    nc_call(c, 0x80073f74); /* _patch_pad */
    R(4) = 0;
    pad_driver_call(c, 0x80073e3c, 0x80073ee8); /* ChangeClearPad */
    for (unsigned i = 0; i < 3; ++i) {
        R(4 + i) = R(16 + i);
        NC_POLL(c, NC_INSN_CYCLES);
    }
    R(7) = R(19); /* fourth argument in the delay slot */
    pad_driver_call(c, 0x80073f44, 0x80073efc);
    R(31) = MEM_R32(R(29) + 0x20);
    NC_POLL(c, NC_LOAD_CYCLES);
    for (int i = 3; i >= 0; --i) {
        R(16 + i) = MEM_R32(R(29) + 0x10 + 4 * i);
        NC_POLL(c, NC_LOAD_CYCLES);
    }
    R(29) += 0x28;
    NC_POLL(c, NC_INSN_CYCLES);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_sdk_start_pad(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    R(31) = 0x80073f2c;
    c->budget -= 4;
    nc_call(c, 0x80073f54); /* BIOS StartPAD */
    R(4) = 0;
    pad_driver_call(c, 0x80073e3c, 0x80073f34); /* ChangeClearPad */
    return_from_pad_driver(c);
    return true;
}

bool nc_start_pad_driver(CPUState *c) {
    if (c->budget <= 8)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    R(4) = PAD_PACKET;
    R(5) = PACKET_BYTES;
    R(6) = PAD_PACKET + PACKET_BYTES;
    R(7) = PACKET_BYTES;
    R(31) = 0x8006fedc;
    c->budget -= 8;
    nc_call(c, 0x80073eb4);                     /* InitPAD */
    pad_driver_call(c, 0x80073f1c, 0x8006fee4); /* StartPAD */
    R(4) = 0;
    pad_driver_call(c, 0x80073e3c, 0x8006feec); /* ChangeClearPad */
    R(4) = 0;
    pad_driver_call(c, 0x80073fd4, 0x8006fef4); /* VSync */
    pad_driver_call(c, 0x8006ff34, 0x8006fefc); /* select from the refreshed packets */
    return_from_pad_driver(c);
    return true;
}

bool nc_stop_pad_driver(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    R(31) = 0x8006ff1c;
    c->budget -= 4;
    nc_call(c, 0x80073d7c); /* StopPAD */
    MEM_W16(R(28) + PAD_MASK, 0);
    NC_POLL(c, NC_INSN_CYCLES);
    MEM_W16(R(28) + PAD_INDEX, 0);
    NC_POLL(c, NC_INSN_CYCLES);
    return_from_pad_driver(c);
    return true;
}

/* Both readers use port one's raw packet. At 800677CC the caller checks
 * for type 7 before asking for the stick bytes at 80067810. */
bool nc_pad_packet_type(CPUState *c) {
    if (c->budget <= 6)
        return false;
    R(2) = MEM_R8(PAD_PACKET + 1) >> 4;
    c->budget -= 6;
    return true;
}

bool nc_read_pad_stick(CPUState *c) {
    if (c->budget <= 13)
        return false;
    R(2) = MEM_R8(PAD_PACKET + 6);
    MEM_W8(R(4), R(2));
    /* The first destination may overlap the packet. Read Y after writing X. */
    R(2) = MEM_R8(PAD_PACKET + 7);
    MEM_W8(R(5), R(2));
    c->budget -= 13;
    return true;
}

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

/* Return zero on completion, or the first guest instruction still to run.
 * Entry headroom covers selection, decoding and the return. The disconnect
 * handlers are guest calls with independent budgets, so check again there. */
u32 nc_read_pad(CPUState *c) {
    if (c->budget <= 700)
        return 0x80070050;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(16));
    MEM_W32(R(29) + 0x14, R(31));
    R(31) = 0x80070064;
    R(16) = R(4);
    c->budget -= 5;
    nc_call(c, 0x8006ff34);

    const u32 gp = R(28);
    bool have_pad = MEM_R16(gp + PAD_MASK) != 0;
    c->budget -= 6;
    if (have_pad) {
        R(3) = (u32)(s32)(s16)MEM_R16(gp + PAD_INDEX);
        R(2) = R(3) << 4;
        c->budget -= 6;
    }
    if (have_pad && R(3) != 1) {
        nc_decode_pad(c); /* entry headroom includes its 550-cycle guard */
    } else {
        R(3) = 0xc0000000;
        c->budget -= 2;
        if (R(16)) {
            R(4) = MEM_R32(gp + 0x244);
            R(3) = 0xc0009840;
            bool suppressed = (MEM_R32(R(4)) & R(3)) != 0;
            R(2) = 0;
            c->budget -= 11;
            if (suppressed)
                goto restore;
            R(3) = 0x03000040;
            R(2) = MEM_R32(R(4) + 4) & R(3);
            c->budget -= 8;
            if (!R(2)) {
                R(31) = 0x800700c8;
                c->budget -= 2;
                nc_call(c, 0x8006eae8);
                if (c->budget <= 15)
                    return 0x800700c8;
                R(2) = MEM_R32(R(28) + 0x244);
                R(1) = 0x80090000;
                MEM_W32(0x8008ef30, 0x10);
                R(4) = 0x80088480;
                R(3) = MEM_R32(R(2) + 4) | 0x40;
                R(5) = 0x10;
                R(31) = 0x800700f4;
                MEM_W32(R(2) + 4, R(3));
                c->budget -= 15;
                nc_call(c, 0x80063058);
            }
        }
        R(2) = 0;
        NC_POLL(c, 2);
    }
restore:
    if (c->budget <= 9)
        return 0x800705d8;
    R(31) = MEM_R32(R(29) + 0x14);
    R(16) = MEM_R32(R(29) + 0x10);
    R(29) += 0x18;
    c->budget -= 9;
    return 0;
}

/* Player control calls 80062D2C instead of polling the controller when
 * either input-sequence flag (0x30) is set. Each four-byte record has a
 * button halfword, an unsigned repeat count, and a byte copied to 8008F04C. */
enum { SEQUENCE_BASE = 0x184, SEQUENCE_INDEX = 0x188, SEQUENCE_REMAINING = 0x3f8 };

/* Level setup clears the active sequence even when playback is disabled.
 * With playback enabled it installs the records, resets the reader, and
 * snapshots 8008EF18 between the two guest critical-section calls. */
bool nc_init_pad_sequence(CPUState *c) {
    if (c->budget <= 54)
        return false;
    const u32 gp = R(28);
    R(3) = MEM_R32(gp + 0x244);
    R(29) -= 0x98;
    MEM_W32(R(29) + 0x90, R(31));
    R(2) = MEM_R32(R(3)) & 0x30;
    MEM_W32(gp + SEQUENCE_BASE, 0);
    c->budget -= 12;
    if (R(2)) {
        u32 flags = MEM_R32(R(3) + 4);
        R(3) = 0x40000;
        bool alternate = (flags & R(3)) != 0;
        MEM_W32(gp + 0x344, alternate ? 0x8000 : 0x200);
        R(2) = MEM_R32(0x800d4190);
        MEM_W32(gp + SEQUENCE_INDEX, 0);
        MEM_W8(gp + SEQUENCE_REMAINING, 0);
        R(1) = 0x80090000;
        MEM_W32(0x8008f04c, 0);
        MEM_W32(gp + SEQUENCE_BASE, R(2));
        R(31) = 0x80062c88;
        c->budget -= alternate ? 19 : 20;
        nc_call(c, 0x80073d8c); /* EnterCriticalSection */
        R(2) = MEM_R32(0x8008ef18);
        R(1) = 0x80090000;
        MEM_W32(0x8008ef1c, R(2));
        R(31) = 0x80062ca0;
        c->budget -= 8;
        nc_call(c, 0x80073d9c); /* ExitCriticalSection */
    }
    R(31) = MEM_R32(R(29) + 0x90);
    R(29) += 0x98;
    c->budget -= 6;
    return true;
}

bool nc_stop_pad_sequence(CPUState *c) {
    if (c->budget <= 6)
        return false;
    R(2) = 5;
    MEM_W32(R(28) + SEQUENCE_BASE, 0);
    R(1) = 0x80090000;
    MEM_W32(0x8008f164, R(2));
    c->budget -= 6;
    return true;
}

bool nc_pad_sequence_ended(CPUState *c) {
    if (c->budget <= 40)
        return false;
    R(3) = MEM_R32(R(28) + SEQUENCE_BASE);
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    c->budget -= 6;
    if (!R(3)) {
        R(2) = 1;
        --c->budget;
    } else {
        R(4) = R(3) + 4 * MEM_R32(R(28) + SEQUENCE_INDEX);
        R(3) = MEM_R16(R(4));
        R(2) = 0;
        c->budget -= 12;
        if (R(3) == 0x4c41) {
            R(3) = MEM_R16(R(4) + 2);
            c->budget -= 6;
            if (R(3) == 0x4e41) { /* "ALAN" in guest byte order */
                R(31) = 0x80062d10;
                c->budget -= 2;
                nc_call(c, 0x80062cb0);
                R(2) = 1;
                c->budget -= 2;
            }
        }
    }
    R(31) = MEM_R32(R(29) + 0x10);
    R(29) += 0x18;
    c->budget -= 6;
    return true;
}

bool nc_read_pad_sequence(CPUState *c) {
    if (c->budget <= 100)
        return false;
    const u32 gp = R(28);
    R(4) = MEM_R32(gp + SEQUENCE_BASE);
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x14, R(31));
    MEM_W32(R(29) + 0x10, R(16));
    c->budget -= 7;
    if (!R(4)) {
        R(2) = 0;
        --c->budget;
    } else {
        u32 remaining = MEM_R8(gp + SEQUENCE_REMAINING);
        c->budget -= 6;
        u32 index = MEM_R32(gp + SEQUENCE_INDEX);
        if (remaining) {
            R(3) = remaining - 1;
            MEM_W8(gp + SEQUENCE_REMAINING, R(3));
            R(16) = R(4) + 4 * index;
            c->budget -= 6;
        } else {
            ++index;
            R(3) = 4 * index;
            MEM_W32(gp + SEQUENCE_INDEX, index);
            R(16) = R(4) + R(3);
            R(2) = index;
            R(31) = 0x80062d6c;
            c->budget -= 9;
            nc_call(c, 0x80062cc8);
            c->budget -= 2;
            if (!R(2)) {
                MEM_W8(gp + SEQUENCE_REMAINING, MEM_R8(R(16) + 2) - 1);
                c->budget -= 8;
            }
        }
        /* On the last repetition, copy the next record's fourth byte.
         * Even the terminating call returns the current record's button word. */
        remaining = MEM_R8(gp + SEQUENCE_REMAINING);
        u32 side_byte = MEM_R8(R(16) + (remaining ? 3 : 7));
        R(1) = 0x80090000;
        MEM_W32(0x8008f04c, side_byte);
        R(2) = MEM_R16(R(16));
        c->budget -= 6 + (remaining ? 3 : 5) + 7;
    }
    R(31) = MEM_R32(R(29) + 0x14);
    R(16) = MEM_R32(R(29) + 0x10);
    R(29) += 0x18;
    c->budget -= 9;
    return true;
}
