#include "port/hw/hw.h"

static u32 i_stat, i_mask;

void irq_raise(int source) {
    i_stat |= 1u << source;
}

bool irq_pending(void) {
    return (i_stat & i_mask) != 0;
}

u32 irq_read(u32 reg) {
    return reg == 0 ? i_stat : i_mask;
}

void irq_write(u32 reg, u32 value) {
    if (reg == 0) {
        i_stat &= value; /* writing 0 bits acknowledges */
    } else {
        i_mask = value & 0x7FFu;
    }
}
