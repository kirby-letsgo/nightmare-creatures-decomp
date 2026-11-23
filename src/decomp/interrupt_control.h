#ifndef NC_DECOMP_INTERRUPT_CONTROL_H
#define NC_DECOMP_INTERRUPT_CONTROL_H

#include "port/recomp.h"

static inline void set_reg(CPUState *c, unsigned reg, u32 value) {
    R(reg) = value;
    NC_POLL(c, NC_INSN_CYCLES);
}
static inline void load_word(CPUState *c, unsigned reg, u32 addr) {
    R(reg) = MEM_R32(addr);
    NC_POLL(c, NC_LOAD_CYCLES);
}

typedef struct {
    u32 state, ports;
    u32 stop, restart;
    u32 enter_critical, exit_critical;
    u32 reset_hook, install_hook;
} InterruptControlLayout;

/* Save the IRQ mask and DMA priorities before disabling the interrupt driver. */
static inline bool stop_driver_interrupts(CPUState *c, const InterruptControlLayout *layout) {
    if (c->budget <= 11)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = layout->state;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    bool active = MEM_R16(R(16)) != 0;
    R(2) = 0;
    c->budget -= NC_LOAD_CYCLES + 3;
    if (active) {
        R(31) = layout->stop + 0x2c;
        NC_POLL(c, 2);
        nc_call(c, layout->enter_critical); /* EnterCriticalSection */
        set_reg(c, 2, (layout->state & 0xffff0000u));
        load_word(c, 2, layout->ports + 4); /* I_MASK */
        set_reg(c, 4, (layout->state & 0xffff0000u));
        load_word(c, 4, layout->ports + 8); /* DPCR */
        R(3) = MEM_R16(R(2));
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, 1, (layout->state & 0xffff0000u));
        MEM_W16(layout->state + 0x32, (u16)R(3));
        NC_POLL(c, 1);
        load_word(c, 3, R(4));
        set_reg(c, 4, (layout->state & 0xffff0000u));
        load_word(c, 4, layout->ports); /* I_STAT */
        set_reg(c, 1, (layout->state & 0xffff0000u));
        MEM_W32(layout->state + 0x34, R(3));
        NC_POLL(c, 1);
        MEM_W16(R(2), 0);
        NC_POLL(c, 1);
        R(2) = MEM_R16(R(2));
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        MEM_W16(R(4), (u16)R(2));
        NC_POLL(c, 1);
        set_reg(c, 4, (layout->state & 0xffff0000u));
        load_word(c, 4, layout->ports + 8);
        set_reg(c, 3, 0x77770000);
        load_word(c, 2, R(4));
        set_reg(c, 3, 0x77777777);
        set_reg(c, 2, R(2) & R(3));
        MEM_W32(R(4), R(2));
        NC_POLL(c, 1);
        R(31) = layout->stop + 0x90;
        NC_POLL(c, 2);
        nc_call(c, layout->reset_hook); /* ResetEntryInt */
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
static inline bool restart_driver_interrupts(CPUState *c, const InterruptControlLayout *layout) {
    if (c->budget <= 11)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = layout->state;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    R(2) = MEM_R16(R(16));
    c->budget -= NC_LOAD_CYCLES + 3;
    if (R(2) == 0) {
        R(31) = layout->restart + 0x2c;
        R(4) = R(16) + 0x38;
        NC_POLL(c, 2);
        nc_call(c, layout->install_hook); /* HookEntryInt */
        set_reg(c, 4, (layout->state & 0xffff0000u));
        load_word(c, 4, layout->ports + 4);
        set_reg(c, 3, (layout->state & 0xffff0000u));
        R(3) = MEM_R16(layout->state + 0x32);
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, 2, 1);
        MEM_W16(R(16), (u16)R(2));
        NC_POLL(c, 1);
        MEM_W16(R(4), (u16)R(3));
        NC_POLL(c, 1);
        set_reg(c, 3, (layout->state & 0xffff0000u));
        load_word(c, 3, layout->ports + 8);
        set_reg(c, 2, (layout->state & 0xffff0000u));
        load_word(c, 2, layout->state + 0x34);
        NC_POLL(c, 1);
        MEM_W32(R(3), R(2));
        NC_POLL(c, 1);
        R(31) = layout->restart + 0x68;
        NC_POLL(c, 2);
        nc_call(c, layout->exit_critical); /* ExitCriticalSection */
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

/* The IRQ, VSync and DMA initializers use identical word-clear loops. */
static inline bool clear_interrupt_words(CPUState *c) {
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

#endif
