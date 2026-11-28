/* Memory card helpers shared by the front end (PSX.EXE 80018DE4-80019438) and the game
 * (PSX2.EXE 800705EC-80070C74): the eight BIOS card events, the card probe and the
 * directory checks. Both copies are the same code with other addresses and gp offsets. */
#ifndef NC_DECOMP_CARD_EVENTS_H
#define NC_DECOMP_CARD_EVENTS_H

#include "decomp/guest_ops.h"

enum {
    CARD_SW_EVENTS = 0, /* SwCARD (0xF4000001): IOE, ERROR, TIMEOUT, NEWCARD */
    CARD_HW_EVENTS = 4, /* HwCARD (0xF0000011): the same four */
    CARD_EVENT_COUNT = 8,
};

typedef struct {
    u32 events[CARD_EVENT_COUNT]; /* gp offsets of the event handles */
    /* Routine entries, also the base of their return addresses. */
    u32 open_events, close_events, test_all, test_sw, test_hw, wait_sw, wait_hw;
    u32 probe, formatted, file_exists;
    /* Callees. */
    u32 enter_critical, exit_critical;
    u32 open_event, close_event, test_event, enable_event, disable_event;
    u32 init_card, start_card, stop_card, bu_init;
    u32 card_info, card_clear, card_load, card_read, new_card;
    u32 memset, sprintf, open, close;
    u32 path_format; /* "bu%02x:%s" style format: port, then file name */
} CardEventLayout;

/* addiu sp, -0x18; sw ra; jal: the four cycles batched before the first call. */
static inline bool card_enter_frame(CPUState *c, u32 target, u32 resume) {
    if (c->budget <= 4)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    R(31) = resume;
    c->budget -= 4;
    nc_call(c, target);
    return true;
}

/* Open all eight card events with no handler, start the card driver and enable them. */
static inline bool card_open_events(CPUState *c, const CardEventLayout *l) {
    static const u32 specs[4] = {0x0004, 0x8000, 0x0100, 0x2000};
    const u32 entry = l->open_events;
    if (!card_enter_frame(c, l->enter_critical, entry + 0x10))
        return false;
    for (unsigned i = 0; i < CARD_EVENT_COUNT; i++) {
        const u32 class = i < CARD_HW_EVENTS ? 0xf4000001u : 0xf0000011u;
        set_reg(c, 4, class & 0xffff0000u);
        set_reg(c, 4, class);
        set_reg(c, 5, specs[i & 3]);
        set_reg(c, 6, 0x2000); /* EvMdNOINTR */
        if (i)
            store_word(c, R(28) + l->events[i - 1], R(2));
        R(7) = 0;
        guest_call(c, l->open_event, entry + 0x28 + 0x1c * i);
    }
    store_word(c, R(28) + l->events[7], R(2));
    R(4) = 0;
    guest_call(c, l->init_card, entry + 0xf8);
    guest_call(c, l->start_card, entry + 0x100);
    guest_call(c, l->bu_init, entry + 0x108);
    for (unsigned i = 0; i < CARD_EVENT_COUNT; i++) {
        load_word(c, 4, R(28) + l->events[i]);
        guest_call(c, l->enable_event, entry + 0x114 + 0xc * i);
    }
    guest_call(c, l->exit_critical, entry + 0x170);
    guest_leave_ra(c, 0x18, 0x10);
    return true;
}

/* Disable the events, stop the driver, consume any pending event and close them. */
static inline bool card_close_events(CPUState *c, const CardEventLayout *l) {
    const u32 entry = l->close_events;
    if (!card_enter_frame(c, l->enter_critical, entry + 0x10))
        return false;
    for (unsigned i = 0; i < CARD_EVENT_COUNT; i++) {
        load_word(c, 4, R(28) + l->events[i]);
        guest_call(c, l->disable_event, entry + 0x1c + 0xc * i);
    }
    guest_call(c, l->stop_card, entry + 0x78);
    guest_call(c, l->test_all, entry + 0x80);
    for (unsigned i = 0; i < CARD_EVENT_COUNT; i++) {
        load_word(c, 4, R(28) + l->events[i]);
        guest_call(c, l->close_event, entry + 0x8c + 0xc * i);
    }
    guest_call(c, l->exit_critical, entry + 0xe8);
    guest_leave_ra(c, 0x18, 0x10);
    return true;
}

/* TestEvent on `count` events from `first`, discarding the results: clears pending events.
 * The first handle is loaded before the frame is built. */
static inline bool card_test_events(CPUState *c, const CardEventLayout *l, u32 entry,
                                    unsigned first, unsigned count) {
    if (c->budget <= 7)
        return false;
    R(4) = MEM_R32(R(28) + l->events[first]);
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    R(31) = entry + 0x14;
    c->budget -= 7;
    nc_call(c, l->test_event);
    for (unsigned i = 1; i < count; i++) {
        load_word(c, 4, R(28) + l->events[first + i]);
        guest_call(c, l->test_event, entry + 0x14 + 0xc * i);
    }
    guest_leave_ra(c, 0x18, 0x10);
    return true;
}

/* Poll four events in order until one has fired and return its index (0 IOE, 1 ERROR,
 * 2 TIMEOUT, 3 NEWCARD). */
static inline bool card_wait_event(CPUState *c, const CardEventLayout *l, u32 entry,
                                   unsigned first) {
    if (c->budget <= 2)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    c->budget -= 2;
    for (bool fired = false; !fired;) {
        for (unsigned i = 0; i < 4 && !fired; i++) {
            load_word(c, 4, R(28) + l->events[first + i]);
            guest_call(c, l->test_event, entry + 0x14 + 0x14 * i);
            fired = R(2) != 0;
            R(2) = i;
            guest_branch(c, fired);
        }
    }
    guest_leave_ra(c, 0x18, 0x10);
    return true;
}

/* Probe the card in port a0: 1 if it is ready and formatted, 0x10 if it is unformatted, 4 after
 * a card change was acknowledged, 2 on timeout and 8 on error. */
static inline bool card_probe(CPUState *c, const CardEventLayout *l) {
    const u32 entry = l->probe;
    if (c->budget <= 8)
        return false;
    R(29) -= 0x28;
    MEM_W32(R(29) + 0x14, R(17));
    R(17) = R(4);
    MEM_W32(R(29) + 0x20, R(31));
    MEM_W32(R(29) + 0x1c, R(19));
    MEM_W32(R(29) + 0x18, R(18));
    R(31) = entry + 0x20;
    MEM_W32(R(29) + 0x10, R(16));
    c->budget -= 8;
    nc_call(c, l->test_all);
    R(4) = R(17);
    guest_call(c, l->card_info, entry + 0x28);
    R(19) = 1;
    guest_call(c, l->wait_sw, entry + 0x30);
    set_reg(c, 16, R(2) & 0xff);

    bool taken = R(16) == R(19);
    R(2) = (s32)R(16) < 2;
    if (guest_branch(c, taken)) {
        set_reg(c, 2, 8); /* ERROR */
    } else {
        taken = R(2) == 0;
        R(18) = 2;
        if (!guest_branch(c, taken)) {
            taken = R(16) == 0;
            if (!guest_branch(c, taken)) {
                R(2) = 8;
                guest_branch(c, true);
            } else {
                /* IOE: the card answered. Is it formatted? */
                R(4) = R(17);
                guest_call(c, l->formatted, entry + 0x74);
                set_reg(c, 3, R(2) & 0xff);
                taken = R(3) == 0;
                R(2) = 0x10;
                if (!guest_branch(c, taken)) {
                    R(2) = 1;
                    guest_branch(c, true);
                }
            }
        } else {
            taken = R(16) == R(18);
            R(2) = 3;
            if (guest_branch(c, taken)) {
                R(2) = 2; /* TIMEOUT */
                guest_branch(c, true);
            } else {
                taken = R(16) == R(2);
                R(2) = 8;
                if (!guest_branch(c, taken)) {
                    guest_branch(c, true);
                } else {
                    /* NEWCARD: acknowledge the change and read the card again. */
                    guest_call(c, l->test_hw, entry + 0x90);
                    R(4) = R(17);
                    guest_call(c, l->card_clear, entry + 0x98);
                    guest_call(c, l->wait_hw, entry + 0xa0);
                    guest_call(c, l->test_sw, entry + 0xa8);
                    R(4) = R(17);
                    guest_call(c, l->card_load, entry + 0xb0);
                    guest_call(c, l->wait_sw, entry + 0xb8);
                    set_reg(c, 3, R(2) & 0xff);
                    taken = R(3) == R(19);
                    R(2) = (s32)R(3) < 2;
                    if (guest_branch(c, taken)) {
                        set_reg(c, 2, 8);
                    } else if (!guest_branch(c, R(2) == 0)) {
                        taken = R(3) == 0;
                        R(2) = 4;
                        if (!guest_branch(c, taken)) {
                            R(2) = 8;
                            guest_branch(c, true);
                        }
                    } else {
                        taken = R(3) == R(18);
                        R(2) = 2;
                        if (!guest_branch(c, taken)) {
                            taken = R(3) == R(16);
                            R(2) = 0x10;
                            if (!guest_branch(c, taken)) {
                                R(2) = 8;
                                guest_branch(c, true);
                            }
                        }
                    }
                }
            }
        }
    }
    guest_leave(c, 0x28, 0x20, 16, 19, 0x10);
    return true;
}

/* Read the first sector of the card in port a0 and check for the "MC" header. */
static inline bool card_formatted(CPUState *c, const CardEventLayout *l) {
    const u32 entry = l->formatted;
    if (c->budget <= 8)
        return false;
    R(29) -= 0x98;
    MEM_W32(R(29) + 0x90, R(16));
    R(16) = R(4);
    R(4) = R(29) + 0x10;
    R(5) = 0;
    MEM_W32(R(29) + 0x94, R(31));
    R(31) = entry + 0x20;
    R(6) = 0x80;
    c->budget -= 8;
    nc_call(c, l->memset);
    guest_call(c, l->test_all, entry + 0x28);
    guest_call(c, l->new_card, entry + 0x30);
    set_reg(c, 4, R(16));
    set_reg(c, 5, 0);
    R(6) = R(29) + 0x10;
    guest_call(c, l->card_read, entry + 0x40);
    guest_call(c, l->wait_hw, entry + 0x48);
    set_reg(c, 2, R(2) & 0xff);
    bool taken = R(2) == 0;
    R(3) = 'M';
    if (!guest_branch(c, taken)) {
        R(2) = 0;
        guest_branch(c, true);
    } else {
        load_byte(c, 4, R(29) + 0x10);
        guest_nop(c);
        taken = R(4) != R(3);
        R(2) = 0;
        if (!guest_branch(c, taken)) {
            load_byte(c, 2, R(29) + 0x11);
            guest_nop(c);
            set_reg(c, 2, R(2) ^ 'C');
            set_reg(c, 2, R(2) < 1);
        }
    }
    guest_leave(c, 0x98, 0x94, 16, 16, 0x90);
    return true;
}

/* Does the file a1 exist on the card in port a0? */
static inline bool card_file_exists(CPUState *c, const CardEventLayout *l) {
    const u32 entry = l->file_exists;
    if (c->budget <= 8)
        return false;
    R(29) -= 0x98;
    MEM_W32(R(29) + 0x90, R(31));
    R(6) = R(4);
    R(7) = R(5);
    R(5) = l->path_format;
    R(31) = entry + 0x20;
    R(4) = R(29) + 0x10;
    c->budget -= 8;
    nc_call(c, l->sprintf);
    set_reg(c, 4, R(29) + 0x10);
    R(5) = 1; /* O_RDONLY */
    guest_call(c, l->open, entry + 0x2c);
    set_reg(c, 4, R(2));
    set_reg(c, 2, UINT32_MAX);
    const bool missing = R(4) == R(2);
    R(2) = 0;
    if (!guest_branch(c, missing)) {
        guest_call(c, l->close, entry + 0x44);
        set_reg(c, 2, 1);
    }
    guest_leave_ra(c, 0x98, 0x90);
    return true;
}

#endif
