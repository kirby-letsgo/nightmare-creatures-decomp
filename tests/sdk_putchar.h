#ifndef NC_TESTS_SDK_PUTCHAR_H
#define NC_TESTS_SDK_PUTCHAR_H

#include "jump_context.h"

static bool check_sdk_putchar(u32 entry, bool (*routine)(CPUState *), u32 column) {
    const u32 saved = MEM_R32(column);
    const struct {
        u32 argument, initial_column, final_column;
    } cases[] = {
        {'A', UINT32_MAX, 0}, {0x123400ff, 3, 3}, {'\r', 6, 6}, {'\n', 5, 0},
        {'\t', 7, 8}, {'\t', 8, 16}, {0x10009, UINT32_MAX, 0},
    };
    bool ok = true;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0] && ok; i++)
        for (unsigned tight = 0; tight < 2 && ok; tight++) {
            MEM_W32(column, cases[i].initial_column);
            CPUState state = arguments(cases[i].argument, 2, 3, tight ? 12 : 4096);
            ok = compare(state, entry, routine, 11) &&
                 MEM_R32(column) == cases[i].final_column &&
                 MEM_R8(state.r[29] - 16) == (u8)cases[i].argument;
        }
    MEM_W32(column, saved);
    return ok;
}

#endif
