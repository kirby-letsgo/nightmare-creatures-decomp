#include "decomp/credits.h"
#include "port/interp.h"

/* CREDITS.EXE 80018C8C. The display loop polls the first InitPAD buffer
 * after VSync. With an event due, let the generated code poll at each
 * original instruction so the pad packet can change during the read. */
bool nc_credits_skip_input(CPUState *c) {
    if (c->budget <= 34)
        return false;

    const u32 packet = 0x80024CA4;
    R(2) = 0;
    R(3) = 0xff;
    c->budget -= 11; /* status check, including the common jr and delay slot */
    if (MEM_R8(packet) == 0xff)
        return true;

    const u16 pressed = (u16)~MEM_R16(packet + 2);
    const u32 type = MEM_R8(packet + 1) >> 4;
    R(4) = pressed;
    R(3) = type;
    c->budget -= 14;
    if (type != 4) {
        c->budget -= 2;
        if (type != 7)
            return true;
    }

    R(2) = 1;
    c->budget -= 3;
    if (pressed & 0x4000) /* Cross */
        return true;
    c->budget -= 3;
    if (pressed & 0x0008) /* Start */
        return true;

    R(2) = 0;
    c->budget--;
    return true;
}

/* Build the SpuCommonAttr used by the per-frame CD fade. The caller supplies
 * independent channel levels; zero converts to 0xffff, as on the disc. */
bool nc_credits_set_cd_volume(CPUState *c) {
    if (c->budget <= 14)
        return false;

    R(29) -= 0x40;
    const u32 attr = R(29) + 0x10;
    const u32 left = (R(4) << 7) - 1;
    R(5) = (R(5) << 7) - 1;
    MEM_W32(attr, 0x2c0); /* CD left volume, right volume, and mix enable */
    R(2) = 1;
    MEM_W16(attr + 0x10, (u16)left);
    R(4) = attr;
    MEM_W32(R(29) + 0x38, R(31));
    MEM_W16(attr + 0x12, (u16)R(5));
    R(31) = 0x8001931c;
    MEM_W32(attr + 0x18, 1);
    c->budget -= 14;
    nc_call(c, 0x8001ed98); /* SpuSetCommonAttr */

    R(31) = MEM_R32(R(29) + 0x38);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(29) += 0x40;
    NC_POLL(c, NC_INSN_CYCLES);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* The credits loader builds a list of allocation records inside its arena.
 * The initial record owns a single free span immediately after its header. */
bool nc_credits_init_heap(CPUState *c) {
    if (c->budget <= 18)
        return false;

    const u32 header_size = 0x1c;
    const u32 head = (R(4) + 3u) & ~3u;
    const u32 size = R(5), gp = R(28);
    R(4) = head;
    MEM_W32(head + 0x0c, head + header_size); /* first free byte */
    R(2) = size - header_size;
    MEM_W32(gp + 0x0c, 0); /* allocation count */
    MEM_W32(gp + 0x2c, head);
    MEM_W32(head + 0x18, 0); /* next and previous records */
    MEM_W32(head + 0x14, 0);
    MEM_W32(head + 0x04, 0); /* payload pointer, requested size, occupied span */
    MEM_W32(head + 0x08, 0);
    MEM_W32(head, 0);
    MEM_W32(head + 0x10, R(2)); /* free space following this record */
    MEM_W32(gp + 0x10, 0);
    MEM_W32(gp + 0x14, size);
    c->budget -= 18;
    return true;
}

/* Locate the record whose payload equals a0. Even the sentinel is tested:
 * its null payload can match. The original does not test the head for null. */
bool nc_credits_find_allocation(CPUState *c) {
    if (c->budget <= 20)
        return false;
    R(3) = MEM_R32(R(28) + 0x2c);
    c->budget -= NC_LOAD_CYCLES;
    for (;;) {
        if (c->budget <= 17) {
            /* A later record can straddle an event. Resume with the current
             * cursor at the original loop head, after the initial head load. */
            interp_call(c, 0x800198b4);
            return true;
        }
        if (MEM_R32(R(3) + 4) == R(4)) {
            R(2) = R(3);
            c->budget -= 10;
            return true;
        }
        R(3) = MEM_R32(R(3) + 0x18);
        R(2) = 0;
        c->budget -= 13;
        if (!R(3)) {
            c->budget -= 4;
            return true;
        }
    }
}

/* Allocation record layout. Each live record owns the free span immediately
 * following its payload. The sentinel participates in best-fit selection. */
enum {
    SPAN = 0,
    PAYLOAD = 4,
    REQUESTED = 8,
    FREE_START = 12,
    FREE_SIZE = 16,
    PREV = 20,
    NEXT = 24,
    HEADER_SIZE = 28
};
enum { CURSOR = 4, NEW_RECORD = 5, BEST = 6, NEEDED = 7, REQUEST = 8 };

static void assign(CPUState *c, unsigned reg, u32 value) {
    R(reg) = value;
    NC_POLL(c, NC_INSN_CYCLES);
}
static void load(CPUState *c, unsigned reg, u32 addr) {
    R(reg) = MEM_R32(addr);
    NC_POLL(c, NC_LOAD_CYCLES);
}
static void store(CPUState *c, u32 addr, u32 value) {
    MEM_W32(addr, value);
    NC_POLL(c, NC_INSN_CYCLES);
}
/* Capture the branch condition before polling its delay slot. */
static bool branch(CPUState *c, bool taken) {
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return taken;
}

static bool better_fit(CPUState *c) {
    load(c, 2, R(CURSOR) + FREE_START);
    NC_POLL(c, NC_INSN_CYCLES);
    if (branch(c, !R(2)))
        return false;
    load(c, 3, R(CURSOR) + FREE_SIZE);
    NC_POLL(c, NC_INSN_CYCLES);
    assign(c, 2, R(3) < R(NEEDED));
    if (branch(c, R(2) != 0))
        return false;
    if (branch(c, !R(BEST)))
        return true;
    load(c, 2, R(BEST) + FREE_SIZE);
    NC_POLL(c, NC_INSN_CYCLES);
    assign(c, 2, R(3) < R(2));
    return branch(c, R(2) != 0);
}

static bool find_space(CPUState *c) {
    load(c, CURSOR, R(28) + 0x2c);
    assign(c, 2, R(REQUEST) + 3);
    assign(c, 3, ~3u);
    assign(c, 2, R(2) & R(3));
    assign(c, NEEDED, R(2) + HEADER_SIZE);
    assign(c, BEST, 0);
    do {
        if (better_fit(c))
            assign(c, BEST, R(CURSOR));
        load(c, CURSOR, R(CURSOR) + NEXT);
        NC_POLL(c, NC_INSN_CYCLES);
    } while (branch(c, R(CURSOR) != 0));
    if (branch(c, !R(BEST)))
        return false;
    /* Recheck the selected span after the scan. */
    load(c, 2, R(BEST) + FREE_SIZE);
    NC_POLL(c, NC_INSN_CYCLES);
    assign(c, 2, R(2) < R(NEEDED));
    bool fits = !R(2);
    R(3) = ~3u;
    return branch(c, fits);
}

static void insert_allocation(CPUState *c) {
    load(c, 2, R(BEST) + FREE_START);
    load(c, CURSOR, R(BEST) + NEXT);
    assign(c, 2, R(2) + 3);
    assign(c, NEW_RECORD, R(2) & R(3));
    bool has_next = R(CURSOR) != 0;
    MEM_W32(R(NEW_RECORD) + NEXT, R(CURSOR));
    if (branch(c, has_next)) {
        load(c, 2, R(BEST) + NEXT);
        NC_POLL(c, NC_INSN_CYCLES);
        store(c, R(2) + PREV, R(NEW_RECORD));
    }
    assign(c, 2, R(NEW_RECORD) + HEADER_SIZE);
    store(c, R(BEST) + NEXT, R(NEW_RECORD));
    store(c, R(NEW_RECORD) + PREV, R(BEST));
    store(c, R(NEW_RECORD) + PAYLOAD, R(2));
    store(c, R(NEW_RECORD) + REQUESTED, R(REQUEST));
    store(c, R(NEW_RECORD) + SPAN, R(NEEDED));

    load(c, 2, R(BEST) + FREE_SIZE);
    NC_POLL(c, NC_INSN_CYCLES);
    bool exact_fit = R(2) == R(NEEDED);
    R(2) = R(NEW_RECORD) + R(NEEDED);
    if (branch(c, exact_fit)) {
        store(c, R(NEW_RECORD) + FREE_START, 0);
        store(c, R(NEW_RECORD) + FREE_SIZE, 0);
    } else {
        store(c, R(NEW_RECORD) + FREE_START, R(2));
        load(c, 2, R(BEST) + FREE_SIZE);
        NC_POLL(c, NC_INSN_CYCLES);
        assign(c, 2, R(2) - R(NEEDED));
        MEM_W32(R(NEW_RECORD) + FREE_SIZE, R(2));
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    }

    load(c, 3, R(28) + 0x10);
    store(c, R(BEST) + FREE_START, 0);
    store(c, R(BEST) + FREE_SIZE, 0);
    load(c, 2, R(NEW_RECORD) + PAYLOAD);
    load(c, 4, R(28) + 0x0c);
    assign(c, 3, R(3) + R(REQUEST));
    store(c, R(28) + 0x10, R(3));
    load(c, 3, R(28) + 0x14);
    assign(c, 4, R(4) + 1);
    store(c, R(28) + 0x0c, R(4));
    assign(c, 3, R(3) - R(REQUEST));
    store(c, R(28) + 0x14, R(3));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
}

bool nc_credits_allocate(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(29) -= 0x18;
    R(REQUEST) = R(4);
    MEM_W32(R(29) + 0x10, R(31));
    c->budget -= 4;
    if (!R(REQUEST)) {
        assign(c, 2, 0);
    } else if (find_space(c)) {
        insert_allocation(c);
    } else {
        assign(c, 4, 0x80020000);
        assign(c, 4, R(4) - 0x7f48); /* out-of-memory message */
        R(31) = 0x800197cc;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x80019a60);
        assign(c, 2, 0);
    }
    load(c, 31, R(29) + 0x10);
    assign(c, 29, R(29) + 0x18);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

static void unlink_allocation(CPUState *c) {
    enum { RECORD = 5, OWNER = 6 };
    load(c, OWNER, R(RECORD) + PREV);
    load(c, 2, R(RECORD) + NEXT);
    NC_POLL(c, NC_INSN_CYCLES);
    bool has_next = R(2) != 0;
    MEM_W32(R(OWNER) + NEXT, R(2));
    if (branch(c, has_next)) {
        load(c, 2, R(RECORD) + NEXT);
        NC_POLL(c, NC_INSN_CYCLES);
        store(c, R(2) + PREV, R(OWNER));
    }

    /* Combine this record, its payload and trailing free space with any
     * free span already owned by its predecessor. */
    load(c, 2, R(OWNER) + FREE_START);
    NC_POLL(c, NC_INSN_CYCLES);
    if (branch(c, R(2) == 0)) {
        store(c, R(OWNER) + FREE_START, R(RECORD));
        load(c, 2, R(RECORD) + SPAN);
        load(c, 3, R(RECORD) + FREE_SIZE);
        NC_POLL(c, NC_INSN_CYCLES);
        assign(c, 2, R(2) + R(3));
        MEM_W32(R(OWNER) + FREE_SIZE, R(2));
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } else {
        load(c, 2, R(RECORD) + SPAN);
        load(c, 4, R(RECORD) + FREE_SIZE);
        load(c, 3, R(OWNER) + FREE_SIZE);
        assign(c, 2, R(2) + R(4));
        assign(c, 3, R(3) + R(2));
        store(c, R(OWNER) + FREE_SIZE, R(3));
    }

    assign(c, 4, R(RECORD));
    load(c, 2, R(28) + 0x0c);
    load(c, 5, R(4) + REQUESTED);
    load(c, 3, R(28) + 0x14);
    load(c, 6, R(4) + REQUESTED);
    assign(c, 2, R(2) - 1);
    assign(c, 3, R(3) + R(6));
    store(c, R(28) + 0x0c, R(2));
    load(c, 2, R(28) + 0x10);
    assign(c, 6, HEADER_SIZE);
    store(c, R(28) + 0x14, R(3));
    assign(c, 2, R(2) - R(5));
    store(c, R(28) + 0x10, R(2));
    R(31) = 0x800198a0;
    R(5) = 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001995c); /* zero the released header */
}

bool nc_credits_free(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    c->budget -= 3;
    if (R(4)) {
        R(31) = 0x800197f4;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x800198b0); /* find allocation by payload address */
        assign(c, 5, R(2));
        if (branch(c, R(5) != 0))
            unlink_allocation(c);
    }
    load(c, 31, R(29) + 0x10);
    assign(c, 29, R(29) + 0x18);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

bool nc_credits_allocation_size(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    c->budget -= 3;
    bool found = false;
    if (R(4)) {
        R(31) = 0x80019900;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, 0x800198b0);
        found = branch(c, R(2) != 0);
    }
    if (found) {
        load(c, 2, R(2) + REQUESTED);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } else {
        assign(c, 2, 0);
    }
    load(c, 31, R(29) + 0x10);
    assign(c, 29, R(29) + 0x18);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}
