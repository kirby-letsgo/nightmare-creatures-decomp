#ifndef NC_DECOMP_CD_POSITION_H
#define NC_DECOMP_CD_POSITION_H

#include "port/recomp.h"

/* CdIntToPos adds the lead-in before splitting the signed frame count.
 * The guest leaves the intermediate BCD digits and last product in registers. */
static inline bool cd_sector_to_position(CPUState *c) {
    if (c->budget <= 65)
        return false;
    s32 frames = (s32)(R(4) + 150u);
    s32 frame = frames % 75;
    s32 second = (frames / 75) % 60;
    s32 minute = frames / 4500;

    R(2) = R(5); /* returned position address */
    R(8) = frame / 10;
    R(7) = (frame / 10) * 16 + frame % 10;
    R(5) = second / 10;
    R(6) = second % 10;
    R(4) = (second / 10) * 16 + second % 10;
    nc_mult(c, (u32)minute, 0x66666667); /* guest's final divide-by-ten product */
    c->budget -= 51;
    MEM_W8(R(2) + 1, (u8)R(4));
    R(4) = minute < 0 ? UINT32_MAX : 0;
    c->budget -= 2;
    MEM_W8(R(2) + 2, (u8)R(7));

    R(3) = minute / 10;
    R(4) = (minute / 10) * 10;
    R(9) = minute % 10;
    R(5) = (minute / 10) * 16 + minute % 10;
    c->budget -= 10;
    MEM_W8(R(2), (u8)R(5)); /* return delay slot */
    c->budget -= 2;
    return true;
}

/* CdPosToInt converts a packed BCD minute:second:frame position to an LBA. */
static inline bool cd_position_to_sector(CPUState *c) {
    if (c->budget <= 38)
        return false;
    u32 position = R(4);
    u32 minute = MEM_R8(position);
    c->budget -= NC_LOAD_CYCLES;
    u32 second = MEM_R8(position + 1);
    c->budget -= NC_LOAD_CYCLES + 19;
    u32 frame = MEM_R8(position + 2);
    u32 minutes = (minute >> 4) * 10 + (minute & 15);
    R(6) = second & 15;
    u32 seconds = (second >> 4) * 10 + R(6);
    R(4) = frame >> 4;
    R(5) = frame & 15;
    R(3) = R(4) * 10 + R(5);
    R(2) = (minutes * 60 + seconds) * 75 + R(3) - 150;
    c->budget -= NC_LOAD_CYCLES + 10;
    return true;
}

#endif
