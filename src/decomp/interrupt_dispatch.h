#ifndef NC_DECOMP_INTERRUPT_DISPATCH_H
#define NC_DECOMP_INTERRUPT_DISPATCH_H

#include "decomp/interrupt_control.h"

static inline u32 interrupt_hi(u32 address) {
    return (address + 0x8000u) & 0xffff0000u;
}

static void load_irq_halfword(CPUState *c, unsigned reg, u32 address) {
    R(reg) = MEM_R16(address);
    NC_POLL(c, NC_LOAD_CYCLES);
}

/* a0 holds the I_STAT pointer on entry. Only dispatch enabled sources with
 * a bit set in both I_STAT and I_MASK. */
static bool sample_pending_interrupts(CPUState *c, const InterruptControlLayout *layout) {
    set_reg(c, 2, interrupt_hi(layout->state + 0x30));
    load_irq_halfword(c, 2, layout->state + 0x30);
    set_reg(c, 3, interrupt_hi(layout->ports + 4));
    load_word(c, 3, layout->ports + 4);
    load_irq_halfword(c, 4, R(4));
    load_irq_halfword(c, 3, R(3));
    set_reg(c, 2, R(2) & R(4));
    set_reg(c, 16, R(3) & R(2));
    bool pending = R(16) != 0;
    NC_POLL(c, 2);
    return pending;
}

static void dispatch_irq_slots(CPUState *c, const InterruptControlLayout *layout) {
    R(17) = 0; /* source index, in the branch delay slot */
    NC_POLL(c, 2);
    set_reg(c, 18, R(20));
    for (;;) {
        set_reg(c, 2, (s32)R(17) < 11);
        bool valid = R(2) != 0;
        R(2) = R(16) & 1;
        NC_POLL(c, 2);
        if (!valid)
            break;
        bool active = R(2) != 0;
        R(2) = R(19) << (R(17) & 31);
        NC_POLL(c, 2);
        if (active) {
            set_reg(c, 3, interrupt_hi(layout->ports));
            load_word(c, 3, layout->ports);
            set_reg(c, 2, ~R(2));
            MEM_W16(R(3), (u16)R(2)); /* acknowledge before calling the handler */
            NC_POLL(c, 1);
            load_word(c, 2, R(18));
            NC_POLL(c, 1);
            bool installed = R(2) != 0;
            NC_POLL(c, 2);
            if (installed) {
                u32 target = R(2);
                R(31) = layout->trap + 0xe8;
                NC_POLL(c, 2);
                nc_call(c, target);
            }
        }
        set_reg(c, 18, R(18) + 4);
        set_reg(c, 16, R(16) >> 1);
        set_reg(c, 2, R(16) & 0xffff);
        bool more = R(2) != 0;
        R(17)++;
        NC_POLL(c, 2);
        if (!more)
            break;
    }
}

static inline bool dispatch_interrupts(CPUState *c, const InterruptControlLayout *layout) {
    if (c->budget <= 9)
        return false;
    R(29) -= 40;
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    R(17) = layout->state;
    c->budget -= 2;
    MEM_W32(R(29) + 36, R(31));
    c->budget--;
    for (unsigned reg = 20; reg >= 18; reg--) {
        MEM_W32(R(29) + 16 + 4 * (reg - 16), R(reg));
        c->budget--;
    }
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    load_irq_halfword(c, 2, R(17));
    NC_POLL(c, 1);
    bool initialized = R(2) != 0;
    NC_POLL(c, 2);
    if (!initialized) {
        set_reg(c, 2, interrupt_hi(layout->ports));
        load_word(c, 2, layout->ports);
        NC_POLL(c, 1);
        load_irq_halfword(c, 5, R(2));
        set_reg(c, 4, interrupt_hi(layout->uninitialized_message));
        set_reg(c, 4, layout->uninitialized_message);
        R(31) = layout->trap + 0x54;
        NC_POLL(c, 2);
        nc_call(c, layout->print); /* printf */
        R(31) = layout->trap + 0x5c;
        NC_POLL(c, 2);
        nc_call(c, layout->return_from_exception); /* ReturnFromException */
    }

    set_reg(c, 4, interrupt_hi(layout->ports));
    load_word(c, 4, layout->ports);
    set_reg(c, 2, 1);
    set_reg(c, 1, interrupt_hi(layout->state + 2));
    MEM_W16(layout->state + 2, (u16)R(2)); /* inside interrupt handler */
    NC_POLL(c, 1);
    if (sample_pending_interrupts(c, layout)) {
        set_reg(c, 19, 1);
        set_reg(c, 20, R(17) + 4);
        do {
            dispatch_irq_slots(c, layout);
            set_reg(c, 4, interrupt_hi(layout->ports));
            load_word(c, 4, layout->ports);
        } while (sample_pending_interrupts(c, layout));
    }

    /* Count consecutive entries with an unhandled interrupt still asserted. */
    set_reg(c, 5, interrupt_hi(layout->ports));
    load_word(c, 5, layout->ports);
    set_reg(c, 6, interrupt_hi(layout->ports + 4));
    load_word(c, 6, layout->ports + 4);
    load_irq_halfword(c, 2, R(5));
    load_irq_halfword(c, 3, R(6));
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) & R(3));
    bool unhandled = R(2) != 0;
    NC_POLL(c, 2);
    if (unhandled) {
        set_reg(c, 2, interrupt_hi(layout->ports + 12));
        load_word(c, 2, layout->ports + 12);
        NC_POLL(c, 1);
        set_reg(c, 3, R(2) + 1);
        set_reg(c, 2, (s32)R(2) < 0x801);
        set_reg(c, 1, interrupt_hi(layout->ports + 12));
        MEM_W32(layout->ports + 12, R(3));
        NC_POLL(c, 1);
        bool report = !R(2);
        NC_POLL(c, 2);
        if (report) {
            set_reg(c, 4, interrupt_hi(layout->unhandled_message));
            set_reg(c, 4, layout->unhandled_message);
            load_irq_halfword(c, 5, R(5));
            load_irq_halfword(c, 6, R(6));
            R(31) = layout->trap + 0x190;
            NC_POLL(c, 2);
            nc_call(c, layout->print);
            set_reg(c, 2, interrupt_hi(layout->ports));
            load_word(c, 2, layout->ports);
            set_reg(c, 1, interrupt_hi(layout->ports + 12));
            MEM_W32(layout->ports + 12, 0);
            NC_POLL(c, 1);
            MEM_W16(R(2), 0);
            NC_POLL(c, 1);
            NC_POLL(c, 2);
        }
    } else {
        set_reg(c, 1, interrupt_hi(layout->ports + 12));
        MEM_W32(layout->ports + 12, 0);
        NC_POLL(c, 1);
    }
    set_reg(c, 1, interrupt_hi(layout->state + 2));
    MEM_W16(layout->state + 2, 0);
    NC_POLL(c, 1);
    R(31) = layout->trap + 0x1c4;
    NC_POLL(c, 2);
    nc_call(c, layout->return_from_exception);
    load_word(c, 31, R(29) + 36);
    for (unsigned reg = 20; reg >= 16; reg--)
        load_word(c, reg, R(29) + 16 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2);
    return true;
}

typedef struct {
    u32 entry, slots, print;
    u32 status_message, channel_message;
} DmaDispatchLayout;

static void dispatch_pending_dma_channels(CPUState *c, const DmaDispatchLayout *layout) {
    bool pending = R(17) != 0;
    R(16) = 0;
    NC_POLL(c, 2);
    if (pending)
        set_reg(c, 18, R(21));
    while (pending) {
        set_reg(c, 2, (s32)R(16) < 7);
        bool valid_channel = R(2) != 0;
        R(2) = R(17) & 1;
        NC_POLL(c, 2);
        if (!valid_channel)
            break;
        bool acknowledge = R(2) != 0;
        R(2) = R(16) + 24;
        NC_POLL(c, 2);
        if (acknowledge) {
            set_reg(c, 4, interrupt_hi(layout->slots - 4));
            load_word(c, 4, layout->slots - 4);
            set_reg(c, 2, R(20) << (R(2) & 31));
            load_word(c, 3, R(4));
            set_reg(c, 2, R(2) | R(19));
            set_reg(c, 2, R(2) & R(3));
            MEM_W32(R(4), R(2)); /* write one to acknowledge this channel */
            NC_POLL(c, 1);
            load_word(c, 2, R(18));
            NC_POLL(c, 1);
            bool installed = R(2) != 0;
            NC_POLL(c, 2);
            if (installed) {
                u32 callback = R(2);
                R(31) = layout->entry + 0xa8;
                NC_POLL(c, 2);
                nc_call(c, callback);
            }
        }
        set_reg(c, 18, R(18) + 4);
        set_reg(c, 17, R(17) >> 1);
        pending = R(17) != 0;
        R(16)++;
        NC_POLL(c, 2);
    }
}

static void report_dma_interrupt(CPUState *c, const DmaDispatchLayout *layout) {
    set_reg(c, 4, interrupt_hi(layout->status_message));
    set_reg(c, 4, layout->status_message);
    load_word(c, 5, R(5));
    R(31) = layout->entry + 0x128;
    R(16) = 0;
    NC_POLL(c, 2);
    nc_call(c, layout->print);
    bool more;
    do {
        set_reg(c, 4, interrupt_hi(layout->channel_message));
        set_reg(c, 4, layout->channel_message);
        set_reg(c, 5, R(16));
        set_reg(c, 2, interrupt_hi(layout->slots + 0x20));
        load_word(c, 2, layout->slots + 0x20); /* channel register base */
        set_reg(c, 3, R(16) << 4);
        set_reg(c, 3, R(3) + R(2));
        load_word(c, 6, R(3));
        R(31) = layout->entry + 0x150;
        R(16)++;
        NC_POLL(c, 2);
        nc_call(c, layout->print);
        set_reg(c, 2, (s32)R(16) < 7);
        more = R(2) != 0;
        NC_POLL(c, 2);
    } while (more);
}

static inline bool dispatch_dma_interrupt(CPUState *c, const DmaDispatchLayout *layout) {
    const int entry_cycles = 14 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    if (c->budget <= entry_cycles)
        return false;
    c->budget--;
    R(2) = MEM_R32(layout->slots - 4); /* DMA interrupt control register */
    R(29) -= 48;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 40, R(31));
    c->budget--;
    for (unsigned reg = 21; reg >= 16; reg--) {
        MEM_W32(R(29) + 16 + 4 * (reg - 16), R(reg));
        c->budget--;
    }
    R(2) = MEM_R32(R(2));
    R(2) >>= 24;
    R(17) = R(2) & 0x7f;
    c->budget -= NC_LOAD_CYCLES + 5;
    bool pending = R(17) != 0;
    if (pending) {
        set_reg(c, 20, 1);
        set_reg(c, 19, 0xff0000);
        set_reg(c, 19, R(19) | 0xffff);
        set_reg(c, 21, interrupt_hi(layout->slots));
        set_reg(c, 21, layout->slots);
    }
    while (pending) {
        dispatch_pending_dma_channels(c, layout);
        set_reg(c, 2, interrupt_hi(layout->slots - 4));
        load_word(c, 2, layout->slots - 4);
        NC_POLL(c, 1);
        load_word(c, 2, R(2));
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) >> 24);
        set_reg(c, 17, R(2) & 0x7f);
        pending = R(17) != 0;
        NC_POLL(c, 2);
    }
    set_reg(c, 5, interrupt_hi(layout->slots - 4));
    load_word(c, 5, layout->slots - 4);
    NC_POLL(c, 1);
    load_word(c, 2, R(5));
    set_reg(c, 3, 0xff000000);
    set_reg(c, 2, R(2) & R(3));
    set_reg(c, 3, 0x80000000);
    bool diagnostic = R(2) == R(3);
    NC_POLL(c, 2);
    if (!diagnostic) {
        load_word(c, 2, R(5));
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) & 0x8000);
        diagnostic = R(2) != 0;
        NC_POLL(c, 2);
    }
    if (diagnostic)
        report_dma_interrupt(c, layout);
    load_word(c, 31, R(29) + 40);
    for (unsigned reg = 21; reg >= 16; reg--)
        load_word(c, reg, R(29) + 16 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + 48);
    NC_POLL(c, 2);
    return true;
}

#endif
