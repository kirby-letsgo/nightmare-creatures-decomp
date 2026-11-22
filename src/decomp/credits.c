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
