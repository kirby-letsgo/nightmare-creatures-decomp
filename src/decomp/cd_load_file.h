/* Load a whole file from the disc into the record heap: CREDITS.EXE 80018E28 and PSX.EXE
 * 800197B0, the same code with other addresses and gp offsets. */
#ifndef NC_DECOMP_CD_LOAD_FILE_H
#define NC_DECOMP_CD_LOAD_FILE_H

#include "decomp/guest_ops.h"

typedef struct {
    u32 entry;  /* also the base of the return addresses */
    u32 prefix; /* gp offset of the root-directory prefix and terminator ("\\", 0) */
    u32 suffix; /* ";1" */
    u32 strcat, search_file, allocate, read_sectors;
} FileLoadLayout;

/* Build the ISO path, find its directory entry, and read the file into a
 * sector-aligned allocation. The result is the allocation even if the
 * lower-level read fails. */
static inline bool cd_load_file(CPUState *c, const FileLoadLayout *l) {
    if (c->budget <= 11)
        return false;
    R(29) -= 0x68;
    R(5) = R(4);
    MEM_W32(R(29) + 0x60, R(16));
    R(16) = R(29) + 0x38;
    R(2) = MEM_R16(R(28) + l->prefix); /* root-directory prefix and terminator */
    R(4) = R(16);
    MEM_W32(R(29) + 0x64, R(31));
    R(31) = l->entry + 0x24;
    MEM_W16(R(29) + 0x38, (u16)R(2));
    c->budget -= 11;
    nc_call(c, l->strcat); /* strcat(filename) */
    set_address(c, 5, l->suffix);
    R(4) = R(16);
    guest_call(c, l->strcat, l->entry + 0x34);

    set_reg(c, 4, R(29) + 0x20);
    bool retry;
    do {
        R(5) = R(29) + 0x38;
        guest_call(c, l->search_file, l->entry + 0x40); /* CdSearchFile */
        retry = R(2) == 0;
        R(4) = R(29) + 0x20;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } while (retry);

    load_word(c, 2, R(29) + 0x24); /* byte length in CdlFILE */
    NC_POLL(c, NC_INSN_CYCLES);
    set_reg(c, 4, R(2) + 2047);
    const bool negative = (s32)R(4) < 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (negative)
        set_reg(c, 4, R(2) + 4094);
    set_reg(c, 4, (u32)((s32)R(4) >> 11));
    R(4) <<= 11;
    guest_call(c, l->allocate, l->entry + 0x6c);

    /* CdlFILE is passed by value: position and length in a2/a3, the
     * directory name in four stack words. Load all four before storing. */
    for (unsigned i = 0; i < 4; i++)
        load_word(c, 3 + i, R(29) + 0x28 + 4 * i);
    for (unsigned i = 0; i < 4; i++) {
        MEM_W32(R(29) + 0x10 + 4 * i, R(3 + i));
        NC_POLL(c, NC_INSN_CYCLES);
    }
    set_reg(c, 4, R(29) + 0x38);
    set_reg(c, 16, R(2));
    load_word(c, 6, R(29) + 0x20);
    load_word(c, 7, R(29) + 0x24);
    R(5) = R(16);
    guest_call(c, l->read_sectors, l->entry + 0xa4);
    set_reg(c, 2, R(16));
    load_word(c, 31, R(29) + 0x64);
    load_word(c, 16, R(29) + 0x60);
    set_reg(c, 29, R(29) + 0x68);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

#endif
