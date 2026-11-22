#include "decomp/credits.h"

/* The TIM occupies three 320 x 512 strips laid out side by side in VRAM.
 * Scroll each strip up, carrying the next strip's first row into its last
 * row. Clear the last row of the final strip. The guest builds each RECT on
 * its stack and passes s0 as the RECT pointer. */
static void scroll_row(CPUState *c) {
    const unsigned strip_x[] = {0, 18, 21}; /* source X in zero, s2, s5 */
    for (unsigned pass = 0; pass < 6; pass++) {
        const unsigned strip = pass / 2;
        const bool bottom_row = (pass & 1) != 0;
        const bool clear = pass == 5;
        const unsigned source = strip + (bottom_row && !clear);
        R(4) = R(16);
        NC_POLL(c, NC_INSN_CYCLES);
        R(5) = clear ? 0 : strip * 320;
        NC_POLL(c, NC_INSN_CYCLES);
        R(6) = bottom_row && !clear ? 511 : 0;
        NC_POLL(c, NC_INSN_CYCLES);
        if (clear) {
            R(7) = 0; /* ClearImage black (r, g, b = 0) */
            NC_POLL(c, NC_INSN_CYCLES);
        }

        MEM_W16(R(29) + 0x90, source ? (u16)R(strip_x[source]) : 0);
        NC_POLL(c, NC_INSN_CYCLES);
        MEM_W16(R(29) + 0x92, clear ? (u16)R(20) : bottom_row ? 0 : (u16)R(19));
        NC_POLL(c, NC_INSN_CYCLES);
        MEM_W16(R(29) + 0x94, (u16)R(18));
        NC_POLL(c, NC_INSN_CYCLES);
        /* Five equally spaced MoveImage call sites, then ClearImage with one
         * additional argument instruction. Height is stored in the jal slot. */
        R(31) = clear ? 0x8001918c : 0x800190e8 + pass * 0x20;
        MEM_W16(R(29) + 0x96, bottom_row ? (u16)R(19) : (u16)R(20));
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, clear ? 0x8001c474 : 0x8001c66c);
    }
}

enum {
    V_SYNC = 0x8001a454,
    RESET_CALLBACK = 0x8001a698,
    SET_DISP_MASK = 0x8001c244,
    CD_VOLUME = 0x800192e4,
};

/* The jal and its delay slot form a single scheduler boundary. Any delay
 * slot argument is set by the caller immediately beforehand. */
static void call(CPUState *c, u32 target, u32 resume) {
    R(31) = resume;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, target);
}

static void set_reg(CPUState *c, unsigned reg, u32 value) {
    R(reg) = value;
    NC_POLL(c, NC_INSN_CYCLES);
}

static void load_word(CPUState *c, unsigned reg, u32 address) {
    R(reg) = MEM_R32(address);
    NC_POLL(c, NC_LOAD_CYCLES);
}

static void init_display(CPUState *c) {
    /* Draw into all of VRAM, then display the leftmost 320 x 240 window. */
    set_reg(c, 4, R(29) + 0x18);
    set_reg(c, 5, 0);
    set_reg(c, 6, 0);
    set_reg(c, 7, 1024);
    set_reg(c, 2, 512);
    R(31) = 0x80019020;
    MEM_W32(R(29) + 0x10, R(2));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001b414); /* SetDefDrawEnv */
    R(4) = R(29) + 0x18;
    call(c, 0x8001c954, 0x80019028); /* PutDrawEnv */

    set_reg(c, 16, R(29) + 0x78);
    set_reg(c, 4, R(16));
    set_reg(c, 5, 0);
    set_reg(c, 6, 0);
    set_reg(c, 7, 320);
    set_reg(c, 2, 240);
    R(31) = 0x80019048;
    MEM_W32(R(29) + 0x10, R(2));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001b4e4); /* SetDefDispEnv */

    /* Apply the player's horizontal and vertical screen offsets. */
    load_word(c, 3, R(28) + 8);
    NC_POLL(c, NC_INSN_CYCLES);
    R(2) = MEM_R8(R(3) + 0x18);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, R(2) - 10);
    MEM_W16(R(29) + 0x80, (u16)R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    R(2) = MEM_R8(R(3) + 0x19);
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 4, R(16));
    set_reg(c, 2, R(2) - 5);
    R(31) = 0x80019074;
    MEM_W16(R(29) + 0x82, (u16)R(2));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001cb2c); /* PutDispEnv */
}

static void show_credits(CPUState *c) {
    bool again;
    /* Hold the first screen for 120 VBlanks. */
    do {
        R(4) = 0;
        call(c, V_SYNC, 0x800190a8);
        set_reg(c, 17, R(17) - 1);
        again = (s32)R(17) > 0;
        R(19) = 1;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } while (again);

    set_reg(c, 17, 1280);
    set_reg(c, 18, 320);
    set_reg(c, 20, 511);
    set_reg(c, 16, R(29) + 0x90);
    set_reg(c, 21, 640);
    do {
        scroll_row(c);
        R(4) = 0;
        call(c, V_SYNC, 0x80019194);
        R(17)--;
        call(c, 0x80018c8c, 0x8001919c); /* skip input */
        set_reg(c, 2, R(2) & 0xff);
        bool skip = R(2) != 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (skip)
            break;
        again = R(17) != 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } while (again);

    /* Fade the CD audio by one step per VBlank. */
    set_reg(c, 17, 128);
    set_reg(c, 4, R(17));
    do {
        R(5) = R(17);
        call(c, CD_VOLUME, 0x800191c0);
        R(4) = 0;
        call(c, V_SYNC, 0x800191c8);
        set_reg(c, 17, R(17) - 1);
        again = (s32)R(17) > 0;
        R(4) = R(17);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } while (again);
}

bool nc_credits_run(CPUState *c) {
    /* Batch the initial stack saves only if no event can interrupt them. */
    if (c->budget <= 9)
        return false;
    R(29) -= 0xb8;
    MEM_W32(R(29) + 0xb0, R(31));
    for (int reg = 21; reg >= 17; reg--)
        MEM_W32(R(29) + 0x98 + 4 * (reg - 16), R(reg));
    R(31) = 0x80018fb8;
    MEM_W32(R(29) + 0x98, R(16));
    c->budget -= 9;
    nc_call(c, 0x8002335c);

    R(17) = 120;
    call(c, RESET_CALLBACK, 0x80018fc0);
    call(c, 0x80018cfc, 0x80018fc8); /* init pads */
    R(4) = 3;
    call(c, 0x8001be40, 0x80018fd0); /* ResetGraph */
    R(4) = 0;
    call(c, SET_DISP_MASK, 0x80018fd8);
    call(c, 0x8001fce8, 0x80018fe0); /* CdInit */

    /* Heap extends from the end of the credits BSS to the reserved stack. */
    set_reg(c, 5, 0x801f0000);
    set_reg(c, 4, 0x80020000);
    load_word(c, 4, R(4) + 0x4c60);
    set_reg(c, 2, 0x80020000);
    load_word(c, 2, R(2) + 0x4c64);
    set_reg(c, 5, R(5) | 0xef00);
    set_reg(c, 4, R(4) + R(2));
    R(5) -= R(4);
    call(c, 0x80019454, 0x80019004);

    init_display(c);
    set_reg(c, 4, 0x80020000);
    set_reg(c, 4, R(4) - 0x8000); /* CREDITS.TIM filename */
    call(c, 0x80018ee4, 0x80019084);
    R(4) = 1;
    call(c, SET_DISP_MASK, 0x8001908c);
    set_reg(c, 4, 128);
    R(5) = 128;
    call(c, CD_VOLUME, 0x80019098);
    R(4) = 7;
    call(c, 0x80019364, 0x800190a0); /* start credits music */
    show_credits(c);

    call(c, 0x80018d4c, 0x800191dc); /* StopPAD wrapper */
    call(c, RESET_CALLBACK, 0x800191e4);
    call(c, 0x8001a78c, 0x800191ec); /* StopCallback */
    load_word(c, 3, R(28) + 8);
    set_reg(c, 2, 2);
    MEM_W32(R(3) + 0x24, R(2)); /* return to launcher */
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 31, R(29) + 0xb0);
    for (int reg = 21; reg >= 16; reg--)
        load_word(c, reg, R(29) + 0x98 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + 0xb8);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}
