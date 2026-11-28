#ifndef NC_DECOMP_SDK_CARD_H
#define NC_DECOMP_SDK_CARD_H

#include "decomp/interrupt_control.h"

/* Addresses of the Psy-Q memory card library (LIBCARD) in one executable. */
typedef struct {
    u32 enter_critical, exit_critical, clear_pad, flush_cache;
    u32 bios_init, bios_start, bios_stop, new_card, card_write;
    u32 patch_card, patch_card2, exit_card;
    u32 saved_ra;                 /* _patch_card and _patch_card2 keep ra here */
    u32 exit_saved_ra;            /* _ExitCard keeps ra here */
    u32 card_code, card_code_end; /* copied into the BIOS by _patch_card */
    u32 patch_slot;               /* end of that copy */
    u32 swap_code, swap_code_end; /* exchanged with the BIOS by _patch_card2 */
    u32 exit_code, exit_code_end; /* copied into the BIOS by _ExitCard */
} CardLayout;

static inline u32 card_hi(u32 address) {
    return (address + 0x8000u) & 0xffff0000u;
}

/* jal: ra is the instruction after the delay slot, which runs before the poll. */
static inline void card_call(CPUState *c, u32 target, u32 ra) {
    R(31) = ra;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, target);
}

static inline void card_return(CPUState *c, unsigned frame, bool restore_s0) {
    load_word(c, 31, R(29) + (restore_s0 ? 0x14 : 0x10));
    if (restore_s0)
        load_word(c, 16, R(29) + 0x10);
    set_reg(c, 29, R(29) + frame);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
}

/* InitCARD(pad_enable): ChangeClearPad(0), then the BIOS InitCARD and both
 * BIOS patches inside a critical section. */
static inline bool sdk_init_card(CPUState *c, u32 entry, const CardLayout *l) {
    if (c->budget <= 4)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(4);
    MEM_W32(R(29) + 0x14, R(31));
    c->budget -= 4;
    R(4) = 0;
    card_call(c, l->clear_pad, entry + 0x18);
    card_call(c, l->enter_critical, entry + 0x20);
    R(4) = R(16);
    card_call(c, l->bios_init, entry + 0x28);
    card_call(c, l->patch_card, entry + 0x30);
    card_call(c, l->patch_card2, entry + 0x38);
    card_call(c, l->exit_critical, entry + 0x40);
    card_return(c, 0x18, true);
    return true;
}

/* StartCARD: the BIOS StartCARD and ChangeClearPad(0) in a critical section. */
static inline bool sdk_start_card(CPUState *c, u32 entry, const CardLayout *l) {
    if (c->budget <= 2)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    c->budget -= 2;
    card_call(c, l->enter_critical, entry + 0x10);
    card_call(c, l->bios_start, entry + 0x18);
    R(4) = 0;
    card_call(c, l->clear_pad, entry + 0x20);
    card_call(c, l->exit_critical, entry + 0x28);
    card_return(c, 0x18, false);
    return true;
}

/* StopCARD: the BIOS StopCARD, then undo both BIOS patches. */
static inline bool sdk_stop_card(CPUState *c, u32 entry, const CardLayout *l) {
    if (c->budget <= 2)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    c->budget -= 2;
    card_call(c, l->bios_stop, entry + 0x10);
    card_call(c, l->patch_card2, entry + 0x18);
    card_call(c, l->exit_card, entry + 0x20);
    card_return(c, 0x18, false);
    return true;
}

/* _card_clear(port): _new_card(), then write sector 0x3f from address zero. */
static inline bool sdk_card_clear(CPUState *c, u32 entry, const CardLayout *l) {
    if (c->budget <= 3)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(16));
    MEM_W32(R(29) + 0x14, R(31));
    c->budget -= 3;
    R(16) = R(4);
    card_call(c, l->new_card, entry + 0x14);
    set_reg(c, 4, R(16));
    set_reg(c, 5, 0x3f);
    R(6) = 0;
    card_call(c, l->card_write, entry + 0x24);
    card_return(c, 0x18, true);
    return true;
}

/* BIOS table lookup by jalr t2 with the service number in the delay slot. */
static inline void card_bios_table(CPUState *c, u32 service, u32 ra) {
    set_reg(c, 10, 0xb0);
    R(31) = ra;
    R(9) = service;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, R(10));
}

static inline void card_leave_patch(CPUState *c, u32 saved_ra) {
    set_reg(c, 31, card_hi(saved_ra));
    load_word(c, 31, saved_ra);
    NC_POLL(c, NC_INSN_CYCLES);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
}

/* _patch_card: find the BIOS card handler from the lui/ori pair at +0x70 of
 * the C0 table's exception entry and copy the library's handler over it. */
static inline bool sdk_patch_card(CPUState *c, const CardLayout *l) {
    const u32 entry = l->patch_card;
    if (c->budget <= 2)
        return false;
    R(1) = card_hi(l->saved_ra);
    MEM_W32(l->saved_ra, R(31));
    c->budget -= 2;
    card_call(c, l->enter_critical, entry + 0x10);
    card_bios_table(c, 0x56, entry + 0x1c); /* GetC0Table */
    load_word(c, 2, R(2) + 0x18);
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 3, R(2) + 0x70);
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 9, R(3) & 0xffff);
    load_word(c, 3, R(2) + 0x74);
    set_reg(c, 9, R(9) << 16);
    set_reg(c, 10, R(3) & 0xffff);
    set_reg(c, 3, R(9) + R(10));
    set_reg(c, 10, card_hi(l->card_code));
    set_reg(c, 9, card_hi(l->card_code_end));
    set_reg(c, 2, R(3) + 0x28);
    set_reg(c, 10, l->card_code);
    set_reg(c, 9, l->card_code_end);
    bool more;
    do {
        load_word(c, 3, R(10));
        set_reg(c, 10, R(10) + 4);
        set_reg(c, 2, R(2) + 4);
        more = R(10) != R(9);
        MEM_W32(R(2) - 4, R(3));
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } while (more);
    set_reg(c, 1, card_hi(l->patch_slot));
    MEM_W32(l->patch_slot, R(2));
    card_call(c, l->flush_cache, entry + 0x74);
    card_leave_patch(c, l->saved_ra);
    return true;
}

/* _patch_card2: exchange five words of the B0 table's card routine with the
 * library's code. */
static inline bool sdk_patch_card2(CPUState *c, const CardLayout *l) {
    const u32 entry = l->patch_card2;
    if (c->budget <= 2)
        return false;
    R(1) = card_hi(l->saved_ra);
    MEM_W32(l->saved_ra, R(31));
    c->budget -= 2;
    card_call(c, l->enter_critical, entry + 0x10);
    card_bios_table(c, 0x57, entry + 0x1c); /* GetB0Table */
    load_word(c, 2, R(2) + 0x16c);
    set_reg(c, 10, card_hi(l->swap_code));
    set_reg(c, 9, card_hi(l->swap_code_end));
    load_word(c, 3, R(2) + 0x9c8);
    set_reg(c, 10, l->swap_code);
    set_reg(c, 9, l->swap_code_end);
    bool more;
    do {
        load_word(c, 3, R(2) + 0x9c8);
        load_word(c, 8, R(10));
        set_reg(c, 10, R(10) + 4);
        MEM_W32(R(10) - 4, R(3));
        NC_POLL(c, NC_INSN_CYCLES);
        set_reg(c, 2, R(2) + 4);
        more = R(10) != R(9);
        MEM_W32(R(2) + 0x9c4, R(8));
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } while (more);
    card_call(c, l->flush_cache, entry + 0x58);
    card_leave_patch(c, l->saved_ra);
    return true;
}

/* _ExitCard: restore the BIOS exception entry the card patch replaced. */
static inline bool sdk_exit_card(CPUState *c, const CardLayout *l) {
    const u32 entry = l->exit_card;
    if (c->budget <= 2)
        return false;
    R(1) = card_hi(l->exit_saved_ra);
    MEM_W32(l->exit_saved_ra, R(31));
    c->budget -= 2;
    card_call(c, l->enter_critical, entry + 0x10);
    card_bios_table(c, 0x56, entry + 0x1c); /* GetC0Table */
    set_reg(c, 10, card_hi(l->exit_code));
    set_reg(c, 9, card_hi(l->exit_code_end));
    load_word(c, 2, R(2) + 0x18);
    set_reg(c, 10, l->exit_code);
    set_reg(c, 9, l->exit_code_end);
    bool more;
    do {
        load_word(c, 3, R(10));
        set_reg(c, 10, R(10) + 4);
        set_reg(c, 2, R(2) + 4);
        more = R(10) != R(9);
        MEM_W32(R(2) + 0x6c, R(3));
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } while (more);
    card_call(c, l->flush_cache, entry + 0x4c);
    card_call(c, l->exit_critical, entry + 0x54);
    card_leave_patch(c, l->exit_saved_ra);
    return true;
}

#endif
