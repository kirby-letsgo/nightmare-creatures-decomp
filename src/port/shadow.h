/* Shadow mode (NC_SHADOW=1, builds with the native registry): every handwritten routine called
 * through the dispatcher is checked against the interpreter on the live game state. */
#ifndef NC_PORT_SHADOW_H
#define NC_PORT_SHADOW_H

#include "port/recomp.h"

#include <stdbool.h>

/* Nonzero until the first call finds NC_SHADOW unset; the registry wrappers test it first. */
extern int nc_shadow_enabled;
/* A native run that will be discarded is in progress: host-visible effects must not happen. */
extern bool nc_shadow_guard;
/* The reference run is in progress: nc_lookup finds no native routines. */
extern bool nc_shadow_reference;

/* Reads NC_SHADOW*; call early so the exit summary runs after other exit handlers. */
void nc_shadow_init(void);
/* Runs `native` and the original instructions at `addr` from the same state, compares the two
 * and keeps the interpreter's result. Returns false when the call was not shadowed (disabled,
 * nested, over the limit, or the native run needed a host effect): run it normally. */
bool nc_shadow_call(CPUState *c, u32 addr, const char *name, bool (*native)(CPUState *c));
_Noreturn void nc_shadow_abort(const char *why);

/* Placed before effects outside the snapshot (frame output, hooks, Exec, memory card files). */
#define NC_SHADOW_HOST_EFFECT(why)                                                                 \
    do {                                                                                           \
        if (nc_shadow_guard) {                                                                     \
            nc_shadow_abort(why);                                                                  \
        }                                                                                          \
    } while (0)

#endif
