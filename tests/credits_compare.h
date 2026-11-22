#ifndef NC_TESTS_CREDITS_COMPARE_H
#define NC_TESTS_CREDITS_COMPARE_H

#include "port/interp.h"
#include "port/runtime.h"
#include "port/savestate.h"
#include <stdlib.h>

/* CPU serialization includes all registers, RAM, scratchpad, the scheduler's
 * budget and global clock. Restore the same clock before interpreting. */
static bool compare(CPUState start, u32 entry, bool (*routine)(CPUState *), int threshold) {
    StateIO initial = {0}, native = {0}, guest = {0};
    nc_cpu = start;
    nc_cpu.budget = threshold;
    cpu_serialize(&initial);
    bool ok = !initial.error && !routine(&nc_cpu);
    cpu_serialize(&native);
    ok = ok && !native.error && initial.len == native.len &&
         !memcmp(initial.buf, native.buf, initial.len);
    native.len = 0;
    nc_cpu.budget = start.budget;
    ok = ok && routine(&nc_cpu);
    cpu_serialize(&native);

    initial.loading = true;
    cpu_serialize(&initial);
    nc_cpu.budget = start.budget;
    interp_call(&nc_cpu, entry);
    cpu_serialize(&guest);
    ok = ok && !initial.error && !guest.error && !native.error && native.len == guest.len &&
         !memcmp(native.buf, guest.buf, guest.len);
    free(initial.buf);
    free(native.buf);
    free(guest.buf);
    return ok;
}

#endif
