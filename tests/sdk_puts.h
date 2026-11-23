#ifndef NC_TESTS_SDK_PUTS_H
#define NC_TESTS_SDK_PUTS_H

#include "jump_context.h"

static bool check_sdk_puts(u32 entry, bool (*routine)(CPUState *), u32 column) {
    const u32 ram = 0x80061001;
    const struct {
        u32 address;
        const char *string;
        u32 final_column;
    } cases[] = {
        {0, NULL, 9}, {ram, "", 3}, {ram, "AB", 5},
        {SCRATCH_BASE + 1, "A\tB", 9}, {0xa0061001, "A\nB", 1},
    };
    u32 saved_column = MEM_R32(column);
    bool ok = true;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0] && ok; i++) {
        if (cases[i].address)
            memcpy(nc_fast_ptr(cases[i].address), cases[i].string, strlen(cases[i].string) + 1);
        for (unsigned tight = 0; tight < 2 && ok; tight++) {
            MEM_W32(column, 3);
            CPUState state = arguments(cases[i].address, 2, 3, tight ? 10 : 4096);
            ok = compare(state, entry, routine, 9) &&
                 MEM_R32(column) == cases[i].final_column;
        }
    }
    MEM_W32(column, saved_column);
    return ok;
}

#endif
