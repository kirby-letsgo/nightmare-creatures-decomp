#include "port/hw/hw.h"
#include "port/runtime.h"

#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#define NC_HAVE_BACKTRACE 1
#endif

CPUState nc_cpu;
u8 nc_ram[RAM_SIZE];
u8 nc_scratch[SCRATCH_SIZE];
u64 nc_cycles;
void (*nc_frame_hook)(void);

enum { COP0_SR = 12, COP0_CAUSE = 13, COP0_EPC = 14 };

/* Interrupts are taken when IEc (bit 0) and the hardware interrupt mask IM2 (bit 10) are set. */
#define SR_INT_ENABLE 0x401u

/* Recompiled code charges POLL_COST cycles (tools/recomp) at every function entry and backward
 * branch and calls nc_poll when its budget runs out; this is the budget handed out each time. */
#define POLL_BUDGET 4096

static u64 next_vblank = PSX_CYCLES_PER_FRAME;

/* Debugging aid: NC_TRACE_STACK=1 prints the guest call stack once per second. Recompiled
 * functions are named <module>_<address>, so the native backtrace reads as the MIPS one. */
static void trace_stack(void) {
#ifdef NC_HAVE_BACKTRACE
    static int enabled = -1;
    static unsigned frames;
    if (enabled < 0) {
        enabled = getenv("NC_TRACE_STACK") != NULL;
    }
    if (!enabled || ++frames % 60 != 0) {
        return;
    }
    void *addrs[48];
    int n = backtrace(addrs, 48);
    char **names = backtrace_symbols(addrs, n);
    NC_LOG("--- guest stack (frame %u) ---", frames);
    for (int i = 2; i < n && names != NULL; i++) {
        if (strstr(names[i], "_exe_") || strstr(names[i], "slus_") || strstr(names[i], "bios")) {
            NC_LOG("  %s", names[i]);
        }
    }
    free(names);
#endif
}
static s32 budget_start = POLL_BUDGET;

void nc_poll(CPUState *c) {
    nc_cycles += (u64)(budget_start - c->budget);

    if (nc_cycles >= next_vblank) {
        next_vblank += PSX_CYCLES_PER_FRAME;
        gpu_vblank();
        irq_raise(IRQ_VBLANK);
        trace_stack();
        if (nc_frame_hook != NULL) {
            nc_frame_hook();
        }
    }
    cdrom_tick(nc_cycles);

    if (irq_pending() && (c->cop0[COP0_SR] & SR_INT_ENABLE) == SR_INT_ENABLE &&
        !bios_in_exception()) {
        bios_exception(c);
    }

    u64 until_vblank = next_vblank - nc_cycles;
    c->budget = until_vblank < POLL_BUDGET ? (s32)until_vblank : POLL_BUDGET;
    budget_start = c->budget;
}

void nc_syscall(CPUState *c) {
    /* Psy-Q uses syscall for Enter/ExitCriticalSection, selected by $a0. */
    u32 sr = c->cop0[COP0_SR];
    switch (c->r[4]) {
    case 1: /* EnterCriticalSection: returns whether interrupts were enabled */
        c->r[2] = (sr & SR_INT_ENABLE) == SR_INT_ENABLE;
        c->cop0[COP0_SR] = sr & ~SR_INT_ENABLE;
        break;
    case 2: /* ExitCriticalSection */
        c->cop0[COP0_SR] = sr | SR_INT_ENABLE;
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
