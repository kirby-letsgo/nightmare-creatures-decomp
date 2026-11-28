/* LIBGTE InitGeom and SetGeomScreen: PSX.EXE 800219B0 and 80021A58, PSX2.EXE 80079FC8 and
 * 8007A490, the same code with other addresses. */
#ifndef NC_DECOMP_SDK_GEOM_H
#define NC_DECOMP_SDK_GEOM_H

#include "decomp/guest_ops.h"

typedef struct {
    u32 entry;     /* InitGeom, also the base of its return address */
    u32 saved_ra;  /* where InitGeom keeps ra across _patch_gte */
    u32 patch_gte; /* _patch_gte */
} GeomLayout;

/* Patch the BIOS exception handler for the GTE, enable COP2 and set the default GTE screen:
 * offsets 0, projection distance 1000, depth queue and Z scaling. */
static inline bool sdk_init_geom(CPUState *c, const GeomLayout *l) {
    if (c->budget <= 4)
        return false;
    R(1) = guest_hi(l->saved_ra);
    MEM_W32(l->saved_ra, R(31));
    R(31) = l->entry + 0x10;
    c->budget -= 4;
    nc_call(c, l->patch_gte);
    load_word_hi(c, 31, l->saved_ra);
    guest_nop(c);
    R(2) = nc_mfc0(c, 12);
    guest_nop(c);
    set_reg(c, 3, 0x40000000);
    set_reg(c, 2, R(2) | R(3));
    nc_mtc0(c, 12, R(2)); /* COP2 usable */
    guest_nop(c);
    guest_nop(c);
    static const struct {
        u8 reg;
        u32 value;
    } controls[] = {
        {29, 0x155},        /* ZSF3 */
        {30, 0x100},        /* ZSF4 */
        {26, 1000},         /* H: projection distance */
        {27, (u32)-0x1062}, /* DQA */
        {28, 0x1400000},    /* DQB */
    };
    for (unsigned i = 0; i < sizeof controls / sizeof controls[0]; i++) {
        set_reg(c, 8, controls[i].value);
        gte_write_ctrl(c, controls[i].reg, R(8));
        guest_nop(c);
        guest_nop(c);
    }
    gte_write_ctrl(c, 24, 0); /* OFX, OFY */
    guest_nop(c);
    gte_write_ctrl(c, 25, 0);
    guest_nop(c);
    guest_nop(c);
    guest_return(c);
    return true;
}

/* SetGeomScreen(h): the projection distance. */
static inline bool sdk_set_geom_screen(CPUState *c) {
    if (c->budget <= 3)
        return false;
    gte_write_ctrl(c, 26, R(4));
    c->budget -= 3;
    return true;
}

#endif
