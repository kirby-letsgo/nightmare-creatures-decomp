#ifndef NC_DECOMP_CD_STATE_H
#define NC_DECOMP_CD_STATE_H

#include "decomp/interrupt_control.h"

/* CdStatus, CdMode and CdLastCom each return one cached unsigned byte. */
static inline bool cd_state_byte(CPUState *c, u32 address) {
    if (c->budget <= 6)
        return false;
    c->budget--; /* lui */
    R(2) = MEM_R8(address);
    c->budget -= NC_LOAD_CYCLES + 2; /* lbu, jr and its delay slot */
    return true;
}

static inline bool cd_last_position(CPUState *c, u32 address) {
    if (c->budget <= 4)
        return false;
    R(2) = address; /* Address of the cached CD position. */
    c->budget -= 4;
    return true;
}

/* These CD setters return the previous word before installing a0. */
static inline bool exchange_cd_word(CPUState *c, u32 address) {
    if (c->budget <= 8)
        return false;
    c->budget--; /* lui */
    R(2) = MEM_R32(address);
    R(1) = (address + 0x8000u) & 0xffff0000u;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(address, R(4));
    c->budget -= 3; /* sw, jr and its delay slot */
    return true;
}

/* CdComstr and CdIntstr return guest pointers into the CD diagnostic strings. */
static inline bool cd_lookup_name(CPUState *c, u32 table, u32 count, u32 unknown) {
    if (c->budget <= 13)
        return false;
    R(4) &= 0xff;
    c->budget -= 4; /* mask, range check, branch and delay slot */
    if (R(4) < count) {
        u32 offset = R(4) * 4;
        R(1) = ((table + 0x8000u) & 0xffff0000u) + offset;
        c->budget -= 2;
        R(2) = MEM_R32(table + offset);
        c->budget -= NC_LOAD_CYCLES + 2; /* load and jump to the return */
    } else {
        R(2) = unknown;
        c->budget -= 2;
    }
    c->budget -= 2; /* jr and delay slot */
    return true;
}

/* The default callbacks notify the BIOS CD event class through DeliverEvent. */
static inline bool cd_default_event(CPUState *c, u32 spec, u32 resume, u32 deliver) {
    if (c->budget <= 6)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(4) = 0xf0000003;
    R(31) = resume;
    R(5) = spec;
    c->budget -= 5;
    nc_call(c, deliver); /* DeliverEvent */
    R(31) = MEM_R32(R(29) + 16);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(29) += 24;
    NC_POLL(c, 1);
    NC_POLL(c, 2);
    return true;
}

enum CdDriverResult { CD_RETURN_STATUS, CD_RETURN_ONE, CD_ZERO_IS_SUCCESS };

/* Forward the arguments, then apply the SDK wrapper's return convention. */
static inline bool call_cd_status_driver(CPUState *c, u32 driver, u32 return_pc,
                                         enum CdDriverResult result) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(31) = return_pc;
    c->budget -= 3;
    nc_call(c, driver);
    if (result != CD_RETURN_STATUS) {
        R(2) = result == CD_RETURN_ONE || R(2) == 0;
        NC_POLL(c, 1);
    }
    R(31) = MEM_R32(R(29) + 16);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(29) += 24;
    NC_POLL(c, 1);
    NC_POLL(c, 2);
    return true;
}

/* Register a handler on one DMA channel (DMACallback) and return its predecessor. */
static inline bool dma_channel_callback(CPUState *c, u32 channel, u32 dma_callback, u32 resume) {
    if (c->budget <= 5)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(5) = R(4);
    R(31) = resume;
    R(4) = channel;
    c->budget -= 4;
    nc_call(c, dma_callback); /* DMACallback */
    R(31) = MEM_R32(R(29) + 16);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(29) += 24;
    NC_POLL(c, 1);
    NC_POLL(c, 2);
    return true;
}

/* Register the CD data handler on DMA channel 3. */
static inline bool cd_data_callback(CPUState *c, u32 dma_callback, u32 resume) {
    return dma_channel_callback(c, 3, dma_callback, resume);
}

static inline bool cd_set_test_parameter_count(CPUState *c, u32 address) {
    if (c->budget <= 4)
        return false;
    R(1) = (address + 0x8000u) & 0xffff0000u;
    c->budget--;
    MEM_W32(address, R(4));
    c->budget -= 3; /* store, return and delay slot */
    return true;
}

/* Clear the CD callbacks and cached status, then install the IRQ 2 handler. */
static inline bool cd_init_interrupt(CPUState *c, u32 state, u32 entry, u32 reset_callback,
                                     u32 interrupt_callback, u32 handler) {
    if (c->budget <= 12)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(31));
    R(1) = (state + 0x8000u) & 0xffff0000u;
    c->budget -= 2;
    MEM_W32(state + 4, 0); /* ready callback */
    c->budget -= 2;
    MEM_W32(state, 0); /* sync callback */
    c->budget -= 2;
    MEM_W32(state + 20, 0);
    c->budget -= 2;
    MEM_W32(state + 16, 0);
    R(31) = entry + 0x30;
    c->budget -= 3;
    nc_call(c, reset_callback); /* ResetCallback */

    set_reg(c, 5, (handler + 0x8000u) & 0xffff0000u);
    set_reg(c, 5, handler);
    R(31) = entry + 0x40;
    R(4) = 2;
    NC_POLL(c, 2);
    nc_call(c, interrupt_callback); /* InterruptCallback */
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* Mode 2 only reinstalls interrupts. Mode 1 also initializes CD audio volume. */
static inline bool cd_reset(CPUState *c, u32 entry, u32 init_interrupt, u32 init_drive,
                            u32 init_volume) {
    if (c->budget <= 6)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    R(2) = 2;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget -= 2;
    if (R(16) == 2) {
        R(31) = entry + 0x20;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, init_interrupt); /* CD_initintr */
        R(2) = 1;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } else {
        R(31) = entry + 0x30;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, init_drive); /* CD_init */
        const bool initialized = R(2) == 0;
        R(2) = 0;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (initialized) {
            set_reg(c, 2, 1);
            const bool volume_requested = R(16) == 1;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            if (volume_requested) {
                R(31) = entry + 0x4c;
                NC_POLL(c, 2 * NC_INSN_CYCLES);
                nc_call(c, init_volume); /* CD_initvol */
                const bool volume_ready = R(2) == 0;
                R(2) = 0;
                NC_POLL(c, 2 * NC_INSN_CYCLES);
                if (volume_ready)
                    set_reg(c, 2, 1);
            }
        }
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

#endif
