#include "decomp/credits.h"

/* CREDITS.EXE 80018C8C. The display loop polls the first InitPAD buffer
 * after VSync. With an event due, let the generated code poll at each
 * original instruction so the pad packet can change during the read. */
bool nc_credits_skip_input(CPUState *c) {
    if (c->budget <= 34)
        return false;

    const u32 packet = 0x80024CA4;
    R(2) = 0;
    R(3) = 0xff;
    c->budget -= 11; /* status check, including the common jr and delay slot */
    if (MEM_R8(packet) == 0xff)
        return true;

    const u16 pressed = (u16)~MEM_R16(packet + 2);
    const u32 type = MEM_R8(packet + 1) >> 4;
    R(4) = pressed;
    R(3) = type;
    c->budget -= 14;
    if (type != 4) {
        c->budget -= 2;
        if (type != 7)
            return true;
    }

    R(2) = 1;
    c->budget -= 3;
    if (pressed & 0x4000) /* Cross */
        return true;
    c->budget -= 3;
    if (pressed & 0x0008) /* Start */
        return true;

    R(2) = 0;
    c->budget--;
    return true;
}

/* Build the SpuCommonAttr used by the per-frame CD fade. The caller supplies
 * independent channel levels; zero converts to 0xffff, as on the disc. */
bool nc_credits_set_cd_volume(CPUState *c) {
    if (c->budget <= 14)
        return false;

    R(29) -= 0x40;
    const u32 attr = R(29) + 0x10;
    const u32 left = (R(4) << 7) - 1;
    R(5) = (R(5) << 7) - 1;
    MEM_W32(attr, 0x2c0); /* CD left volume, right volume, and mix enable */
    R(2) = 1;
    MEM_W16(attr + 0x10, (u16)left);
    R(4) = attr;
    MEM_W32(R(29) + 0x38, R(31));
    MEM_W16(attr + 0x12, (u16)R(5));
    R(31) = 0x8001931c;
    MEM_W32(attr + 0x18, 1);
    c->budget -= 14;
    nc_call(c, 0x8001ed98); /* SpuSetCommonAttr */

    R(31) = MEM_R32(R(29) + 0x38);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(29) += 0x40;
    NC_POLL(c, NC_INSN_CYCLES);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}
