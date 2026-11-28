/* The Psy-Q TIM and TMD readers (OpenTIM, ReadTIM, OpenTMD, ReadTMD and their get_tim_addr and
 * get_tmd_addr helpers). PSX.EXE has them at 8002020C-80020824; PSX2.EXE (80078824) and the
 * three FMV players carry the same code with other addresses. */
#ifndef NC_DECOMP_SDK_TMD_H
#define NC_DECOMP_SDK_TMD_H

#include "decomp/guest_ops.h"

typedef struct {
    u32 tim_cursor; /* next TIM in the open TIM file */
    /* The open TMD object: vertex, normal and primitive cursors, and its primitive count. */
    u32 vertices, normals, primitives, count;
    /* Routine entries, also the base of their return addresses. */
    u32 read_tim, tim_info, open_tmd, read_tmd, tmd_info;
    /* Callees. */
    u32 graph_debug, print, unpack_packet;
    u32 tim_messages[3]; /* id, flag and address traces (GetGraphDebug() == 2) */
    u32 tmd_messages[5]; /* header, object, vertex, normal and primitive traces */
} TmdLayout;

/* OpenTIM: remember where the TIM file starts. */
static inline bool sdk_open_tim(CPUState *c, const TmdLayout *l) {
    if (c->budget <= 4)
        return false;
    R(1) = guest_hi(l->tim_cursor);
    MEM_W32(l->tim_cursor, R(4));
    R(2) = 0;
    c->budget -= 4;
    return true;
}

/* ReadTIM: describe the next TIM in a0 and advance past it. Returns a0, or 0 at the end. */
static inline bool sdk_read_tim(CPUState *c, const TmdLayout *l) {
    if (c->budget <= 10)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(4);
    R(4) = MEM_R32(l->tim_cursor);
    MEM_W32(R(29) + 0x14, R(31));
    R(31) = l->read_tim + 0x20;
    R(5) = R(16);
    c->budget -= 10;
    nc_call(c, l->tim_info);
    set_reg(c, 3, R(2));
    set_reg(c, 2, UINT32_MAX);
    const bool invalid = R(3) == R(2);
    R(3) <<= 2;
    if (guest_branch(c, invalid)) {
        set_reg(c, 2, 0);
    } else {
        load_word_hi(c, 4, l->tim_cursor);
        guest_nop(c);
        set_reg(c, 3, R(3) + R(4));
        store_word_at(c, l->tim_cursor, R(3));
        R(2) = R(16);
        guest_branch(c, true);
    }
    guest_leave(c, 0x18, 0x14, 16, 16, 0x10);
    return true;
}

/* Print a trace when the graphics debug level is 2. Each check calls GetGraphDebug. */
static inline bool sdk_trace_enabled(CPUState *c, const TmdLayout *l, u32 resume) {
    guest_call(c, l->graph_debug, resume);
    return R(2) != R(18);
}

/* get_tim_addr(tim a0, info a1): fill in the GsIMAGE-style description of one TIM and return
 * its size in words, or -1 when a0 is not a TIM. */
static inline bool sdk_tim_info(CPUState *c, const TmdLayout *l) {
    const u32 entry = l->tim_info;
    if (c->budget <= 13)
        return false;
    R(29) -= 0x20;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(4);
    MEM_W32(R(29) + 0x14, R(17));
    R(17) = R(5);
    MEM_W32(R(29) + 0x1c, R(31));
    MEM_W32(R(29) + 0x18, R(18));
    R(3) = MEM_R32(R(16));
    R(2) = 0x10;
    const bool tim = R(3) == R(2);
    R(16) += 4;
    c->budget -= 13;
    if (!tim) {
        R(2) = UINT32_MAX;
        guest_branch(c, true);
    } else {
        load_word(c, 2, R(16)); /* flag word */
        set_reg(c, 16, R(16) + 4);
        MEM_W32(R(17), R(2));
        guest_call(c, l->graph_debug, entry + 0x44);
        set_reg(c, 18, 2);
        if (!guest_branch(c, R(2) != R(18))) {
            set_address(c, 4, l->tim_messages[0]);
            R(5) = 0x10;
            guest_call(c, l->print, entry + 0x60);
        }
        if (!guest_branch(c, sdk_trace_enabled(c, l, entry + 0x68))) {
            load_word(c, 5, R(17));
            set_address(c, 4, l->tim_messages[1]);
            guest_call(c, l->print, entry + 0x84);
        }
        if (!guest_branch(c, sdk_trace_enabled(c, l, entry + 0x8c))) {
            set_address(c, 4, l->tim_messages[2]);
            R(5) = R(16);
            guest_call(c, l->print, entry + 0xa4);
        }

        /* An optional palette block, then the pixel block: length, rectangle, data. */
        load_word(c, 2, R(17));
        guest_nop(c);
        set_reg(c, 2, R(2) & 8);
        const bool palette = R(2) != 0;
        R(2) = R(16) + 4;
        if (guest_branch(c, palette)) {
            load_word(c, 3, R(16));
            store_word(c, R(17) + 4, R(2));
            set_reg(c, 2, R(16) + 0xc);
            store_word(c, R(17) + 8, R(2));
            set_reg(c, 4, R(3) >> 2);
            set_reg(c, 2, R(4) << 2);
            R(16) += R(2);
            guest_branch(c, true);
        } else {
            set_reg(c, 4, 0);
            store_word(c, R(17) + 4, 0);
            store_word(c, R(17) + 8, 0);
        }
        load_word(c, 2, R(16));
        set_reg(c, 3, R(16) + 4);
        store_word(c, R(17) + 0xc, R(3));
        set_reg(c, 3, R(16) + 0xc);
        store_word(c, R(17) + 0x10, R(3));
        set_reg(c, 2, R(2) >> 2);
        set_reg(c, 2, R(2) + 2);
        set_reg(c, 2, R(4) + R(2));
    }
    guest_leave(c, 0x20, 0x1c, 16, 18, 0x10);
    return true;
}

/* OpenTMD(tmd a0, object a1): set the cursors of one object and return its primitive count. */
static inline bool sdk_open_tmd(CPUState *c, const TmdLayout *l) {
    if (c->budget <= 10)
        return false;
    R(29) -= 0x20;
    R(2) = l->normals;
    R(6) = l->primitives;
    R(7) = l->vertices;
    MEM_W32(R(29) + 0x18, R(31));
    R(31) = l->open_tmd + 0x28;
    MEM_W32(R(29) + 0x10, R(2));
    c->budget -= 10;
    nc_call(c, l->tmd_info);
    store_word_at(c, l->count, R(2));
    guest_leave_ra(c, 0x20, 0x18);
    return true;
}

/* Address of the 28-byte object entry s1 in the object table at s2. */
static inline void sdk_object_entry(CPUState *c, unsigned reg) {
    set_reg(c, reg, R(reg) - R(17));
    set_reg(c, reg, R(reg) << 2);
    set_reg(c, reg, R(reg) + R(18));
}

/* get_tmd_addr(tmd a0, object a1, &primitives a2, &vertices a3, &normals): convert the object's
 * offsets to addresses and return its primitive count. */
static inline bool sdk_tmd_info(CPUState *c, const TmdLayout *l) {
    const u32 entry = l->tmd_info;
    if (c->budget <= 17)
        return false;
    R(29) -= 0x38;
    MEM_W32(R(29) + 0x30, R(22));
    R(22) = MEM_R32(R(29) + 0x48);
    MEM_W32(R(29) + 0x18, R(16));
    R(16) = R(4);
    MEM_W32(R(29) + 0x1c, R(17));
    R(17) = R(5);
    MEM_W32(R(29) + 0x2c, R(21));
    R(21) = R(6);
    MEM_W32(R(29) + 0x28, R(20));
    R(20) = R(7);
    MEM_W32(R(29) + 0x34, R(31));
    MEM_W32(R(29) + 0x24, R(19));
    R(31) = entry + 0x3c;
    MEM_W32(R(29) + 0x20, R(18));
    c->budget -= 17;
    nc_call(c, l->graph_debug);
    set_reg(c, 19, 2);
    bool skip = R(2) != R(19);
    R(18) = R(16) + 0xc; /* object table */
    if (!guest_branch(c, skip)) {
        set_address(c, 4, l->tmd_messages[0]);
        guest_call(c, l->print, entry + 0x58);
    }
    guest_call(c, l->graph_debug, entry + 0x60);
    if (!guest_branch(c, R(2) != R(19))) {
        store_word(c, R(29) + 0x10, R(17));
        load_word(c, 5, R(16));
        load_word(c, 6, R(16) + 4);
        load_word(c, 7, R(16) + 8);
        set_address(c, 4, l->tmd_messages[1]);
        guest_call(c, l->print, entry + 0x88);
    }
    /* Vertex and normal traces: two words of the object entry each. */
    for (unsigned i = 0; i < 2; i++) {
        guest_call(c, l->graph_debug, entry + 0x90 + 0x34 * i);
        skip = R(2) != R(19);
        R(2) = R(17) << 3;
        if (!guest_branch(c, skip)) {
            sdk_object_entry(c, 2);
            load_word(c, 5, R(2) + 8 * i);
            load_word(c, 6, R(2) + 8 * i + 4);
            set_address(c, 4, l->tmd_messages[2 + i]);
            guest_call(c, l->print, entry + 0xbc + 0x34 * i);
        }
    }
    guest_call(c, l->graph_debug, entry + 0xf8);
    skip = R(2) != R(19);
    R(3) = R(17) << 3;
    if (!guest_branch(c, skip)) {
        set_reg(c, 2, R(17) << 3);
        sdk_object_entry(c, 2);
        load_word(c, 5, R(2) + 0x10);
        load_word(c, 6, R(2) + 0x14);
        set_address(c, 4, l->tmd_messages[4]);
        guest_call(c, l->print, entry + 0x128);
        set_reg(c, 3, R(17) << 3);
    }

    sdk_object_entry(c, 3);
    static const struct {
        u8 offset, out;
    } tops[3] = {{0x00, 20}, {0x08, 22}, {0x10, 21}}; /* vertices, normals, primitives */
    for (unsigned i = 0; i < 3; i++) {
        load_word(c, 2, R(3) + tops[i].offset);
        guest_nop(c);
        set_reg(c, 2, R(18) + R(2));
        store_word(c, R(tops[i].out), R(2));
    }
    load_word(c, 2, R(3) + 0x14); /* primitive count */
    guest_leave(c, 0x38, 0x34, 16, 22, 0x18);
    return true;
}

/* lhu v0, index(s0); nop; sll v0, v0, 3; addu v0, v0, base: an SVECTOR table entry. */
static inline void sdk_tmd_entry(CPUState *c, u32 index, unsigned base) {
    load_half(c, 2, R(16) + index);
    guest_nop(c);
    set_reg(c, 2, R(2) << 3);
    set_reg(c, 2, R(2) + R(base));
}

/* Copy three SVECTORs into the packet at s0 + out, one halfword at a time. The index of each
 * entry is reloaded for every component; the next entry is looked up before each store. */
static inline void sdk_tmd_copy(CPUState *c, u32 index, unsigned base, u32 out, unsigned steps) {
    for (unsigned t = 0; t < steps; t++) {
        load_half(c, 3, R(2) + 2 * (t % 3));
        sdk_tmd_entry(c, index + 2 * ((t + 1) / 3), base);
        store_half(c, R(16) + out + 8 * (t / 3) + 2 * (t % 3), R(3));
    }
}

/* ReadTMD(packet a0): unpack the next primitive and copy its four normals and four vertices
 * into the packet. Returns a0, or 0 when unpack_packet fails. */
static inline bool sdk_read_tmd(CPUState *c, const TmdLayout *l) {
    enum { VERTICES = 5, NORMALS = 4 };
    if (c->budget <= 10)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(4);
    R(4) = MEM_R32(l->primitives);
    MEM_W32(R(29) + 0x14, R(31));
    R(31) = l->read_tmd + 0x20;
    R(5) = R(16);
    c->budget -= 10;
    nc_call(c, l->unpack_packet);
    set_reg(c, 6, R(2)); /* primitive size in words */
    const bool failed = (s32)R(6) < 0;
    R(2) = 0;
    if (!guest_branch(c, failed)) {
        load_word_hi(c, VERTICES, l->vertices);
        load_half(c, 2, R(16) + 0x70);
        load_word_hi(c, NORMALS, l->normals);
        set_reg(c, 2, R(2) << 3);
        set_reg(c, 2, R(2) + R(NORMALS));
        store_word(c, R(16) + 0x60, R(VERTICES));
        store_word(c, R(16) + 0x64, R(NORMALS));

        /* Normals 0-2 (indices at 0x70) into 0x40, then the last component and normal 3,
         * with the primitive cursor advanced in between. */
        sdk_tmd_copy(c, 0x70, NORMALS, 0x40, 8);
        load_half(c, 3, R(16) + 0x76);
        load_half(c, 2, R(2) + 4);
        set_reg(c, 3, R(3) << 3);
        set_reg(c, 3, R(3) + R(NORMALS));
        store_half(c, R(16) + 0x54, R(2));
        load_word_hi(c, 2, l->primitives);
        load_half(c, 3, R(3));
        set_reg(c, 2, R(6) + R(2));
        store_word_at(c, l->primitives, R(2));
        store_half(c, R(16) + 0x58, R(3));
        sdk_tmd_entry(c, 0x76, NORMALS);
        load_half(c, 3, R(2) + 2);
        sdk_tmd_entry(c, 0x76, NORMALS);
        store_half(c, R(16) + 0x5a, R(3));
        load_half(c, 3, R(2) + 4);
        sdk_tmd_entry(c, 0x68, VERTICES);
        store_half(c, R(16) + 0x5c, R(3));

        /* Vertices 0-2 (indices at 0x68) into 0x20, then vertex 3. */
        sdk_tmd_copy(c, 0x68, VERTICES, 0x20, 9);
        load_half(c, 2, R(2));
        guest_nop(c);
        store_half(c, R(16) + 0x38, R(2));
        sdk_tmd_entry(c, 0x6e, VERTICES);
        load_half(c, 3, R(2) + 2);
        sdk_tmd_entry(c, 0x6e, VERTICES);
        store_half(c, R(16) + 0x3a, R(3));
        load_half(c, 3, R(2) + 4);
        set_reg(c, 2, R(16));
        store_half(c, R(2) + 0x3c, R(3));
    }
    guest_leave(c, 0x18, 0x14, 16, 16, 0x10);
    return true;
}

#endif
