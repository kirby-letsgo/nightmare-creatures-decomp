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
    u32 start, stop, restart;
    u32 clear, save_context, trap, start_vsync, start_dma, remove;
    u32 enter_critical, exit_critical;
    u32 reset_hook, install_hook;
} InterruptControlLayout;

static inline bool leave_interrupt_start(CPUState *c) {
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* The BIOS interrupt hook re-enters startIntr at the return address of setjmp. */
static inline bool resume_interrupt_start(CPUState *c, const InterruptControlLayout *layout) {
    const bool from_exception = R(2) != 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (from_exception) {
        R(31) = layout->start + 0x78;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, layout->trap); /* trapIntr */
    }
    set_reg(c, 16, (layout->state + 0x3c + 0x8000u) & 0xffff0000u);
    set_reg(c, 16, layout->state + 0x3c);
    set_reg(c, 4, R(16) - 4); /* saved jump buffer */
    set_reg(c, 2, R(16) + 0xfdc);
    R(31) = layout->start + 0x90;
    MEM_W32(R(16), R(2)); /* interrupt stack pointer */
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, layout->install_hook); /* HookEntryInt */
    set_reg(c, 2, 1);
    R(31) = layout->start + 0x9c;
    MEM_W16(R(16) - 0x3c, (u16)R(2));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, layout->start_vsync); /* startIntrVSync */
    set_reg(c, 3, (layout->ports + 0x8000u) & 0xffff0000u);
    load_word(c, 3, layout->ports - 4);
    R(31) = layout->start + 0xac;
    MEM_W32(R(3) + 20, R(2));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, layout->start_dma); /* startIntrDMA */
    set_reg(c, 4, (layout->ports + 0x8000u) & 0xffff0000u);
    load_word(c, 4, layout->ports - 4);
    R(31) = layout->start + 0xbc;
    MEM_W32(R(4) + 4, R(2));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, layout->remove); /* _96_remove */
    R(31) = layout->start + 0xc4;
    R(16) -= 0x3c;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, layout->exit_critical);
    set_reg(c, 2, R(16));
    return leave_interrupt_start(c);
}

static inline bool start_driver_interrupts(CPUState *c, const InterruptControlLayout *layout) {
    if (c->budget <= 8 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = layout->state;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    R(2) = MEM_R16(R(16));
    const bool active = R(2) != 0;
    R(2) = 0;
    c->budget -= NC_LOAD_CYCLES + 3;
    if (active)
        return leave_interrupt_start(c);

    set_reg(c, 3, (layout->ports + 0x8000u) & 0xffff0000u);
    load_word(c, 3, layout->ports);
    set_reg(c, 2, (layout->ports + 0x8000u) & 0xffff0000u);
    load_word(c, 2, layout->ports + 4);
    set_reg(c, 5, 0x33330000);
    MEM_W16(R(2), 0); /* mask IRQs before clearing pending requests */
    NC_POLL(c, NC_INSN_CYCLES);
    R(2) = MEM_R16(R(2));
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 5, R(5) | 0x3333);
    MEM_W16(R(3), (u16)R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 2, (layout->ports + 0x8000u) & 0xffff0000u);
    load_word(c, 2, layout->ports + 8);
    set_reg(c, 4, R(16));
    MEM_W32(R(2), R(5)); /* reset DMA priorities */
    NC_POLL(c, NC_INSN_CYCLES);
    R(31) = layout->start + 0x60;
    R(5) = 0x41a;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, layout->clear); /* memclr */
    R(31) = layout->start + 0x68;
    R(4) = R(16) + 0x38;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, layout->save_context); /* setjmp */
    return resume_interrupt_start(c, layout);
}

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

typedef struct {
    u32 slots, entry, clear, register_irq, handler, setter;
} InterruptInitLayout;

/* Clear the DMA callback table and DICR, then register the channel IRQ handler. */
static inline bool start_dma(CPUState *c, const InterruptInitLayout *layout) {
    if (c->budget <= 6)
        return false;
    R(29) -= 24;
    R(4) = layout->slots;
    c->budget -= 3;
    MEM_W32(R(29) + 16, R(31));
    R(31) = layout->entry + 0x18;
    R(5) = 8;
    c->budget -= 3;
    nc_call(c, layout->clear); /* clear eight callback slots */

    set_reg(c, 4, 3); /* DMA IRQ */
    set_reg(c, 2, (layout->slots - 4 + 0x8000u) & 0xffff0000u);
    load_word(c, 2, layout->slots - 4);
    set_reg(c, 5, (layout->handler + 0x8000u) & 0xffff0000u);
    set_reg(c, 5, layout->handler); /* trapIntrDMA */
    MEM_W32(R(2), 0);
    NC_POLL(c, 1);
    R(31) = layout->entry + 0x38;
    NC_POLL(c, 2);
    nc_call(c, layout->register_irq); /* InterruptCallback */
    set_reg(c, 2, (layout->setter + 0x8000u) & 0xffff0000u);
    set_reg(c, 2, layout->setter); /* setIntrDMA */
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* Initialize timer 1 and the VBlank callback table, then return its slot setter. */
static inline bool start_vsync(CPUState *c, const InterruptInitLayout *layout) {
    if (c->budget <= 14)
        return false;
    R(29) -= 24;
    R(4) = layout->slots;
    c->budget -= 4;
    R(3) = MEM_R32(layout->slots + 36);
    R(2) = 0x107;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 16, R(31));
    c->budget--;
    MEM_W32(R(3), R(2));
    R(1) = (layout->slots + 32 + 0x8000u) & 0xffff0000u;
    c->budget -= 2;
    MEM_W32(layout->slots + 32, 0); /* VBlank count */
    R(31) = layout->entry + 0x30;
    R(5) = 8;
    c->budget -= 3;
    nc_call(c, layout->clear); /* clear the callback slots */

    set_reg(c, 5, (layout->handler + 0x8000u) & 0xffff0000u);
    set_reg(c, 5, layout->handler); /* trapIntrVSync */
    R(31) = layout->entry + 0x40;
    R(4) = 0;
    NC_POLL(c, 2);
    nc_call(c, layout->register_irq); /* InterruptCallback */
    set_reg(c, 2, (layout->setter + 0x8000u) & 0xffff0000u);
    set_reg(c, 2, layout->setter); /* setIntrVSync */
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* Count this VBlank and visit the live table so callbacks can change later slots. */
static inline bool dispatch_vblank(CPUState *c, u32 slots, u32 resume) {
    if (c->budget <= 18)
        return false;
    const u32 counter = slots + 8 * sizeof(u32);
    c->budget--;
    R(2) = MEM_R32(counter);
    R(29) -= 32;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 20, R(17));
    R(17) = 0;
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(16));
    R(16) = slots;
    c->budget -= 3;
    MEM_W32(R(29) + 24, R(31));
    R(2)++;
    R(1) = (counter + 0x8000u) & 0xffff0000u;
    c->budget -= 3;
    MEM_W32(counter, R(2));
    c->budget -= 2;
    load_word(c, 2, counter);

    bool more;
    do {
        load_word(c, 2, R(16));
        NC_POLL(c, 1);
        bool occupied = R(2) != 0;
        R(16) += 4;
        NC_POLL(c, 2);
        if (occupied) {
            u32 callback = R(2);
            R(31) = resume;
            NC_POLL(c, 2);
            nc_call(c, callback);
        }
        set_reg(c, 17, R(17) + 1);
        set_reg(c, 2, (s32)R(17) < 8);
        more = R(2) != 0;
        NC_POLL(c, 2);
    } while (more);
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

#endif
