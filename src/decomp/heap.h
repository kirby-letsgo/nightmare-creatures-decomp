/* The record allocator built into CREDITS.EXE (80019454-80019928) and PSX.EXE
 * (80019EDC-8001A3B0). Both copies are the same code with other addresses and gp offsets.
 * The allocator keeps a list of allocation records inside its arena. */
#ifndef NC_DECOMP_HEAP_H
#define NC_DECOMP_HEAP_H

#include "port/interp.h"
#include "port/recomp.h"

typedef struct {
    u32 count, used, available, head; /* gp offsets of the heap globals */
    /* Routine entries, also the base of their return addresses. */
    u32 init, resize, allocate, release, find, size;
    /* Callees: memset, memcpy, printf and the out-of-memory message. */
    u32 fill, copy, print, out_of_memory;
} HeapLayout;

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

/* Callee-saved registers used across the resize helper's nested calls. */
enum { BLOCK = 16, NEW_SIZE = 17, EXTENT = 18, OLD_PTR = 19, DIFFERENCE = 20 };

static inline void heap_assign(CPUState *c, unsigned reg, u32 value) {
    R(reg) = value;
    NC_POLL(c, NC_INSN_CYCLES);
}
static inline void heap_load(CPUState *c, unsigned reg, u32 addr) {
    R(reg) = MEM_R32(addr);
    NC_POLL(c, NC_LOAD_CYCLES);
}
static inline void heap_store(CPUState *c, u32 addr, u32 value) {
    MEM_W32(addr, value);
    NC_POLL(c, NC_INSN_CYCLES);
}
/* Capture the branch condition before polling its delay slot. */
static inline bool heap_branch(CPUState *c, bool taken) {
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return taken;
}
static inline void heap_call(CPUState *c, u32 target, u32 resume) {
    R(31) = resume;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, target);
}

/* The initial record owns a single free span immediately after its header. */
static inline bool heap_init(CPUState *c, const HeapLayout *l) {
    if (c->budget <= 18)
        return false;

    const u32 head = (R(4) + 3u) & ~3u;
    const u32 size = R(5), gp = R(28);
    R(4) = head;
    MEM_W32(head + 0x0c, head + HEADER_SIZE); /* first free byte */
    R(2) = size - HEADER_SIZE;
    MEM_W32(gp + l->count, 0); /* allocation count */
    MEM_W32(gp + l->head, head);
    MEM_W32(head + 0x18, 0); /* next and previous records */
    MEM_W32(head + 0x14, 0);
    MEM_W32(head + 0x04, 0); /* payload pointer, requested size, occupied span */
    MEM_W32(head + 0x08, 0);
    MEM_W32(head, 0);
    MEM_W32(head + 0x10, R(2)); /* free space following this record */
    MEM_W32(gp + l->used, 0);
    MEM_W32(gp + l->available, size);
    c->budget -= 18;
    return true;
}

/* Locate the record whose payload equals a0. Even the sentinel is tested:
 * its null payload can match. The original does not test the head for null. */
static inline bool heap_find(CPUState *c, const HeapLayout *l) {
    if (c->budget <= 20)
        return false;
    R(3) = MEM_R32(R(28) + l->head);
    c->budget -= NC_LOAD_CYCLES;
    for (;;) {
        if (c->budget <= 17) {
            /* A later record can straddle an event. Resume with the current
             * cursor at the original loop head, after the initial head load. */
            interp_call(c, l->find + 4);
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

static inline bool heap_better_fit(CPUState *c) {
    heap_load(c, 2, R(CURSOR) + FREE_START);
    NC_POLL(c, NC_INSN_CYCLES);
    if (heap_branch(c, !R(2)))
        return false;
    heap_load(c, 3, R(CURSOR) + FREE_SIZE);
    NC_POLL(c, NC_INSN_CYCLES);
    heap_assign(c, 2, R(3) < R(NEEDED));
    if (heap_branch(c, R(2) != 0))
        return false;
    if (heap_branch(c, !R(BEST)))
        return true;
    heap_load(c, 2, R(BEST) + FREE_SIZE);
    NC_POLL(c, NC_INSN_CYCLES);
    heap_assign(c, 2, R(3) < R(2));
    return heap_branch(c, R(2) != 0);
}

static inline bool heap_find_space(CPUState *c, const HeapLayout *l) {
    heap_load(c, CURSOR, R(28) + l->head);
    heap_assign(c, 2, R(REQUEST) + 3);
    heap_assign(c, 3, ~3u);
    heap_assign(c, 2, R(2) & R(3));
    heap_assign(c, NEEDED, R(2) + HEADER_SIZE);
    heap_assign(c, BEST, 0);
    do {
        if (heap_better_fit(c))
            heap_assign(c, BEST, R(CURSOR));
        heap_load(c, CURSOR, R(CURSOR) + NEXT);
        NC_POLL(c, NC_INSN_CYCLES);
    } while (heap_branch(c, R(CURSOR) != 0));
    if (heap_branch(c, !R(BEST)))
        return false;
    /* Recheck the selected span after the scan. */
    heap_load(c, 2, R(BEST) + FREE_SIZE);
    NC_POLL(c, NC_INSN_CYCLES);
    heap_assign(c, 2, R(2) < R(NEEDED));
    bool fits = !R(2);
    R(3) = ~3u;
    return heap_branch(c, fits);
}

static inline void heap_insert(CPUState *c, const HeapLayout *l) {
    heap_load(c, 2, R(BEST) + FREE_START);
    heap_load(c, CURSOR, R(BEST) + NEXT);
    heap_assign(c, 2, R(2) + 3);
    heap_assign(c, NEW_RECORD, R(2) & R(3));
    bool has_next = R(CURSOR) != 0;
    MEM_W32(R(NEW_RECORD) + NEXT, R(CURSOR));
    if (heap_branch(c, has_next)) {
        heap_load(c, 2, R(BEST) + NEXT);
        NC_POLL(c, NC_INSN_CYCLES);
        heap_store(c, R(2) + PREV, R(NEW_RECORD));
    }
    heap_assign(c, 2, R(NEW_RECORD) + HEADER_SIZE);
    heap_store(c, R(BEST) + NEXT, R(NEW_RECORD));
    heap_store(c, R(NEW_RECORD) + PREV, R(BEST));
    heap_store(c, R(NEW_RECORD) + PAYLOAD, R(2));
    heap_store(c, R(NEW_RECORD) + REQUESTED, R(REQUEST));
    heap_store(c, R(NEW_RECORD) + SPAN, R(NEEDED));

    heap_load(c, 2, R(BEST) + FREE_SIZE);
    NC_POLL(c, NC_INSN_CYCLES);
    bool exact_fit = R(2) == R(NEEDED);
    R(2) = R(NEW_RECORD) + R(NEEDED);
    if (heap_branch(c, exact_fit)) {
        heap_store(c, R(NEW_RECORD) + FREE_START, 0);
        heap_store(c, R(NEW_RECORD) + FREE_SIZE, 0);
    } else {
        heap_store(c, R(NEW_RECORD) + FREE_START, R(2));
        heap_load(c, 2, R(BEST) + FREE_SIZE);
        NC_POLL(c, NC_INSN_CYCLES);
        heap_assign(c, 2, R(2) - R(NEEDED));
        MEM_W32(R(NEW_RECORD) + FREE_SIZE, R(2));
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    }

    heap_load(c, 3, R(28) + l->used);
    heap_store(c, R(BEST) + FREE_START, 0);
    heap_store(c, R(BEST) + FREE_SIZE, 0);
    heap_load(c, 2, R(NEW_RECORD) + PAYLOAD);
    heap_load(c, 4, R(28) + l->count);
    heap_assign(c, 3, R(3) + R(REQUEST));
    heap_store(c, R(28) + l->used, R(3));
    heap_load(c, 3, R(28) + l->available);
    heap_assign(c, 4, R(4) + 1);
    heap_store(c, R(28) + l->count, R(4));
    heap_assign(c, 3, R(3) - R(REQUEST));
    heap_store(c, R(28) + l->available, R(3));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
}

static inline bool heap_allocate(CPUState *c, const HeapLayout *l) {
    if (c->budget <= 4)
        return false;
    R(29) -= 0x18;
    R(REQUEST) = R(4);
    MEM_W32(R(29) + 0x10, R(31));
    c->budget -= 4;
    if (!R(REQUEST)) {
        heap_assign(c, 2, 0);
    } else if (heap_find_space(c, l)) {
        heap_insert(c, l);
    } else {
        heap_assign(c, 4, (l->out_of_memory + 0x8000u) & 0xffff0000u);
        heap_assign(c, 4, l->out_of_memory);
        heap_call(c, l->print, l->allocate + 0x14c);
        heap_assign(c, 2, 0);
    }
    heap_load(c, 31, R(29) + 0x10);
    heap_assign(c, 29, R(29) + 0x18);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

static inline void heap_unlink(CPUState *c, const HeapLayout *l) {
    enum { RECORD = 5, OWNER = 6 };
    heap_load(c, OWNER, R(RECORD) + PREV);
    heap_load(c, 2, R(RECORD) + NEXT);
    NC_POLL(c, NC_INSN_CYCLES);
    bool has_next = R(2) != 0;
    MEM_W32(R(OWNER) + NEXT, R(2));
    if (heap_branch(c, has_next)) {
        heap_load(c, 2, R(RECORD) + NEXT);
        NC_POLL(c, NC_INSN_CYCLES);
        heap_store(c, R(2) + PREV, R(OWNER));
    }

    /* Combine this record, its payload and trailing free space with any
     * free span already owned by its predecessor. */
    heap_load(c, 2, R(OWNER) + FREE_START);
    NC_POLL(c, NC_INSN_CYCLES);
    if (heap_branch(c, R(2) == 0)) {
        heap_store(c, R(OWNER) + FREE_START, R(RECORD));
        heap_load(c, 2, R(RECORD) + SPAN);
        heap_load(c, 3, R(RECORD) + FREE_SIZE);
        NC_POLL(c, NC_INSN_CYCLES);
        heap_assign(c, 2, R(2) + R(3));
        MEM_W32(R(OWNER) + FREE_SIZE, R(2));
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } else {
        heap_load(c, 2, R(RECORD) + SPAN);
        heap_load(c, 4, R(RECORD) + FREE_SIZE);
        heap_load(c, 3, R(OWNER) + FREE_SIZE);
        heap_assign(c, 2, R(2) + R(4));
        heap_assign(c, 3, R(3) + R(2));
        heap_store(c, R(OWNER) + FREE_SIZE, R(3));
    }

    heap_assign(c, 4, R(RECORD));
    heap_load(c, 2, R(28) + l->count);
    heap_load(c, 5, R(4) + REQUESTED);
    heap_load(c, 3, R(28) + l->available);
    heap_load(c, 6, R(4) + REQUESTED);
    heap_assign(c, 2, R(2) - 1);
    heap_assign(c, 3, R(3) + R(6));
    heap_store(c, R(28) + l->count, R(2));
    heap_load(c, 2, R(28) + l->used);
    heap_assign(c, 6, HEADER_SIZE);
    heap_store(c, R(28) + l->available, R(3));
    heap_assign(c, 2, R(2) - R(5));
    heap_store(c, R(28) + l->used, R(2));
    R(5) = 0;
    heap_call(c, l->fill, l->release + 0xc0); /* zero the released header */
}

static inline bool heap_release(CPUState *c, const HeapLayout *l) {
    if (c->budget <= 3)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    c->budget -= 3;
    if (R(4)) {
        heap_call(c, l->find, l->release + 0x14); /* find allocation by payload address */
        heap_assign(c, 5, R(2));
        if (heap_branch(c, R(5) != 0))
            heap_unlink(c, l);
    }
    heap_load(c, 31, R(29) + 0x10);
    heap_assign(c, 29, R(29) + 0x18);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

static inline bool heap_size(CPUState *c, const HeapLayout *l) {
    if (c->budget <= 3)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    c->budget -= 3;
    bool found = false;
    if (R(4)) {
        heap_call(c, l->find, l->size + 0x14);
        found = heap_branch(c, R(2) != 0);
    }
    if (found) {
        heap_load(c, 2, R(2) + REQUESTED);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } else {
        heap_assign(c, 2, 0);
    }
    heap_load(c, 31, R(29) + 0x10);
    heap_assign(c, 29, R(29) + 0x18);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

static inline void heap_resize_extent(CPUState *c) {
    heap_assign(c, 3, ~3u);
    heap_assign(c, 2, R(2) & R(3));
    heap_assign(c, EXTENT, R(2) + HEADER_SIZE);
}

static inline void heap_shrink(CPUState *c, const HeapLayout *l) {
    heap_resize_extent(c);
    heap_load(c, 2, R(BLOCK) + PAYLOAD);
    heap_load(c, 4, R(BLOCK) + SPAN);
    heap_load(c, 5, R(BLOCK) + REQUESTED);
    heap_assign(c, 3, R(BLOCK) + R(EXTENT));
    heap_store(c, R(BLOCK) + FREE_START, R(3));
    heap_load(c, 3, R(28) + l->used);
    heap_store(c, R(BLOCK) + REQUESTED, R(NEW_SIZE));
    heap_store(c, R(BLOCK) + SPAN, R(EXTENT));
    heap_assign(c, DIFFERENCE, R(4) - R(EXTENT));
    heap_assign(c, 4, R(7) - R(NEW_SIZE));
    heap_assign(c, 3, R(3) - R(4));
    heap_assign(c, 5, R(5) - R(NEW_SIZE));
    heap_store(c, R(28) + l->used, R(3));
    heap_load(c, 3, R(28) + l->available);
    heap_load(c, 4, R(BLOCK) + FREE_SIZE);
    heap_assign(c, 3, R(3) + R(5));
    heap_assign(c, 4, R(4) + R(DIFFERENCE));
    heap_store(c, R(28) + l->available, R(3));
    MEM_W32(R(BLOCK) + FREE_SIZE, R(4));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
}

static inline bool heap_can_grow_in_place(CPUState *c) {
    heap_resize_extent(c);
    heap_load(c, 2, R(BLOCK) + SPAN);
    heap_load(c, 3, R(BLOCK) + FREE_START);
    NC_POLL(c, NC_INSN_CYCLES);
    bool adjacent = R(3) != 0;
    R(DIFFERENCE) = R(EXTENT) - R(2);
    if (!heap_branch(c, adjacent))
        return false;
    heap_load(c, 2, R(BLOCK) + FREE_SIZE);
    NC_POLL(c, NC_INSN_CYCLES);
    heap_assign(c, 2, R(2) < R(DIFFERENCE));
    bool fits = R(2) == 0;
    R(5) = 0;
    return heap_branch(c, fits);
}

static inline void heap_extend(CPUState *c, const HeapLayout *l) {
    heap_assign(c, 3, R(7) - R(NEW_SIZE));
    heap_load(c, 4, R(BLOCK) + PAYLOAD);
    heap_load(c, 6, R(BLOCK) + REQUESTED);
    heap_load(c, 2, R(28) + l->used);
    heap_assign(c, 4, R(4) + R(6));
    heap_assign(c, 2, R(2) - R(3));
    heap_store(c, R(28) + l->used, R(2));
    heap_load(c, 2, R(BLOCK) + REQUESTED);
    heap_load(c, 3, R(28) + l->available);
    heap_assign(c, 2, R(2) - R(NEW_SIZE));
    heap_assign(c, 3, R(3) + R(2));
    heap_store(c, R(28) + l->available, R(3));
    R(6) = R(NEW_SIZE) - R(6);
    heap_call(c, l->fill, l->resize + 0x12c); /* clear the extension */
    heap_load(c, 2, R(BLOCK) + PAYLOAD);
    heap_load(c, 4, R(BLOCK) + FREE_SIZE);
    heap_assign(c, 3, R(BLOCK) + R(EXTENT));
    heap_store(c, R(BLOCK) + FREE_START, R(3));
    heap_store(c, R(BLOCK) + REQUESTED, R(NEW_SIZE));
    heap_store(c, R(BLOCK) + SPAN, R(EXTENT));
    heap_assign(c, 4, R(4) - R(DIFFERENCE));
    MEM_W32(R(BLOCK) + FREE_SIZE, R(4));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
}

static inline void heap_move(CPUState *c, const HeapLayout *l) {
    R(4) = R(NEW_SIZE);
    heap_call(c, l->allocate, l->resize + 0x158);
    heap_assign(c, EXTENT, R(2)); /* now holds the new payload pointer */
    const bool allocated = R(EXTENT) != 0;
    R(2) = 0;
    if (!heap_branch(c, allocated))
        return;

    heap_assign(c, 4, R(EXTENT));
    heap_load(c, 6, R(BLOCK) + REQUESTED);
    R(5) = R(OLD_PTR);
    heap_call(c, l->copy, l->resize + 0x174); /* copy old bytes */
    heap_load(c, 6, R(BLOCK) + REQUESTED);
    heap_assign(c, 5, 0);
    heap_assign(c, 4, R(EXTENT) + R(6));
    R(6) = R(NEW_SIZE) - R(6);
    heap_call(c, l->fill, l->resize + 0x188); /* clear only new bytes */
    R(4) = R(OLD_PTR);
    heap_call(c, l->release, l->resize + 0x190);
    R(2) = R(EXTENT);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
}

static inline void heap_resize_existing(CPUState *c, const HeapLayout *l) {
    heap_call(c, l->find, l->resize + 0x38);
    heap_assign(c, BLOCK, R(2));
    const bool found = R(BLOCK) != 0;
    R(2) = 0;
    if (!heap_branch(c, found))
        return;

    heap_load(c, 7, R(BLOCK) + REQUESTED);
    NC_POLL(c, NC_INSN_CYCLES);
    heap_assign(c, 2, R(NEW_SIZE) < R(7));
    bool shrink = R(2) != 0;
    R(2) = R(NEW_SIZE) + 3;
    if (heap_branch(c, shrink)) {
        heap_shrink(c, l);
        return;
    }
    heap_assign(c, 2, R(7) < R(NEW_SIZE));
    bool grow = R(2) != 0;
    R(2) = R(NEW_SIZE) + 3;
    if (!heap_branch(c, grow)) {
        heap_load(c, 2, R(BLOCK) + PAYLOAD);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } else if (heap_can_grow_in_place(c)) {
        heap_extend(c, l);
    } else {
        heap_move(c, l);
    }
}

static inline bool heap_resize(CPUState *c, const HeapLayout *l) {
    if (c->budget <= 10)
        return false;
    R(29) -= 0x28;
    MEM_W32(R(29) + 0x1c, R(OLD_PTR));
    R(OLD_PTR) = R(4);
    MEM_W32(R(29) + 0x14, R(NEW_SIZE));
    R(NEW_SIZE) = R(5);
    MEM_W32(R(29) + 0x24, R(31));
    MEM_W32(R(29) + 0x20, R(DIFFERENCE));
    MEM_W32(R(29) + 0x18, R(EXTENT));
    MEM_W32(R(29) + 0x10, R(BLOCK));
    c->budget -= 10;
    if (!R(NEW_SIZE)) {
        R(4) = R(OLD_PTR);
        heap_call(c, l->release, l->resize + 0x1bc);
        heap_assign(c, 2, 0);
    } else if (heap_branch(c, !R(OLD_PTR))) {
        R(4) = R(NEW_SIZE);
        heap_call(c, l->allocate, l->resize + 0x1ac);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } else {
        heap_resize_existing(c, l);
    }
    heap_load(c, 31, R(29) + 0x24);
    for (int reg = DIFFERENCE; reg >= BLOCK; reg--)
        heap_load(c, reg, R(29) + 0x10 + 4 * (reg - BLOCK));
    heap_assign(c, 29, R(29) + 0x28);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* The four legacy allocation entry points (calloc, malloc, realloc, free) only print a
 * diagnostic from the executable's data segment. */
static inline bool heap_warning(CPUState *c, u32 entry, u32 message, u32 print) {
    if (c->budget <= 6)
        return false;
    R(29) -= 0x18;
    MEM_W32(R(29) + 0x10, R(31));
    R(4) = message;
    R(31) = entry + 0x18;
    c->budget -= 6;
    nc_call(c, print);
    heap_load(c, 31, R(29) + 0x10);
    heap_assign(c, 29, R(29) + 0x18);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

#endif
