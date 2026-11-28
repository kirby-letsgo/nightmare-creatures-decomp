/* The record shared with the launcher, reset by CREDITS.EXE 80019220 and PSX.EXE 8001A444 (the
 * same code; only the gp offset of the record pointer differs). */
#ifndef NC_DECOMP_SHARED_RECORD_H
#define NC_DECOMP_SHARED_RECORD_H

#include "port/recomp.h"

/* Reset the record shared with the launcher. The displays later subtract 10 and 5 from its two
 * screen-position bytes. */
static inline bool init_shared_record(CPUState *c, u32 slot_offset) {
    const int cost = 36 * NC_INSN_CYCLES + 13 * NC_LOAD_CYCLES;
    if (c->budget <= cost)
        return false;
    const u32 slot = R(28) + slot_offset;
    u32 record = MEM_R32(slot);
    MEM_W8(record + 0x22, 0);
    const u32 settings = MEM_R32(slot);
    MEM_W32(record, 0);
    MEM_W32(record + 4, 0);
    MEM_W16(settings + 0x0c, 0);
    const u8 halfwords[] = {0x08, 0x0a, 0x10, 0x16};
    for (unsigned i = 0; i < sizeof halfwords; i++)
        MEM_W16(settings + halfwords[i], 128);
    MEM_W8(settings + 0x18, 10);

    /* The guest reloads the shared pointer before each byte store. */
    static const struct {
        u8 off, value;
    } bytes[] = {
        {0x19, 5},    {0x1a, 0x4d}, {0x1e, 0x13}, {0x1b, 0x4c}, {0x1f, 10},
        {0x1c, 0xb2}, {0x20, 0xec}, {0x1d, 0xb1}, {0x21, 0xec},
    };
    for (unsigned i = 0; i < sizeof bytes / sizeof bytes[0]; i++)
        MEM_W8(MEM_R32(slot) + bytes[i].off, bytes[i].value);

    R(4) = MEM_R32(slot);
    R(2) = MEM_R32(R(4) + 4);
    R(3) = 0x08000000;
    MEM_W32(R(4) + 0x24, 0);
    R(2) |= R(3);
    MEM_W32(R(4) + 4, R(2));
    c->budget -= cost;
    return true;
}

#endif
