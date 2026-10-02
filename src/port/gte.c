#include "port/runtime.h"

/* Placeholder until the full GTE implementation lands. */
u32 gte_read_data(CPUState *c, int reg) {
    return c->gte.data[reg & 31];
}
void gte_write_data(CPUState *c, int reg, u32 value) {
    c->gte.data[reg & 31] = value;
}
u32 gte_read_ctrl(CPUState *c, int reg) {
    return c->gte.ctrl[reg & 31];
}
void gte_write_ctrl(CPUState *c, int reg, u32 value) {
    c->gte.ctrl[reg & 31] = value;
}
void gte_command(CPUState *c, u32 cmd) {
    (void)c;
    NC_FATAL("GTE command 0x%07X not implemented", cmd);
}
