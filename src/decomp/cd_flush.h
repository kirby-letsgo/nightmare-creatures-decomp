#ifndef NC_DECOMP_CD_FLUSH_H
#define NC_DECOMP_CD_FLUSH_H

#include "decomp/interrupt_control.h"

/* Load a CD port pointer from the guest table, including the load delay nop. */
static inline void cd_flush_port(CPUState *c, u32 slot) {
    set_reg(c, 2, (slot + 0x8000u) & 0xffff0000u);
    load_word(c, 2, slot);
    NC_POLL(c, 1);
}

static inline bool cd_flush_pending(CPUState *c, u32 ports, bool first) {
    cd_flush_port(c, ports + 12);
    R(2) = MEM_R8(R(2));
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) & 7);
    bool pending = R(2) != 0;
    if (first)
        R(3) = 7;
    NC_POLL(c, 2);
    return pending;
}

static inline bool cd_flush_controller(CPUState *c, u32 ports) {
    const u32 upper = (ports + 0x8000u) & 0xffff0000u;
    if (c->budget <= 6)
        return false;
    c->budget--;
    R(3) = MEM_R32(ports);
    R(2) = 1;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W8(R(3), 1); /* select the interrupt bank */
    c->budget--;
    if (cd_flush_pending(c, ports, true)) {
        set_reg(c, 4, 1);
        do {
            cd_flush_port(c, ports);
            MEM_W8(R(2), (u8)R(4));
            NC_POLL(c, 1);
            cd_flush_port(c, ports + 12);
            MEM_W8(R(2), (u8)R(3)); /* acknowledge pending interrupts */
            NC_POLL(c, 1);
            cd_flush_port(c, ports + 8);
            MEM_W8(R(2), (u8)R(3)); /* enable the next interrupt */
            NC_POLL(c, 1);
        } while (cd_flush_pending(c, ports, false));
    }
    set_reg(c, 1, upper);
    MEM_W8(ports + 26, 0);
    NC_POLL(c, 1);
    set_reg(c, 3, upper);
    R(3) = MEM_R8(ports + 26);
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 2, upper);
    set_reg(c, 2, ports + 25);
    MEM_W8(R(2), (u8)R(3));
    NC_POLL(c, 1);
    set_reg(c, 3, upper);
    load_word(c, 3, ports);
    set_reg(c, 2, 2);
    set_reg(c, 1, upper);
    MEM_W8(ports + 24, (u8)R(2));
    NC_POLL(c, 1);
    MEM_W8(R(3), 0);
    NC_POLL(c, 1);
    cd_flush_port(c, ports + 12);
    MEM_W8(R(2), 0); /* clear the data request */
    NC_POLL(c, 1);
    set_reg(c, 3, upper);
    load_word(c, 3, ports + 16);
    set_reg(c, 2, 0x1325);
    MEM_W32(R(3), R(2));
    NC_POLL(c, 1);
    NC_POLL(c, 2);
    return true;
}

#endif
