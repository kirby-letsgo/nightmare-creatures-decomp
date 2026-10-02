#include "port/runtime.h"

CPUState nc_cpu;
u8 nc_ram[RAM_SIZE];
u8 nc_scratch[SCRATCH_SIZE];

enum { COP0_SR = 12, COP0_CAUSE = 13, COP0_EPC = 14 };

void nc_poll(CPUState *c) {
    /* TODO(phase 2 step 5): advance timers, raise VBlank, deliver pending interrupts. */
    c->budget += 1 << 16;
}

void nc_syscall(CPUState *c) {
    /* Psy-Q uses syscall for Enter/ExitCriticalSection, selected by $a0. */
    u32 sr = c->cop0[COP0_SR];
    switch (c->r[4]) {
    case 1: /* EnterCriticalSection: returns whether interrupts were enabled */
        c->r[2] = (sr & 0x404u) == 0x404u;
        c->cop0[COP0_SR] = sr & ~0x404u;
        break;
    case 2: /* ExitCriticalSection */
        c->cop0[COP0_SR] = sr | 0x404u;
        break;
    default:
        NC_LOG("syscall a0=%u ignored", c->r[4]);
        break;
    }
}

void nc_break(CPUState *c, u32 pc, u32 code) {
    (void)c;
    /* Psy-Q emits break 7 on division by zero and break 6 on overflow; both are non-fatal. */
    NC_LOG("break %u at 0x%08X", code, pc);
}

void nc_unimplemented(CPUState *c, u32 pc, u32 word) {
    (void)c;
    NC_FATAL("unimplemented instruction 0x%08X at 0x%08X", word, pc);
}

u32 nc_mfc0(CPUState *c, int reg) {
    return c->cop0[reg & 31];
}

void nc_mtc0(CPUState *c, int reg, u32 value) {
    c->cop0[reg & 31] = value;
}

void nc_rfe(CPUState *c) {
    u32 sr = c->cop0[COP0_SR];
    c->cop0[COP0_SR] = (sr & ~0x0Fu) | ((sr >> 2) & 0x0Fu);
}
