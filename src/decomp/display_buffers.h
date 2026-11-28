/* Double-buffered display setup shared by the front end (PSX.EXE 80019A18-80019D28) and the
 * game (PSX2.EXE 80061FF0-80062300). Each buffer starts with a DRAWENV, its DISPENV at +0x5C
 * and its ordering table at +0x70; the second buffer follows the first at +0x14130. */
#ifndef NC_DECOMP_DISPLAY_BUFFERS_H
#define NC_DECOMP_DISPLAY_BUFFERS_H

#include "decomp/guest_ops.h"

enum {
    DB_DISPENV = 0x5c,
    DB_OT = 0x70,
    DB_OT_LENGTH = 0x8eb,
    DB_SECOND = 0x14130,
};

typedef struct {
    u32 buffer;        /* first buffer; the second is at buffer + DB_SECOND */
    u32 settings_slot; /* gp offset of the pointer to the shared settings record */
    /* Routine entries, also the base of their return addresses. */
    u32 init_buffers, setup, apply_offset, clear;
    /* Callees. */
    u32 set_geom_offset, set_geom_screen, set_def_draw_env, set_def_disp_env;
    u32 draw_sync, clear_otag_r, clear_image;
} DisplayLayout;

/* init_buffers(width a0, height a1, projection distance a2): center the GTE offset, then
 * draw into one buffer while the other is displayed. Heights other than 480 use two
 * 256-line buffers. */
static inline bool display_init_buffers(CPUState *c, const DisplayLayout *l) {
    const u32 entry = l->init_buffers;
    const u32 first = l->buffer, second = l->buffer + DB_SECOND;
    if (c->budget <= 19)
        return false;
    R(29) -= 0x30;
    MEM_W32(R(29) + 0x24, R(19));
    R(19) = R(4);
    MEM_W32(R(29) + 0x18, R(16));
    R(16) = R(5);
    MEM_W32(R(29) + 0x20, R(18));
    R(18) = R(6);
    R(4) = (u32)((s32)(R(19) + (R(19) >> 31)) >> 1);
    R(5) = (u32)((s32)(R(16) + (R(16) >> 31)) >> 1);
    MEM_W32(R(29) + 0x1c, R(17));
    R(17) = (R(16) ^ 0x1e0) != 0;
    MEM_W32(R(29) + 0x28, R(31));
    R(17) <<= 8;
    R(31) = entry + 0x4c;
    c->budget -= 19;
    nc_call(c, l->set_geom_offset);
    R(4) = R(18);
    guest_call(c, l->set_geom_screen, entry + 0x54);
    set_address(c, 18, first);

    static const struct {
        bool disp, second, offset;
    } envs[4] = {
        {false, false, false}, {true, false, true}, {false, true, true}, {true, true, false}};
    for (unsigned i = 0; i < 4; i++) {
        const u32 env = (envs[i].second ? second : first) + (envs[i].disp ? DB_DISPENV : 0);
        if (envs[i].second)
            set_address(c, 4, env);
        else
            set_reg(c, 4, env);
        set_reg(c, 5, 0);
        set_reg(c, 6, envs[i].offset ? R(17) : 0);
        set_reg(c, 7, R(19));
        MEM_W32(R(29) + 0x10, R(16));
        guest_call(c, envs[i].disp ? l->set_def_disp_env : l->set_def_draw_env,
                   entry + 0x74 + 0x18 * i + (i > 1 ? 4 * (i - 1) : 0));
    }

    set_reg(c, 2, 1);
    store_byte_at(c, second + 0x17, 0); /* dfe */
    store_byte_at(c, first + 0x17, 0);
    store_byte_at(c, second + 0x18, R(2)); /* isbg */
    store_byte_at(c, first + 0x18, R(2));
    store_byte_at(c, second + 0x16, 0); /* dtd */
    store_byte_at(c, first + 0x16, 0);
    store_half_at(c, first + DB_DISPENV + 0xe, R(16)); /* screen height */
    store_half_at(c, second + DB_DISPENV + 0xe, R(16));
    guest_leave(c, 0x30, 0x28, 16, 19, 0x18);
    return true;
}

/* setup(width, height, unused, projection, r, g, b, buffers): an interlaced display doubles
 * the width passed to init_buffers. Then store the screen height and clear color in both
 * buffers and apply the player's screen offset. */
static inline bool display_setup(CPUState *c, const DisplayLayout *l) {
    const u32 entry = l->setup;
    if (c->budget <= 33) /* the prologue, with the shift for an interlaced mode */
        return false;
    R(2) = MEM_R32(R(28) + l->settings_slot);
    R(29) -= 0x28;
    MEM_W32(R(29) + 0x18, R(18));
    R(18) = MEM_R32(R(29) + 0x38);
    MEM_W32(R(29) + 0x1c, R(19));
    R(19) = MEM_R32(R(29) + 0x3c);
    MEM_W32(R(29) + 0x20, R(20));
    R(20) = MEM_R32(R(29) + 0x40);
    MEM_W32(R(29) + 0x14, R(17));
    R(17) = R(5);
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = MEM_R32(R(29) + 0x44);
    MEM_W32(R(29) + 0x24, R(31));
    R(3) = MEM_R32(R(2));
    R(2) = 0x20000000;
    R(3) &= R(2);
    R(6) = R(7);
    const bool interlaced = R(3) != 0;
    if (interlaced)
        R(4) <<= 1;
    R(31) = entry + 0x54;
    R(5) = R(17);
    c->budget -= 32 + interlaced;
    nc_call(c, l->init_buffers);

    set_reg(c, 2, R(17));
    store_half(c, R(16) + DB_DISPENV + 0xe, R(2));
    set_reg(c, 1, 0x10000);
    set_reg(c, 1, R(16) + R(1));
    store_half(c, R(1) + DB_DISPENV + 0xe + DB_SECOND - 0x10000, R(2));
    store_byte(c, R(16) + 0x19, R(18)); /* r0, g0, b0 */
    store_byte(c, R(16) + 0x1a, R(19));
    store_byte(c, R(16) + 0x1b, R(20));
    for (unsigned i = 0; i < 3; i++) {
        set_reg(c, 1, 0x10000);
        set_reg(c, 1, R(16) + R(1));
        store_byte(c, R(1) + DB_SECOND + 0x19 + i - 0x10000, R(18 + i));
    }
    guest_call(c, l->apply_offset, entry + 0xa0);
    guest_leave(c, 0x28, 0x24, 16, 20, 0x10);
    return true;
}

/* Move both display areas by the player's screen offset (settings bytes 0x18 and 0x19, centered
 * at 10 and 5). */
static inline bool display_apply_offset(CPUState *c, const DisplayLayout *l) {
    const u32 first = l->buffer + DB_DISPENV + 8, second = first + DB_SECOND;
    if (c->budget <= 2)
        return false;
    load_word(c, 2, R(28) + l->settings_slot);
    guest_nop(c);
    load_byte(c, 3, R(2) + 0x18);
    guest_nop(c);
    set_reg(c, 3, R(3) - 10);
    store_half_at(c, first, R(3)); /* screen.x */
    store_half_at(c, second, R(3));
    load_byte(c, 2, R(2) + 0x19);
    guest_nop(c);
    set_reg(c, 2, R(2) - 5);
    store_half_at(c, first + 2, R(2)); /* screen.y */
    store_half_at(c, second + 2, R(2));
    guest_return(c);
    return true;
}

/* Clear both ordering tables and all of the 1024 x 512 frame buffer to black. */
static inline bool display_clear(CPUState *c, const DisplayLayout *l) {
    const u32 entry = l->clear;
    if (c->budget <= 4)
        return false;
    R(29) -= 0x38;
    MEM_W32(R(29) + 0x30, R(31));
    R(31) = entry + 0x10;
    R(4) = 0;
    c->budget -= 4;
    nc_call(c, l->draw_sync);
    for (unsigned i = 0; i < 2; i++) {
        set_address(c, 4, l->buffer + DB_SECOND * i + DB_OT);
        R(5) = DB_OT_LENGTH;
        guest_call(c, l->clear_otag_r, entry + 0x20 + 0x10 * i);
    }

    /* Four 512 x 256 quadrants. */
    set_reg(c, 4, R(29) + 0x10);
    for (unsigned reg = 5; reg <= 7; reg++)
        set_reg(c, reg, 0);
    set_reg(c, 3, 0x200);
    set_reg(c, 2, 0x100);
    for (unsigned i = 0; i < 16; i++) {
        const unsigned quadrant = i / 4, field = i % 4;
        u32 value;
        if (field == 0)
            value = quadrant & 1 ? R(3) : 0;
        else if (field == 1)
            value = quadrant & 2 ? R(2) : 0;
        else
            value = field == 2 ? R(3) : R(2);
        MEM_W16(R(29) + 0x10 + 2 * i, (u16)value);
        if (i < 15)
            guest_nop(c);
    }
    guest_call(c, l->clear_image, entry + 0x8c);
    for (unsigned i = 1; i < 4; i++) {
        set_reg(c, 4, R(29) + 0x10 + 8 * i);
        set_reg(c, 5, 0);
        set_reg(c, 6, 0);
        R(7) = 0;
        guest_call(c, l->clear_image, entry + 0x8c + 0x14 * i);
    }
    R(4) = 0;
    guest_call(c, l->draw_sync, entry + 0xd0);
    guest_leave_ra(c, 0x38, 0x30);
    return true;
}

#endif
