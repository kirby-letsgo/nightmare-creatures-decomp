#include "decomp/credits.h"
#include "decomp/heap.h"
#include "decomp/shared_record.h"

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

static const HeapLayout credits_heap = {
    .count = 0x0c,
    .used = 0x10,
    .available = 0x14,
    .head = 0x2c,
    .init = 0x80019454,
    .resize = 0x8001949c,
    .allocate = 0x80019680,
    .release = 0x800197e0,
    .find = 0x800198b0,
    .size = 0x800198ec,
    .fill = 0x8001995c,
    .copy = 0x80019928,
    .print = 0x80019a60,
    .out_of_memory = 0x800180b8,
};

bool nc_credits_init_heap(CPUState *c) {
    return heap_init(c, &credits_heap);
}

bool nc_credits_find_allocation(CPUState *c) {
    return heap_find(c, &credits_heap);
}

bool nc_credits_allocate(CPUState *c) {
    return heap_allocate(c, &credits_heap);
}

bool nc_credits_free(CPUState *c) {
    return heap_release(c, &credits_heap);
}

bool nc_credits_allocation_size(CPUState *c) {
    return heap_size(c, &credits_heap);
}

bool nc_credits_resize(CPUState *c) {
    return heap_resize(c, &credits_heap);
}

bool nc_credits_init_shared_record(CPUState *c) {
    return init_shared_record(c, 8);
}

/* The four legacy allocation entry points only print a diagnostic. The
 * original messages are in the executable's data segment. */
bool nc_credits_heap_warning(CPUState *c, NcHeapWarning warning) {
    static const struct {
        u32 entry, message;
    } sites[] = {
        [NC_WARN_CALLOC] = {0x800193b4, 0x8001800c},
        [NC_WARN_MALLOC] = {0x800193dc, 0x80018038},
        [NC_WARN_REALLOC] = {0x80019404, 0x80018064},
        [NC_WARN_FREE] = {0x8001942c, 0x80018090},
    };
    return heap_warning(c, sites[warning].entry, sites[warning].message, 0x80019a60);
}
