/* The Psy-Q __start of each overlay (CREDITS.EXE 800232F4, PSX.EXE 80025C70, ...): publish the
 * heap bounds, set gp and jump to main. */
#ifndef NC_DECOMP_SDK_START_H
#define NC_DECOMP_SDK_START_H

#include "port/recomp.h"

typedef struct {
    u32 memory_size, stack_size; /* configured sizes (_ramsize, _stacksize) */
    u32 heap_start;              /* first byte after BSS */
    u32 heap_size, heap_base;    /* where the heap bounds are published */
    u32 gp, main;
} StartLayout;

static inline bool sdk_start(CPUState *c, const StartLayout *l) {
    const int cycles = 24 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    if (c->budget <= cycles)
        return false;

    c->budget -= 5;                 /* BSS bounds into t0/t1, then the first address */
    R(8) = MEM_R32(l->memory_size); /* configured memory size */
    R(10) = l->heap_start & 0x1fffffffu;
    c->budget -= NC_LOAD_CYCLES + 6;
    R(9) = MEM_R32(l->stack_size); /* reserved stack size */
    R(12) = 0x80000000;
    R(11) = R(8) - R(9) - R(10);
    R(1) = (l->heap_size + 0x8000u) & 0xffff0000u;
    c->budget -= NC_LOAD_CYCLES + 5;
    MEM_W32(l->heap_size, R(11));
    R(10) |= R(12);
    R(1) = (l->heap_base + 0x8000u) & 0xffff0000u;
    c->budget -= 3;
    MEM_W32(l->heap_base, R(10));
    R(28) = l->gp;
    c->budget -= 5; /* store, gp setup, jump and delay slot */
    nc_jump(c, l->main);
    return true;
}

#endif
