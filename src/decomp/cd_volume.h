#ifndef NC_DECOMP_CD_VOLUME_H
#define NC_DECOMP_CD_VOLUME_H

#include "port/recomp.h"

static inline void cd_mix_control(CPUState *c, u32 slot, u8 value) {
    c->budget--;
    R(3) = MEM_R32(slot);
    R(2) = value;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W8(R(3), value);
    c->budget--;
}

static inline void cd_mix_channel(CPUState *c, u32 slot, u32 coefficient) {
    c->budget--;
    R(3) = MEM_R32(slot);
    c->budget -= NC_LOAD_CYCLES;
    R(2) = MEM_R8(coefficient);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W8(R(3), (u8)R(2));
    c->budget--;
}

/* CD_vol programs four CD audio mix coefficients through register banks 2 and 3. */
static inline bool cd_volume(CPUState *c, u32 ports) {
    if (c->budget <= 56)
        return false;
    cd_mix_control(c, ports, 2);
    cd_mix_channel(c, ports + 8, R(4));
    cd_mix_channel(c, ports + 12, R(4) + 1);
    cd_mix_control(c, ports, 3);
    cd_mix_channel(c, ports + 4, R(4) + 2);
    cd_mix_channel(c, ports + 8, R(4) + 3);
    cd_mix_control(c, ports + 12, 0x20); /* apply the mix */
    R(2) = 0;
    c->budget -= 2;
    return true;
}

#endif
