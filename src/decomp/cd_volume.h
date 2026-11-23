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

/* CD_initvol installs the default stereo mix and enables CD input on the SPU. */
static inline bool cd_init_volume(CPUState *c, u32 ports) {
    if (c->budget <= 91)
        return false;
    c->budget--;
    R(3) = MEM_R32(ports + 20);
    c->budget -= NC_LOAD_CYCLES + 1;
    u16 left = MEM_R16(R(3) + 0x1b8);
    R(29) -= 8;
    c->budget -= NC_LOAD_CYCLES + 3;
    if (!left) {
        u16 right = MEM_R16(R(3) + 0x1ba);
        R(2) = 0x3fff;
        c->budget -= NC_LOAD_CYCLES + 3;
        if (!right) {
            MEM_W16(R(3) + 0x180, R(2));
            c->budget--;
            MEM_W16(R(3) + 0x182, R(2));
            c->budget -= 2;
            R(3) = MEM_R32(ports + 20);
            c->budget -= NC_LOAD_CYCLES + 1;
        }
    } else {
        R(2) = 0x3fff;
        c->budget--;
    }
    MEM_W16(R(3) + 0x1b0, R(2));
    c->budget--;
    MEM_W16(R(3) + 0x1b2, R(2));
    c->budget -= 2;
    MEM_W16(R(3) + 0x1aa, 0xc001);
    c->budget -= 2;
    R(3) = MEM_R32(ports);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W8(R(29) + 2, 0x80);
    c->budget--;
    MEM_W8(R(29), 0x80);
    c->budget -= 2;
    MEM_W8(R(29) + 3, 0);
    c->budget--;
    MEM_W8(R(29) + 1, 0);
    c->budget--;
    MEM_W8(R(3), 2);
    c->budget--;
    cd_mix_channel(c, ports + 8, R(29));
    cd_mix_channel(c, ports + 12, R(29) + 1);
    cd_mix_control(c, ports, 3);
    cd_mix_channel(c, ports + 4, R(29) + 2);
    cd_mix_channel(c, ports + 8, R(29) + 3);
    cd_mix_control(c, ports + 12, 0x20);
    R(2) = 0;
    R(29) += 8;
    c->budget -= 4;
    return true;
}

#endif
