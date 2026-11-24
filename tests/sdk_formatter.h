#ifndef NC_TESTS_SDK_FORMATTER_H
#define NC_TESTS_SDK_FORMATTER_H

#include "jump_context.h"
#include <stdio.h>

static bool check_format_output(u32 entry, u32 put_char, bool (*routine)(CPUState *)) {
    enum { fmt = 0x80060600, args = fmt + 128, output = args + 64,
           sample = output + 128, count = sample + 32 };
    const struct {
        const char *format, *text;
        u32 args[6], result;
    } cases[] = {
        {NULL, "", {0}, 0}, {"", "", {0}, 0},
        {"plain%", "plain", {0}, 0}, {"a%%b%?%z", "a%b?z", {0}, 3},
        {"%d|%D|%i", "-2147483648|0|42", {0x80000000, 0, 42}, 14},
        {"%+ d % d", "+3  4", {3, 4}, 4},
        {"%hd:%hu:%lo", "-2:4294967294:10", {0xfffe, 0xfffe, 8}, 14},
        {"%#o:%#o:%O", "011:0:17", {9, 0, 15}, 6},
        {"%#06x|%#X|%x", "0x000f|0XAB|ab", {15, 171, 171}, 12},
        {"%p %U", "abcd 4294967295", {0xabcd, UINT32_MAX}, 14},
        {"%.3s:%s", "(nu:(null)", {0, 0}, 9},
        {"%#hx|%#ho", "0xfffffffe|037777777776", {0xfffe, 0xfffe}, 22},
        {"%12.4d", "        0007", {7}, 12},
        {"%*.*s", "abc    ", {(u32)-7, 3, sample}, 7},
        {"%*.*s", " abcdef", {7, (u32)-5, sample}, 7},
        {"%.12s:%.0d:%#.0x", "abcdef::", {sample, 0, 0}, 6},
        {"%-4c%.s%Lu", "A   9", {65, sample, 9}, 5},
        {"lit%c%n%hn", "litZ", {90, count, count + 4}, 1},
        {"%3\x81%.\x82", "\x81\x82", {0}, 2},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* Record every putchar argument in guest RAM, including field padding. */
    MEM_W32(put_char, 0xa3040000);
    MEM_W32(put_char + 4, 0x03e00008);
    MEM_W32(put_char + 8, 0x27180001);
    memcpy(nc_fast_ptr(sample), "abcdef", 7);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        if (cases[i].format)
            strcpy((char *)nc_fast_ptr(fmt), cases[i].format);
        for (unsigned j = 0; j < 6; j++)
            MEM_W32(args + 4 * j, cases[i].args[j]);
        memset(nc_fast_ptr(output), 0, 128);
        memset(nc_fast_ptr(count), 0xff, 8);
        CPUState state = arguments(1, cases[i].format ? fmt : 0, args, i & 1 ? 2 : 8192);
        state.r[24] = output;
        ok = compare(state, entry, routine, 1) &&
             nc_cpu.r[2] == cases[i].result &&
             nc_cpu.r[24] == output + strlen(cases[i].text) &&
             !memcmp(nc_fast_ptr(output), cases[i].text, strlen(cases[i].text) + 1);
        if (cases[i].args[1] == count)
            ok = ok && MEM_R32(count) == 1 && MEM_R32(count + 4) == 0xffff0001;
        if (!ok)
            fprintf(stderr, "prnt case %u returned %u and emitted [%s]\n",
                    i, nc_cpu.r[2], (char *)nc_fast_ptr(output));
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif
