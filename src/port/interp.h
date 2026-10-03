/* MIPS interpreter, used for game code without a native (recompiled/decompiled) function. */
#ifndef NC_PORT_INTERP_H
#define NC_PORT_INTERP_H

#include "port/recomp.h"

#include <stdbool.h>

/* Address patches and hooks from config/patches.txt and config/hooks.txt, as a table generated
 * at build time (tools/gen_patch_table.py). */
#define NC_PATCH_LAUNCHER (-2)

typedef struct NcAddrPatch {
    int module; /* NcModule, or NC_PATCH_LAUNCHER */
    u32 addr;
    void (*hook)(CPUState *c); /* called before the instruction, or NULL */
    const bool *flag;          /* instruction skipped while *flag, or NULL */
} NcAddrPatch;

extern const NcAddrPatch nc_addr_patches[];
extern const unsigned nc_addr_patch_count;

/* Runs guest code from `addr` until it returns to the current $ra with the stack restored. */
void interp_call(CPUState *c, u32 addr);

#endif
