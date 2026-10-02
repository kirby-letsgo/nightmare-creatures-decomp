#include "port/runtime.h"

void bios_init(void) {}

void bios_call(CPUState *c, u32 table) {
    NC_FATAL("BIOS %02X:%02X not implemented (ra=0x%08X)", table, c->r[9], c->r[31]);
}
