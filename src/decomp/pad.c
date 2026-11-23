#include "decomp/pad.h"

enum { PAD_PACKET = 0x8008f1c0u, PACKET_BYTES = 34, PAD_MASK = 0x238, PAD_INDEX = 0x23a };

/* Accept a masked press only when the previous word contains none of its bits.
 * A successful press replaces the previous word with the entire requested mask. */
bool nc_record_player_button_press(CPUState *c) {
    if (c->budget <= 14)
        return false;

    const u32 mask = R(6);
    R(5) &= mask;
    R(2) = 0;
    c->budget -= 5;
    if (R(5)) {
        const u32 previous_addr = R(4) + 0x2f8;
        c->budget -= 7;
        if (!(MEM_R32(previous_addr) & mask)) {
            MEM_W32(previous_addr, mask);
            R(2) = 1;
            c->budget -= 2;
        }
    }
    return true;
}

/* On the Up path, a Y sample above the threshold selects action 1.
 * Below the threshold, X must lie strictly between the horizontal bounds. */
u32 nc_player_up_stick_bounds(CPUState *c) {
    if (c->budget <= 37)
        return 0x80044f2c;

    R(4) = MEM_R32(R(28) + 0x244);
    R(5) = MEM_R8(0x8008f1b4);
    R(3) = MEM_R8(R(4) + 0x1f);
    c->budget -= 14;
    if (R(5) <= R(3)) {
        R(2) = R(17) & 0x10;
        c->budget -= 3;
        if (R(5) == R(3))
            return 0x80044f94;

        R(3) = MEM_R8(0x8008f1b0);
        c->budget -= 11;
        if (R(3) <= MEM_R8(R(4) + 0x1a))
            return 0x80044f94;

        const u8 upper = MEM_R8(R(4) + 0x1c);
        c->budget -= 7;
        if (R(3) >= upper) {
            R(2) = 1;
            return 0x80044f90;
        }
    }
    R(2) = 1;
    MEM_W8(R(16) + 0x39b, 1);
    c->budget -= 2;
    return 0x800451d0;
}

/* Up selects action 6 when the sample is below the configured threshold.
 * The alternate entry already has the Up result in v0. Both entries set
 * v0 to the Down bit in the first branch's delay slot. */
u32 nc_player_up_action(CPUState *c, bool test_up) {
    if (c->budget <= (test_up ? 19 : 18))
        return test_up ? 0x80044f90 : 0x80044f94;

    if (test_up) {
        R(2) = R(17) & 0x10;
        c->budget -= 1;
    }
    const bool up = R(2) != 0;
    R(2) = R(17) & 0x40;
    c->budget -= 2;
    if (!up)
        return 0x80044fc8;

    const u32 config = MEM_R32(R(28) + 0x244);
    const u8 sample = MEM_R8(0x8008f1b4);
    R(3) = sample < MEM_R8(config + 0x1f);
    R(2) = 6;
    c->budget -= 14;
    if (!R(3))
        return 0x80044fc4;
    MEM_W8(R(16) + 0x39b, (u8)R(2));
    c->budget -= 2;
    return 0x800451d0;
}

/* Down, R1 and L1 select action bytes in priority order. The alternate
 * entry receives the Down result in v0 from an earlier delay slot. */
u32 nc_player_down_shoulder_action(CPUState *c, bool test_down) {
    if (c->budget <= (test_down ? 12 : 11))
        return test_down ? 0x80044fc4 : 0x80044fc8;

    const u32 buttons = R(17);
    if (test_down) {
        R(2) = buttons & 0x40;
        c->budget -= 1;
    }
    if (R(2)) {
        R(2) = 2;
        c->budget -= 4;
    } else if (buttons & 0x800) {
        R(2) = 4;
        c->budget -= 7;
    } else if (buttons & 0x400) {
        R(2) = 3;
        c->budget -= 10;
    } else {
        R(2) = buttons & 0xa0;
        R(4) = R(16);
        c->budget -= 11;
        return R(2) ? 0x800451d4 : 0x8004500c;
    }
    MEM_W8(R(16) + 0x39b, (u8)R(2));
    return 0x800451d0;
}

/* Circle skips the Down path. The other entry already has the Down test
 * in v0 from the earlier branch's delay slot. */
u32 nc_player_down_path(CPUState *c, bool check_circle) {
    if (c->budget <= (check_circle ? 12 : 9))
        return check_circle ? 0x80045014 : 0x80045020;

    if (check_circle) {
        const u32 buttons = R(17);
        R(2) = buttons & 0x40;
        c->budget -= 3;
        if (buttons & 0x2000)
            return 0x800450a8;
    }
    c->budget -= 2;
    if (!R(2))
        return 0x800450a8;
    R(2) = MEM_R32(R(16) + 0x2c8) & 0x8000;
    c->budget -= 7;
    return R(2) ? 0x80045094 : 0x8004503c;
}

/* On the Down path, mode zero tests Square and mode one tests Cross.
 * The second branch reloads the mode into v1 and leaves the Cross bit in v0. */
u32 nc_player_down_face_buttons(CPUState *c) {
    if (c->budget <= 16)
        return 0x8004503c;

    const u32 mode_addr = R(16) + 0x388;
    const u8 mode = MEM_R8(mode_addr);
    R(2) = R(17) & 0x8000;
    c->budget -= 6;
    if (!mode) {
        c->budget -= 2;
        if (R(2))
            return 0x8004506c;
    }

    R(3) = MEM_R8(mode_addr);
    R(2) = R(17) & 0x4000;
    c->budget -= 6;
    if (R(3) != 1)
        return 0x80045080;
    R(4) = R(16);
    c->budget -= 2;
    return R(2) ? 0x8004506c : 0x80045084;
}

/* Both face-button checks use the actor already in a0 for the press test.
 * The branch delay slot reloads a0 from s0 for the next action. */
bool nc_check_player_face_press(CPUState *c, bool square) {
    if (c->budget <= 19)
        return false;

    R(5) = R(17);
    R(31) = square ? 0x800450c8 : 0x80045168;
    R(6) = square ? 0x8000 : 0x4000;
    c->budget -= 3;
    nc_record_player_button_press(c);
    R(4) = R(16);
    c->budget -= 2;
    return true;
}

/* The Triangle branch loads 0x13 in its delay slot on both outcomes.
 * The store also has a direct entry with the byte already in v0. */
u32 nc_player_triangle_action(CPUState *c, bool check_press) {
    if (c->budget <= (check_press ? 21 : 2))
        return check_press ? 0x80045180 : 0x80045194;

    if (check_press) {
        R(5) = R(17);
        R(31) = 0x8004518c;
        R(6) = 0x1000;
        c->budget -= 3;
        nc_record_player_button_press(c);
        const bool pressed = R(2) != 0;
        R(2) = 0x13;
        c->budget -= 2;
        if (!pressed)
            return 0x8004519c;
    }
    MEM_W8(R(16) + 0x39b, (u8)R(2));
    c->budget -= 2;
    return 0x800451e8;
}

/* When Triangle has no new press, four actor states keep the current action.
 * Each taken branch leaves its delay-slot value in v0. */
bool nc_player_triangle_fallback(CPUState *c) {
    if (c->budget <= 15)
        return false;
    R(3) = MEM_R8(R(16) + 0x38a);
    switch (R(3)) {
    case 3:
        R(2) = 9;
        c->budget -= 6;
        break;
    case 9:
        R(2) = 0x22;
        c->budget -= 8;
        break;
    case 0x22:
        R(2) = 0xa;
        c->budget -= 10;
        break;
    case 0xa:
        R(2) = 0xa;
        c->budget -= 12;
        break;
    default:
        R(2) = 0xa;
        MEM_W8(R(16) + 0x39b, 0);
        MEM_W8(R(16) + 0x38a, 0);
        c->budget -= 15;
        break;
    }
    return true;
}

/* A fresh L2 press resets the direction-repeat counter. */
bool nc_reset_repeat_on_l2_press(CPUState *c) {
    if (c->budget <= 28)
        return false;

    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    R(31) = 0x800445ec;
    R(6) = 0x100;
    c->budget -= 4;
    nc_record_player_button_press(c); /* entry budget covers the full nested call */
    if (R(2)) {
        R(1) = 0x800d0000;
        MEM_W16(0x800cbffa, 0);
        c->budget -= 2;
    }
    R(31) = MEM_R32(R(29) + 0x10);
    R(29) += 0x18;
    c->budget -= 8;
    return true;
}

/* A fresh R2 press clears the direction-repeat counter before the action check. */
bool nc_check_player_r2_press(CPUState *c) {
    if (c->budget <= 26)
        return false;

    R(29) -= 0x20;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(4);
    R(6) = 0x200;
    MEM_W32(R(29) + 0x18, R(31));
    R(31) = 0x80044628;
    MEM_W32(R(29) + 0x14, R(17));
    c->budget -= 7;
    nc_record_player_button_press(c); /* entry budget covers the nested call */
    c->budget -= 2;
    if (R(2)) {
        R(17) = 0x800cbffa;
        MEM_W16(R(17), 0);
        c->budget -= 3;
    }
    return true;
}

/* This handler keeps the original input in s1 for the masked store after
 * the action call. A held or absent R2 takes the plain return with v0 zero. */
u32 nc_dispatch_player_r2_action(CPUState *c) {
    if (c->budget <= 28)
        return 0x8004468c;

    R(29) -= 0x20;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(4);
    MEM_W32(R(29) + 0x14, R(17));
    R(17) = R(5);
    MEM_W32(R(29) + 0x18, R(31));
    R(31) = 0x800446ac;
    R(6) = 0x200;
    c->budget -= 8;
    nc_record_player_button_press(c);
    const bool pressed = R(2) != 0;
    R(2) = 0; /* branch delay slot */
    c->budget -= 2;
    if (!pressed)
        return 0x800446d4;

    R(1) = 0x800d0000;
    MEM_W16(0x800cbffa, 0);
    R(31) = 0x800446c4;
    R(4) = R(16);
    c->budget -= 4;
    nc_call(c, 0x80052b10);
    return 0x800446c4;
}

/* Both R2 handlers restore the same frame. After an accepted action, the
 * second handler first saves the input word with both trigger bits cleared. */
bool nc_leave_player_r2(CPUState *c, bool record_buttons) {
    const int cycles = record_buttons ? 16 : 12;
    if (c->budget <= cycles)
        return false;
    if (record_buttons) {
        R(2) = 1;
        R(3) = R(17) & ~0x300u;
        MEM_W32(R(16) + 0x2f8, R(3));
    }
    R(31) = MEM_R32(R(29) + 0x18);
    R(17) = MEM_R32(R(29) + 0x14);
    R(16) = MEM_R32(R(29) + 0x10);
    R(29) += 0x20;
    c->budget -= cycles;
    return true;
}

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

/* These exits save both ports in the delay slot of a jump to the epilogue. */
bool nc_save_player_buttons_on_exit(CPUState *c) {
    if (c->budget <= 2)
        return false;
    MEM_W32(R(16) + 0x2f8, R(17));
    c->budget -= 2;
    return true;
}

/* The shorter L2 entry already has its actor in a0. Both calls pass the
 * full button word from s1, including when L2 has modified the previous word. */
bool nc_call_player_trigger(CPUState *c, u32 entry) {
    const int cycles = entry == 0x800451d4 ? 2 : 3;
    if (c->budget <= cycles)
        return false;

    if (entry != 0x800451d4)
        R(4) = R(16);
    const bool r2 = entry == 0x800451dc;
    R(31) = r2 ? 0x800451e8 : 0x800451dc;
    R(5) = R(17);
    c->budget -= cycles;
    nc_call(c, r2 ? 0x8004460c : 0x800445dc);
    return true;
}

/* Both live-input callers pass (0x800d7c04, 1). Only player control
 * resumes at the modern-controls hook with the decoded word in v0. */
bool nc_call_live_pad(CPUState *c, u32 return_address) {
    if (c->budget <= 4)
        return false;
    R(4) = 0x800d7c04;
    R(5) = 1;
    R(31) = return_address;
    c->budget -= 4;
    nc_call(c, 0x80070050);
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
    c->budget -= 3;
    nc_call_live_pad(c, 0x80044780); /* entry guard covers the four call instructions */
    return 0x80044780;
}

/* The live pad returns both ports in v0. Keep the complete word after the
 * control hook has applied its direction edits. */
void nc_take_live_player_buttons(CPUState *c) {
    R(17) = R(2);
}

/* Live and sequenced input meet here in s1, after the live-input hook.
 * Select skips the store. A missing actor or zero halfword takes the store
 * path. The remaining paths also load the previous word for edge checks. */
u32 nc_dispatch_player_buttons(CPUState *c) {
    if (c->budget <= 17)
        return 0x80044784;

    const u32 buttons = R(17);
    R(2) = buttons & 1;
    c->budget -= 3;
    if (R(2))
        return 0x800451ec;

    c->budget -= 2;
    if (!R(16))
        return 0x800451e8;

    bool active = MEM_R16(0x800d7c26) != 0;
    R(2) = buttons & 0x100;
    c->budget -= 7;
    if (!active)
        return 0x800451e8;

    R(3) = MEM_R32(R(16) + 0x2f8);
    R(2) = 0x50; /* delay slot on both branches */
    c->budget -= 5;
    return (buttons & 0x100) ? 0x800447b8 : 0x800448d4;
}

/* Player control suppresses the button word when any masked actor flag is set. */
bool nc_check_player_button_suppression(CPUState *c) {
    if (c->budget <= 7)
        return false;
    R(5) = MEM_R32(R(16) + 0x2c8);
    R(2) = R(5) & 0x01780000;
    c->budget -= 7;
    return true;
}

/* Shared by the suppression check and the later player-control branches.
 * The jump clears the complete button word in its delay slot. */
bool nc_clear_player_buttons(CPUState *c) {
    if (c->budget <= 2)
        return false;
    R(17) = 0;
    c->budget -= 2;
    return true;
}

/* The L2 branch records left/right press edges. Holding both gives left
 * priority, even when left was already down. Bit 1 marks an active sequence.
 * Bit 2 and the adjacent halfword track direction and repeat count. */
bool nc_player_l2_direction(CPUState *c, bool l2_held) {
    if (c->budget <= (l2_held ? 37 : 3))
        return false;
    R(1) = 0x800d0000;
    if (!l2_held) {
        R(2) = 0x3c;
        MEM_W16(0x800cc002, R(2));
        c->budget -= 3;
        return true;
    }
    MEM_W16(0x800cc002, R(2)); /* 0x50 from the preceding delay slot */
    bool left = (R(17) & 0x80) != 0;
    int cycles = 5;
    if (!left) {
        R(2) = R(3) & 0x20;
        cycles += 3;
        if (!(R(17) & 0x20)) {
            c->budget -= cycles;
            return true;
        }
    }
    R(2) = R(3) & (left ? 0x80 : 0x20);
    cycles += 2;
    if (!R(2)) {
        const u32 flags = MEM_R16(0x800cbff8);
        cycles += 8;
        if (!(flags & 2)) {
            R(3) = flags;
            R(2) = flags | (left ? 6 : 2);
            MEM_W16(0x800cbff8, R(2));
            MEM_W16(0x800cbffa, 0);
            cycles += left ? 8 : 7;
        } else {
            bool same_direction = left == ((flags & 4) != 0);
            if (same_direction)
                MEM_W16(0x800cbffa, MEM_R16(0x800cbffa) + 1);
            else
                MEM_W16(0x800cbffa, 0);
            R(3) = 0x800cbff8;
            R(2) = left ? flags | 4 : flags & 0xfffb;
            MEM_W16(R(3), R(2));
            cycles += 2 + 9; /* direction test, then flags update and exit */
            if (left)
                cycles += same_direction ? 10 : 2;
            else
                cycles += same_direction ? 8 : 4;
        }
    }
    c->budget -= cycles;
    return true;
}

/* Save the caller's frame and select live input or playback. Playback
 * can issue the guest call with argument 0x21 before checking the pad. */
u32 nc_enter_pad_dispatch(CPUState *c) {
    if (c->budget <= 24)
        return 0x80042d9c;
    R(4) = MEM_R32(R(28) + 0x244);
    R(29) -= 0x28;
    MEM_W32(R(29) + 0x24, R(31));
    MEM_W32(R(29) + 0x20, R(16));
    R(3) = MEM_R32(R(4));
    R(2) = R(3) & 0x8000;
    c->budget -= 13;
    if (!(R(3) & 0x30))
        return 0x80042e7c;

    R(3) = 0x40000;
    c->budget -= 2;
    if (!R(2)) {
        R(2) = MEM_R32(R(4) + 4) & R(3);
        c->budget -= 7;
        if (!R(2)) {
            R(4) = 0x21;
            R(31) = 0x80042de4;
            c->budget -= 2;
            nc_call(c, 0x800632e0);
        }
    }
    return 0x80042de4;
}

/* Playback skips the live pad read if either flag is set. The first
 * check leaves s0 untouched. The second sets the mask used after polling. */
u32 nc_pad_playback_poll_gate(CPUState *c) {
    if (c->budget <= 19)
        return 0x80042de4;

    R(3) = MEM_R32(R(28) + 0x244);
    u32 flags = MEM_R32(R(3));
    R(2) = 1; /* both branch delay slots */
    c->budget -= 11;
    if (flags & 0x10)
        return 0x80042f54;

    u32 state = MEM_R32(R(3) + 4);
    R(16) = 0x02000000;
    c->budget -= 7;
    if (state & R(16))
        return 0x80042f54;

    R(4) = 0;
    --c->budget;
    return 0x80042e18;
}

/* Any live button interrupts playback. Only an idle read consults the
 * sequence-end marker. Keep both guest return addresses for the callees. */
u32 nc_poll_playback_input(CPUState *c) {
    if (c->budget <= 2)
        return 0x80042e18;
    R(31) = 0x80042e20;
    R(5) = 1;
    c->budget -= 2;
    nc_call(c, 0x80070050);
    bool pressed = R(2) != 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (pressed)
        return 0x80042e38;

    R(31) = 0x80042e30;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x80062cc8);
    return 0x80042e30;
}

/* Live input goes straight to the flag check. With idle pads, the caller
 * tests the sequence-end result first. Both paths return 1 in v0. */
bool nc_pad_playback_interrupt(CPUState *c, bool check_sequence_end) {
    if (c->budget <= (check_sequence_end ? 23 : 21))
        return false;
    if (check_sequence_end) {
        bool ended = R(2) != 0;
        R(2) = 1;
        c->budget -= 2;
        if (!ended)
            return true;
    }

    R(4) = MEM_R32(R(28) + 0x244);
    R(3) = MEM_R32(R(4) + 4);
    if (!(R(3) & R(16))) {
        MEM_W32(R(4) + 4, R(3) | R(16));
        MEM_W16(0x800d7c30, 30);
        R(1) = 0x80090000;
        MEM_W32(0x8008f164, 5);
        c->budget -= 10;
    }
    R(2) = 1;
    c->budget -= 11;
    return true;
}

/* Gate the Start action on a rising edge with Select released. The caller
 * keeps both ports in s0 for its later Select check and button history. */
u32 nc_pad_start_edge(CPUState *c) {
    if (c->budget <= 34)
        return 0x80042e8c;
    R(16) = R(2);
    R(2) = R(16) & 8;
    c->budget -= 4;
    if (!R(2))
        return 0x80042efc;

    u32 previous = MEM_R32(R(28) + 0x144);
    R(2) = R(16) & 1; /* delay slot also runs when Start was held */
    c->budget -= 7;
    if (previous & 8)
        return 0x80042efc;

    c->budget -= 2;
    if (R(2))
        return 0x80042efc;

    R(3) = MEM_R32(R(28) + 0x244);
    R(4) = MEM_R32(R(3));
    R(2) = R(4) & 0x40;
    c->budget -= 11;
    if (R(2))
        return 0x80042f18; /* save the full button word */

    u32 flags = MEM_R32(R(3) + 4);
    R(3) = 0x03000000;
    R(2) = 0x08000000; /* delay slot precedes the last flag test */
    c->budget -= 7;
    if (flags & R(3))
        return 0x80042efc;

    R(2) &= R(4);
    c->budget -= 3;
    return R(2) ? 0x80042efc : 0x80042ef4;
}

/* Start resumes at the Select check. A new Select press skips the history
 * store and returns -1, regardless of the value returned by the action. */
bool nc_call_pad_action(CPUState *c, bool select_pressed) {
    if (c->budget <= 2)
        return false;
    R(31) = select_pressed ? 0x80042f48 : 0x80042efc;
    c->budget -= 2;
    nc_call(c, select_pressed ? 0x80055f5c : 0x8006eae8);
    if (select_pressed) {
        R(2) = 0xffffffff;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    }
    return true;
}

bool nc_leave_pad_dispatch(CPUState *c) {
    if (c->budget <= 9)
        return false;
    R(31) = MEM_R32(R(29) + 0x24);
    R(16) = MEM_R32(R(29) + 0x20);
    R(29) += 0x28;
    c->budget -= 9;
    return true;
}

/* With flag 0x40 set, save both ports and report 1. Otherwise only a
 * released Select updates history here. A new press continues at the call. */
u32 nc_pad_select_edge(CPUState *c) {
    if (c->budget <= 20)
        return 0x80042efc;

    u32 config = MEM_R32(R(28) + 0x244);
    u32 flags = MEM_R32(config);
    c->budget -= 11;
    if (flags & 0x40) {
        MEM_W32(R(28) + 0x144, R(16));
        R(2) = 1;
        c->budget -= 3;
        return 0x80042f54;
    }

    R(2) = 0xffffffff; /* both Select branches set v0 in the delay slot */
    c->budget -= 2;
    if (!(R(16) & 1)) {
        MEM_W32(R(28) + 0x144, R(16));
        --c->budget;
        return 0x80042f54;
    }
    u32 previous = MEM_R32(R(28) + 0x144);
    c->budget -= 7;
    return (previous & 1) ? 0x80042f54 : 0x80042f40;
}

/* Only menu records with a signed halfword of 6 take the horizontal
 * input path here. Left also requires bit 27 of the configuration flags. */
u32 nc_menu_left_input(CPUState *c) {
    if (c->budget <= 29)
        return 0x800683d0;

    u32 index = MEM_R32(R(28) + 0x190);
    R(6) = index * 12;
    R(1) = 0x80090000 + R(6);
    R(3) = (u32)(s32)(s16)MEM_R16(0x800883ca + R(6));
    R(2) = R(30) & 0x10; /* Up mask in the branch delay slot */
    c->budget -= 15;
    if (R(3) != 6)
        return 0x8006848c;

    R(2) = R(30) & 0x80;
    R(3) = 0x08000000;
    c->budget -= 3;
    if (!R(2))
        return 0x80068438;

    u32 config = MEM_R32(R(28) + 0x244);
    R(2) = MEM_R32(config + 4) & R(3);
    R(4) = R(22);
    c->budget -= 11;
    return R(2) ? 0x80068424 : 0x80068438;
}

/* Right uses the opposite configuration flag sense from Left. Reload the
 * record index here because the intervening Left action can change it. */
u32 nc_menu_right_input(CPUState *c) {
    if (c->budget <= 24)
        return 0x80068438;

    R(2) = R(30) & 0x20;
    R(3) = 0x08000000;
    c->budget -= 3;
    if (!R(2))
        return 0x80068488;

    u32 config = MEM_R32(R(28) + 0x244);
    u32 flags = MEM_R32(config + 4);
    R(2) = R(30) & 0x10; /* delay slot prepares the Up check */
    c->budget -= 11;
    if (flags & R(3))
        return 0x8006848c;

    R(4) = R(22);
    R(5) = MEM_R32(R(28) + 0x190) * 12;
    R(6) = 0x10;
    R(2) = 0x800883c0; /* added to the offset in the upcoming call's delay slot */
    c->budget -= 10;
    return 0x80068480;
}

/* Record 12 plays the selected sound, then prepares the text for its
 * new setting. The Left path jumps to the shared formatting call. */
bool nc_menu_horizontal_feedback(CPUState *c, bool right) {
    if (c->budget <= 2)
        return false;
    R(7) = 0;
    R(31) = right ? 0x80068754 : 0x800686a0;
    c->budget -= 2;
    nc_call(c, 0x8006c2e4);
    R(4) = 0x80080000;
    NC_POLL(c, NC_INSN_CYCLES);
    R(4) = MEM_R32(0x80086e34);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(5) = right ? 0x80090000 : 0x80020000;
    NC_POLL(c, NC_INSN_CYCLES);
    R(5) = right ? 0x8008ef8c : 0x80018eb8;
    NC_POLL(c, NC_INSN_CYCLES);
    if (!right)
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Both record 12 directions format the chosen text before reloading the
 * selected record. Leave a1 as its byte offset for the action call delay slot. */
bool nc_menu_format_horizontal_feedback(CPUState *c) {
    if (c->budget <= 2)
        return false;
    R(31) = 0x8006876c;
    c->budget -= 2;
    nc_call(c, 0x80073454);
    R(4) = R(22);
    NC_POLL(c, NC_INSN_CYCLES);
    R(2) = MEM_R32(R(28) + 0x190);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(6) = 1;
    NC_POLL(c, NC_INSN_CYCLES);
    R(5) = R(2) << 1;
    NC_POLL(c, NC_INSN_CYCLES);
    R(5) += R(2);
    NC_POLL(c, NC_INSN_CYCLES);
    R(5) <<= 2;
    NC_POLL(c, NC_INSN_CYCLES);
    R(2) = 0x80090000;
    NC_POLL(c, NC_INSN_CYCLES);
    R(2) = 0x800883c0;
    NC_POLL(c, NC_INSN_CYCLES);
    return true;
}

/* Left supplies the record offset in a2. Right has already prepared
 * the offset in a1, the base in v0, and the action in a2. */
bool nc_menu_horizontal_action(CPUState *c, bool left) {
    int cycles = left ? 5 : 2;
    if (c->budget <= cycles)
        return false;
    if (left) {
        R(5) = 0x800883c0 + R(6);
        R(6) = 0x10;
        R(31) = 0x80068438;
    } else {
        R(5) += R(2);
        R(31) = 0x80068488;
    }
    c->budget -= cycles;
    nc_call(c, 0x80063b70);
    return true;
}

/* A new Cross press takes priority. A held Cross still lets a new
 * Triangle through. Both actions use the same menu record. */
u32 nc_menu_face_press(CPUState *c) {
    if (c->budget <= 25)
        return 0x8006851c;

    const u32 buttons = R(30);
    u32 action = 0;
    c->budget -= 3;
    if (buttons & 0x4000) {
        c->budget -= 7;
        if (!(MEM_R32(R(28) + 0x18c) & 0x4000))
            action = 0x10;
    }
    if (!action) {
        c->budget -= 2;
        if (buttons & 0x1000) {
            c->budget -= 7;
            if (!(MEM_R32(R(28) + 0x18c) & 0x1000))
                action = 0x20;
        }
    }
    if (!action) {
        R(2) = buttons & 0x10; /* Up mask from the branch delay slot */
        return 0x80068578;
    }
    R(4) = R(22);
    R(2) = MEM_R32(R(28) + 0x190);
    R(6) = action;
    c->budget -= 6;
    return 0x800685fc;
}

static s32 increment_menu_repeat(CPUState *c) {
    u32 count = MEM_R32(R(28) + 0x194) + 1u;
    MEM_W32(R(28) + 0x194, count);
    return (s32)count;
}

/* The menu scan requires a Select or shoulder button, then checks the
 * entire button word for a new press, including buttons outside that mask. */
u32 nc_menu_button_scan_entry(CPUState *c) {
    if (c->budget <= 25)
        return 0x80067130;

    R(29) -= 0x38;
    for (unsigned i = 0; i < 8; ++i)
        MEM_W32(R(29) + 0x10 + 4 * i, R(16 + i));
    MEM_W32(R(29) + 0x30, R(31));
    R(20) = R(4);
    R(2) = R(20) & 0xf01;
    c->budget -= 13;
    if (!R(2))
        return 0x80067308;

    R(2) = MEM_R32(R(28) + 0x18c) & R(20);
    R(19) = 0;
    c->budget -= 7;
    if (R(2) == R(20))
        return 0x80067308;

    R(23) = 0x02000000;
    R(22) = 0x80088596;
    R(21) = R(22) - 2;
    R(2) = 0;
    c->budget -= 5;
    return 0x8006718c;
}

/* Both record paths reset on mismatch and advance on a matching non-Select
 * word. A matching Select continues with the action for that record type. */
static u32 match_menu_sequence_step(CPUState *c, u32 offset) {
    R(5) = MEM_R8(0x80088594 + offset);
    u32 expected = (u32)(s32)(s16)MEM_R16(R(3) + 2 * R(5));
    R(2) = 1;
    c->budget -= 14;
    if (R(20) != expected)
        return 0x800672ec;

    R(3) = R(5) + 1;
    c->budget -= 2;
    if (R(20) != 1)
        return 0x800672e0;

    return 0;
}

/* Each scan record spans 50 bytes. The loop carries twice the index in v0.
 * Type 10 matches the full button word against a signed halfword sequence. */
u32 nc_menu_sequence_match(CPUState *c) {
    if (c->budget <= 39)
        return 0x8006718c;

    u32 offset = ((R(2) + R(19)) * 8 + R(19)) * 2;
    R(4) = offset;
    R(1) = 0x80090000 + offset;
    R(3) = MEM_R8(0x80088595 + offset);
    R(2) = R(3) << 3;
    c->budget -= 12;
    if (R(3) != 10)
        return 0x8006725c;

    R(3) = R(22) + offset;
    u32 next = match_menu_sequence_step(c, offset);
    if (next)
        return next;

    R(18) = 0;
    R(3) = MEM_R32(R(28) + 0x244);
    R(17) = R(21);
    R(2) = MEM_R32(R(3) + 4) | 0x2000;
    R(16) = 0;
    MEM_W32(R(3) + 4, R(2));
    c->budget -= 11;
    return 0x80067200;
}

/* Other record types consult a 28-byte descriptor before matching input. */
u32 nc_menu_record_sequence(CPUState *c) {
    if (c->budget <= 28)
        return 0x8006725c;

    u32 descriptor_offset = (R(2) - R(3)) * 4;
    u32 offset = R(4);
    R(1) = 0x80090000 + descriptor_offset;
    R(2) = MEM_R32(0x80088178 + descriptor_offset) & R(23);
    R(3) = offset + R(22);
    c->budget -= 11;
    if (!R(2))
        return 0x800672f8;

    R(1) = 0x80090000 + offset;
    u32 next = match_menu_sequence_step(c, offset);
    if (next)
        return next;

    R(4) = offset + R(21);
    c->budget--;
    return 0x800672b4;
}

/* A type 10 completion visits ten records and applies each enabled action. */
bool nc_menu_apply_enabled_record(CPUState *c) {
    if (c->budget <= 20)
        return false;
    u32 type = MEM_R8(0x80088595 + R(16));
    R(3) = type * 28;
    R(1) = 0x80090000 + R(3);
    R(2) = MEM_R32(0x80088178 + R(3)) & R(23);
    R(4) = R(17);
    c->budget -= 18;
    if (R(2)) {
        R(31) = 0x80067240;
        R(5) = 1;
        c->budget -= 2;
        nc_call(c, 0x80066ed0);
    }
    return true;
}

u32 nc_menu_advance_enabled_record(CPUState *c) {
    if (c->budget <= 7)
        return 0x80067240;
    R(17) += 50;
    R(18)++;
    R(2) = (s32)R(18) < 10;
    R(16) += 50;
    c->budget -= 5;
    if (R(2))
        return 0x80067200;
    R(19)++;
    c->budget -= 2;
    return 0x800672fc;
}

bool nc_menu_apply_sequence(CPUState *c) {
    if (c->budget <= 2)
        return false;
    R(31) = 0x800672bc;
    R(5) = 1;
    c->budget -= 2;
    nc_call(c, 0x80066ed0);
    return true;
}

/* Completing an ordinary sequence clears all eleven progress bytes,
 * starting with the last record. The common scan tail increments s3. */
bool nc_menu_reset_sequences(CPUState *c) {
    if (c->budget <= 69)
        return false;
    for (int offset = 500; offset >= 0; offset -= 50)
        MEM_W8(0x80088594u + offset, 0);
    R(1) = 0x80090000; /* last record address before the signed displacement */
    R(2) = (u32)-50;
    R(19) = 11;
    c->budget -= 69;
    return true;
}

/* A matching step saves the low byte of the next position. A mismatch
 * clears the position in the fixed record table. */
bool nc_menu_store_progress(CPUState *c, bool matched) {
    if (c->budget <= 3)
        return false;
    if (matched) {
        R(2) = R(4) + R(21);
        MEM_W8(R(2), (u8)R(3));
    } else {
        R(1) = 0x80090000 + R(4);
        MEM_W8(0x80088594 + R(4), 0);
    }
    c->budget -= 3;
    return true;
}

u32 nc_menu_next_record(CPUState *c, bool increment) {
    int cycles = increment ? 4 : 3;
    if (c->budget <= cycles)
        return increment ? 0x800672f8 : 0x800672fc;
    if (increment)
        R(19)++;
    R(2) = R(19) << 1; /* branch delay slot also runs on the last record */
    c->budget -= cycles;
    return (s32)R(19) < 11 ? 0x8006718c : 0x80067308;
}

bool nc_leave_menu_button_scan(CPUState *c) {
    if (c->budget <= 30)
        return false;

    R(31) = MEM_R32(R(29) + 0x30);
    for (int i = 7; i >= 0; --i)
        R(16 + i) = MEM_R32(R(29) + 0x10 + 4 * i);
    R(29) += 0x38;
    c->budget -= 30;
    return true;
}

/* Ordinary menu records process the direction before saving the button word. */
bool nc_menu_apply_direction(CPUState *c) {
    if (c->budget <= 2)
        return false;
    R(31) = 0x800687bc;
    c->budget -= 2;
    nc_call(c, 0x80063ccc);
    MEM_W32(R(28) + 0x18c, R(30));
    NC_POLL(c, NC_INSN_CYCLES);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* The scan detects edges against the previous word. Publish this frame's
 * full word only after the scan returns. */
bool nc_menu_scan_and_save_buttons(CPUState *c) {
    if (c->budget <= 2)
        return false;
    R(31) = 0x800687d4;
    R(4) = R(30);
    c->budget -= 2;
    nc_call(c, 0x80067130);
    MEM_W32(R(28) + 0x18c, R(30));
    NC_POLL(c, NC_INSN_CYCLES);
    return true;
}

/* Config bit 0x40 enables this menu input path. Records flagged 0x4000
 * accumulate actions on the stack, with the first shoulder taking priority. */
u32 nc_menu_record_shoulders(CPUState *c) {
    if (c->budget <= 42)
        return 0x800687d8;
    u32 config = MEM_R32(R(28) + 0x244);
    R(2) = MEM_R32(config) & 0x40;
    c->budget -= 11;
    if (!R(2))
        return 0x80068d60;

    R(3) = MEM_R32(R(28) + 0x190) * 12u;
    R(1) = 0x80090000 + R(3);
    u32 item = MEM_R32(0x800883c0 + R(3));
    u32 flags = MEM_R32(item + 4);
    R(2) = R(30) & 0x400;
    c->budget -= 20;
    if (!(flags & 0x4000))
        return 0x800689a8;

    u32 action = 0x100;
    c->budget -= 2;
    if (!R(2)) {
        action = 0x200;
        c->budget -= 2;
        if (!(R(30) & 0x800)) {
            R(2) = R(30) & 0x10;
            return 0x8006885c;
        }
    }
    R(10) = MEM_R32(R(29) + 0x18) | action;
    MEM_W32(R(29) + 0x18, R(10));
    R(2) = R(30) & 0x10;
    c->budget -= 7;
    return 0x8006885c;
}

/* Each pair contributes at most one action. The second button is tested
 * only when the first is clear. An empty pair leaves t2 untouched. */
static void menu_action_pair(CPUState *c, u32 first, u32 second, u32 first_action,
                             u32 second_action) {
    u32 action = first_action;
    c->budget -= 2;
    if (!first) {
        c->budget -= 2;
        if (!second)
            return;
        action = second_action;
    }
    R(10) = MEM_R32(R(29) + 0x18) | action;
    MEM_W32(R(29) + 0x18, R(10));
    c->budget -= 7;
}

bool nc_menu_record_button_pairs(CPUState *c) {
    if (c->budget <= 33)
        return false;
    const u32 buttons = R(30);
    menu_action_pair(c, R(2), buttons & 0x40, 0x4, 0x8);             /* Up, Down */
    menu_action_pair(c, buttons & 0x80, buttons & 0x20, 0x80, 0x40); /* Left, Right */
    menu_action_pair(c, buttons & 0x8000, buttons & 0x2000, 0x400, 0x800);
    R(2) = buttons & 0x1000;
    return true;
}

/* A fresh Triangle press replaces the accumulated actions and saves the
 * full button word. A held Triangle preserves the accumulated actions. */
u32 nc_menu_record_triangle(CPUState *c) {
    if (c->budget <= 28)
        return 0x800688ec;
    const u32 action_slot = R(29) + 0x18;
    c->budget -= 2;
    if (R(2)) {
        R(2) = MEM_R32(R(28) + 0x18c) & 0x1000;
        c->budget -= 7;
        if (!R(2)) {
            MEM_W32(action_slot, 0x20);
            MEM_W32(R(28) + 0x18c, R(30));
            c->budget -= 2;
        }
    }
    R(10) = MEM_R32(action_slot);
    R(4) = R(22);
    c->budget -= 6;
    if (!R(10))
        return 0x80068944;

    R(5) = MEM_R32(R(28) + 0x190) * 12u;
    R(6) = R(10);
    R(2) = 0x800883c0; /* added to the offset in the call delay slot */
    c->budget -= 11;
    return 0x8006893c;
}

static void call_offset_record_action(CPUState *c, u32 return_address) {
    R(31) = return_address;
    R(5) += R(2); /* record address formed in the call delay slot */
    c->budget -= 2;
    nc_call(c, 0x80063b70);
}

bool nc_menu_dispatch_record_action(CPUState *c) {
    if (c->budget <= 2)
        return false;
    call_offset_record_action(c, 0x80068944);
    return true;
}

bool nc_menu_dispatch_feedback_action(CPUState *c) {
    if (c->budget <= 2)
        return false;
    call_offset_record_action(c, 0x80068794);
    NC_POLL(c, 2 * NC_INSN_CYCLES); /* jump to the menu scan and its delay slot */
    return true;
}

/* Only record 10 handles a fresh Select press here. Each branch delay slot
 * leaves 100 in v0, including when the special action is taken. */
u32 nc_menu_record_select(CPUState *c) {
    if (c->budget <= 19)
        return 0x80068944;
    R(3) = MEM_R32(R(28) + 0x190);
    R(2) = 100;
    c->budget -= 6;
    if (R(3) != 10)
        return 0x80068d00;
    c->budget -= 3;
    if (!(R(30) & 1))
        return 0x80068d00;
    c->budget -= 7;
    if (MEM_R32(R(28) + 0x18c) & 1)
        return 0x80068d00;
    R(4) = R(22);
    R(5) = 0x80088438;
    c->budget -= 3;
    return 0x80068980;
}

/* A fresh Select on record 10 dispatches action 0x20, clears button
 * history, and sets the guest flag before the second call. */
bool nc_menu_apply_record_select(CPUState *c) {
    if (c->budget <= 2)
        return false;
    R(31) = 0x80068988;
    R(6) = 0x20;
    c->budget -= 2;
    nc_call(c, 0x80063b70);

    R(2) = 1;
    NC_POLL(c, NC_INSN_CYCLES);
    MEM_W32(R(28) + 0x18c, 0);
    NC_POLL(c, NC_INSN_CYCLES);
    R(1) = 0x80090000;
    NC_POLL(c, NC_INSN_CYCLES);
    MEM_W32(0x8008eee4, R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    R(31) = 0x800689a0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8006ebc4);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

static bool menu_record_repeat_ready(CPUState *c, u32 button) {
    R(2) = increment_menu_repeat(c) < 6;
    R(4) = R(22);
    c->budget -= 9;
    if (R(2)) {
        R(2) = MEM_R32(R(28) + 0x18c) & button;
        c->budget -= 7;
        return R(2) == 0;
    }
    return true;
}

static u32 repeat_menu_record_action(CPUState *c, u32 button, u32 action) {
    if (!menu_record_repeat_ready(c, button))
        return 0x80068cf8;
    R(5) = 0x800883c0 + R(3);
    R(6) = action;
    c->budget -= 5;
    return 0x80068ce0;
}

/* Ordinary records take one direction in disc order. Before the sixth
 * tick, only a direction absent from the previous word dispatches. */
u32 nc_menu_record_direction_repeat(CPUState *c) {
    if (c->budget <= 30)
        return 0x800689a8;
    static const struct {
        u32 button, action;
    } directions[] = {{0x10, 4}, {0x40, 8}, {0x80, 0x80}, {0x20, 0x40}};

    c->budget--; /* initial Up mask */
    for (unsigned i = 0; i < sizeof directions / sizeof directions[0]; ++i) {
        c->budget -= 2;
        if (!(R(30) & directions[i].button))
            continue;

        return repeat_menu_record_action(c, directions[i].button, directions[i].action);
    }
    R(2) = R(30) & 0x400; /* next button mask, from the final delay slot */
    return 0x80068adc;
}

u32 nc_menu_record_shoulder_repeat(CPUState *c) {
    if (c->budget <= 25)
        return 0x80068adc;
    c->budget -= 2;
    if (R(2)) /* L1 mask prepared by the preceding D-pad check */
        return repeat_menu_record_action(c, 0x400, 0x100);
    c->budget -= 2;
    if (R(30) & 0x800)
        return repeat_menu_record_action(c, 0x800, 0x200);
    R(2) = R(30) & 0x4000; /* Cross mask in the branch delay slot */
    return 0x80068b74;
}

/* Cross uses the offset already in v1. Triangle reloads the record index
 * and computes its own offset, even when Cross was held. */
u32 nc_menu_record_face_edges(CPUState *c) {
    if (c->budget <= 30)
        return 0x80068b74;
    bool cross = R(2) != 0;
    R(2) = R(30) & 0x1000;
    c->budget -= 2;
    if (cross) {
        c->budget -= 7;
        if (!(MEM_R32(R(28) + 0x18c) & 0x4000)) {
            R(4) = R(22);
            R(5) = 0x800883c0 + R(3);
            R(6) = 0x10;
            c->budget -= 6;
            return 0x80068ce0;
        }
    }
    bool triangle = R(2) != 0;
    R(2) = R(30) & 0x8000;
    c->budget -= 2;
    if (triangle) {
        c->budget -= 7;
        if (!(MEM_R32(R(28) + 0x18c) & 0x1000)) {
            R(4) = R(22);
            R(2) = 0x800883c0;
            R(5) = R(2) + 12u * MEM_R32(R(28) + 0x190);
            R(6) = 0x20;
            c->budget -= 12;
            return 0x80068ce0;
        }
    }
    return 0x80068bec;
}

/* Square and Circle reload the selected index before dispatch. */
u32 nc_menu_record_face_repeat(CPUState *c) {
    if (c->budget <= 31)
        return 0x80068bec;
    bool square = R(2) != 0;
    c->budget -= 2;
    if (!square) {
        c->budget -= 2;
        if (!(R(30) & 0x2000)) {
            R(2) = R(30) & 8; /* Start mask from the delay slot */
            return 0x80068ca4;
        }
    }
    if (!menu_record_repeat_ready(c, square ? 0x8000 : 0x2000))
        return 0x80068cf8;
    R(2) = 0x800883c0;
    R(5) = R(2) + 12u * MEM_R32(R(28) + 0x190);
    R(6) = square ? 0x400 : 0x800;
    c->budget -= 11;
    return 0x80068ce0;
}

/* Only record 1 turns a fresh Start press into action 0x20. */
u32 nc_menu_record_start(CPUState *c) {
    if (c->budget <= 19)
        return 0x80068ca4;
    c->budget -= 2;
    if (!R(2))
        return 0x80068cf4;
    u32 previous = MEM_R32(R(28) + 0x18c);
    R(2) = 1;
    c->budget -= 7;
    if (previous & 8)
        return 0x80068cf4;
    R(3) = MEM_R32(R(28) + 0x190);
    R(2) = 100;
    c->budget -= 6;
    if (R(3) != 1)
        return 0x80068d00;
    R(4) = R(22);
    R(5) = 0x800883cc;
    R(6) = 0x20;
    c->budget -= 4;
    return 0x80068ce0;
}

bool nc_menu_dispatch_and_save_buttons(CPUState *c) {
    if (c->budget <= 2)
        return false;
    R(31) = 0x80068ce8;
    c->budget -= 2;
    nc_call(c, 0x80063b70);
    MEM_W32(R(28) + 0x18c, R(30));
    NC_POLL(c, NC_INSN_CYCLES);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Some paths arrive with the record comparison already in v1 and v0.
 * Record 100 replaces the actor's button word when an actor is present. */
u32 nc_menu_save_record_buttons(CPUState *c, bool load_record) {
    if (c->budget <= (load_record ? 20 : 16))
        return load_record ? 0x80068cf8 : 0x80068d00;
    if (load_record) {
        R(3) = MEM_R32(R(28) + 0x190);
        R(2) = 100;
        c->budget -= 4;
    }
    MEM_W32(R(28) + 0x18c, R(30));
    c->budget -= 3;
    if (R(3) != R(2))
        return 0x80068d3c;

    u32 actor = MEM_R32(0x800ce9c4);
    R(2) = 0xfffffcff;
    c->budget -= 7;
    if (actor) {
        R(3) = actor;
        MEM_W32(actor + 0x2f8, R(2));
        c->budget -= 6;
    }
    return 0x80068d30;
}

static void clear_menu_history_and_call(CPUState *c, u32 return_address) {
    MEM_W32(R(28) + 0x18c, 0);
    R(31) = return_address;
    c->budget -= 3;
    nc_call(c, 0x8006ebc4);
}

bool nc_menu_reset_record_100(CPUState *c) {
    if (c->budget <= 3)
        return false;
    clear_menu_history_and_call(c, 0x80068d3c);
    return true;
}

/* Reload the record after the record 100 call. Record 101 takes the early
 * exit with v0 set to one in the final jump's delay slot. */
u32 nc_menu_check_record_101(CPUState *c) {
    if (c->budget <= 9)
        return 0x80068d3c;
    R(3) = MEM_R32(R(28) + 0x190);
    R(2) = 101;
    c->budget -= 6;
    if (R(3) != R(2))
        return 0x80068d60;
    clear_menu_history_and_call(c, 0x80068d58);
    R(2) = 1;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return 0x80068fe4;
}

/* Record 4 delays the Cross check until 600 elapsed ticks. After 24000,
 * the original code takes a separate action before checking Cross. */
u32 nc_menu_record_four_timing(CPUState *c) {
    if (c->budget <= 33)
        return 0x80068d60;
    u32 config = MEM_R32(R(28) + 0x244);
    R(3) = 0x80001800;
    R(2) = 4;
    c->budget -= 11;
    if (!(MEM_R32(config) & R(3)))
        return 0x80068e84;

    R(3) = MEM_R32(R(28) + 0x190);
    R(2) = R(30) & 0x4000;
    c->budget -= 6;
    if (R(3) != 4)
        return 0x80068dd4;

    R(3) = MEM_R32(0x8008ef10) - MEM_R32(R(28) + 0x3e8);
    s32 elapsed = (s32)R(3);
    R(2) = elapsed < 600;
    c->budget -= 12;
    if (elapsed > 24000)
        return 0x80068dbc;
    R(2) = R(30) & 0x4000;
    c->budget -= 2;
    if (elapsed >= 600)
        return 0x80068dd4;
    R(2) = 0;
    c->budget -= 2;
    return 0x80068fe4;
}

/* The timeout dispatches record 4 before testing this frame's Cross bit. */
bool nc_menu_dispatch_timed_record(CPUState *c) {
    if (c->budget <= 5)
        return false;
    R(4) = R(22);
    R(5) = 0x800883f0;
    R(6) = 0x10;
    R(31) = 0x80068dd0;
    c->budget -= 5;
    nc_call(c, 0x80063b70);
    R(2) = R(30) & 0x4000;
    NC_POLL(c, NC_INSN_CYCLES);
    return true;
}

/* v0 holds the current Cross bit. Only a fresh press dispatches the
 * selected 12-byte record and saves the complete button word. */
bool nc_menu_record_cross(CPUState *c) {
    if (c->budget <= 20)
        return false;
    c->budget -= 2;
    if (!R(2))
        return true;

    R(2) = MEM_R32(R(28) + 0x18c) & 0x4000;
    R(4) = R(22);
    c->budget -= 7;
    if (R(2))
        return true;

    u32 index = MEM_R32(R(28) + 0x190);
    R(2) = 0x800883c0;
    R(5) = R(2) + 12 * index;
    R(6) = 0x10;
    R(31) = 0x80068e14;
    c->budget -= 11;
    nc_call(c, 0x80063b70);
    MEM_W32(R(28) + 0x18c, R(30));
    NC_POLL(c, NC_INSN_CYCLES);
    return true;
}

/* Following Cross dispatch, record 102 changes the mode bits and clears
 * the saved buttons. Reload the record for the comparison at 0x80068e50. */
bool nc_menu_check_record_102(CPUState *c) {
    if (c->budget <= 22)
        return false;
    R(3) = MEM_R32(R(28) + 0x190);
    R(2) = 103;
    c->budget -= 6;
    if (R(3) == 102) {
        u32 config = MEM_R32(R(28) + 0x244);
        u32 flags = MEM_R32(config);
        R(4) = ~0x800u;
        MEM_W32(R(28) + 0x18c, 0);
        MEM_W32(config, (flags | 0x2000) & R(4));
        R(3) = MEM_R32(R(28) + 0x190);
        c->budget -= 16;
    }
    return true;
}

/* Record 103 also clears the high mode bit, then exits with v0 = 2. */
u32 nc_menu_check_record_103(CPUState *c) {
    if (c->budget <= 17)
        return 0x80068e50;
    R(6) = 0x7fff0000;
    c->budget -= 2;
    if (R(3) != R(2))
        return 0x80068e84;

    R(6) = 0x7fffffff;
    R(5) = MEM_R32(R(28) + 0x244);
    R(2) = 2;
    R(3) = MEM_R32(R(5));
    R(4) = ~0x1000u;
    MEM_W32(R(28) + 0x18c, 0);
    R(3) = (R(3) | 0x2000) & R(4) & R(6);
    MEM_W32(R(5), R(3));
    c->budget -= 15;
    return 0x80068fe4;
}

/* In mode 0x8000, mark the menu status and dispatch a fresh Up before
 * considering Down. The Down test consumes its bit already masked in v0. */
u32 nc_menu_mode_up(CPUState *c) {
    if (c->budget <= 34)
        return 0x80068e84;
    u32 config = MEM_R32(R(28) + 0x244);
    R(2) = 0;
    c->budget -= 11;
    if (!(MEM_R32(config) & 0x8000))
        return 0x80068fe4;

    R(3) = MEM_R16(0x800cbff8) | 8;
    MEM_W16(0x800cbff8, R(3));
    R(2) = R(30) & 0x40;
    c->budget -= 11;
    if (!(R(30) & 0x10))
        return 0x80068ee8;

    R(4) = R(22);
    c->budget -= 7;
    if (MEM_R32(R(28) + 0x18c) & 0x10) {
        c->budget--;
        return 0x80068ee8;
    }

    R(2) = MEM_R32(R(28) + 0x190);
    R(6) = 4;
    c->budget -= 5;
    return 0x80068f0c;
}

/* v0 holds the current Down bit from the preceding Up check. */
u32 nc_menu_mode_down(CPUState *c) {
    if (c->budget <= 13)
        return 0x80068ee8;
    c->budget -= 2;
    if (!R(2)) {
        R(2) = R(30) & 0x4000;
        return 0x80068f38;
    }

    R(2) = MEM_R32(R(28) + 0x18c) & 0x40;
    R(4) = R(22);
    c->budget -= 7;
    if (R(2))
        return 0x80068f34;

    R(2) = MEM_R32(R(28) + 0x190);
    R(6) = 8;
    c->budget -= 4;
    return 0x80068f0c;
}

/* These dispatch sites call the selected record and save the full button word. */
static void dispatch_menu_record(CPUState *c, u32 return_address) {
    R(5) = 0x800883c0 + 12 * R(2);
    R(2) = 0x800883c0;
    R(31) = return_address;
    c->budget -= 7;
    nc_call(c, 0x80063b70);
    MEM_W32(R(28) + 0x18c, R(30));
    NC_POLL(c, NC_INSN_CYCLES);
}

/* The common Up, Down, Cross and Triangle path resumes at the menu scan. */
bool nc_menu_dispatch_input_action(CPUState *c) {
    if (c->budget <= 7)
        return false;
    dispatch_menu_record(c, 0x80068618);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_menu_dispatch_direction(CPUState *c) {
    if (c->budget <= 7)
        return false;
    dispatch_menu_record(c, 0x80068f28);
    R(2) = 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* A fresh Cross takes priority over Triangle. After held Down, the first
 * instruction masks Cross again; the other entry already has it in v0. */
u32 nc_menu_mode_face_buttons(CPUState *c, bool mask_cross) {
    if (c->budget <= (mask_cross ? 23 : 22))
        return mask_cross ? 0x80068f34 : 0x80068f38;
    if (mask_cross) {
        R(2) = R(30) & 0x4000;
        c->budget--;
    }

    bool cross = R(2) != 0;
    R(2) = R(30) & 0x1000;
    c->budget -= 2;
    if (cross) {
        c->budget -= 7;
        if (!(MEM_R32(R(28) + 0x18c) & 0x4000)) {
            R(4) = R(22);
            R(2) = MEM_R32(R(28) + 0x190);
            R(6) = 0x10;
            c->budget -= 6;
            return 0x80068f88;
        }
    }

    c->budget -= 2;
    if (!R(2))
        return 0x80068fa8;
    R(2) = MEM_R32(R(28) + 0x18c) & 0x1000;
    R(4) = R(22);
    c->budget -= 7;
    if (R(2))
        return 0x80068fa8;

    R(2) = MEM_R32(R(28) + 0x190);
    R(6) = 0x20;
    c->budget -= 4;
    return 0x80068f88;
}

bool nc_menu_dispatch_face_button(CPUState *c) {
    if (c->budget <= 7)
        return false;
    dispatch_menu_record(c, 0x80068fa4);
    return true;
}

/* Read the record after dispatch: the guest call may have changed it. */
bool nc_menu_mode_record_result(CPUState *c) {
    if (c->budget <= 13)
        return false;
    R(3) = MEM_R32(R(28) + 0x190);
    MEM_W32(R(28) + 0x18c, R(30));
    c->budget -= 7;
    if (R(3) == 103) {
        MEM_W32(R(28) + 0x18c, 0);
        R(2) = 2;
        c->budget -= 3;
    } else if (R(3) == 105 || R(3) == 106) {
        MEM_W32(R(28) + 0x18c, 0);
        R(2) = 4;
        c->budget -= 6;
    } else {
        R(2) = 0;
        c->budget -= 4;
    }
    return true;
}

/* Save the menu frame and capture the incoming button word in fp. */
u32 nc_enter_menu_input(CPUState *c) {
    if (c->budget <= 31)
        return 0x80067358;
    R(29) -= 0x48;
    u32 sp = R(29);
    MEM_W32(sp + 0x38, R(22));
    R(22) = R(4);
    MEM_W32(sp + 0x40, R(30));
    R(4) = MEM_R32(R(28) + 0x244);
    MEM_W32(sp + 0x44, R(31));
    MEM_W32(sp + 0x3c, R(23));
    for (unsigned i = 0; i < 6; ++i)
        MEM_W32(sp + 0x34 - 4 * i, R(21 - i));
    MEM_W32(sp + 0x18, 0);

    R(3) = 0xc0009840;
    R(2) = MEM_R32(R(4)) & R(3);
    R(30) = R(5);
    c->budget -= 24;
    if (R(2))
        return 0x800673bc;

    R(3) = MEM_R32(R(4) + 4);
    R(2) = R(3) & 0x10;
    c->budget -= 7;
    return (R(3) & 0x40) ? 0x800673bc : 0x80067400;
}

/* Dispatch action 2 for the current record, then reload the configuration
 * and clear bit 0x10 in its second mode word. */
bool nc_menu_mode_record_call(CPUState *c) {
    if (c->budget <= 12)
        return false;
    R(4) = R(22);
    u32 record = MEM_R32(R(28) + 0x190);
    R(6) = 2;
    R(2) = 0x800883c0;
    R(5) = R(2) + 12 * record;
    R(31) = 0x800673e4;
    c->budget -= 12;
    nc_call(c, 0x80063b70);

    R(4) = MEM_R32(R(28) + 0x244);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, NC_INSN_CYCLES);
    R(2) = MEM_R32(R(4) + 4);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(3) = ~0x10u;
    NC_POLL(c, NC_INSN_CYCLES);
    R(2) &= R(3);
    NC_POLL(c, NC_INSN_CYCLES);
    MEM_W32(R(4) + 4, R(2));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* With the secondary mode bit set, value 20 selects a table of record
 * pointers. Other values select the ordinary 12-byte records. */
u32 nc_menu_select_mode_record(CPUState *c) {
    if (c->budget <= 21)
        return 0x80067400;
    bool enabled = R(2) != 0;
    R(2) = 20;
    c->budget -= 2;
    if (!enabled)
        return 0x800674a0;

    R(3) = (s32)(s16)MEM_R16(0x800d7c20);
    R(4) = R(22);
    u32 index = MEM_R32(R(28) + 0x190);
    R(6) = 2;
    if (R(3) == 20) {
        R(2) = index << 2;
        R(1) = 0x800d0000 + R(2);
        R(5) = MEM_R32(R(1) + 0x4d10);
        c->budget -= 19;
    } else {
        R(2) = 0x800883c0;
        R(5) = R(2) + 12 * index;
        c->budget -= 17;
    }
    return 0x8006745c;
}

bool nc_menu_call_selected_record(CPUState *c) {
    if (c->budget <= 2)
        return false;
    R(31) = 0x80067464;
    c->budget -= 2;
    nc_call(c, 0x80063b70);
    return true;
}

/* The guest subtracts the counters modulo 2^32, then compares the signed
 * difference. Bit 0x10 stays set through elapsed tick 12000. */
bool nc_menu_record_timeout(CPUState *c) {
    if (c->budget <= 23)
        return false;
    u32 now = MEM_R32(0x8008ef10);
    R(3) = MEM_R32(0x800cc040);
    R(2) = (s32)(now - R(3)) < 12001;
    R(4) = ~0x10u;
    c->budget -= 13;
    if (!R(2)) {
        R(2) = MEM_R32(R(28) + 0x244);
        R(3) = MEM_R32(R(2) + 4) & R(4);
        MEM_W32(R(2) + 4, R(3));
        c->budget -= 10;
    }
    return true;
}

/* The high mode flag bypasses the record check. For record 29 the
 * signed counter selects the early increment until it reaches five. */
u32 nc_menu_check_record_29(CPUState *c) {
    if (c->budget <= 24)
        return 0x800674a0;
    u32 config = MEM_R32(R(28) + 0x244);
    R(4) = MEM_R32(config);
    R(2) = 29;
    c->budget -= 11;
    if (R(4) & 0x40000000)
        return 0x80067698;

    R(3) = MEM_R32(R(28) + 0x190);
    c->budget -= 6;
    if (R(3) != 29)
        return 0x8006767c;

    R(3) = MEM_R32(R(28) + 0x2b8);
    R(2) = 5;
    c->budget -= 7;
    return (s32)R(3) < 5 ? 0x800674e0 : 0x800674f0;
}

/* Branches enter at the store when their delay slot has prepared v0. */
bool nc_menu_store_counter(CPUState *c, bool increment) {
    int cycles = increment ? 4 : 3;
    if (c->budget <= cycles)
        return false;
    u32 value = increment ? R(3) + 1u : R(2);
    MEM_W32(R(28) + 0x2b8, value);
    R(2) = 0;
    c->budget -= cycles;
    return true;
}

/* v0 carries the preceding signed comparison against 30. At count 30
 * the mode flags choose the menu result instead of advancing the counter. */
u32 nc_menu_record_29_thresholds(CPUState *c) {
    if (c->budget <= 11)
        return 0x8006755c;
    u32 count = R(3);
    c->budget -= 2;
    if (R(2)) {
        R(2) = count + 1;
        return 0x800674e4;
    }

    c->budget -= 3;
    if (count == 30) {
        R(3) = R(4) & 0x07800000;
        R(2) = R(3) ? 6 : 4;
        c->budget -= R(3) ? 6 : 4;
        return 0x80068fe4;
    }

    R(4) = count;
    R(2) = 105;
    c->budget -= 4;
    if ((s32)count >= 100)
        return 0x800675a0;
    R(2) = count + 1;
    c->budget -= 2;
    return 0x800674e4;
}

/* Reload the counter after the record calls before advancing it. */
bool nc_menu_advance_record_counter(CPUState *c) {
    if (c->budget <= 8)
        return false;
    R(3) = MEM_R32(R(28) + 0x2b8) + 1u;
    MEM_W32(R(28) + 0x2b8, R(3));
    R(2) = 0;
    c->budget -= 8;
    return true;
}

u32 nc_menu_check_count_130(CPUState *c) {
    if (c->budget <= 6)
        return 0x800675e8;
    c->budget -= 3;
    if ((s32)R(4) < 130) {
        R(2) = R(4) + 1u;
        return 0x800674e4;
    }

    R(2) = 130;
    c->budget -= 3;
    return R(3) == 130 ? 0x80067600 : 0x8006767c;
}

/* The direct entry has already checked bit 30 of the primary mode. */
u32 nc_menu_record_input_gate(CPUState *c, bool check_primary) {
    if (c->budget <= (check_primary ? 28 : 17))
        return check_primary ? 0x8006767c : 0x80067698;
    if (check_primary) {
        u32 config = MEM_R32(R(28) + 0x244);
        R(3) = 0x40000000;
        R(2) = MEM_R32(config) & R(3);
        c->budget -= 11;
        if (!R(2))
            return 0x800687d8;
    }

    R(2) = 0;
    c->budget -= 6;
    if (MEM_R32(R(28) + 0x3fc))
        return 0x80068fe4;

    u32 config = MEM_R32(R(28) + 0x244);
    R(4) = MEM_R32(config + 4);
    c->budget -= 11;
    return (R(4) & 0x03000000) ? 0x80068fe4 : 0x800676c4;
}

/* Record 24 passes the full button word to its handler. Other records
 * continue with v0 already set to 29 by the branch delay slot. */
u32 nc_menu_dispatch_record_24(CPUState *c) {
    if (c->budget <= 8)
        return 0x800676c4;
    R(3) = MEM_R32(R(28) + 0x190);
    R(2) = 29;
    c->budget -= 6;
    if (R(3) != 24)
        return 0x800676e4;

    R(4) = R(30);
    R(31) = 0x800676dc;
    c->budget -= 2;
    nc_call(c, 0x80065f60);
    R(2) = 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return 0x80068fe4;
}

/* The record 24 check supplies 29 in v0. Each branch delay slot
 * prepares v0 for the next record or the count-five action. */
u32 nc_menu_dispatch_record_29(CPUState *c) {
    if (c->budget <= 8)
        return 0x800676e4;
    bool matches = R(3) == R(2);
    R(2) = 26;
    c->budget -= 2;
    if (!matches)
        return 0x80067730;

    R(3) = MEM_R32(R(28) + 0x2b8);
    R(2) = 4;
    c->budget -= 6;
    return R(3) == 5 ? 0x800676fc : 0x800674e0;
}

/* Record 26 checks Left when its signed halfword at +10 is five.
 * The Right check follows even when the Left action was dispatched. */
u32 nc_menu_record_26_left(CPUState *c) {
    if (c->budget <= 18)
        return 0x80067730;
    bool matches = R(3) == R(2);
    R(2) = 5;
    c->budget -= 2;
    if (!matches)
        return 0x800677bc;

    R(3) = (s32)(s16)MEM_R16(0x80088502);
    R(2) = R(30) & 0x80;
    c->budget -= 7;
    if (R(3) != 5)
        return 0x800677bc;

    R(2) = 0x08000000;
    c->budget -= 2;
    if (!(R(30) & 0x80))
        return 0x80067770;
    R(2) &= R(4);
    R(4) = R(22);
    c->budget -= 3;
    if (R(2)) {
        R(5) = 0x800884f8;
        R(6) = 0x10;
        R(31) = 0x80067770;
        c->budget -= 4;
        nc_call(c, 0x80063b70);
    }
    return 0x80067770;
}

/* After Left, Right reloads both the mode word and the selected record. */
bool nc_menu_record_26_right(CPUState *c) {
    if (c->budget <= 25)
        return false;
    R(2) = R(30) & 0x20;
    R(3) = 0x08000000;
    c->budget -= 3;
    if (!R(2))
        return true;

    u32 config = MEM_R32(R(28) + 0x244);
    R(2) = MEM_R32(config + 4) & R(3);
    R(4) = R(22);
    c->budget -= 11;
    if (R(2))
        return true;

    u32 record = MEM_R32(R(28) + 0x190);
    R(6) = 0x10;
    R(2) = 0x800883c0;
    R(5) = R(2) + 12 * record;
    R(31) = 0x800677bc;
    c->budget -= 11;
    nc_call(c, 0x80063b70);
    return true;
}

/* Record 37 uses the first pad packet's type to select its stick path. */
u32 nc_menu_record_37_pad_type(CPUState *c) {
    if (c->budget <= 17)
        return 0x800677bc;
    R(3) = MEM_R32(R(28) + 0x190);
    R(2) = 26; /* branch delay slot */
    c->budget -= 6;
    if (R(3) != 37)
        return 0x8006833c;

    R(31) = 0x800677d4;
    c->budget -= 2;
    nc_pad_packet_type(c);
    R(3) = 7;
    R(4) = R(22);
    c->budget -= 3;
    return R(2) == 7 ? 0x8006780c : 0x800677e0;
}

static void call_record_37_action(CPUState *c, u32 return_address) {
    u32 record = MEM_R32(R(28) + 0x190);
    R(2) = 0x800883c0;
    R(5) = R(2) + 12 * record;
    R(31) = return_address;
    c->budget -= 11;
    nc_call(c, 0x80063b70);
}

/* Record 37 dispatches action 0x20 when the packet type is not seven. */
bool nc_menu_record_37_action(CPUState *c) {
    if (c->budget <= 11)
        return false;
    R(6) = 0x20;
    call_record_37_action(c, 0x80067804);
    NC_POLL(c, 2 * NC_INSN_CYCLES); /* jump back to the selected-record check */
    return true;
}

/* Calibration already supplied the action in a2. This caller sets a0 from s6. */
bool nc_menu_calibration_action(CPUState *c) {
    if (c->budget <= 11)
        return false;
    R(4) = R(22);
    call_record_37_action(c, 0x800682fc);
    return true;
}

/* Returning from calibration saves the current buttons. The direct entry
 * already has the Triangle mask in v0 from the confirmation check. */
u32 nc_menu_calibration_triangle(CPUState *c, bool after_action) {
    if (c->budget <= (after_action ? 15 : 13))
        return after_action ? 0x800682fc : 0x80068304;
    if (after_action) {
        MEM_W32(R(28) + 0x18c, R(30));
        R(2) = R(30) & 0x1000;
        c->budget -= 2;
    }
    R(4) = R(22);
    c->budget -= 2;
    if (!R(2))
        return 0x80068334;
    R(6) = 0x20;
    call_record_37_action(c, 0x80068330);
    return 0x80068330;
}

/* The Triangle call returns at the history store. Other menu paths
 * enter at the record load or with the comparison operands already set. */
u32 nc_menu_record_pad_gate(CPUState *c, u32 entry) {
    int cycles = entry == 0x80068330 ? 7 : entry == 0x80068334 ? 6 : 2;
    if (c->budget <= cycles)
        return entry;
    if (entry == 0x80068330)
        MEM_W32(R(28) + 0x18c, R(30));
    if (entry != 0x8006833c) {
        R(3) = MEM_R32(R(28) + 0x190);
        R(2) = 26;
    }
    bool matches = R(3) == R(2);
    R(2) = R(30) & 0x10; /* Up mask in the branch delay slot */
    c->budget -= cycles;
    return matches ? 0x80068344 : 0x8006848c;
}

/* On the type-7 path, sample both stick bytes before testing the counter. */
u32 nc_menu_record_37_read_stick(CPUState *c) {
    if (c->budget <= 24)
        return 0x8006780c;
    R(4) = R(29) + 0x10;
    R(5) = R(29) + 0x11;
    R(31) = 0x80067818;
    c->budget -= 3;
    nc_read_pad_stick(c);
    R(2) = MEM_R32(0x8008f040) < 0x1f9u;
    R(17) = 0x100;
    c->budget -= 8;
    return R(2) ? 0x80067830 : 0x800680ec;
}

/* All menu input results leave through this 0x48-byte frame. */
bool nc_leave_menu_input(CPUState *c) {
    if (c->budget <= 33)
        return false;
    u32 sp = R(29);
    R(31) = MEM_R32(sp + 0x44);
    R(30) = MEM_R32(sp + 0x40);
    for (unsigned i = 0; i < 8; ++i)
        R(23 - i) = MEM_R32(sp + 0x3c - 4 * i);
    R(29) = sp + 0x48;
    c->budget -= 33;
    return true;
}

bool nc_menu_clear_repeat(CPUState *c) {
    if (c->budget <= 1)
        return false;
    MEM_W32(R(28) + 0x194, 0);
    c->budget--;
    return true;
}

/* For menu record 12, Left selects zero and Right selects one. A change
 * records the full button word before the guest call. */
u32 nc_menu_direction_selection(CPUState *c, bool right) {
    if (c->budget <= (right ? 22 : 21))
        return right ? 0x8006870c : 0x8006865c;

    R(3) = MEM_R32(R(28) + 0x190);
    R(2) = 12;
    R(4) = R(3) << 1;
    c->budget -= 6;
    if (R(3) != 12) {
        /* Ordinary records are twelve bytes each. Both paths join at
         * the same call, with the direction encoded in a1. */
        R(2) = 0x800883c0;
        R(4) = R(2) + R(3) * 12u;
        R(5) = right ? 0x40 : 0x80;
        c->budget -= right ? 6 : 7;
        return 0x800687b4;
    }

    const u32 address = 0x800d7c22;
    u32 current = (u32)(s32)(s16)MEM_R16(address);
    if (right) {
        R(8) = address;
        R(3) = current;
        R(2) = 1;
    } else {
        R(3) = address;
        R(2) = current;
    }
    R(5) = 0x2fff;
    c->budget -= 8;
    if (current == (u32)right)
        return 0x800687cc;

    R(6) = 2;
    R(4) = (u32)(s32)(s16)MEM_R16(0x800d7c6e);
    MEM_W16(address, right ? 1 : 0);
    MEM_W32(R(28) + 0x18c, R(30));
    c->budget -= right ? 8 : 7;
    return right ? 0x8006874c : 0x80068698;
}

/* Left is tested first. Once a direction is selected, a held press
 * below the repeat threshold skips both actions. */
u32 nc_menu_horizontal_repeat(CPUState *c) {
    if (c->budget <= 20)
        return 0x80068624;
    bool left = R(2) != 0;
    R(2) = R(30) & 0x20;
    c->budget -= 2;
    if (!left) {
        c->budget -= 2;
        if (!R(2))
            return 0x800687c8;
    }

    R(2) = increment_menu_repeat(c) < 6;
    c->budget -= 9;
    if (R(2)) {
        R(2) = MEM_R32(R(28) + 0x18c) & (left ? 0x80 : 0x20);
        c->budget -= 7;
        if (R(2))
            return 0x800687cc;
    }
    return left ? 0x8006865c : 0x8006870c;
}

/* Start arrives in v0 from the Up check. A fresh Start takes priority
 * over Down and leaves the repeat counter untouched. */
u32 nc_menu_start_down(CPUState *c) {
    if (c->budget <= 19)
        return 0x800684d0;
    bool start = R(2) != 0;
    R(2) = R(30) & 0x40;
    c->budget -= 2;
    if (start) {
        c->budget -= 7;
        if (!(MEM_R32(R(28) + 0x18c) & 8)) {
            R(4) = R(22);
            R(2) = MEM_R32(R(28) + 0x190);
            R(6) = 0x10;
            c->budget -= 6;
            return 0x800685fc;
        }
    }

    c->budget -= 2;
    if (!R(2))
        return 0x8006851c;
    R(2) = increment_menu_repeat(c) < 11;
    c->budget -= 8;
    return 0x800685d8;
}

u32 nc_menu_down_repeat(CPUState *c) {
    if (c->budget <= 13)
        return 0x800685d8;
    R(4) = R(22);
    c->budget -= 2;
    if (R(2)) {
        R(2) = MEM_R32(R(28) + 0x18c) & 0x40;
        c->budget -= 7;
        if (R(2))
            return 0x800687cc;
    }
    R(2) = MEM_R32(R(28) + 0x190);
    R(6) = 8;
    c->budget -= 4;
    return 0x800685fc;
}

static u32 dispatch_menu_up(CPUState *c, s32 threshold) {
    s32 count = increment_menu_repeat(c);
    R(4) = R(22);
    c->budget -= 9;
    if (count < threshold) {
        R(2) = MEM_R32(R(28) + 0x18c) & 0x10;
        c->budget -= 7;
        if (R(2))
            return 0x800687cc;
    }

    R(2) = MEM_R32(R(28) + 0x190);
    R(6) = 4;
    c->budget -= 5;
    return 0x800685fc;
}

/* v0 carries the Up mask from the preceding path. A held Up starts
 * repeating when the incremented signed counter reaches 11. */
u32 nc_menu_up_repeat(CPUState *c) {
    if (c->budget <= 23)
        return 0x8006848c;

    bool up = R(2) != 0;
    R(2) = R(30) & 8; /* Start mask for the next check */
    c->budget -= 2;
    if (!up)
        return 0x800684d0;

    return dispatch_menu_up(c, 11);
}

/* Horizontal navigation reaches the repeat check with a fresh Up mask. */
u32 nc_menu_up_from_buttons(CPUState *c) {
    if (c->budget <= 24)
        return 0x80068488;
    R(2) = R(30) & 0x10;
    --c->budget;
    return nc_menu_up_repeat(c);
}

/* After the face-button checks, the same counter uses a threshold of six.
 * Up takes priority over Down, including on a suppressed repeat. */
u32 nc_menu_vertical_repeat(CPUState *c) {
    if (c->budget <= 23)
        return 0x80068578;
    c->budget -= 2;
    if (R(2))
        return dispatch_menu_up(c, 6);

    bool down = (R(30) & 0x40) != 0;
    R(2) = R(30) & 0x80; /* Left mask in the Down branch delay slot */
    c->budget -= 2;
    if (!down)
        return 0x80068624;
    R(2) = increment_menu_repeat(c) < 6;
    c->budget -= 7;
    return 0x800685d8;
}

/* Menu state 0x1a switches flags according to the first packet's type
 * nibble. Each path writes only when its old mode bit is present. */
bool nc_menu_pad_type_flags(CPUState *c) {
    if (c->budget <= 28)
        return false;
    R(31) = 0x8006834c;
    c->budget -= 2;
    nc_pad_packet_type(c); /* six cycles are covered by the entry guard */
    bool analog = R(2) == 7;
    R(3) = 7;
    R(5) = 0x80087a40;
    R(4) = MEM_R32(R(5));
    c->budget -= 12;
    if (analog) {
        R(2) = 0xffff0000; /* delay slot */
        if (R(4) & 0x8000) {
            R(3) = 0xfffeffff;
            R(2) = (R(4) & ~0x18000u) | 6;
            MEM_W32(R(5), R(2));
            c->budget -= 8;
        }
    } else {
        R(2) = 0xfffe0000; /* delay slot */
        if (R(4) & 2) {
            R(2) = 0xfffeffff;
            R(3) = (R(4) & ~0x10006u) | 0x8000;
            R(4) = 0xfffffffb;
            MEM_W32(R(5), R(3));
            c->budget -= 8;
        }
    }
    return true;
}

/* Each axis has independent byte-sized bounds, including before the
 * first sample when the stored minimum can exceed the maximum. */
static void extend_stick_bounds(CPUState *c, u8 sample, u32 minimum, u32 maximum) {
    if (sample < MEM_R8(minimum)) {
        MEM_W8(minimum, sample);
        --c->budget;
    }
    if (sample > MEM_R8(maximum)) {
        MEM_W8(maximum, sample);
        --c->budget;
    }
}

bool nc_menu_sample_stick_bounds(CPUState *c) {
    if (c->budget <= 38)
        return false;
    u32 gp = R(28);
    extend_stick_bounds(c, MEM_R8(R(29) + 0x10), gp + 0x2f0, gp + 0x2fc);
    R(3) = MEM_R8(R(29) + 0x11);
    extend_stick_bounds(c, R(3), gp + 0x2f4, gp + 0x300);
    R(4) = R(3);
    R(2) = R(30) & 8; /* Start mask for the following edge check */
    c->budget -= 34;
    return true;
}

/* Confirmation expands the sampled range to include [0x40, 0xc0] on both axes. */
bool nc_menu_expand_stick_bounds(CPUState *c) {
    if (c->budget <= 32)
        return false;
    u32 gp = R(28);
    for (unsigned axis = 0; axis < 2; ++axis) {
        u32 minimum = gp + 0x2f0 + 4 * axis;
        if (MEM_R8(minimum) > 0x40) {
            MEM_W8(minimum, 0x40);
            --c->budget;
        }
    }
    for (unsigned axis = 0; axis < 2; ++axis) {
        u32 maximum = gp + 0x2fc + 4 * axis;
        if (MEM_R8(maximum) < 0xc0) {
            MEM_W8(maximum, 0xc0);
            --c->budget;
        }
    }
    R(2) = 0xc0;
    c->budget -= 28;
    return true;
}

/* Store four midpoints rounded toward the fresh stick sample, followed by
 * the four sampled limits moved one unit inward. Reload the destination
 * pointer for each byte, as the original does. */
bool nc_menu_store_stick_calibration(CPUState *c) {
    if (c->budget <= 112)
        return false;
    u32 gp = R(28);
    u32 sample = R(29) + 0x12;
    R(4) = sample;
    R(5) = sample + 1;
    R(31) = 0x800681f8;
    c->budget -= 3;
    nc_read_pad_stick(c);

    const u32 offsets[] = {0x2f0, 0x2f4, 0x2fc, 0x300};
    for (unsigned i = 0; i < 4; ++i) {
        int center = MEM_R8(sample + (i & 1));
        int limit = MEM_R8(gp + offsets[i]);
        u32 midpoint = center + (limit - center) / 2;
        R(3) = MEM_R32(gp + 0x244);
        MEM_W8(R(3) + 0x1a + i, midpoint);
        if (i == 2)
            R(4) = midpoint;
        if (i == 3)
            R(5) = midpoint;
    }
    R(6) = 0x20;
    for (unsigned i = 0; i < 4; ++i) {
        int limit = MEM_R8(gp + offsets[i]);
        R(2) = limit + (i < 2 ? 1 : -1);
        R(3) = MEM_R32(gp + 0x244);
        MEM_W8(R(3) + 0x1e + i, R(2));
    }
    c->budget -= 96;
    return true;
}

/* The preceding delay slot tested Start in the current word (fp).
 * A new Start takes priority over Cross. Each exit leaves the next
 * button mask in v0, including when both buttons are pressed. */
u32 nc_menu_confirm_edge(CPUState *c) {
    if (c->budget <= 18)
        return 0x80068154;

    bool start = R(2) != 0;
    R(2) = R(30) & 0x4000;
    c->budget -= 2;
    if (start) {
        c->budget -= 7;
        if (!(MEM_R32(R(28) + 0x18c) & 8))
            return 0x8006818c;
    }

    bool cross = R(2) != 0;
    R(2) = R(30) & 0x1000;
    c->budget -= 2;
    if (cross) {
        c->budget -= 7;
        if (!(MEM_R32(R(28) + 0x18c) & 0x4000))
            return 0x8006818c;
    }
    return 0x80068304;
}

/* The decoder returns both ports in v0. Any set bit resets the countdown
 * before the state check, including on a frame that also decrements it. */
bool nc_pad_inactivity(CPUState *c) {
    if (c->budget <= 29)
        return false;

    int cycles = 4;
    if (R(2)) {
        R(19) = 1250;
        ++cycles;
    }
    R(2) = 11;
    if (R(19)) {
        R(3) = MEM_R32(0x8008ef30);
        cycles += 9;
        if (R(3) == 11) {
            --R(19);
            ++cycles;
        }
    }
    if (!R(19)) {
        R(4) = MEM_R32(R(28) + 0x244);
        R(3) = MEM_R32(R(4) + 4);
        R(13) = 0x02000000;
        R(2) = R(3) | R(13); /* branch delay slot */
        cycles += 11;
        if (!(R(3) & R(13))) {
            MEM_W32(R(4) + 4, R(2));
            R(2) = 30;
            R(1) = 0x800d0000;
            MEM_W16(0x800d7c30, R(2));
            cycles += 4;
        }
    }
    c->budget -= cycles;
    return true;
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
