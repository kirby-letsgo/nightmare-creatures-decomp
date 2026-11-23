#include "decomp/distance.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include <stdio.h>

static CPUState initial(void) {
    CPUState cpu = {0};
    for (unsigned r = 1; r < 32; r++)
        cpu.r[r] = r * 0x12345;
    cpu.budget = 512;
    cpu.r[31] = 0x80010000;
    return cpu;
}

/* Run the disc routine up to its return (or the caller's next instruction).
 * Check all registers, timing, and the transition to the timed path. */
static bool compare(CPUState input, u32 pc, bool (*native_fn)(CPUState *), bool disc,
                    CPUState *result) {
    *result = input;
    if (!native_fn(result))
        return false;
    if (disc) {
        CPUState guest = input;
        interp_call(&guest, pc);
        if (memcmp(&guest, result, sizeof guest)) {
            fprintf(stderr, "%08x input %08x: guest/native result %u/%u, budget %d/%d\n", pc,
                    input.r[4], guest.r[2], result->r[2], guest.budget, result->budget);
            return false;
        }
    }
    input.budget -= result->budget;
    CPUState cutoff = input;
    if (native_fn(&cutoff) || memcmp(&input, &cutoff, sizeof input))
        return false;
    input.budget++;
    return native_fn(&input) && input.budget == 1;
}

static bool target_vertical_velocities(bool disc) {
    const u32 actor = 0x80121000, record = SCRATCH_BASE;
    static const struct {
        s32 distance, y;
        s16 height;
        bool trap;
    } cases[] = {{0, -300, 0, false},
                 {1, -1123, -37, false},
                 {65535, 900, INT16_MIN, false},
                 {-141, -301, 0, false},
                 {0, INT32_MAX, INT16_MAX, false},
                 {-140, 7, 0, true},
                 {-141, 0x10000000 - 300, 0, true}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[2] = cases[i].distance;
        input.r[18] = record;
        input.r[31] = 0x80058830;
        MEM_W32(0x800ce9c4, actor);
        MEM_W32(actor + 0x280, cases[i].y);
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W16(record + 0x12, cases[i].height);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = cases[i].distance == -141 ? 29 : 27;
        CPUState native = input;
        if (nc_store_target_vertical_velocity(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (cases[i].trap) {
            if (nc_store_target_vertical_velocity(&native) ||
                memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            continue;
        }
        if (!nc_store_target_vertical_velocity(&native) || native.budget != 1 ||
            MEM_R16(record + 0x38) || MEM_R16(record + 0x22) != (u16)native.lo)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x800587cc);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool spatial_attenuation(bool disc) {
    const s32 distances[] = {-1, 0, 1, 1200, 2399, 2400, 2401, 4801, 65536, INT32_MIN, INT32_MAX};
    for (unsigned mode = 0; mode < 2; ++mode) {
        for (unsigned i = 0; i < sizeof distances / sizeof distances[0]; ++i) {
            CPUState input = initial();
            input.r[4] = 0x80 | (mode << 1);
            input.r[5] = distances[i];
            input.budget = 31;
            CPUState native = input;
            if (nc_spatial_attenuation(&native) || memcmp(&input, &native, sizeof input))
                return false;
            native.budget = ++input.budget;
            if (!nc_spatial_attenuation(&native) || native.r[2] > 0x2fff ||
                (distances[i] == 0 && native.r[2] != 0x2fff) ||
                (distances[i] == 2400 && native.r[2] != 0))
                return false;
            if (disc) {
                interp_call(&input, 0x8006c63c);
                if (memcmp(&input, &native, sizeof input))
                    return false;
            }
        }
    }
    return true;
}

static bool spatial_frames(bool disc, bool actor) {
    const u32 frame = SCRATCH_BASE + 128, stub = 0x80120000;
    const unsigned frame_size = actor ? 48 : 40;
    const u32 entries[] = {actor ? 0x8006c6f0 : 0x8006c8e4, actor ? 0x8006c8bc : 0x8006ca60,
                           actor ? 0x8006c894 : 0x8006ca34};
    bool (*start)(CPUState *) = actor ? nc_begin_spatial_actor : nc_begin_spatial_point;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned path = 0; path < 3; ++path) {
        const bool begin = path == 0, distant = path == 2;
        const u32 entry = entries[path];
        CPUState input = initial();
        input.r[29] = begin ? frame + frame_size : frame;
        input.r[31] = begin ? (actor ? 0x8006c714 : 0x8006c904) : 0x80010000;
        memset(nc_scratch, 0xa5, sizeof before);
        for (unsigned reg = 16; reg <= (actor ? 21u : 20u); ++reg)
            MEM_W32(frame + 16 + 4 * (reg - 16), 0x80000000u + reg);
        MEM_W32(frame + (actor ? 40 : 36), input.r[31]);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = begin ? (actor ? 9 : 8) : (actor ? 24 : 21) + (distant ? 2 : 0);
        CPUState native = input;
        if ((begin ? start(&native) : nc_finish_spatial_frame(&native, actor, distant)) ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!begin)
            native.r[31] = 0xdeadbeef; /* The return address must come from the frame. */
        if (!(begin ? start(&native) : nc_finish_spatial_frame(&native, actor, distant)) ||
            native.budget != 1 || native.r[29] != (begin ? frame : frame + frame_size))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            /* Match the interpreter's stop stack at the continuation. */
            MEM_W32(stub, 0x08000000u | ((entry >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bd0000u | (u16)(begin ? frame_size : -frame_size));
            input.r[29] = native.r[29];
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool actor_level_selection(bool disc) {
    static const struct {
        bool flags_entry;
        s32 distance;
        u32 flags, secondary, state, stop;
        NcActorLevelPath path;
        int cycles;
    } cases[] = {
        {false, 2401, 0, 0, 0, 0x8006c880, NC_ACTOR_LEVEL_DISTANT, 3},
        {false, 2400, 0x40000, 0, 0, 0x8006c848, NC_ACTOR_LEVEL_CHECK_FLAGS, 10},
        {false, -1, 0, 0x800, 0, 0x8006c848, NC_ACTOR_LEVEL_CHECK_FLAGS, 17},
        {false, 0, 0, 0x8010, 0, 0x8006c884, NC_ACTOR_LEVEL_DISTANT_RECORD_READY, 17},
        {true, 0, 0x10080000, 0, 0, 0x8006c884, NC_ACTOR_LEVEL_DISTANT_RECORD_READY, 7},
        {true, 0, 0x40000, 0, 0, 0x8006c89c, NC_ACTOR_LEVEL_SET, 11},
        {true, 0, 0x80000, 0, 0x100, 0x8006c8a0, NC_ACTOR_LEVEL_CHANNEL_READY, 18},
        {true, 0, 0x80000, 0, 0x10, 0x8006c880, NC_ACTOR_LEVEL_DISTANT, 18},
        {true, 0, 0x80000, 0, 0x40000000, 0x8006c880, NC_ACTOR_LEVEL_DISTANT, 18},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[17] = cases[i].distance;
        input.r[20] = SCRATCH_BASE;
        input.r[31] = cases[i].stop;
        MEM_W32(SCRATCH_BASE + 0x2c8, cases[i].flags);
        MEM_W32(SCRATCH_BASE + 0x2cc, cases[i].secondary);
        MEM_W32(SCRATCH_BASE + 0x330, cases[i].state);
        NcActorLevelPath (*select_path)(CPUState *) =
            cases[i].flags_entry ? nc_check_actor_level_flags : nc_check_actor_level_distance;
        input.budget = cases[i].flags_entry ? 18 : 17;
        CPUState native = input;
        if (select_path(&native) != NC_ACTOR_LEVEL_FALLBACK ||
            memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (select_path(&native) != cases[i].path ||
            native.budget != input.budget - cases[i].cycles)
            return false;
        if (disc) {
            interp_call(&input, cases[i].flags_entry ? 0x8006c848 : 0x8006c814);
            if (memcmp(&input, &native, sizeof input))
                return false;
        }
    }
    return true;
}

static bool placement_matrix_calls(bool disc) {
    static const struct {
        u32 entry, callee, matrix_reg;
        bool (*prepare)(CPUState *);
    } calls[] = {{0x8002b14c, 0x8007a7a0, 0, nc_prepare_placement_rotation},
                 {0x8002b160, 0x8007a1f0, 0, nc_prepare_placement_scale},
                 {0x8002b16c, 0x8007a320, 0, nc_prepare_placement_position},
                 {0x8002b180, 0x8007a370, 0, nc_prepare_placement_translation},
                 {0x8002b18c, 0x8007a7a0, 0, nc_prepare_placement_light_rotation},
                 {0x8002b19c, 0x8007a1f0, 0, nc_prepare_placement_light_scale},
                 {0x8002b1a8, 0x8007a3a0, 16, NULL},
                 {0x8002b1b0, 0x8007a430, 16, NULL},
                 {0x8002b1b8, 0x8007a3d0, 17, NULL},
                 {0x8002b1c0, 0x8007a750, 0, nc_prepare_placement_transpose}};
    const u32 record = SCRATCH_BASE, scale = SCRATCH_BASE + 0x200;
    CPUState input = initial();
    input.r[19] = record;
    input.r[20] = scale;
    input.r[31] = 0x8002b1d0;
    memset(nc_scratch, 0, SCRATCH_SIZE);
    MEM_W16(record, -32768);
    MEM_W16(record + 2, 0);
    MEM_W16(record + 4, 32767);
    MEM_W32(scale, 8192);
    MEM_W32(scale + 4, 4096);
    MEM_W32(scale + 8, 4096);
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    memcpy(before, nc_scratch, sizeof before);
    CPUState native = input;
    native.budget = input.budget = 4096;
    for (unsigned i = 0; i < sizeof calls / sizeof calls[0]; ++i) {
        CPUState entry = native, result;
        entry.r[31] = calls[i].callee;
        if (calls[i].prepare) {
            if (!compare(entry, calls[i].entry, calls[i].prepare, disc, &result) ||
                !calls[i].prepare(&native))
                return false;
        } else {
            const u32 matrix = native.r[calls[i].matrix_reg], resume = calls[i].entry + 8;
            result = entry;
            result.budget = 2;
            CPUState cutoff = result;
            if (nc_prepare_placement_call(&result, matrix, resume) ||
                memcmp(&result, &cutoff, sizeof result))
                return false;
            if (!nc_prepare_placement_call(&native, matrix, resume))
                return false;
            if (disc) {
                interp_call(&entry, calls[i].entry);
                if (memcmp(&native, &entry, sizeof entry))
                    return false;
            }
        }
        if (disc)
            interp_call(&native, calls[i].callee);
    }
    if (disc) {
        const s32 expected[] = {-65536, 0, 32767};
        for (unsigned axis = 0; axis < 3; ++axis)
            if ((s32)MEM_R32(record + 0x98 + 4 * axis) != expected[axis] ||
                (s32)MEM_R32(record + 0x38 + 0x14 + 4 * axis) != expected[axis])
                return false;
        memcpy(after, nc_scratch, sizeof after);
        memcpy(nc_scratch, before, sizeof before);
        interp_call(&input, calls[0].entry);
        return !memcmp(&native, &input, sizeof input) && !memcmp(after, nc_scratch, sizeof after);
    }
    return true;
}

static bool placement_matrix_scales(bool disc) {
    const u32 frame = SCRATCH_BASE + 0x100, flags = SCRATCH_BASE + 0x60;
    const u32 masks[] = {0, 0x20000000, 0xdfffffff, 0xffffffff};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof masks / sizeof masks[0]; ++i) {
        const bool wide = (masks[i] & 0x20000000) != 0;
        CPUState input = initial();
        input.r[4] = SCRATCH_BASE;
        input.r[28] = SCRATCH_BASE;
        input.r[29] = frame + 0x48;
        input.r[31] = 0x8002b14c;
        input.budget = 59;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(input.r[28] + 0x244, flags);
        MEM_W32(flags, masks[i]);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        if (nc_begin_placement_matrices(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_begin_placement_matrices(&native) || native.budget != (wide ? 1 : 2) ||
            native.r[20] != frame + (wide ? 0x20 : 0x10) || native.r[29] != frame)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            /* Restore the caller's SP in the jump delay slot so the interpreter
             * stops at the scale-selection join with the frame allocated. */
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((0x8002b0b0u >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bd0048);
            input.r[29] = frame;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_tick_latch(bool disc) {
    const u32 record = 0x800d7630;
    u8 *memory = &nc_ram[record & (RAM_SIZE - 1)];
    u8 before[0x17c], after[sizeof before];
    memset(memory, 0xa5, sizeof before);
    MEM_W16(record + 0x16c, 0xffdf);
    for (unsigned call = 0; call < 2; ++call) {
        CPUState input = initial();
        input.r[4] = call ? (u32)-32768 : 0;
        input.r[5] = call ? 0 : 0x80121000;
        input.budget = call ? 11 : 18;
        MEM_W32(0x8008ef10, call ? 0 : UINT32_MAX);
        memcpy(before, memory, sizeof before);
        CPUState native = input;
        if (nc_latch_placement_tick(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(memory, before, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_latch_placement_tick(&native) || native.budget != 1 ||
            MEM_R16(record + 0x16c) != 0xffff || MEM_R32(record + 0x168) != UINT32_MAX)
            return false;
        if (disc) {
            memcpy(after, memory, sizeof after);
            memcpy(memory, before, sizeof before);
            interp_call(&input, 0x800473e8);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, memory, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_clocks(bool disc) {
    const u32 addresses[] = {0x800cc044, 0x800cc048, 0x800cc04c, 0x800cc050, 0x800d77a8};
    const u32 clocks[] = {0, 0, 0xfffffff0, 0x80000000};
    for (unsigned i = 0; i < 4; ++i) {
        const u32 before[] = {1, 0x80000000, 0xffffffff, 0x7fffffff, 0xfffffff0};
        CPUState input = initial();
        input.r[2] = i ? 0x50 : 0;
        input.r[31] = i ? 0x8002b4b0 : 0x8002b5d8;
        input.budget = i ? 41 : 2;
        MEM_W32(0x8008ef10, clocks[i]);
        for (unsigned j = 0; j < 5; ++j)
            MEM_W32(addresses[j], before[j]);
        CPUState native = input;
        if (nc_update_placement_clock(&native) || memcmp(&native, &input, sizeof input))
            return false;
        for (unsigned j = 0; j < 5; ++j)
            if (MEM_R32(addresses[j]) != before[j])
                return false;
        native.budget = ++input.budget;
        if (!nc_update_placement_clock(&native) || native.budget != 1)
            return false;
        u32 after[5];
        for (unsigned j = 0; j < 5; ++j) {
            after[j] = MEM_R32(addresses[j]);
            if (after[j] != before[j] + (i ? clocks[i] - before[4] : 0))
                return false;
            MEM_W32(addresses[j], before[j]);
        }
        if (disc) {
            interp_call(&input, 0x8002b43c);
            if (memcmp(&native, &input, sizeof input))
                return false;
            for (unsigned j = 0; j < 5; ++j)
                if (MEM_R32(addresses[j]) != after[j])
                    return false;
        }
    }
    return true;
}

static bool placement_reset(bool disc) {
    CPUState input = initial(), result;
    input.r[16] = SCRATCH_BASE;
    input.r[31] = 0x8006070c;
    if (!compare(input, 0x8002b5b8, nc_call_placement_reset, disc, &result))
        return false;
    input.r[31] = 0x8002b5e0;
    input.budget = 8;
    MEM_W16(SCRATCH_BASE + 0x16c, 0xf4ff);
    result = input;
    if (nc_finish_placement_reset(&result) || memcmp(&result, &input, sizeof input) ||
        MEM_R16(SCRATCH_BASE + 0x16c) != 0xf4ff)
        return false;
    result.budget = ++input.budget;
    if (!nc_finish_placement_reset(&result) || result.budget != 1 ||
        MEM_R16(SCRATCH_BASE + 0x16c) != 0xf72f)
        return false;
    if (disc) {
        MEM_W16(SCRATCH_BASE + 0x16c, 0xf4ff);
        interp_call(&input, 0x8002b5c0);
        return !memcmp(&result, &input, sizeof input) && MEM_R16(SCRATCH_BASE + 0x16c) == 0xf72f;
    }
    return true;
}

static bool placement_refreshes(bool disc) {
    const u32 record = SCRATCH_BASE;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned path = 0; path < 6; ++path) {
        const bool begin = path < 2;
        const bool refresh_flag = path != 0;
        const bool success = path >= 4;
        const u32 counter = (path & 1) ? 7 : 0;
        const u16 flags = refresh_flag ? 0xfed0 : 0xfe90;
        CPUState input = initial();
        input.r[16] = record;
        input.r[2] = success;
        input.r[31] = begin && refresh_flag ? 0x8002b22c : 0x8002b5e0;
        input.budget = begin ? 9 : 16;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W16(record + 0x16c, flags);
        MEM_W32(record + 0x170, counter);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        bool refresh = false;
        if ((begin ? nc_begin_placement_refresh(&native, &refresh)
                   : nc_finish_placement_refresh(&native)) ||
            memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (begin) {
            if (!nc_begin_placement_refresh(&native, &refresh) || refresh != refresh_flag ||
                native.budget != input.budget - (refresh ? 9 : 7))
                return false;
        } else {
            if (!nc_finish_placement_refresh(&native) ||
                MEM_R16(record + 0x16c) != (success ? flags : (flags & 0xffaf)) ||
                MEM_R32(record + 0x170) != (counter ? counter : 1) ||
                native.budget != input.budget - (8 + (success ? 0 : 6) + (counter ? 0 : 2)))
                return false;
        }
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, begin ? 0x8002b56c : 0x8002b588);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_counters(bool disc) {
    static const struct {
        u32 counter, threshold;
        u16 flags;
        NcPlacementCounterPath path;
        u32 stop;
        int cycles;
    } cases[] = {
        {0, 0, 0x10, NC_PLACEMENT_COUNTER_ZERO, 0x8002b5b8, 6},
        {3, 3, 0x40, NC_PLACEMENT_CHECK_TARGET, 0x8002b56c, 13},
        {2, 0, 0x10, NC_PLACEMENT_UPDATE_MODE, 0x8002b5d8, 20},
        {1, 0, 0x10, NC_PLACEMENT_APPLY_PRESET, 0x8002b4e8, 20},
        {0xffffffff, 0, 0x50, NC_PLACEMENT_APPLY_PRESET, 0x8002b4e8, 20},
        {0x80000000, 0, 0x10, NC_PLACEMENT_UPDATE_MODE, 0x8002b5d8, 20},
    };
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[16] = SCRATCH_BASE;
        input.r[31] = cases[i].stop;
        input.budget = 20;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(SCRATCH_BASE + 0x170, cases[i].counter);
        MEM_W32(SCRATCH_BASE + 0x174, cases[i].threshold);
        MEM_W16(SCRATCH_BASE + 0x16c, cases[i].flags);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        if (nc_check_placement_counter(&native) != NC_PLACEMENT_COUNTER_FALLBACK ||
            memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        const bool store = cases[i].cycles == 20;
        if (nc_check_placement_counter(&native) != cases[i].path ||
            native.budget != input.budget - cases[i].cycles ||
            MEM_R32(SCRATCH_BASE + 0x170) != cases[i].counter - (u32)store)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x8002b4b0);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_presets(bool disc) {
    const s16 indices[] = {-1, 0, 2};
    const u16 words[] = {0x8000, 0, 0x7fff, 0xabcd, 0, 1024, 4095, 0xef01, 0x1234, 0x5678};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof indices / sizeof indices[0]; ++i) {
        const u32 record = SCRATCH_BASE + (i % 2) * 2;
        const u32 source = 0x800d00c0u + 20 * indices[i];
        for (unsigned j = 0; j < 10; ++j)
            MEM_W16(source + 2 * j, words[j]);
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W16(record + 0x16e, indices[i]);
        MEM_W16(record + 0x16c, 0x4010);
        memcpy(before, nc_scratch, sizeof before);
        CPUState input = initial();
        input.r[16] = record;
        input.r[31] = 0x8002b56c;
        input.budget = 55;
        CPUState native = input;
        if (nc_apply_placement_preset(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_apply_placement_preset(&native) || native.budget != 1 ||
            MEM_R16(record + 0x16c) != 0x4018 || MEM_R16(record + 16) != 0xa5a5)
            return false;
        for (unsigned j = 0; j < 8; ++j)
            if (MEM_R16(record + 2 * j) != words[j])
                return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x8002b4e8);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_update_entry(bool disc) {
    const u16 flags[] = {0, 4, 0x10, 0x40, 0xff54};
    const u32 record = SCRATCH_BASE, frame = record + 0x200;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; ++i) {
        CPUState input = initial();
        input.r[4] = record;
        input.r[29] = frame + 0x18;
        input.r[31] = (flags[i] & 4) ? 0x8002b40c : 0x8002b43c;
        input.budget = 17;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W16(record + 0x16c, flags[i]);
        MEM_W32(0x800d7c10, 0);
        MEM_W32(0x800d7c14, 0);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        if (nc_begin_placement_update(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before) || MEM_R32(0x800d7c10) || MEM_R32(0x800d7c14))
            return false;
        native.budget = ++input.budget;
        if (!nc_begin_placement_update(&native) || native.budget != 1 || native.r[3] != flags[i] ||
            native.r[2] != (flags[i] & 0x50) || MEM_R32(0x800d7c10) != 0x790000 ||
            MEM_R32(0x800d7c14) != 0xb00)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            MEM_W32(0x800d7c10, 0);
            MEM_W32(0x800d7c14, 0);
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((0x8002b3d0u >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bd0018);
            input.r[29] = frame;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after) ||
                MEM_R32(0x800d7c10) != 0x790000 || MEM_R32(0x800d7c14) != 0xb00)
                return false;
        }
    }
    return true;
}

static bool placement_mode_entry(bool disc) {
    const u32 record = SCRATCH_BASE, frame = record + 0x200, actor = 0x80128000;
    static const struct {
        s16 mode;
        bool linked;
        int cycles;
    } cases[] = {{-1, true, 10}, {3, false, 10}, {1, false, 16}, {1, true, 21}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const bool call = cases[i].mode == 1 && cases[i].linked;
        CPUState input = initial();
        input.r[4] = record;
        input.r[29] = frame + 0x18;
        input.r[31] = call ? 0x80047948 : 0x8002b384;
        input.budget = 21;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(record + 0x160, cases[i].linked ? actor : 0);
        MEM_W8(actor + 0x398, 0xfe);
        MEM_W16(0x800d7c28, cases[i].mode);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        bool update_actor = false;
        if (nc_begin_placement_mode(&native, &update_actor) ||
            memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_begin_placement_mode(&native, &update_actor) || update_actor != call ||
            native.budget != input.budget - cases[i].cycles || (call && native.r[5] != 254))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            /* Stop at the selected continuation with the new frame allocated. */
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((0x8002b348u >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bd0018);
            input.r[29] = frame;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_step_selection(bool disc) {
    static const struct {
        u32 flags;
        s16 mode;
        int cycles;
    } cases[] = {{0, 3, 11}, {2, -1, 18}, {0xffffffff, 3, 20}};
    const u32 record = SCRATCH_BASE, frame = record + 0x300, flags = record + 0x260;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[16] = record;
        input.r[28] = record;
        input.r[29] = frame;
        input.budget = 1024;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(record + 0x244, flags);
        MEM_W32(flags, cases[i].flags);
        MEM_W16(0x800d7c28, cases[i].mode);
        MEM_W32(frame + 0x10, 0x12345678);
        MEM_W32(frame + 0x14, input.r[31]);
        MEM_W16(record, -32768);
        MEM_W16(record + 0x18, 32767);
        MEM_W16(record + 0x1a, 0);
        MEM_W32(record + 0x14c, 1);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        native.budget = 20;
        CPUState saved = native;
        bool step = false;
        if (nc_select_placement_step(&native, &step) || memcmp(&saved, &native, sizeof saved))
            return false;
        native.budget = input.budget;
        if (!nc_select_placement_step(&native, &step) || step != (cases[i].cycles == 20) ||
            native.budget != input.budget - cases[i].cycles)
            return false;
        if (step && !nc_step_placement_record(&native))
            return false;
        saved = native;
        saved.budget = 9;
        CPUState cutoff = saved;
        if (nc_restore_placement_frame(&cutoff) || memcmp(&saved, &cutoff, sizeof saved))
            return false;
        if (!nc_restore_placement_frame(&native) || native.r[29] != frame + 0x18 ||
            native.r[16] != 0x12345678 || native.r[31] != input.r[31])
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            /* Enter with the frame allocated and stop at the restored caller SP. */
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((0x8002b384u >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bdffe8);
            input.r[29] += 0x18;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool actor_placement_paths(bool disc) {
    const u32 record = SCRATCH_BASE, actor = 0x80128000;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned present = 0; present <= 1; ++present) {
        CPUState input = initial();
        input.r[4] = record;
        input.r[29] = SCRATCH_BASE + 0x228;
        input.budget = 4096;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(record + 0x164, actor);
        MEM_W32(actor + 0x20, present ? actor + 0x400 : 0);
        MEM_W32(actor + 0x404, actor + 0x500);
        MEM_W32(actor + 0x448, 0xc000);
        MEM_W32(actor + 0x540, 0);
        MEM_W32(actor + 0x27c, -257);
        MEM_W32(actor + 0x284, 255);
        MEM_W32(actor + 0x50, actor + 0x600);
        MEM_W32(actor + 0x630, -80);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        bool has_pose = false;
        native.budget = 17;
        CPUState saved = native;
        if (nc_begin_actor_placement_target(&native, &has_pose) ||
            memcmp(&saved, &native, sizeof saved) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = input.budget;
        if (!nc_begin_actor_placement_target(&native, &has_pose) || has_pose != (present != 0) ||
            native.budget != input.budget - (present ? 17 : 15))
            return false;
        if (has_pose && disc) {
            interp_call(&native, 0x800472d8);
            if (!nc_prepare_actor_placement_target(&native))
                return false;
            interp_call(&native, 0x80047128);
        }
        CPUState cutoff = native;
        cutoff.budget = has_pose ? 114 : 12;
        saved = cutoff;
        memcpy(after, nc_scratch, sizeof after);
        if (nc_finish_actor_placement_target(&cutoff, has_pose) ||
            memcmp(&cutoff, &saved, sizeof saved) || memcmp(after, nc_scratch, sizeof after))
            return false;
        if (!nc_finish_actor_placement_target(&native, has_pose) || native.r[2] != present ||
            native.r[31] != input.r[31] || native.r[29] != input.r[29])
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x8002b22c);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool actor_placement_targets(bool disc) {
    const u32 record = SCRATCH_BASE, actor = 0x80128000;
    const u32 headings[] = {0, 0x3fff, 0x4000, 0x8000, 0xc000, 0x1234ffff};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof headings / sizeof headings[0]; ++i) {
        CPUState input = initial();
        input.r[2] = headings[i];
        input.r[17] = record;
        input.r[31] = 0x80047128;
        input.budget = 71;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(record + 0x164, actor);
        MEM_W32(actor + 0x27c, -257);
        MEM_W32(actor + 0x284, 255);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        if (nc_prepare_actor_placement_target(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_prepare_actor_placement_target(&native) || native.budget != 1)
            return false;
        const u32 offset = (headings[i] >> 3) & 0x1ffe;
        const s32 sine = (s16)MEM_R16(0x80083be4 + offset);
        const s32 cosine = (s16)MEM_R16(0x80081be4 + offset);
        if ((s32)MEM_R32(record + 0x150) != 256 * (-2 + ((25 * sine) >> 12)) ||
            (s32)MEM_R32(record + 0x158) != 256 * ((25 * cosine) >> 12))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x8002b260);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_matrix_return(bool disc) {
    const u32 frame = SCRATCH_BASE + 0x200;
    CPUState input = initial();
    input.r[19] = SCRATCH_BASE;
    input.r[29] = frame;
    memset(nc_scratch, 0xa5, SCRATCH_SIZE);
    MEM_W16(SCRATCH_BASE + 0x0a, 1024);
    MEM_W32(frame + 0x44, input.r[31]);
    for (unsigned i = 0; i < 5; ++i)
        MEM_W32(frame + 0x30 + 4 * i, 0x80000000u + i);
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    memcpy(before, nc_scratch, sizeof before);
    CPUState native = input;
    native.budget = 2;
    CPUState saved = native;
    if (nc_prepare_placement_call(&native, native.r[19], 0x8002b208) ||
        memcmp(&native, &saved, sizeof saved))
        return false;
    native.budget = input.budget = 4096;
    if (!nc_prepare_placement_call(&native, native.r[19], 0x8002b208) ||
        native.r[4] != SCRATCH_BASE || native.r[31] != 0x8002b208)
        return false;
    if (disc)
        interp_call(&native, 0x8002ae78);
    CPUState cutoff = native;
    cutoff.budget = 21;
    saved = cutoff;
    if (nc_finish_placement_matrices(&cutoff) || memcmp(&cutoff, &saved, sizeof saved))
        return false;
    const int budget = native.budget;
    if (!nc_finish_placement_matrices(&native) || native.budget != budget - 21 ||
        native.r[31] != input.r[31] || native.r[29] != frame + 0x48)
        return false;
    for (unsigned i = 0; i < 5; ++i)
        if (native.r[16 + i] != 0x80000000u + i)
            return false;
    if (disc) {
        memcpy(after, nc_scratch, sizeof after);
        memcpy(nc_scratch, before, sizeof before);
        /* Enter with the frame allocated and stop at the restored caller SP. */
        const u32 stub = 0x80120000;
        MEM_W32(stub, 0x08000000u | ((0x8002b200u >> 2) & 0x03ffffffu));
        MEM_W32(stub + 4, 0x27bdffb8);
        input.r[29] += 0x48;
        input.budget += 2;
        interp_call(&input, stub);
        return !memcmp(&native, &input, sizeof input) && !memcmp(after, nc_scratch, sizeof after);
    }
    return true;
}

static bool placement_axis_samples(bool disc) {
    const u32 record = SCRATCH_BASE, frame = SCRATCH_BASE + 0x180;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned wide = 0; wide < 2; ++wide) {
        CPUState input = initial();
        input.r[4] = record;
        input.r[28] = SCRATCH_BASE;
        input.r[29] = frame + 0x28;
        input.r[17] = wide ? 0x20000000 : 0;
        memset(nc_scratch, 0xa5, sizeof before);
        /* The mode word aliases saved s1, so the read must follow the spill. */
        MEM_W32(input.r[28] + 0x244, frame + 0x14);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 71;
        CPUState native = input;
        if (nc_sample_placement_axes(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = 72;
        if (!nc_sample_placement_axes(&native) || native.budget != (wide ? 1 : 3) ||
            native.r[20] != (wide ? 0x2e38u : 0x18e3u) || native.r[31] != 0x8002ae1c)
            return false;
        native.budget += 35;
        if (!nc_store_placement_axes(&native))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            input.budget = 107;
            interp_call(&input, 0x8002ada0);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_axes(bool disc) {
    const u32 record = SCRATCH_BASE, frame = SCRATCH_BASE + 0x180;
    static const s16 samples[] = {INT16_MIN, -65, -1, 0, 64, INT16_MAX};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
        CPUState input = initial();
        input.r[19] = record;
        input.r[29] = frame;
        input.r[2] = 0xbeef0000u | (u16)samples[i];
        input.r[16] = 511;
        input.r[17] = -512;
        input.r[18] = -1;
        memset(nc_scratch, 0xa5, sizeof before);
        for (unsigned reg = 16; reg <= 20; ++reg)
            MEM_W32(frame + 0x10 + 4 * (reg - 16), reg * 123);
        MEM_W32(frame + 0x24, input.r[31]);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 35;
        CPUState native = input;
        if (nc_store_placement_axes(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_store_placement_axes(&native) || native.budget != 1 ||
            MEM_R16(record + 0x100) != (u16)(samples[i] >> 6) ||
            MEM_R16(record + 0xf8) != (u16) - (samples[i] >> 6))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            /* Recreate the frame on entry so the interpreter stops at the caller. */
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((0x8002ae1cu >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bdffd8);
            input.r[29] += 0x28;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_vector_frame(bool disc, bool begin) {
    const u32 frame = SCRATCH_BASE + 0x100;
    const u32 entry = begin ? 0x8002ae78 : 0x8002af68;
    bool (*run)(CPUState *) = begin ? nc_begin_placement_vectors : nc_finish_placement_vectors;
    CPUState input = initial();
    input.r[4] = SCRATCH_BASE;
    input.r[29] = begin ? frame + 0x68 : frame;
    input.r[31] = begin ? 0x8002aee0 : 0x80010000;
    input.budget = begin ? 42 : 9;
    memset(nc_scratch, 0xa5, SCRATCH_SIZE);
    MEM_W32(frame + 0x60, 0x87654321);
    MEM_W32(frame + 0x64, input.r[31]);
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    memcpy(before, nc_scratch, sizeof before);
    CPUState native = input;
    if (run(&native) || memcmp(&native, &input, sizeof input) ||
        memcmp(before, nc_scratch, sizeof before))
        return false;
    native.budget = ++input.budget;
    if (!begin)
        native.r[31] = 0xdeadbeef;
    if (!run(&native) || native.budget != 1 || native.r[29] != (begin ? frame : frame + 0x68))
        return false;
    if (begin) {
        if (MEM_R32(frame + 0x60) != input.r[16] || MEM_R32(frame + 0x64) != input.r[31] ||
            native.r[16] != input.r[4])
            return false;
        for (unsigned offset = 0; offset < 0x20; offset += 4)
            if (MEM_R32(frame + 0x40 + offset) != MEM_R32(0x800184d8 + offset))
                return false;
    } else if (native.r[16] != 0x87654321 || native.r[31] != input.r[31]) {
        return false;
    }
    if (disc) {
        memcpy(after, nc_scratch, sizeof after);
        memcpy(nc_scratch, before, sizeof before);
        /* Enter with the stop stack adjusted for the frame allocation/restoration. */
        const u32 stub = 0x80120000;
        MEM_W32(stub, 0x08000000u | ((entry >> 2) & 0x03ffffffu));
        MEM_W32(stub + 4, 0x27bd0000u | (u16)(begin ? 0x68 : -0x68));
        input.r[29] = native.r[29];
        input.budget += 2;
        interp_call(&input, stub);
        return !memcmp(&native, &input, sizeof input) && !memcmp(after, nc_scratch, sizeof after);
    }
    return true;
}

static bool placement_vector_pairs(bool disc) {
    static const struct {
        u32 source, result;
        s16 xyz[3];
    } vectors[] = {{0xc8, 0xa8, {-32768, 0, 32767}},
                   {0xd0, 0xb8, {0, -1, 0}},
                   {0xf8, 0xd8, {0, 0, -256}},
                   {0x100, 0xe8, {32767, -32768, 1}}};
    const u32 record = SCRATCH_BASE, frame = record + 0x200;
    CPUState input = initial();
    input.r[16] = record;
    input.r[29] = frame;
    input.r[31] = 0x8002af68;
    memset(nc_scratch, 0xa5, SCRATCH_SIZE);
    for (unsigned i = 0; i < 9; ++i)
        MEM_W16(frame + 0x10 + 2 * i, i % 4 == 0 ? 4096 : 0);
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned axis = 0; axis < 3; ++axis)
            MEM_W16(record + vectors[i].source + 2 * axis, vectors[i].xyz[axis]);
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    memcpy(before, nc_scratch, sizeof before);
    CPUState native = input;
    for (unsigned i = 0; i < 4; ++i) {
        const u32 resume = 0x8002af38 + i * 0x10;
        CPUState cutoff = native;
        cutoff.budget = 4;
        CPUState saved = cutoff;
        memcpy(after, nc_scratch, sizeof after);
        if (nc_prepare_placement_vector(&cutoff, vectors[i].source, vectors[i].result, resume) ||
            memcmp(&cutoff, &saved, sizeof saved) || memcmp(nc_scratch, after, sizeof after))
            return false;
        const int budget = native.budget;
        if (!nc_prepare_placement_vector(&native, vectors[i].source, vectors[i].result, resume) ||
            native.budget != budget - 4 || native.r[31] != resume || native.r[4] != frame + 0x10 ||
            native.r[5] != record + vectors[i].source || native.r[6] != record + vectors[i].result)
            return false;
        if (disc) {
            interp_call(&native, 0x8007a320);
            for (unsigned axis = 0; axis < 3; ++axis)
                if ((s32)MEM_R32(native.r[6] + 4 * axis) != vectors[i].xyz[axis])
                    return false;
        }
    }
    if (disc) {
        memcpy(after, nc_scratch, sizeof after);
        memcpy(nc_scratch, before, sizeof before);
        interp_call(&input, 0x8002af28);
        return !memcmp(&native, &input, sizeof input) && !memcmp(after, nc_scratch, sizeof after);
    }
    return true;
}

static bool placement_heading_calls(bool disc) {
    static const struct {
        u32 entry, callee;
        int cycles;
        bool (*prepare)(CPUState *);
    } calls[] = {{0x8002aee0, 0x8007a7a0, 9, nc_prepare_placement_yaw},
                 {0x8002aefc, 0x8007a1f0, 3, nc_prepare_placement_yaw_scale},
                 {0x8002af08, 0x8007a320, 8, nc_prepare_placement_heading_vector}};
    const u16 headings[] = {0, 1024, 2048, 3072, 4095, 0xffff};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof headings / sizeof headings[0]; ++i) {
        for (unsigned j = 0; j < sizeof calls / sizeof calls[0]; ++j) {
            CPUState input = initial();
            input.r[16] = SCRATCH_BASE;
            input.r[29] = SCRATCH_BASE + 0x200;
            input.r[31] = calls[j].callee;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W16(input.r[16] + 0x0a, headings[i]);
            memcpy(before, nc_scratch, sizeof before);
            input.budget = calls[j].cycles;
            CPUState native = input;
            if (calls[j].prepare(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!calls[j].prepare(&native) || native.budget != 1)
                return false;
            if (j == 0 && (MEM_R16(native.r[4]) || MEM_R16(native.r[4] + 2) != headings[i] ||
                           MEM_R16(native.r[4] + 4)))
                return false;
            if (j == 2 && (MEM_R16(native.r[5]) || MEM_R16(native.r[5] + 2) ||
                           (s16)MEM_R16(native.r[5] + 4) != -256))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, calls[j].entry);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool transposed_placement_scales(bool disc) {
    static const struct {
        s32 input[3], expected[3];
    } cases[] = {
        {{8192, 8192, 8192}, {2048, 2048, 2048}},
        {{16384, 8192, 8192}, {4096, 2048, 2048}},
        {{-1, 0, 3}, {-1, 0, 0}},
        {{INT32_MIN, -5, INT32_MAX}, {-536870912, -2, 536870911}},
    };
    const u32 scale = SCRATCH_BASE + 0x10;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[16] = SCRATCH_BASE + 0x58;
        input.r[20] = scale;
        input.r[31] = 0x8007a1f0; /* Stop at ScaleMatrixL after its call delay slot. */
        input.budget = 18;
        memset(nc_scratch, 0xa5, sizeof before);
        for (unsigned axis = 0; axis < 3; ++axis)
            MEM_W32(scale + 4 * axis, cases[i].input[axis]);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        if (nc_prepare_transposed_placement_scale(&native) ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_prepare_transposed_placement_scale(&native) || native.budget != 1)
            return false;
        for (unsigned axis = 0; axis < 3; ++axis)
            if ((s32)MEM_R32(scale + 4 * axis) != cases[i].expected[axis])
                return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x8002b1d0);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_pose_initializers(bool disc) {
    const u32 record = 0x800d7630;
    u8 *memory = &nc_ram[record & (RAM_SIZE - 1)];
    u8 before[0x170], after[sizeof before];
    static const struct {
        u32 angles[3];
        s16 point[3];
    } cases[] = {{{0, 2048, 0}, {-17, 300, -1}},
                 {{0x88, 0, 0}, {0, 500, INT16_MIN}},
                 {{0xffff1000, 0xffffffff, 4095}, {INT16_MIN, -1, INT16_MAX}}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        memcpy(&input.r[4], cases[i].angles, sizeof cases[i].angles);
        input.r[7] = (s32)cases[i].point[0];
        /* In the last case the first angle stores overwrite the stack arguments. */
        input.r[29] = i == 2 ? record - 8 : SCRATCH_BASE;
        memset(memory, 0xa5, sizeof before);
        MEM_W32(input.r[29] + 0x10, (s32)cases[i].point[1]);
        MEM_W32(input.r[29] + 0x14, (s32)cases[i].point[2]);
        memcpy(before, memory, sizeof before);
        input.budget = 29;
        CPUState native = input;
        if (nc_init_placement_pose(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(memory, before, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_init_placement_pose(&native) || native.budget != 1 || MEM_R16(record + 0x16c))
            return false;
        for (unsigned axis = 0; axis < 3; ++axis) {
            const u32 pos = record + 2 * axis;
            if (MEM_R16(pos) != (u16)cases[i].point[axis] || MEM_R16(pos + 0x18) != MEM_R16(pos) ||
                MEM_R16(pos + 8) != (u16)cases[i].angles[axis] ||
                MEM_R16(pos + 0x10) != MEM_R16(pos + 8))
                return false;
        }
        if (disc) {
            memcpy(after, memory, sizeof after);
            memcpy(memory, before, sizeof before);
            interp_call(&input, 0x8002ad3c);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, memory, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_record_steps(bool disc) {
    const u32 record = SCRATCH_BASE;
    static const struct {
        u32 shift;
        s16 result[3];
    } cases[] = {{0, {32767, -32768, -1}}, {33, {-1, -1, -1}}, {31, {-32768, 32766, -1}}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[4] = record;
        input.r[29] = SCRATCH_BASE + SCRATCH_SIZE;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W16(record, -32768);
        MEM_W16(record + 2, 32767);
        MEM_W16(record + 4, 0);
        MEM_W16(record + 0x18, 32767);
        MEM_W16(record + 0x1a, -32768);
        MEM_W16(record + 0x1c, -1);
        MEM_W16(record + 0x08, 0);
        MEM_W16(record + 0x0a, -1);
        MEM_W16(record + 0x10, 0xf7ff);
        MEM_W16(record + 0x12, i == 2 ? 0xf800 : 0xf000);
        MEM_W32(record + 0x128, 0xffffff00u);
        MEM_W32(record + 0x12c, i == 2 ? 0x80000800u : 0x80000000u);
        MEM_W32(record + 0x148, 2);
        MEM_W32(record + 0x14c, cases[i].shift);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 151;
        CPUState native = input;
        if (nc_step_placement_record(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_step_placement_record(&native) || native.budget != (i == 2 ? 1 : 3))
            return false;
        for (unsigned axis = 0; axis < 3; ++axis)
            if ((s16)MEM_R16(record + 2 * axis) != cases[i].result[axis])
                return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x8002afb4);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool record_angle_pairs(bool disc) {
    static const struct {
        s16 first, first_target, second, second_target;
        u32 shift;
        s16 first_result, second_result;
        int cycles;
    } cases[] = {
        {0, 0, 0, 1024, 2, 0, 256, 46},
        {0, 2047, -1, 0, 1, -1025, -1, 47},
        {0, 2046, 0, 2048, 1, 1023, -1024, 47},
        {-3, -8, 1, 0, 33, -6, 0, 48},
        {32767, -32768, -32768, 32767, 32, -32768, 32767, 47},
    };
    const u32 record = SCRATCH_BASE;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[16] = record;
        input.r[31] = 0x8002b5e0;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W16(record + 0x0a, cases[i].first);
        MEM_W16(record + 0x12, cases[i].first_target);
        MEM_W16(record + 0x08, cases[i].second);
        MEM_W16(record + 0x10, cases[i].second_target);
        MEM_W32(record + 0x148, cases[i].shift);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 48;
        CPUState native = input;
        if (nc_step_record_angle_pair(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_step_record_angle_pair(&native) ||
            native.budget != input.budget - cases[i].cycles ||
            MEM_R16(record + 0x0a) != (u16)cases[i].first_result ||
            MEM_R16(record + 0x08) != (u16)cases[i].second_result)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x8002b40c);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool record_angle_steps(bool disc) {
    static const struct {
        u32 target, current, shift;
        s32 delta, step;
    } cases[] = {{0, 0, 0, 0, 0},          {1024, 0, 2, 1024, 256},
                 {2046, 0, 1, 2046, 1023}, {2047, 0, 1, -2049, -1025},
                 {2048, 0, 31, -2048, -1}, {0, 1, 32, -1, -1},
                 {0, 4095, 0, 1, 1},       {(u32)-8, (u32)-3, 33, -5, -3},
                 {0, UINT32_MAX, 0, 1, 1}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial(), result;
        input.r[4] = cases[i].target;
        input.r[5] = cases[i].current;
        input.r[6] = cases[i].shift;
        if (!compare(input, 0x8002af7c, nc_step_record_angle, disc, &result) ||
            result.r[4] != (u32)cases[i].delta ||
            result.r[2] != cases[i].current + (u32)cases[i].step)
            return false;
    }
    return true;
}

static bool paired_point_return(bool disc) {
    const u32 frame = SCRATCH_BASE + 128, stub = 0x80120000;
    u8 before[SCRATCH_SIZE];
    for (unsigned finish = 0; finish < 2; ++finish) {
        CPUState input = initial();
        input.r[16] = 0;
        input.r[29] = frame;
        input.r[31] = finish ? 0x80010000 : 0x8002b3d0;
        MEM_W32(frame + 40, 0x80000001);
        MEM_W32(frame + 44, 0x7fffffff);
        MEM_W32(frame + 48, input.r[31]);
        memcpy(before, nc_scratch, sizeof before);
        bool (*run)(CPUState *) =
            finish ? nc_finish_paired_point_placement : nc_call_placement_record_update;
        input.budget = finish ? 13 : 4;
        CPUState native = input;
        if (run(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (finish)
            native.r[31] = 0x8006e06c;
        if (!run(&native) || native.budget != 1 || memcmp(before, nc_scratch, sizeof before))
            return false;
        if (finish) {
            if (native.r[2] || native.r[29] != frame + 56 || native.r[31] != input.r[31])
                return false;
        } else if (native.r[4] != 0x800d7630 || native.r[31] != 0x8006e06c)
            return false;
        if (disc) {
            u32 entry = 0x8006e05c;
            if (finish) {
                /* Begin at caller SP so interp_call recognizes the restored frame. */
                MEM_W32(stub, 0x08000000u | ((0x8006e06cu >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, 0x27bdffc8);
                input.r[29] += 56;
                input.budget += 2;
                entry = stub;
            }
            interp_call(&input, entry);
            if (memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool paired_point_placement(bool disc) {
    const u16 points[][4] = {{0, 0, 0, 0},
                             {0xffff, 1, 0, 1},
                             {0, 1, 0xffff, 1},
                             {0x8000, 0x7fff, 0x8000, 0x8000},
                             {5, 0xffff, 5, 0}};
    const u32 entries[] = {0x8006dfe8, 0x8006e014, 0x8006e024, 0x8006e04c};
    const u32 callees[] = {0x80047684, 0x80039f00, 0x80039f00, 0x800470dc};
    const int cycles[] = {19, 20, 14, 4};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof points / sizeof points[0]; ++i) {
        MEM_W16(0x8008f078, points[i][0]);
        MEM_W16(0x8008f07c, points[i][1]);
        MEM_W16(0x8008f0bc, points[i][2]);
        MEM_W16(0x8008f0c0, points[i][3]);
        for (unsigned path = 0; path < 4; ++path) {
            CPUState input = initial();
            input.r[16] = SCRATCH_BASE;
            input.r[2] = 1024 * i;
            input.r[31] = callees[path];
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(SCRATCH_BASE + 0x20, SCRATCH_BASE + 96);
            memcpy(before, nc_scratch, sizeof before);
            for (int extra = 0; extra <= 1; ++extra) {
                input.budget = cycles[path] + extra;
                CPUState native = input;
                bool ran;
                if (path == 0)
                    ran = nc_prepare_paired_point_bearing(&native);
                else if (path == 3)
                    ran = nc_call_actor_coordinate_init(&native);
                else
                    ran = nc_place_paired_point_actor(&native, path == 1);
                if (!extra) {
                    if (ran || memcmp(&input, &native, sizeof input) ||
                        memcmp(before, nc_scratch, sizeof before))
                        return false;
                    continue;
                }
                if (!ran || native.budget != 1)
                    return false;
                if (disc) {
                    memcpy(after, nc_scratch, sizeof after);
                    memcpy(nc_scratch, before, sizeof before);
                    interp_call(&input, entries[path]);
                    if (memcmp(&input, &native, sizeof input) ||
                        memcmp(after, nc_scratch, sizeof after))
                        return false;
                }
            }
        }
    }
    return true;
}

static bool actor_level_calls(bool disc) {
    const s32 components[] = {0, 0x7fff, 0x8000, -1, -32769};
    for (unsigned path = 0; path < 4; ++path) {
        const bool distant = path < 2, first = (path & 1) == 0;
        const u32 entry = (distant ? 0x8006c880 : 0x8006c89c) + (first ? 0 : 4);
        bool (*prepare)(CPUState *, bool) = distant ? nc_call_distant_actor : nc_set_actor_levels;
        for (unsigned i = 0; i < sizeof components / sizeof components[0]; ++i) {
            CPUState input = initial();
            input.r[4] = 0x80010000;
            input.r[21] = 0x12345678;
            input.r[6] = 0x2fff;
            input.r[7] = components[i];
            input.r[31] = distant ? 0x8006bfe8 : 0x8006c524;
            input.budget = (distant ? 4 : 7) + first;
            CPUState native = input;
            if (prepare(&native, first) || memcmp(&native, &input, sizeof input))
                return false;
            native.budget = ++input.budget;
            if (!prepare(&native, first) || native.budget != 1 ||
                native.r[31] != (distant ? 0x8006c894u : 0x8006c8bcu))
                return false;
            if (disc) {
                interp_call(&input, entry);
                if (memcmp(&input, &native, sizeof input))
                    return false;
            }
        }
    }
    return true;
}

static bool point_level_selection(bool disc) {
    static const struct {
        bool flagged;
        u32 flags;
        s32 distance;
        NcPointLevelAction action;
        int cycles;
    } cases[] = {{false, 4, 2401, NC_POINT_LEVEL_SET, 9},
                 {true, 6, 2401, NC_POINT_LEVEL_SET, 9},
                 {true, 2, -1, NC_POINT_LEVEL_SET, 13},
                 {true, 2, 2400, NC_POINT_LEVEL_SET, 13},
                 {true, 2, 2401, NC_POINT_LEVEL_DISTANT, 11}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[3] = cases[i].flags;
        input.r[17] = cases[i].distance;
        input.r[20] = 0xabcd8001;
        input.r[6] = 0x2fff;
        input.r[7] = i & 1 ? 0x8000 : (u32)-7;
        input.r[31] = cases[i].action == NC_POINT_LEVEL_SET ? 0x8006c524 : 0x8006bfe8;
        input.budget = cases[i].cycles;
        CPUState native = input;
        if (nc_select_point_levels(&native, cases[i].flagged) != NC_POINT_LEVEL_FALLBACK ||
            memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (nc_select_point_levels(&native, cases[i].flagged) != cases[i].action ||
            native.budget != 1)
            return false;
        if (disc) {
            interp_call(&input, cases[i].flagged ? 0x8006c9f8 : 0x8006ca3c);
            if (memcmp(&input, &native, sizeof input))
                return false;
        }
    }
    return true;
}

static bool spatial_levels(bool disc, bool actor) {
    const u32 record = SCRATCH_BASE, descriptor = record + 32;
    const s32 levels[] = {0, 0x2fff, 0x8001, 0x10001, -4097};
    const u32 entry = actor ? 0x8006c7c8 : 0x8006c9b0;
    const u32 resume = actor ? 0x8006c7dc : 0x8006c9c4;
    MEM_W32(record, descriptor);
    for (unsigned i = 0; i < sizeof levels / sizeof levels[0]; ++i) {
        CPUState input = initial();
        const bool flagged = (i & 1) != 0;
        MEM_W16(descriptor + 8, flagged ? 0x82 : 0x80);
        input.r[actor ? 18 : 19] = record;
        input.r[31] = 0x8006c63c;
        input.r[2] = 2400 + i;
        input.budget = 9;
        CPUState native = input;
        if (nc_call_spatial_attenuation(&native, actor) || memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (!nc_call_spatial_attenuation(&native, actor) || native.budget != 1 ||
            native.r[31] != resume)
            return false;
        if (disc) {
            interp_call(&input, entry);
            if (memcmp(&input, &native, sizeof input))
                return false;
        }

        input = native;
        input.r[2] = levels[i];
        input.r[16] = 2049;
        input.r[31] =
            actor ? (flagged ? 0x8006c814 : 0x8006c89c) : (flagged ? 0x8006c9f8 : 0x8006ca3c);
        const s32 product = 2049 * (actor ? (s16)levels[i] : levels[i]);
        input.budget = (actor ? 17 : 16) + (product < 0);
        native = input;
        if (nc_scale_spatial_level(&native, actor) || memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (!nc_scale_spatial_level(&native, actor) || native.budget != 1 ||
            native.r[2] != (flagged ? 2u : 0u))
            return false;
        if (disc) {
            interp_call(&input, resume);
            if (memcmp(&input, &native, sizeof input))
                return false;
        }
    }
    return true;
}

static bool spatial_distances(bool disc, bool actor) {
    const u16 offsets[] = {0, 1, 2046, 2047, 2048, 2049, 4095};
    const u16 gaps[] = {0, 1, 2046, 2049, 2048, 2047, 1};
    const u32 entry = actor ? 0x8006c750 : 0x8006c938;
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        CPUState input = initial();
        const s16 reference = i % 2 ? INT16_MIN : INT16_MAX;
        MEM_W16(0x800d763a, reference);
        input.r[2] = (u32)(reference - 1024 + offsets[i]) & 4095;
        input.r[actor ? 19 : 18] = i ? -65536 : 0;
        input.r[17] = i ? -65537 : 1;
        input.r[31] = 0x800475e4;
        input.budget = offsets[i] >= 2047 ? 28 : 24;
        CPUState native = input;
        if (nc_prepare_spatial_distance(&native, actor) || memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (!nc_prepare_spatial_distance(&native, actor) || native.budget != 1 ||
            native.r[16] != gaps[i] || native.r[4] != (i ? 131073u : 1u))
            return false;
        if (disc) {
            interp_call(&input, entry);
            if (memcmp(&input, &native, sizeof input))
                return false;
        }
    }
    return true;
}

static bool spatial_bearings(bool disc, bool actor) {
    const s16 points[][2] = {{-2, -3}, {-3, -3}, {-1, -3},
                             {-2, -4}, {-2, -2}, {INT16_MIN, INT16_MAX}};
    const u32 record = SCRATCH_BASE, source = record + 64;
    const u32 entry = actor ? 0x8006c714 : 0x8006c904;
    bool (*prepare)(CPUState *) =
        actor ? nc_prepare_spatial_actor_bearing : nc_prepare_spatial_point_bearing;
    u8 before[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof points / sizeof points[0]; ++i) {
        CPUState input = initial();
        input.r[actor ? 18 : 19] = record;
        input.r[31] = 0x80047684; /* Compare at the callee entry, after the delay slot. */
        MEM_W32(record + 4, source);
        MEM_W16(0x800d7630, -2);
        MEM_W16(0x800d7634, 3);
        MEM_W16(source + 0x18, points[i][0]);
        MEM_W16(source + 0x1c, points[i][1]);
        MEM_W32(source + 0x27c, (u32)points[i][0] * 256 + 255);
        MEM_W32(source + 0x284, (u32)points[i][1] * 256 + 1);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = actor ? 25 : 23;
        CPUState native = input;
        if (prepare(&native) || memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (!prepare(&native) || native.budget != 1 || native.r[4] != (u32)(points[i][0] + 2) ||
            native.r[5] != (u32)(points[i][1] + 3) || memcmp(before, nc_scratch, sizeof before))
            return false;
        if (disc) {
            interp_call(&input, entry);
            if (memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool target_displacements(bool disc) {
    const s32 offsets[][2] = {{0, 0}, {-1, 0},   {1, 0},      {0, -1},
                              {0, 1}, {37, -81}, {-65536, 5}, {5, -65536}};
    const u32 actor = 0x80121000, record = SCRATCH_BASE;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        CPUState input = initial();
        input.r[18] = record;
        input.r[19] = actor;
        input.r[31] = 0x800587cc;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W16(0x800d4ca0, -3);
        MEM_W16(0x800d4ca2, 2);
        MEM_W32(actor + 0x27c, (u32)(-3 - offsets[i][0]) * 256 + 255);
        MEM_W32(actor + 0x284, (u32)(2 - offsets[i][1]) * 256 + 1);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input, cutoff = input;
        native.budget = cutoff.budget = 21;
        if (nc_prepare_target_bearing(&native) || memcmp(&native, &cutoff, sizeof cutoff))
            return false;
        native.budget = input.budget;
        if (!nc_prepare_target_bearing(&native) || native.budget != input.budget - 21 ||
            native.r[4] != (u32)offsets[i][0] || native.r[5] != (u32)offsets[i][1] ||
            native.r[31] != 0x800587a0 || !nc_vector_heading(&native))
            return false;
        const u32 heading = -(native.r[2] << 4);
        cutoff = native;
        cutoff.budget = 11;
        CPUState attempt = cutoff;
        if (nc_prepare_target_distance(&attempt) || memcmp(&attempt, &cutoff, sizeof cutoff) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        const int remaining = native.budget - 11;
        const u64 z_squared = (s64)offsets[i][1] * offsets[i][1];
        if (!nc_prepare_target_distance(&native) || native.budget != remaining ||
            native.hi != (u32)(z_squared >> 32) || native.lo != (u32)z_squared ||
            MEM_R16(record + 0x34) != (u16)heading || native.r[31] != input.r[31] ||
            !nc_distance_root(&native))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x8005876c);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool planar_velocities(bool disc) {
    const u32 record = SCRATCH_BASE, actor = 0x80121000;
    const s32 cases[][3] = {{0, 0, 0x1900}, {-1, 0, 0x3200}, {0, -1, 0x2c00},
                            {1, 0, -257},   {0, 1, 257},     {-32768, 32767, INT32_MAX}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[4] = record;
        input.r[5] = cases[i][2];
        input.r[29] = record + 512;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(0x800ce9c4, actor);
        MEM_W32(actor + 0x27c, -257);
        MEM_W32(actor + 0x284, 511);
        MEM_W16(record + 0x10, cases[i][0] + 2);
        MEM_W16(record + 0x14, 1 - cases[i][1]);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        native.budget = 28;
        CPUState cutoff = native;
        if (nc_prepare_planar_velocity(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = input.budget;
        if (!nc_prepare_planar_velocity(&native) || native.budget != input.budget - 28 ||
            native.r[4] != (u32)cases[i][0] || native.r[5] != (u32)cases[i][1])
            return false;
        if (disc)
            interp_call(&native, 0x80047684);
        else if (!nc_vector_heading(&native))
            return false;
        const u16 heading = -(native.r[2] << 4);
        const int remaining = native.budget - 53;
        cutoff = native;
        cutoff.budget = 53;
        CPUState attempt = cutoff;
        memcpy(after, nc_scratch, sizeof after);
        if (nc_finish_planar_velocity(&attempt) || memcmp(&attempt, &cutoff, sizeof cutoff) ||
            memcmp(after, nc_scratch, sizeof after) || !nc_finish_planar_velocity(&native) ||
            native.budget != remaining || MEM_R16(record + 0x34) != heading)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x80058868);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool roots(bool disc) {
    CPUState c = initial(), result;
    const u32 cases[][2] = {{0, 0},
                            {1, 1},
                            {2, 2},
                            {3, 1},
                            {4, 2},
                            {15, 3},
                            {16, 4},
                            {17, 4},
                            {65535, 255},
                            {65536, 256},
                            {0xffffffffu, 0xffffffffu}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        c.r[4] = cases[i][0];
        if (!compare(c, 0x800475E4, nc_distance_root, disc, &result) || result.r[2] != cases[i][1])
            return false;
    }
    c.r[4] = 0x40000000;
    CPUState before = c;
    if (nc_distance_root(&c) || memcmp(&before, &c, sizeof c))
        return false;
    u32 sample = 0;
    for (unsigned i = 0; i < 4096; i++) {
        sample = sample * 1664525u + 1013904223u;
        c.r[4] = sample & 0x7fffffffu;
        if (!compare(c, 0x800475E4, nc_distance_root, disc, &result))
            return false;
    }
    return true;
}

static bool part_heading_entry(bool disc) {
    const u32 frame = SCRATCH_BASE + 0x100, stub = 0x80120000;
    for (unsigned mode = 0; mode < 2; ++mode) {
        CPUState input = initial();
        input.r[29] = frame + 0x40;
        input.r[31] = mode ? 0x8001be98 : 0x8001bd48;
        MEM_W16(0x800d4c9e, mode ? 0x8000 : 0);
        memset(nc_scratch, 0xa5, SCRATCH_SIZE);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 18;
        CPUState native = input;
        if (nc_begin_part_pose_heading(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_begin_part_pose_heading(&native) || native.budget != 1 ||
            native.r[2] != (mode ? 0xffff8000u : 0u))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            MEM_W32(stub, 0x08000000u | ((0x8001bd08u >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bd0040);
            input.r[29] = frame;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool part_turn_resets(bool disc) {
    const u32 actor = 0x80123000, pose = 0x80124000;
    CPUState input = initial();
    input.r[17] = actor;
    input.r[31] = 0x80053f38;
    MEM_W32(actor + 0x20, pose);
    MEM_W32(pose + 0x48, 0xffff8000);
    input.budget = 5;
    CPUState native = input;
    if (nc_reset_part_pose_heading(&native) || memcmp(&input, &native, sizeof input) ||
        MEM_R32(pose + 0x48) != 0xffff8000)
        return false;
    native.budget = ++input.budget;
    if (!nc_reset_part_pose_heading(&native) || native.budget != 1 || MEM_R32(pose + 0x48))
        return false;
    if (disc) {
        MEM_W32(pose + 0x48, 0xffff8000);
        interp_call(&input, 0x8001bd84);
        if (memcmp(&input, &native, sizeof input) || MEM_R32(pose + 0x48))
            return false;
    }

    for (unsigned first = 0; first <= 5; ++first) {
        input = initial();
        input.r[17] = actor;
        input.r[16] = (u32)-1;
        input.r[28] = SCRATCH_BASE;
        input.r[31] = first == 0 ? 0x8001bea8 : first < 5 ? 0x8001beac : 0x8001be6c;
        for (unsigned i = 0; i < 5; ++i) {
            const u32 index = 0xff - i, part = 0x80125000 + 0x100 * i;
            MEM_W8(input.r[28] + 0x108 + i, index);
            MEM_W32(actor + 0x4c + 4 * index, part);
            MEM_W8(part + 0x4d, i >= first ? 0xff : 0xf7);
        }
        input.budget = 85;
        native = input;
        if (nc_check_turn_parts(&native) != NC_PART_FLAGS_FALLBACK ||
            memcmp(&input, &native, sizeof input))
            return false;
        native.budget = ++input.budget;
        const NcPartFlagPath expected = first == 0  ? NC_PART_FLAGS_FIRST
                                        : first < 5 ? NC_PART_FLAGS_LATER
                                                    : NC_PART_FLAGS_CLEAR;
        if (nc_check_turn_parts(&native) != expected ||
            native.budget != 86 - 17 * (int)(first < 5 ? first + 1 : 5))
            return false;
        if (disc) {
            interp_call(&input, 0x8001bd90);
            if (memcmp(&input, &native, sizeof input))
                return false;
        }
    }
    return true;
}

static bool part_heading_turns(bool disc) {
    static const struct {
        s16 ticks;
        s32 step;
    } cases[] = {{180, 1},
                 {181, 1},
                 {-181, 1},
                 {-181, -1},
                 {0, INT32_MAX},
                 {INT16_MIN, INT32_MAX},
                 {INT16_MAX, INT32_MIN}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial(), native;
        input.r[16] = cases[i].step;
        const u32 product = (u32)cases[i].ticks * 364u * input.r[16];
        const bool crossed = (s32)product > 65535;
        input.r[31] = crossed ? 0x8001bd84 : 0x8001bea8;
        MEM_W16(0x800d4c9c, cases[i].ticks);
        if (!compare(input, 0x8001bd48, nc_check_part_heading_turn, disc, &native) ||
            native.r[2] != crossed || native.r[8] != product)
            return false;
    }
    return true;
}

static bool part_turn_completion(bool disc) {
    for (unsigned reset = 0; reset < 2; ++reset) {
        CPUState input = initial();
        input.r[17] = SCRATCH_BASE;
        input.r[31] = reset ? 0x80053f38 : 0x8001c04c;
        input.budget = reset ? 4 : 13;
        bool (*run)(CPUState *) = reset ? nc_reset_part_turn_counter : nc_finish_part_turn;
        memset(nc_scratch, 0xff, SCRATCH_SIZE);
        MEM_W16(0x800d4c9c, 0xffff);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        if (run(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before) || MEM_R16(0x800d4c9c) != 0xffff)
            return false;
        native.budget = ++input.budget;
        if (!run(&native) || native.budget != 1 || MEM_R16(0x800d4c9c) != (reset ? 0 : 0xffff) ||
            (!reset && (native.r[2] != 1 || MEM_R32(SCRATCH_BASE + 0x330) != ~0x18u)))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            MEM_W16(0x800d4c9c, 0xffff);
            interp_call(&input, reset ? 0x8001be98 : 0x8001be6c);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after) ||
                MEM_R16(0x800d4c9c) != (reset ? 0 : 0xffff))
                return false;
        }
    }
    return true;
}

static bool part_pose_heading_steps(bool disc) {
    const u32 actor = 0x80122000, counter = 0x800d4c9c;
    const s32 steps[] = {-1, 0, 1, INT32_MIN, INT32_MAX};
    for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; ++i) {
        for (unsigned path = 0; path < 4; ++path) {
            const bool skip = path & 1, double_step = path & 2;
            CPUState input = initial();
            const u32 heading = i == 2 ? counter : 0x80123048;
            input.r[16] = steps[i];
            input.r[2] = double_step ? 0xdeadbeef : 2 * input.r[16];
            input.r[17] = actor;
            input.r[18] = skip;
            input.r[31] = skip ? 0x8001c04c : 0x8001bf18;
            MEM_W32(actor + 0x20, heading - 0x48);
            MEM_W32(heading, 0xffffff00);
            MEM_W16(counter, 0xffff);
            const u32 before = MEM_R32(heading);
            input.budget = (skip ? 25 : 33) + double_step;
            CPUState native = input;
            bool search = false;
            if (nc_advance_part_pose_heading(&native, double_step, &search) ||
                memcmp(&native, &input, sizeof input) || MEM_R32(heading) != before ||
                MEM_R16(counter) != 0xffff)
                return false;
            native.budget = ++input.budget;
            if (!nc_advance_part_pose_heading(&native, double_step, &search) ||
                native.budget != 1 || search != !skip)
                return false;
            const u32 after = MEM_R32(heading);
            const u16 ticks = MEM_R16(counter);
            if (heading != counter && (after != before + 364u * steps[i] || ticks != 0))
                return false;
            if (disc) {
                MEM_W32(heading, before);
                MEM_W16(counter, 0xffff);
                interp_call(&input, double_step ? 0x8001bea8 : 0x8001beac);
                if (memcmp(&native, &input, sizeof input) || MEM_R32(heading) != after ||
                    MEM_R16(counter) != ticks)
                    return false;
            }
        }
    }
    return true;
}

static bool part_search_exits(bool disc) {
    static const struct {
        bool matched;
        u32 candidate, part, stop;
        NcPartSearchStep step;
        int cycles;
    } paths[] = {{false, 3, 0, 0x8001bf54, NC_PART_SEARCH_NEXT_HEADING, 4},
                 {false, 4, 3, 0x8001bf18, NC_PART_SEARCH_NEXT_PART, 9},
                 {true, 1, 3, 0x8001bf18, NC_PART_SEARCH_NEXT_PART, 6},
                 {false, 4, 4, 0x8001c04c, NC_PART_SEARCH_DONE, 10},
                 {true, 0, 4, 0x8001c04c, NC_PART_SEARCH_DONE, 7}};
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        CPUState input = initial();
        input.r[6] = paths[i].candidate;
        input.r[19] = paths[i].part;
        input.r[31] = paths[i].stop;
        input.budget = paths[i].cycles;
        CPUState native = input;
        if (nc_advance_part_heading_search(&native, paths[i].matched) != NC_PART_SEARCH_FALLBACK ||
            memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (nc_advance_part_heading_search(&native, paths[i].matched) != paths[i].step ||
            native.budget != 1)
            return false;
        if (disc) {
            interp_call(&input, paths[i].matched ? 0x8001c01c : 0x8001c024);
            if (memcmp(&native, &input, sizeof input))
                return false;
        }
    }

    const u32 frame = SCRATCH_BASE + 0x100, stub = 0x80120000;
    CPUState input = initial();
    input.r[29] = frame;
    for (unsigned reg = 16; reg <= 22; ++reg)
        MEM_W32(frame + 0x20 + 4 * (reg - 16), 0xfedcb000u + reg);
    MEM_W32(frame + 0x3c, input.r[31]);
    input.budget = 27;
    CPUState native = input;
    if (nc_restore_part_search_frame(&native) || memcmp(&native, &input, sizeof input))
        return false;
    native.budget = ++input.budget;
    if (!nc_restore_part_search_frame(&native) || native.budget != 1 ||
        native.r[29] != frame + 0x40 || native.r[2] != input.r[2])
        return false;
    if (disc) {
        MEM_W32(stub, 0x08000000u | ((0x8001c04cu >> 2) & 0x03ffffffu));
        MEM_W32(stub + 4, 0x27bdffc0);
        input.r[29] += 0x40;
        input.budget += 2;
        interp_call(&input, stub);
        return !memcmp(&native, &input, sizeof input);
    }
    return true;
}

static bool part_heading_windows(bool disc) {
    const s16 degrees[] = {INT16_MIN, -180, -1, 0, 90, 180, INT16_MAX};
    const int offsets[] = {-183, -182, -181, 0, 181, 182, 183};
    CPUState input = initial(), result;
    input.r[17] = 0x80122000;
    input.r[20] = SCRATCH_BASE;
    input.r[21] = 0xb60b60b7;
    input.r[22] = SCRATCH_BASE + 0x40;
    MEM_W32(input.r[17] + 0x20, 0x80123000);
    MEM_W32(0x80123048, 0xffff);
    for (unsigned i = 0; i < sizeof degrees / sizeof degrees[0]; ++i) {
        MEM_W16(input.r[20], degrees[i]);
        const int heading = degrees[i] * 65536 / 360;
        input.r[31] = 0x8001bf54;
        if (!compare(input, 0x8001bf18, nc_begin_part_heading_search, disc, &result) ||
            result.r[4] != (u16)(heading - 0xffff))
            return false;
        input.r[2] = 2 * i;
        MEM_W16(input.r[22] + input.r[2], degrees[i]);
        for (unsigned j = 0; j < sizeof offsets / sizeof offsets[0]; ++j) {
            const bool match = offsets[j] > -182 && offsets[j] < 182;
            input.r[4] = (u16)(heading + offsets[j]);
            input.r[31] = match ? 0x8001bf98 : 0x8001c024;
            if (!compare(input, 0x8001bf54, nc_test_part_heading, disc, &result) ||
                result.r[3] != match)
                return false;
        }
    }
    return true;
}

static bool part_bearing_calls(bool disc) {
    const u32 slot = SCRATCH_BASE + 0x50;
    const u32 headings[] = {0, 1, 1024, 2048, 3072, 4095, 0xffffffff};
    for (unsigned i = 0; i < sizeof headings / sizeof headings[0]; ++i) {
        CPUState input = initial();
        input.r[2] = headings[i];
        input.r[18] = SCRATCH_BASE;
        input.r[29] = slot - 0x10;
        input.r[31] = 0x8001b5d4;
        memset(nc_scratch, 0xa5, SCRATCH_SIZE);
        MEM_W8(input.r[18], 0x80);
        /* On the last case the selected pointer aliases the fifth argument. */
        input.r[17] = i == 6 ? slot - 0x24c : 0x80120000;
        MEM_W32(input.r[17] + 0x24c, 0x80123000);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 16;
        CPUState native = input;
        if (nc_call_part_with_bearing(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_call_part_with_bearing(&native) || native.budget != 1 ||
            native.r[6] != (u16)(0u - 16 * headings[i]) || MEM_R32(slot) != 1 ||
            native.r[5] != (i == 6 ? 1u : 0x80123000u))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x8001bfec);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool part_point_bearings(bool disc) {
    const u32 frame = SCRATCH_BASE + 0x100, actor = 0x80122000, points = 0x80124000;
    static const struct {
        s16 part_x, part_z, point_x, point_z;
        u8 part, point;
    } cases[] = {{-8, 6, -8, 0, 0, 0},
                 {0, -1, -17, -1, 3, 4},
                 {INT16_MIN, INT16_MIN, INT16_MAX, INT16_MAX, 0xff, 0x80}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[17] = actor;
        input.r[18] = SCRATCH_BASE;
        input.r[29] = frame;
        input.r[5] = frame + 0x18;
        input.r[6] = i;
        input.r[31] = 0x80034828;
        const u32 point = points + 36u * cases[i].point;
        MEM_W8(SCRATCH_BASE, cases[i].part);
        MEM_W8(0x8008eeb0 + i, cases[i].point);
        MEM_W32(0x8008f0c8, points);
        MEM_W32(actor + 0x4c + 4u * cases[i].part, 0x80126000);
        CPUState native;
        if (!compare(input, 0x8001bf98, nc_select_part_bearing_point, disc, &native) ||
            native.r[4] != 0x80126000 || native.r[16] != point)
            return false;
        input.r[16] = point;
        input.r[31] = 0x80047684;
        MEM_W16(frame + 0x18, cases[i].part_x);
        MEM_W16(frame + 0x1c, cases[i].part_z);
        MEM_W16(point + 0xa, cases[i].point_x);
        MEM_W16(point + 0xc, cases[i].point_z);
        if (!compare(input, 0x8001bfd0, nc_prepare_part_point_bearing, disc, &native) ||
            (s32)native.r[4] != cases[i].part_x - cases[i].point_x ||
            (s32)native.r[5] != cases[i].point_z - cases[i].part_z)
            return false;
    }
    return true;
}

static bool bearing_point_selection(bool disc) {
    const u32 frame = SCRATCH_BASE + 0x100, stub = 0x80120000;
    const s16 cursors[] = {0, 1, 2, 3, 4, -1, -2, -3, -4, INT16_MIN, INT16_MAX};
    for (unsigned i = 0; i < sizeof cursors / sizeof cursors[0]; ++i) {
        CPUState input = initial();
        input.r[29] = frame + 0x28;
        input.r[31] = 0x8001c234;
        memset(nc_scratch, 0xa5, SCRATCH_SIZE);
        MEM_W16(0x800d4c9a, cursors[i]);
        MEM_W32(0x8008f0c8, 0x80124000);
        for (int j = -3; j < 4; ++j) {
            const u32 address = j < 0 ? frame + 0x10 : 0x8008eeb8;
            MEM_W16(address + j * 2, (u16)(j - 1));
        }
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 52;
        CPUState native = input;
        if (nc_select_bearing_point(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        const int expected_offset = ((cursors[i] % 4) - 1) * 36;
        if (!nc_select_bearing_point(&native) || native.budget != (cursors[i] < 0 ? 1 : 2) ||
            native.r[3] != 0x80124000u + expected_offset)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            /* Match the interpreter's stop stack at the selected-record load. */
            MEM_W32(stub, 0x08000000u | ((0x8001c19cu >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bd0028);
            input.r[29] = frame;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool selected_point_bearings(bool disc) {
    const u32 output = 0x800d4ca0;
    static const struct {
        s16 x, z;
        s32 actor_x, actor_z;
        bool alias;
    } cases[] = {
        {0, 0, -1, 255, false}, {INT16_MIN, -1, 0, -256, false}, {17, INT16_MAX, 256, -257, true}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[3] = cases[i].alias ? output - 0xa : 0x80121000;
        input.r[16] = 0x80122000;
        input.r[31] = 0x80047684;
        MEM_W32(output, 0xa5a5a5a5);
        MEM_W16(input.r[3] + 0xa, cases[i].x);
        MEM_W16(input.r[3] + 0xc, cases[i].z);
        MEM_W32(input.r[16] + 0x27c, cases[i].actor_x);
        MEM_W32(input.r[16] + 0x284, cases[i].actor_z);
        const u32 before = MEM_R32(output);
        input.budget = 30;
        CPUState native = input;
        if (nc_prepare_selected_point_bearing(&native) || memcmp(&input, &native, sizeof input) ||
            MEM_R32(output) != before)
            return false;
        native.budget = ++input.budget;
        const s32 target_x = cases[i].alias ? cases[i].x : -cases[i].x;
        if (!nc_prepare_selected_point_bearing(&native) || native.budget != 1 ||
            (s32)native.r[4] != target_x - (cases[i].actor_x >> 8) ||
            (s32)native.r[5] != cases[i].z - (cases[i].actor_z >> 8) ||
            MEM_R16(output) != (u16)-cases[i].x || MEM_R16(output + 2) != (u16)cases[i].z)
            return false;
        if (disc) {
            const u32 after = MEM_R32(output);
            MEM_W32(output, before);
            interp_call(&input, 0x8001c234);
            if (memcmp(&input, &native, sizeof input) || MEM_R32(output) != after)
                return false;
        }
    }
    return true;
}

static bool selected_point_returns(bool disc) {
    const u32 frame = SCRATCH_BASE + 0x100, stub = 0x80120000;
    static const struct {
        u32 heading, step;
        u16 index;
        u8 first, second;
        int cycles;
    } cases[] = {{0, 1, 0xffff, 0, 0, 35},
                 {4095, (u32)-1, 0, 0, 0, 35},
                 {1024, 1, 3, 0x80, 0, 23},
                 {2048, (u32)-1, 2, 0, 0xff, 28}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[2] = cases[i].heading;
        input.r[16] = 0x80122000;
        input.r[17] = SCRATCH_BASE;
        input.r[18] = cases[i].step;
        input.r[29] = frame;
        MEM_W8(input.r[16] + 0x38a, cases[i].first);
        MEM_W8(input.r[16] + 0x39b, cases[i].second);
        memset(nc_scratch, 0xa5, SCRATCH_SIZE);
        MEM_W16(input.r[17], cases[i].index);
        MEM_W32(frame + 0x24, input.r[31]);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 35;
        CPUState native = input;
        if (nc_finish_selected_point_bearing(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        const u16 expected = cases[i].index + (cases[i].cycles == 35 ? cases[i].step : 0);
        if (!nc_finish_selected_point_bearing(&native) || native.budget != 36 - cases[i].cycles ||
            native.r[2] != cases[i].heading * 16 || MEM_R16(SCRATCH_BASE) != expected)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            MEM_W32(stub, 0x08000000u | ((0x8001c27cu >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bdffd8);
            input.r[29] += 0x28;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool reference_bearing_gaps(bool disc) {
    const u32 frame = SCRATCH_BASE + 0x100, stub = 0x80120000;
    static const s32 gaps[] = {0, 32766, -32769, -32768, -1};
    for (unsigned i = 0; i < sizeof gaps / sizeof gaps[0]; ++i) {
        CPUState input = initial();
        input.r[16] = 0xc000;
        input.r[2] = (s32)(s16)(gaps[i] + 0x4000);
        input.r[29] = frame;
        MEM_W32(frame + 0x10, 0x98765432);
        MEM_W32(frame + 0x14, input.r[31]);
        input.budget = gaps[i] < 0 ? 25 : 22;
        CPUState native = input;
        if (nc_finish_reference_bearing(&native) || memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (!nc_finish_reference_bearing(&native) || native.budget != 1 ||
            (s32)native.r[2] != gaps[i] || native.r[16] != 0x98765432)
            return false;
        if (disc) {
            /* Enter at the final block with an allocated frame. */
            MEM_W32(stub, 0x08000000u | ((0x8001a764u >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bdffe8);
            input.budget += 2;
            input.r[29] += 0x18;
            interp_call(&input, stub);
            if (memcmp(&input, &native, sizeof input))
                return false;
        }
    }
    return true;
}

static bool actor_distances(bool disc) {
    const u32 from = 0x80120000, to = 0x80121000;
    static const struct {
        bool missing;
        s32 dx, dz;
        u32 distance;
    } cases[] = {{true, 768, 1024, 0},
                 {false, 768, -1024, 5},
                 {false, 0, -1, 1},
                 {false, 255, 0, 0},
                 {false, -256, 0, 1}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[4] = from;
        input.r[5] = cases[i].missing ? 0 : to;
        input.r[29] = SCRATCH_BASE + 0x100;
        MEM_W32(from + 0x27c, (u32)-513);
        MEM_W32(from + 0x284, 127);
        MEM_W32(to + 0x27c, (u32)-513 + cases[i].dx);
        MEM_W32(to + 0x284, 127u + cases[i].dz);
        memset(nc_scratch, 0xa5, SCRATCH_SIZE);
        CPUState native = input;
        if (!nc_begin_actor_distance(&native))
            return false;
        if (!cases[i].missing &&
            (!nc_actor_distance_terms(&native) || !nc_call_actor_distance_root(&native) ||
             !nc_distance_root(&native)))
            return false;
        if (!nc_finish_actor_distance(&native, cases[i].missing) ||
            native.r[2] != cases[i].distance || native.r[31] != input.r[31] ||
            native.r[29] != input.r[29] || MEM_R32(input.r[29] - 8) != input.r[31])
            return false;
        if (disc) {
            u8 after[SCRATCH_SIZE];
            memcpy(after, nc_scratch, sizeof after);
            memset(nc_scratch, 0xa5, sizeof after);
            interp_call(&input, 0x8004ce18);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool point_pose_and_query(bool disc) {
    static const struct {
        s32 x, z, whole_x, whole_z;
        s16 heading;
    } cases[] = {{0, -1, 0, -1, INT16_MIN},
                 {-257, 256, -2, 1, -1},
                 {255, 0, 0, 0, INT16_MAX},
                 {INT32_MIN, INT32_MAX, -8388608, 8388607, 0}};
    const u32 actor = SCRATCH_BASE, pose = actor + 0x100;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial(), native;
        input.r[19] = actor;
        input.r[17] = actor + 0x210;
        input.r[5] = cases[i].z;
        input.r[31] = 0x80024b70;
        MEM_W32(actor + 0x27c, cases[i].x);
        if (!compare(input, 0x8001ae0c, nc_prepare_point_coordinate_query, disc, &native) ||
            (s32)native.r[4] != cases[i].whole_x || (s32)native.r[5] != cases[i].whole_z ||
            native.r[6] != input.r[29] + 0x10 || native.r[31] != 0x8001ae20)
            return false;
        input.r[31] = 0x8001ae34;
        input.budget = 8;
        MEM_W32(actor + 0x20, pose);
        MEM_W16(input.r[17], cases[i].heading);
        MEM_W32(pose + 0x48, 0xa5a5a5a5);
        memcpy(before, nc_scratch, sizeof before);
        native = input;
        if (nc_store_point_pose_heading(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_store_point_pose_heading(&native) || native.budget != 1 ||
            (s32)MEM_R32(pose + 0x48) != cases[i].heading)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x8001adfc);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool point_proximity(bool disc) {
    const s32 distances[] = {-1, 0, 149, 150, 151};
    const u32 actor = SCRATCH_BASE, sp = actor + 0x100;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned path = 0; path < 3; ++path) {
        const bool from_query = path != 0, copy_result = path == 2;
        const u32 entry = from_query ? 0x8001ae20 : 0x8001ae34;
        for (unsigned i = 0; i < sizeof distances / sizeof distances[0]; ++i) {
            CPUState input = initial();
            input.r[2] = path == 1 ? 0 : UINT32_MAX;
            input.r[19] = actor;
            input.r[20] = i % 4;
            input.r[21] = distances[i];
            input.r[29] = sp;
            input.r[31] = distances[i] < 150 ? 0x8001ae40 : 0x8001ae94;
            input.budget = from_query ? (copy_result ? 10 : 4) : 3;
            MEM_W16(sp + 0x14, 0x8001);
            MEM_W16(actor + 0x360, 0xa5a5);
            memcpy(before, nc_scratch, sizeof before);
            CPUState native = input;
            if (nc_check_point_proximity(&native, from_query) ||
                memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_check_point_proximity(&native, from_query) || native.budget != 1 ||
                native.r[2] != (distances[i] < 150) || native.r[4] != input.r[20] ||
                MEM_R16(actor + 0x360) != (copy_result ? 0x8001 : 0xa5a5))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, entry);
                if (memcmp(&input, &native, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool point_index_advances(bool disc) {
    static const struct {
        u32 row;
        u16 current;
        s16 point;
    } cases[] = {{0, 0, 7},      {1, 4, 0}, {2, 0xffff, -1}, {3, 0xfffe, INT16_MIN},
                 {0, 0x7fff, 1}, {3, 8, 0}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        const u32 row = cases[i].row, indices = 0x800d4ca2;
        const u16 next = cases[i].current + 1;
        const u32 entry = 0x80081a08 + 20 * row + 2 * (s16)next;
        const u16 saved = MEM_R16(entry);
        MEM_W16(entry, cases[i].point);
        u16 before[4], expected[4];
        for (unsigned j = 0; j < 4; ++j) {
            before[j] = j == row ? cases[i].current : 0xa500 + j;
            expected[j] = j == row ? (cases[i].point ? next : 0) : before[j];
            MEM_W16(indices + 2 * j, before[j]);
        }
        input.r[4] = row;
        input.r[31] = 0x8001ae94;
        input.budget = cases[i].point ? 24 : 25;
        CPUState native = input;
        if (nc_advance_point_index(&native) || memcmp(&native, &input, sizeof input))
            return false;
        for (unsigned j = 0; j < 4; ++j)
            if (MEM_R16(indices + 2 * j) != before[j])
                return false;
        native.budget = ++input.budget;
        if (!nc_advance_point_index(&native) || native.budget != 1 ||
            (s32)native.r[2] != cases[i].point || native.r[3] != entry)
            return false;
        for (unsigned j = 0; j < 4; ++j) {
            if (MEM_R16(indices + 2 * j) != expected[j])
                return false;
            MEM_W16(indices + 2 * j, before[j]);
        }
        if (disc) {
            interp_call(&input, 0x8001ae40);
            if (memcmp(&native, &input, sizeof input))
                return false;
            for (unsigned j = 0; j < 4; ++j)
                if (MEM_R16(indices + 2 * j) != expected[j])
                    return false;
        }
        MEM_W16(entry, saved);
    }
    return true;
}

static bool point_update_returns(bool disc) {
    const s32 states[] = {INT32_MIN, -1, 0, INT32_MAX};
    const u32 actor = SCRATCH_BASE, frame = SCRATCH_BASE + 0x80;
    for (unsigned i = 0; i < sizeof states / sizeof states[0]; ++i) {
        CPUState input = initial(), native;
        const bool call = states[i] >= 0;
        input.r[19] = actor;
        input.r[31] = call ? 0x80053f38 : 0x8001aeac;
        MEM_W32(actor + 0x2cc, states[i]);
        if (!compare(input, 0x8001ae94, nc_finish_point_update, disc, &native) ||
            native.r[2] != (u32)states[i] || native.r[4] != actor ||
            native.r[5] != (call ? 5 : input.r[5]) ||
            native.budget != input.budget - (call ? 8 : 6))
            return false;
    }
    CPUState input = initial();
    input.r[29] = frame;
    MEM_W32(frame + 0x40, input.r[31]);
    for (unsigned reg = 16; reg <= 21; ++reg)
        MEM_W32(frame + 0x28 + 4 * (reg - 16), 0xabc00000 + reg);
    input.budget = 24;
    CPUState native = input;
    if (nc_restore_point_update_frame(&native) || memcmp(&input, &native, sizeof input))
        return false;
    native.budget = ++input.budget;
    if (!nc_restore_point_update_frame(&native) || native.budget != 1 ||
        native.r[29] != frame + 0x48)
        return false;
    for (unsigned reg = 16; reg <= 21; ++reg)
        if (native.r[reg] != 0xabc00000 + reg)
            return false;
    if (disc) {
        /* Enter with the caller's stack pointer so interp_call recognizes the return. */
        const u32 stub = 0x80120000;
        MEM_W32(stub, 0x08000000u | ((0x8001aeacu >> 2) & 0x03ffffffu));
        MEM_W32(stub + 4, 0x27bdffb8);
        input.r[29] += 0x48;
        input.budget += 2;
        interp_call(&input, stub);
        return !memcmp(&input, &native, sizeof input);
    }
    return true;
}

static bool point_row_records(bool disc) {
    static const struct {
        u32 row;
        s16 cursor, point;
    } cases[] = {
        {0, 0, 0}, {1, 9, 4}, {2, -1, -3}, {3, INT16_MIN, INT16_MAX}, {0, INT16_MAX, INT16_MIN}};
    const u32 base_slot = 0x8008f0c8;
    const u32 saved_base = MEM_R32(base_slot);
    MEM_W32(base_slot, 0x80120000);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const u32 row = cases[i].row, slot = 0x800d4ca2 + 2 * row;
        const u32 entry = 0x80081a08 + 20 * row + 2 * cases[i].cursor;
        const u16 saved_cursor = MEM_R16(slot), saved_point = MEM_R16(entry);
        MEM_W16(slot, cases[i].cursor);
        MEM_W16(entry, cases[i].point);
        CPUState input = initial(), native;
        input.r[18] = row;
        input.r[31] = 0x8001acf0;
        const u32 offset = 36 * cases[i].point;
        if (!compare(input, 0x8001aca0, nc_lookup_point_row_record, disc, &native) ||
            native.budget != input.budget - 26 || native.r[18] != 2 * row ||
            native.r[2] != offset || native.r[3] != 0x80120000 + offset ||
            MEM_R16(slot) != (u16)cases[i].cursor || MEM_R16(entry) != (u16)cases[i].point)
            return false;
        MEM_W16(slot, saved_cursor);
        MEM_W16(entry, saved_point);
    }
    MEM_W32(base_slot, saved_base);
    return true;
}

static bool point_update_entries(bool disc) {
    const u32 frame = SCRATCH_BASE + 0x80, stub = 0x80120000;
    CPUState input = initial(), native;
    input.r[29] = frame + 0x48;
    input.r[31] = 0x8001ac4c;
    input.r[4] = 0x80124000;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    memset(nc_scratch, 0xa5, sizeof before);
    memcpy(before, nc_scratch, sizeof before);
    input.budget = 9;
    native = input;
    if (nc_save_point_update_frame(&native) || memcmp(&native, &input, sizeof input) ||
        memcmp(before, nc_scratch, sizeof before))
        return false;
    native.budget = ++input.budget;
    if (!nc_save_point_update_frame(&native) || native.budget != 1 || native.r[29] != frame ||
        native.r[19] != input.r[4] || MEM_R32(frame + 0x40) != input.r[31])
        return false;
    for (unsigned reg = 16; reg <= 21; ++reg)
        if (MEM_R32(frame + 0x28 + 4 * (reg - 16)) != input.r[reg])
            return false;
    if (disc) {
        memcpy(after, nc_scratch, sizeof after);
        memcpy(nc_scratch, before, sizeof before);
        MEM_W32(stub, 0x08000000u | ((0x8001ac28u >> 2) & 0x03ffffffu));
        MEM_W32(stub + 4, 0x27bd0048);
        input.r[29] = frame;
        input.budget += 2;
        interp_call(&input, stub);
        if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
            return false;
    }
    static const struct {
        u16 state, row, cycles;
        bool selected;
    } cases[] = {{0, 3, 12, false}, {1, 3, 12, false}, {2, 3, 11, true},  {3, 1, 9, true},
                 {4, 2, 13, true},  {5, 0, 15, true},  {6, 0, 13, false}, {65535, 0, 13, false}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        input = initial();
        input.r[19] = 0x80124000;
        input.r[31] = cases[i].selected ? 0x8001aca0 : 0x8001aeac;
        MEM_W16(input.r[19] + 0x386, cases[i].state);
        if (!compare(input, 0x8001ac4c, nc_select_point_update_row, disc, &native) ||
            native.budget != input.budget - cases[i].cycles || native.r[20] != cases[i].row ||
            native.r[18] != (cases[i].selected ? cases[i].row : input.r[18]))
            return false;
    }
    return true;
}

static bool record_point_bearings(bool disc) {
    const u32 record = SCRATCH_BASE, point = record + 0x100, link = record + 0x200;
    static const struct {
        s16 x, z, point_x, point_z;
        s32 dx, dz;
    } cases[] = {{0, 0, 0, 0, 0, 0},
                 {-7, 4, -7, 3, 0, 7},
                 {-4, -8, 3, 8, -7, 0},
                 {INT16_MIN, INT16_MIN, INT16_MAX, INT16_MIN, -65535, -65536},
                 {INT16_MAX, INT16_MAX, INT16_MIN, INT16_MAX, 65535, 65534}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial(), native;
        input.r[16] = record;
        input.r[17] = point;
        input.r[31] = 0x80047684;
        MEM_W16(record + 0x18, cases[i].x);
        MEM_W16(record + 0x1c, cases[i].z);
        MEM_W16(point, cases[i].point_x);
        MEM_W16(point + 4, cases[i].point_z);
        if (!compare(input, 0x800295c4, nc_call_record_point_bearing, disc, &native) ||
            native.r[4] != (u32)cases[i].dx || native.r[5] != (u32)cases[i].dz ||
            native.r[31] != 0x800295e0)
            return false;
        input = native;
        input.r[2] = i * 0x3ff;
        const u32 target = i % 2 ? point : 0;
        input.r[31] = target ? 0x800295f8 : 0x80029688;
        MEM_W32(record + 4, link);
        MEM_W32(link + 4, target);
        if (!compare(input, 0x800295e0, nc_expand_record_point_bearing, disc, &native) ||
            native.r[5] != target || native.r[6] != input.r[2] * 16)
            return false;
    }
    return true;
}

static bool linked_heading_offsets(bool disc) {
    const u32 record = SCRATCH_BASE, point = record + 0x80, node = record + 0x100;
    static const struct {
        s16 point_angle, record_angle;
        u32 bearing, flag;
    } cases[] = {{0, 0, 0, 0},
                 {-0x800, 0, 0x4000, 0x200},
                 {INT16_MIN, INT16_MAX, 0x8000, 0},
                 {INT16_MAX, INT16_MIN, 0xc000, 0x200}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[16] = record;
        input.r[17] = point;
        input.r[5] = node;
        input.r[6] = cases[i].bearing;
        memset(nc_scratch, 0xaa, sizeof before);
        MEM_W16(point + 0xa, cases[i].point_angle);
        MEM_W16(record + 0x24, cases[i].record_angle);
        MEM_W32(record + 0x2c, cases[i].flag | 1);
        const u32 offset = cases[i].point_angle * 16 + 0x8000 - cases[i].record_angle;
        for (unsigned apply = 0; apply <= 1; ++apply) {
            bool (*step)(CPUState *) =
                apply ? nc_apply_linked_heading_offset : nc_set_linked_heading_offset;
            input.r[31] = apply ? 0x80029638 : 0x80029620;
            input.budget = apply ? (cases[i].flag ? 8 : 7) : 16;
            CPUState native = input;
            memcpy(before, nc_scratch, sizeof before);
            if (step(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!step(&native) || native.budget != 1 || MEM_R8(node + 0x4d) != 0xab ||
                MEM_R32(node + 0x40) != offset ||
                MEM_R32(node + 0x44) !=
                    (apply && cases[i].flag ? cases[i].bearing + offset : 0xaaaaaaaau) ||
                native.r[6] != cases[i].bearing + (apply ? offset : 0))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, apply ? 0x80029620 : 0x800295f8);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
            input = native;
        }
    }
    return true;
}

static bool linked_heading_traversal(bool disc) {
    const u32 node = SCRATCH_BASE + 0x100, record = SCRATCH_BASE;
    for (unsigned i = 0; i < 4; ++i) {
        const bool first = (i & 2) == 0;
        const u32 next = i & 1 ? node + 0x100 : 0;
        CPUState input = initial();
        input.r[5] = node;
        input.r[31] = next ? 0x80029648 : 0x800296d0;
        input.budget = !first && next ? 8 : 6;
        MEM_W32(node + 0xc, next);
        CPUState native = input;
        if (nc_follow_heading_link(&native, first) || memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (!nc_follow_heading_link(&native, first) || native.budget != 1 || native.r[5] != next)
            return false;
        if (disc) {
            interp_call(&input, first ? 0x80029638 : 0x80029670);
            if (memcmp(&input, &native, sizeof input))
                return false;
        }
    }
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    const u32 offsets[] = {0, 0xffff8000, 0x8000, 0x10000};
    for (unsigned i = 0; i < 4; ++i) {
        CPUState input = initial();
        input.r[3] = offsets[i];
        input.r[5] = node;
        input.r[6] = i * 0x4000 + offsets[i];
        input.r[16] = record;
        memset(nc_scratch, 0xaa, sizeof before);
        MEM_W32(record + 0x2c, i & 1 ? 0x201 : 1);
        for (unsigned bearing = 0; bearing <= 1; ++bearing) {
            bool (*step)(CPUState *) =
                bearing ? nc_copy_linked_bearing : nc_copy_linked_heading_offset;
            input.r[31] = bearing ? 0x80029670 : 0x80029658;
            input.budget = bearing ? (i & 1 ? 8 : 7) : 6;
            CPUState native = input;
            memcpy(before, nc_scratch, sizeof before);
            if (step(&native) || memcmp(&input, &native, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!step(&native) || native.budget != 1 || MEM_R32(node + 0x40) != offsets[i] ||
                MEM_R8(node + 0x4d) != 0xab ||
                MEM_R32(node + 0x44) != (bearing && (i & 1) ? input.r[6] : 0xaaaaaaaau))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, bearing ? 0x80029658 : 0x80029648);
                if (memcmp(&input, &native, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
            input = native;
        }
    }
    return true;
}

static bool unlinked_pose_headings(bool disc) {
    const u32 record = SCRATCH_BASE, point = record + 0x80, pose = record + 0x100;
    const s16 angles[] = {0, -0x800, INT16_MIN, INT16_MAX};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < 4; ++i) {
        CPUState input = initial();
        input.r[3] = pose;
        input.r[16] = record;
        input.r[17] = point;
        input.r[6] = i * 0x4000;
        const u32 heading = angles[i] * 16 + 0x8000;
        const u32 adjusted = input.r[6] + heading;
        memset(nc_scratch, 0xaa, sizeof before);
        MEM_W16(point + 0xa, angles[i]);
        MEM_W32(record + 4, pose);
        MEM_W32(record + 0x2c, i & 1 ? 0x201 : 1);
        for (unsigned apply = 0; apply <= 1; ++apply) {
            bool (*step)(CPUState *) =
                apply ? nc_apply_unlinked_pose_bearing : nc_set_unlinked_pose_heading;
            input.r[31] = apply ? 0x800296d0 : 0x800296b0;
            input.budget = apply ? (i & 1 ? 12 : 7) : 16;
            CPUState native = input;
            memcpy(before, nc_scratch, sizeof before);
            if (step(&native) || memcmp(&input, &native, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!step(&native) || native.budget != 1 || MEM_R32(pose + 0x48) != heading ||
                MEM_R8(pose + 0x4d) != 0xab || native.r[6] != (apply ? adjusted : input.r[6]) ||
                MEM_R32(pose + 0x44) != (apply && (i & 1) ? adjusted : 0xaaaaaaaau))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, apply ? 0x800296b0 : 0x80029688);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
            input = native;
        }
    }
    return true;
}

static bool record_heading_frames(bool disc) {
    const u32 record = SCRATCH_BASE, frame = SCRATCH_BASE + 0x100;
    const u32 stub = 0x80120000;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned skip = 0; skip <= 1; ++skip) {
        CPUState input = initial();
        input.r[29] = frame + 0x20;
        input.r[31] = skip ? 0x800296d0 : 0x800295c4;
        input.r[4] = record;
        input.r[5] = record + 0x80;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(record + 0x2c, skip ? 0x40000200 : 0x200);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 12;
        CPUState native = input;
        if (nc_begin_record_point_heading(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_begin_record_point_heading(&native) || native.budget != 1 ||
            native.r[2] != (skip ? 0x40000000u : 0u) || native.r[29] != frame ||
            native.r[16] != record || native.r[17] != input.r[5] ||
            MEM_R32(frame + 0x10) != input.r[16] || MEM_R32(frame + 0x14) != input.r[17] ||
            MEM_R32(frame + 0x18) != input.r[31])
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            MEM_W32(stub, 0x08000000u | ((0x8002959cu >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bd0020);
            input.r[29] = frame;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
        input = native;
        input.r[31] = 0x80010000;
        MEM_W32(frame + 0x18, input.r[31]);
        input.budget = 12;
        native = input;
        if (nc_return_record_point_heading(&native) || memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (!nc_return_record_point_heading(&native) || native.budget != 1 ||
            native.r[29] != frame + 0x20 || native.r[16] != MEM_R32(frame + 0x10) ||
            native.r[17] != MEM_R32(frame + 0x14))
            return false;
        if (disc) {
            MEM_W32(stub, 0x08000000u | ((0x800296d0u >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bdffe0);
            input.r[29] += 0x20;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input))
                return false;
        }
    }
    return true;
}

static bool projection_bypass_flags(bool disc) {
    const u32 gp = SCRATCH_BASE, settings = gp + 0x100;
    static const u32 flags[] = {0, 0x20, 0x800000, 0x800020, ~0x800020u};
    MEM_W32(gp + 0x244, settings);
    for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; ++i) {
        CPUState input = initial(), native;
        input.r[28] = gp;
        const u32 bypass = flags[i] & 0x800020;
        input.r[31] = bypass ? 0x80029310 : 0x80029270;
        MEM_W32(settings + 4, flags[i]);
        if (!compare(input, 0x80029254, nc_check_projection_bypass, disc, &native) ||
            native.r[2] != bypass || native.r[3] != 0x800020 || native.budget != input.budget - 11)
            return false;
    }
    return true;
}

static bool record_point_projections(bool disc) {
    const u32 record = SCRATCH_BASE, point = record + 0x80;
    static const struct {
        s16 x, z, point_x, point_z;
        s32 weight_x, weight_z, limit;
    } cases[] = {{0, 0, 0, 0, 1, 1, 0},
                 {-1, 0, 511, 0, 1, 0, 1},
                 {0, 0, 513, 0, 1, 0, 1},
                 {0, 0, 0, 1, 0, 1, 1},
                 {-4, -3, 2, -5, -4, 3, 0},
                 {1, 0, 0, 0, -1, 0, INT32_MAX},
                 {INT16_MAX, INT16_MIN, INT16_MIN, INT16_MIN, INT32_MIN, INT32_MAX, 1}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial(), native;
        input.r[16] = record;
        input.r[19] = point;
        MEM_W16(record + 0x18, cases[i].x);
        MEM_W16(record + 0x1c, cases[i].z);
        MEM_W16(point, cases[i].point_x);
        MEM_W16(point + 4, cases[i].point_z);
        MEM_W32(0x800d7748, cases[i].weight_x);
        MEM_W32(0x800d7750, cases[i].weight_z);
        MEM_W32(0x800d7c14, cases[i].limit);
        const s64 dx = cases[i].point_x - cases[i].x;
        const s64 dz = -cases[i].point_z - cases[i].z;
        const s32 projection = (s32)(dx * cases[i].weight_x + dz * cases[i].weight_z);
        input.r[31] = projection < 0 ? 0x80029460 : 0x800292b8;
        if (!compare(input, 0x80029270, nc_project_record_point, disc, &native) ||
            (s32)native.r[3] != projection || native.budget != input.budget - 30)
            return false;
        if (projection >= 0) {
            input = native;
            const bool outside = projection > (s32)((u32)cases[i].limit << 9);
            input.r[31] = outside ? 0x80029460 : 0x800292d4;
            if (!compare(input, 0x800292b8, nc_check_record_projection_limit, disc, &native) ||
                native.r[2] != outside || native.budget != input.budget - 9)
                return false;
        }
    }
    return true;
}

static bool record_pose_coordinates(bool disc) {
    const u32 record = SCRATCH_BASE + 0x80, pose = record + 0x100;
    const s16 coords[][3] = {
        {0, 0, 0}, {-1, 0, 1}, {INT16_MIN, INT16_MAX, -3}, {INT16_MAX, -1, INT16_MIN}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned call = 0; call < 2; ++call) {
            CPUState input = initial();
            input.r[16] = record;
            input.r[18] = call ? 3 : 1;
            input.r[19] = pose + 0x80;
            input.r[31] = call ? 0x8002959c : 0x80029364;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(record + 4, pose);
            MEM_W32(record + 0x2c, 0x12345);
            for (unsigned axis = 0; axis < 3; ++axis)
                MEM_W16(record + 0x18 + 2 * axis, coords[i][axis]);
            memcpy(before, nc_scratch, sizeof before);
            input.budget = call ? 35 : 33;
            CPUState native = input;
            if (nc_place_record_pose(&native) || memcmp(&input, &native, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_place_record_pose(&native) || native.budget != 1 ||
                MEM_R32(record + 0x2c) != 0x80012345 ||
                MEM_R16(pose + 0x10) != (u16)-coords[i][0] ||
                MEM_R16(pose + 0x12) != (u16)coords[i][1] ||
                MEM_R16(pose + 0x14) != (u16)coords[i][2] || native.r[4] != record ||
                native.r[5] != (call ? input.r[19] : input.r[5]))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80029310);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool placement_point_flags(bool disc) {
    const u32 record = SCRATCH_BASE, table = 0x80120000;
    const u32 saved_table = MEM_R32(0x8008f0c8);
    static const struct {
        u16 count, index;
        u32 flags;
    } cases[] = {{0, 0, 0}, {1, 0, 0x10}, {1, 1, 0x20}, {1, 0x8000, 0x11}, {1, 0xffff, 0xfffffff0}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial(), native;
        input.r[2] = 0; /* The projection limit check passed. */
        input.r[6] = cases[i].count;
        input.r[16] = record;
        const u32 flag = cases[i].flags & 0x10;
        input.r[31] = !cases[i].count || flag ? 0x80029310 : 0x80029460;
        /* Keep the selected entry in RAM even for high unsigned indices. */
        const u32 base = table - 36u * cases[i].index;
        const u32 entry = base + 36u * cases[i].index;
        const u32 saved_flags = MEM_R32(entry);
        MEM_W32(entry, cases[i].flags);
        MEM_W32(0x8008f0c8, base);
        MEM_W16(record + 0x3a, cases[i].index);
        if (!compare(input, 0x800292d4, nc_check_placement_point, disc, &native) ||
            native.r[2] != (cases[i].count ? flag : input.r[2]) ||
            native.r[3] != (cases[i].count ? base : input.r[3]) ||
            native.budget != input.budget - (cases[i].count ? 21 : 2))
            return false;
        MEM_W32(entry, saved_flags);
    }
    MEM_W32(0x8008f0c8, saved_table);
    return true;
}

static bool placement_point_scans(bool disc) {
    const u32 record = SCRATCH_BASE, table = 0x80120000;
    const u32 saved_table = MEM_R32(0x8008f0c8);
    MEM_W32(0x8008f0c8, table);
    static const u16 indices[] = {0, 7, 3};
    for (unsigned i = 0; i < 3; ++i)
        MEM_W16(record + 0x3a + 2 * i, indices[i]);
    for (unsigned count = 0; count <= 3; ++count) {
        for (unsigned match = 0; match <= count; ++match) {
            CPUState input = initial(), native;
            input.r[16] = record;
            input.r[20] = UINT32_MAX;
            input.r[31] = count < 2 ? 0x80029254 : 0x80029200;
            MEM_W16(record + 0x38, count);
            if (!compare(input, 0x800291d8, nc_begin_placement_point_scan, disc, &native) ||
                native.r[6] != count || native.budget != input.budget - (count < 2 ? 7 : 14))
                return false;
            if (count < 2)
                continue;
            for (unsigned i = 0; i < count; ++i) {
                const u32 entry = table + 36 * indices[i];
                const u32 saved_flags = MEM_R32(entry);
                MEM_W32(entry, i == match ? 0x11 : 1);
                input = native;
                input.r[31] = i == match ? 0x8002923c : 0x8002922c;
                if (!compare(input, 0x80029200, nc_test_placement_point_index, disc, &native) ||
                    native.r[3] != entry || native.r[2] != (i == match ? 0x10u : 0u))
                    return false;
                MEM_W32(entry, saved_flags);
                if (i == match)
                    break;
                input = native;
                input.r[31] = i + 1 < count ? 0x80029200 : 0x8002923c;
                if (!compare(input, 0x8002922c, nc_advance_placement_point_index, disc, &native) ||
                    native.r[4] != record + 2 * (i + 1) || native.r[5] != i + 1)
                    return false;
            }
            input = native;
            input.r[31] = match == count ? 0x80029464 : 0x80029310;
            if (!compare(input, 0x8002923c, nc_finish_placement_point_scan, disc, &native) ||
                native.r[20] != (match == count ? 0u : UINT32_MAX))
                return false;
        }
    }
    MEM_W32(0x8008f0c8, saved_table);
    return true;
}

static bool placement_traversal_calls(bool disc) {
    const u32 record = SCRATCH_BASE, pose = record + 0x80;
    MEM_W32(record + 4, pose);
    for (unsigned path = 0; path < 3; ++path) {
        const bool parts = path == 2, nodes = path == 1;
        CPUState input = initial(), native;
        input.r[16] = record;
        input.r[19] = 0x80121000;
        input.r[31] = parts ? 0x800266f4 : nodes ? 0x80026560 : 0x80029388;
        MEM_W32(record + 8, nodes ? pose + 0x80 : 0);
        const u32 pc = parts ? 0x800293d4 : 0x80029364;
        bool (*step)(CPUState *) =
            parts ? nc_call_placement_part_traversal : nc_call_placement_node_traversal;
        if (!compare(input, pc, step, disc, &native) ||
            native.budget != input.budget - (nodes ? 9 : 6) ||
            native.r[4] != (parts ? pose : record) ||
            native.r[5] != (parts || nodes ? input.r[19] + 0x38 : input.r[5]) ||
            native.r[6] != (parts || nodes ? input.r[19] + 0x78 : input.r[6]) ||
            native.r[31] != (parts   ? 0x800293e4
                             : nodes ? 0x80029380
                                     : input.r[31]))
            return false;
    }
    return true;
}

static bool type7_pose_angles(bool disc) {
    const u32 record = SCRATCH_BASE, pose = record + 0x80, child = pose + 0x80;
    static const struct {
        u16 type;
        bool linked;
        s16 angle, global_angle;
    } cases[] = {{5, true, -1, 0},
                 {7, false, INT16_MIN, 0},
                 {7, true, 0, -0x800},
                 {7, true, INT16_MIN, INT16_MAX},
                 {7, true, INT16_MAX, INT16_MIN}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    const u16 saved = MEM_R16(0x800d763a);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[4] = record;
        input.r[5] = record + 0x20;
        input.r[8] = pose;
        memset(nc_scratch, 0xaa, sizeof before);
        MEM_W16(record + 0x5a, cases[i].type);
        MEM_W16(record + 0x24, cases[i].angle);
        MEM_W32(pose + 4, cases[i].linked ? child : 0);
        MEM_W16(0x800d763a, cases[i].global_angle);
        const bool special = cases[i].type == 7;
        const bool has_child = special && cases[i].linked;
        for (unsigned linked = 0; linked <= (unsigned)has_child; ++linked) {
            bool (*step)(CPUState *) =
                linked ? nc_set_type7_linked_heading : nc_set_type7_pose_angles;
            input.r[31] = !special ? 0x80029544 : has_child && !linked ? 0x80029524 : 0x80029584;
            input.budget = linked ? 12 : special ? 19 : 6;
            CPUState native = input;
            memcpy(before, nc_scratch, sizeof before);
            if (step(&native) || memcmp(&input, &native, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!step(&native) || native.budget != 1 ||
                MEM_R32(pose + 0x40) != (special ? (u32)-0x4000 : 0xaaaaaaaau) ||
                MEM_R32(pose + 0x44) != MEM_R32(pose + 0x40) ||
                MEM_R32(pose + 0x48) != (special ? (u32)(s32)cases[i].angle : 0xaaaaaaaau) ||
                MEM_R32(child + 0x40) !=
                    (linked ? (u32)(cases[i].global_angle * 16 + cases[i].angle + 0x8000)
                            : 0xaaaaaaaau))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, linked ? 0x80029524 : 0x800294f0);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
            input = native;
        }
    }
    MEM_W16(0x800d763a, saved);
    return true;
}

static bool placement_angle_steps(bool disc) {
    const u32 record = SCRATCH_BASE, pose = record + 0x80, child = pose + 0x80;
    static const struct {
        u16 type;
        bool linked;
        s16 angle, global_angle;
    } cases[] = {{6, false, 0, 0},
                 {5, true, -1, 1},
                 {6, true, INT16_MIN, INT16_MAX},
                 {6, true, INT16_MAX, INT16_MIN},
                 {0, true, -0x200, -0x800}};
    const u16 saved = MEM_R16(0x800d763a);
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[3] = cases[i].type;
        input.r[4] = record;
        input.r[5] = record + 0x20;
        input.r[7] = 0xffff0000u + i;
        input.r[8] = pose;
        memset(nc_scratch, 0xaa, sizeof before);
        MEM_W32(record + 4, pose);
        MEM_W32(pose + 4, cases[i].linked ? child : 0);
        MEM_W16(record + 0x24, cases[i].angle);
        MEM_W16(0x800d763a, cases[i].global_angle);
        const bool writes = cases[i].linked && cases[i].type != 5;
        for (unsigned finish = 0; finish < 2; ++finish) {
            bool (*step)(CPUState *) =
                finish ? nc_finish_placement_angle_step : nc_set_other_placement_angles;
            input.r[31] = finish ? 0x80010000 : 0x80029584;
            input.budget = finish ? 8 : writes ? 24 : cases[i].linked ? 9 : 6;
            CPUState native = input;
            memcpy(before, nc_scratch, sizeof before);
            if (step(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!step(&native) || native.budget != 1 ||
                MEM_R32(child + 0x40) != (writes ? (u32)(s32)cases[i].angle : 0xaaaaaaaau) ||
                MEM_R32(pose + 0x48) !=
                    (writes ? (u32)(cases[i].global_angle * 16 + 0x8000) : 0xaaaaaaaau) ||
                MEM_R16(record + 0x24) != (u16)(cases[i].angle + (finish ? 0x200 : 0)) ||
                (finish && native.r[2] != input.r[7]))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, finish ? 0x80029584 : 0x80029544);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
            input = native;
        }
    }
    MEM_W16(0x800d763a, saved);
    return true;
}

static bool placement_angle_calls(bool disc) {
    const u32 record = SCRATCH_BASE;
    for (unsigned i = 0; i < 8; ++i) {
        const bool enabled = i & 1, previous = i & 2, result = i & 4;
        const u32 initial_flags = 0x80004001u | (previous ? 0x20000000u : 0);
        CPUState input = initial(), native;
        input.r[4] = input.r[16] = record;
        input.r[31] = enabled ? 0x80029498 : 0x800293d4;
        MEM_W32(record + 0x2c, enabled ? 0x40000001 : 1);
        MEM_W32(record + 0x30, initial_flags);
        if (!compare(input, 0x80029388, nc_call_placement_angle_update, disc, &native) ||
            native.r[5] != record + 0x20 || native.budget != input.budget - (enabled ? 14 : 7))
            return false;
        if (!enabled)
            continue;
        if (native.r[6] != record + 0x28 || native.r[7] != (initial_flags & 0x20000000) ||
            native.r[31] != 0x800293b0)
            return false;
        input = native;
        input.r[2] = result ? UINT32_MAX : 0;
        input.r[31] = 0x800293d4;
        input.budget = 8;
        native = input;
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        if (nc_store_placement_angle_result(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_store_placement_angle_result(&native) || native.budget != 1 ||
            MEM_R32(record + 0x30) != (0x80004001u | (result ? 0x20000000u : 0)))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x800293b0);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_quad_windings(bool disc) {
    const u32 vertices = SCRATCH_BASE + 0x80, indices = SCRATCH_BASE;
    const struct {
        u32 second, third;
        s32 area;
    } cases[] = {{1, 0x10000, 3}, {0x10000, 1, -3}, {1, 1, 0}};
    for (unsigned textured = 0; textured < 2; ++textured) {
        const u32 entry = textured ? 0x800279f8 : 0x80027c0c;
        for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            CPUState input = initial();
            input.r[22] = vertices;
            input.r[textured ? 17 : 20] = textured ? indices - 0xb : indices + 8;
            input.r[29] = vertices + 0x100;
            input.r[31] = entry + 0x44;
            MEM_W16(indices, -1);
            MEM_W16(indices + (textured ? 4 : 2), 1);
            MEM_W16(indices + (textured ? 8 : 6), 0);
            MEM_W32(vertices - 8, 0xffffffff);
            MEM_W32(vertices + 8, cases[i].second);
            MEM_W32(vertices, cases[i].third);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 38;
            CPUState native = input;
            if (nc_measure_placement_quad(&native, textured) ||
                memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_measure_placement_quad(&native, textured) || native.budget != 1 ||
                (s32)native.r[2] != cases[i].area || native.r[3] != 8)
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, entry);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool placement_triangle_windings(bool disc) {
    const u32 vertices = SCRATCH_BASE + 0x80;
    const struct {
        u32 second, third;
        s32 area;
    } cases[] = {
        {0x00000001, 0x00010000, 3}, {0x00010000, 0x00000001, -3}, {0x00000001, 0x00000001, 0}};
    const struct {
        u32 entry, face;
        int first_offset, second_offset, third_offset;
        unsigned first_reg, second_reg, third_reg;
        bool (*measure)(CPUState *);
    } layouts[] = {
        {0x80027600, SCRATCH_BASE + 3, 13, 15, 11, 19, 17, 18, nc_measure_placement_triangle},
        {0x800277e0, SCRATCH_BASE + 10, -6, -4, -2, 18, 16, 17, nc_measure_untextured_triangle}};
    for (unsigned j = 0; j < sizeof layouts / sizeof layouts[0]; ++j) {
        const u32 face = layouts[j].face;
        for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            CPUState input = initial();
            input.r[20] = face;
            input.r[8] = vertices;
            input.r[29] = SCRATCH_BASE + 0x100;
            input.r[31] = layouts[j].entry + 0x44;
            MEM_W16(face + layouts[j].first_offset, 0xffff);
            MEM_W16(face + layouts[j].second_offset, 0);
            MEM_W16(face + layouts[j].third_offset, 1);
            MEM_W32(vertices - 8, 0xffffffff);
            MEM_W32(vertices, cases[i].second);
            MEM_W32(vertices + 8, cases[i].third);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 38;
            CPUState native = input;
            if (layouts[j].measure(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(nc_scratch, before, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!layouts[j].measure(&native) || native.budget != 1 ||
                (s32)native.r[2] != cases[i].area ||
                native.r[layouts[j].first_reg] != vertices - 8 ||
                native.r[layouts[j].second_reg] != vertices ||
                native.r[layouts[j].third_reg] != vertices + 8)
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, layouts[j].entry);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(nc_scratch, after, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool textured_face_entries(bool disc) {
    const u32 frame = SCRATCH_BASE + 0x180;
    for (unsigned quad = 0; quad < 2; ++quad) {
        const u32 entry = quad ? 0x80027954 : 0x8002755c;
        const u32 packet_offset = quad ? 0x380 : 0x37c;
        const u32 count_offset = quad ? 0x2a0 : 0x29c;
        const u32 list_offset = quad ? 0x20 : 0x18;
        for (unsigned overlap = 0; overlap < 2; ++overlap) {
            CPUState input = initial();
            input.r[29] = frame + 0x68;
            input.r[31] = entry + 0x4c;
            input.r[28] = overlap ? frame + 0x54 - packet_offset : SCRATCH_BASE;
            input.r[4] = overlap ? frame + 0x18 - list_offset : SCRATCH_BASE + 0x80;
            MEM_W32(input.r[28] + packet_offset, 0x8000fffc);
            MEM_W32(input.r[28] + count_offset, 0xff);
            MEM_W32(input.r[4] + list_offset, 0x8000fff8);
            MEM_W32(frame + 0x78, 0x80000007);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 27;
            CPUState native = input;
            if (nc_begin_textured_faces(&native, quad) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_begin_textured_faces(&native, quad) || native.budget != 1 ||
                native.r[29] != frame || native.r[quad ? 22 : 8] != input.r[5] ||
                native.r[21] != (overlap ? input.r[21] : 0x8000fffc) ||
                native.r[3] != (overlap ? input.r[7] : 0x8000fff8))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                // Match interp_call's entry SP to the SP at the continuation.
                input.r[29] = frame;
                --input.budget;
                interp_call(&input, entry + 4);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool textured_face_cursors(bool disc) {
    const u32 descriptor = SCRATCH_BASE, frame = descriptor + 0x180;
    const u8 counts[] = {0, 1, 0xff};
    for (unsigned quad = 0; quad < 2; ++quad) {
        const u32 entry = quad ? 0x800279d4 : 0x800275dc;
        for (unsigned i = 0; i < sizeof counts; ++i) {
            CPUState input = initial();
            input.r[4] = descriptor;
            input.r[29] = frame;
            input.r[3] = 0xfffffffe;
            input.r[21] = 0xfffffff0;
            input.r[31] = counts[i] ? entry + 0x10 : (quad ? 0x80027b54 : 0x80027728);
            MEM_W8(descriptor + (quad ? 3 : 1), counts[i]);
            MEM_W32(frame + 0x20, 0xfeedface);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 6;
            CPUState native = input;
            if (nc_read_textured_face_count(&native, quad) ||
                memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_read_textured_face_count(&native, quad) || native.budget != 1 ||
                native.r[4] != counts[i] || native.r[quad ? 23 : 22] ||
                MEM_R32(frame + 0x20) != counts[i])
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, entry);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
            if (!counts[i])
                continue;
            input = native;
            input.r[31] = entry + 0x24;
            input.budget = 5;
            native = input;
            if (nc_init_textured_face_cursors(&native, quad) ||
                memcmp(&native, &input, sizeof input))
                return false;
            native.budget = ++input.budget;
            if (!nc_init_textured_face_cursors(&native, quad) || native.budget != 1 ||
                native.r[quad ? 17 : 20] != 1 || native.r[16] != 6)
                return false;
            if (disc) {
                interp_call(&input, entry + 0x10);
                if (memcmp(&native, &input, sizeof input))
                    return false;
            }
        }
    }
    return true;
}

static bool textured_face_flags(bool disc) {
    const int costs[] = {8, 10, 10, 12, 9, 11, 11, 13};
    for (unsigned quad = 0; quad < 2; ++quad) {
        const u32 entry = quad ? 0x800279a0 : 0x800275a8;
        const unsigned command_reg = quad ? 9 : 7, page_reg = quad ? 10 : 9;
        for (unsigned flags = 0; flags < 16; ++flags) {
            CPUState input = initial();
            input.r[5] = flags | 0x80000000;
            input.r[31] = entry + 0x34;
            input.budget = costs[flags & 7];
            CPUState native = input;
            if (nc_apply_textured_face_flags(&native, quad) ||
                memcmp(&native, &input, sizeof input))
                return false;
            native.budget = ++input.budget;
            if (!nc_apply_textured_face_flags(&native, quad) || native.budget != 1 ||
                native.r[2] != (flags & 4) ||
                native.r[command_reg] != ((flags & 7) ? 2 : input.r[command_reg]) ||
                native.r[page_reg] != ((flags & 2)   ? 0x40u
                                       : (flags & 1) ? 0x20u
                                                     : 0u))
                return false;
            if (disc) {
                interp_call(&input, entry);
                if (memcmp(&native, &input, sizeof input))
                    return false;
            }
        }
    }
    return true;
}

static bool untextured_face_entry(bool disc, bool quad) {
    const u32 frame = SCRATCH_BASE + 0x180, descriptor = SCRATCH_BASE + 0x80;
    const u32 entry = quad ? 0x80027b90 : 0x80027764;
    const u32 branch = entry + 0x60;
    bool (*begin)(CPUState *) = quad ? nc_begin_untextured_quads : nc_begin_untextured_triangles;
    for (unsigned count = 0; count < 2; ++count) {
        for (unsigned flags = 0; flags < 16; ++flags) {
            CPUState input = initial();
            input.r[29] = frame + 0x68;
            input.r[28] = SCRATCH_BASE;
            input.r[4] = descriptor;
            input.r[31] = branch;
            MEM_W32(frame + 0x78, flags);
            MEM_W8(descriptor + (quad ? 2 : 0), count);
            MEM_W32(descriptor + (quad ? 0x1c : 0x14), 0xfffffff8);
            MEM_W32(SCRATCH_BASE + (quad ? 0x358 : 0x354), 0xfffffffc);
            MEM_W32(SCRATCH_BASE + (quad ? 0x3d8 : 0x3d0), 0x7f);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 34;
            CPUState native = input;
            if (begin(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!begin(&native) || native.budget != 1 || native.r[29] != frame ||
                native.r[4] != count || native.r[2] != ((flags & 7) ? 2u : 0u))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                // Account for the initial stack adjustment so interp_call stops at the branch.
                input.r[29] = frame;
                --input.budget;
                interp_call(&input, entry + 4);
                if (memcmp(&input, &native, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
            input = native;
            input.r[31] = count ? branch + 0x1c : (quad ? 0x80027d68 : 0x80027918);
            input.budget = count ? 7 : 2;
            native = input;
            memcpy(before, nc_scratch, sizeof before);
            if (nc_select_untextured_faces(&native, quad) ||
                memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_select_untextured_faces(&native, quad) || native.budget != 1 ||
                MEM_R8(frame + 0x20) != input.r[2] ||
                (count && (native.r[20] != (quad ? 4u : 2u) || native.r[19] != 3)))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, branch);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool placement_face_returns(bool disc) {
    const struct {
        u32 entry, packet_offset, count_offset;
        unsigned count_reg;
    } paths[] = {{0x80027728, 0x37c, 0x29c, 23},
                 {0x80027918, 0x354, 0x3d0, 22},
                 {0x80027b54, 0x380, 0x2a0, 30},
                 {0x80027d68, 0x358, 0x3d8, 23}};
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        for (unsigned overlap = 0; overlap < 2; ++overlap) {
            CPUState input = initial();
            const u32 frame = SCRATCH_BASE + 0x100;
            const u32 gp = overlap ? frame + 0x40 - paths[i].count_offset : SCRATCH_BASE;
            const u32 count = input.r[paths[i].count_reg];
            input.r[29] = frame;
            input.r[28] = gp;
            for (unsigned j = 0; j < 9; ++j)
                MEM_W32(frame + 0x40 + 4 * j, 0xbada0000 + j);
            MEM_W32(frame + 0x64, 0);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 35;
            CPUState native = input;
            if (nc_return_placement_faces(&native, paths[i].packet_offset, paths[i].count_offset,
                                          count) ||
                memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_return_placement_faces(&native, paths[i].packet_offset, paths[i].count_offset,
                                           count) ||
                native.budget != 1 || native.r[29] != frame + 0x68 ||
                native.r[16] != (overlap ? count : 0xbada0000) ||
                MEM_R32(gp + paths[i].packet_offset) != input.r[21] ||
                MEM_R32(gp + paths[i].count_offset) != count)
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_resume_exe(&input, paths[i].entry);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool placement_face_advances(bool disc) {
    const struct {
        bool textured, quad;
        u32 entry, head, done;
        unsigned index_reg;
    } paths[] = {{true, false, 0x80027710, 0x80027600, 0x80027728, 22},
                 {false, false, 0x800278fc, 0x800277e0, 0x80027918, 30},
                 {true, true, 0x80027b3c, 0x800279f8, 0x80027b54, 23},
                 {false, true, 0x80027d4c, 0x80027c0c, 0x80027d68, 8}};
    const struct {
        u32 index, count;
        bool again;
    } cases[] = {
        {0, 2, true}, {0, 1, false}, {1, 1, false}, {0x7fffffff, 0, true}, {0, 0xffffffff, false}};
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        for (unsigned j = 0; j < sizeof cases / sizeof cases[0]; ++j) {
            CPUState input = initial();
            input.r[paths[i].index_reg] = cases[j].index;
            input.r[29] = SCRATCH_BASE + 0x100;
            input.r[31] = cases[j].again ? paths[i].head : paths[i].done;
            MEM_W32(input.r[29] + (paths[i].textured ? 0x20 : 0x18), cases[j].count);
            u8 before[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = paths[i].textured ? 8 : 9;
            CPUState native = input;
            if (nc_advance_placement_face(&native, paths[i].textured, paths[i].quad) ||
                memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_advance_placement_face(&native, paths[i].textured, paths[i].quad) ||
                native.budget != 1 || native.r[2] != cases[j].again ||
                native.r[paths[i].index_reg] != cases[j].index + 1 ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            if (disc) {
                interp_call(&input, paths[i].entry);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(before, nc_scratch, sizeof before))
                    return false;
            }
        }
    }
    return true;
}

static bool untextured_packet_finishes(bool disc) {
    const u32 packet = SCRATCH_BASE + 0x87, source = SCRATCH_BASE + 0x20;
    for (unsigned quad = 0; quad < 2; ++quad) {
        const u32 finish = quad ? 0x80027cf8 : 0x800278a8;
        for (unsigned indexed = 0; indexed < 2; ++indexed) {
            CPUState input = initial();
            input.r[19] = packet;
            input.r[20] = SCRATCH_BASE;
            input.r[quad ? 10 : 9] = source - 4u * 0xffff;
            input.r[quad ? 30 : 23] = source;
            input.r[quad ? 23 : 22] = 0xffffffff;
            input.r[4] = 0x800007ff;
            input.r[29] = packet - 0x20;
            input.r[31] = finish;
            MEM_W16(SCRATCH_BASE, 0xffff);
            MEM_W32(source, 0x81020304);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = indexed ? 11 : 5;
            CPUState native = input;
            if (nc_set_untextured_packet_color(&native, quad, indexed) ||
                memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_set_untextured_packet_color(&native, quad, indexed) || native.budget != 1 ||
                native.r[2] != 0x81020304 || MEM_R32(packet - 3) != native.r[2])
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, finish - (indexed ? 0x28 : 0xc));
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
            input = native;
            if (!indexed) {
                input.r[29] = SCRATCH_BASE + 0x100;
                MEM_W8(input.r[29] + 0x20, 2);
            }
            input.r[31] = finish + 0x1c;
            input.budget = 11;
            native = input;
            memcpy(before, nc_scratch, sizeof before);
            if (nc_finish_untextured_packet(&native, quad) ||
                memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_finish_untextured_packet(&native, quad) || native.budget != 1 ||
                native.r[4] != 0x1ffc || native.r[quad ? 23 : 22] != 0 ||
                native.r[19] != packet + (quad ? 24 : 20) ||
                MEM_R8(packet) != (indexed ? 0x81 : 0x83))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, finish);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool textured_packet_finishes(bool disc) {
    const u32 packet = SCRATCH_BASE + 0x40;
    for (unsigned quad = 0; quad < 2; ++quad) {
        const u32 entry = quad ? 0x80027ae8 : 0x800276bc;
        for (unsigned overlap = 0; overlap < 2; ++overlap) {
            CPUState input = initial();
            const u32 face = overlap ? packet : packet + 0x40;
            input.r[16] = packet;
            input.r[quad ? 17 : 20] = face;
            input.r[quad ? 10 : 9] = 0xbeef0040;
            input.r[quad ? 9 : 7] = 0xcafe0002;
            input.r[31] = entry + 0x1c;
            MEM_W8(face, 0x81);
            MEM_W16(packet, 0x8021);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 11;
            CPUState native = input;
            if (nc_finish_textured_packet(&native, quad) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_finish_textured_packet(&native, quad) || native.budget != 1 ||
                native.r[2] != 0xbeef8061 || native.r[3] != (overlap ? 0xcafe0023 : 0xcafe0083) ||
                native.r[16] != packet + (quad ? 40 : 32) || MEM_R16(packet) != 0x8061 ||
                MEM_R8(packet - 15) != (u8)native.r[3])
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, entry);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool placement_packet_links(bool disc) {
    const struct {
        u32 entry, size;
        unsigned table, tag, address;
    } paths[] = {{0x800276d8, 32, 11, 10, 30},
                 {0x800278c4, 20, 11, 10, 7},
                 {0x80027b04, 40, 12, 11, 8},
                 {0x80027d14, 24, 12, 11, 9}};
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        for (unsigned alias = 0; alias < 2; ++alias) {
            for (unsigned last = 0; last < 2; ++last) {
                CPUState input = initial();
                const u32 offset = last ? 0x1ffc : 0;
                const u32 bucket = SCRATCH_BASE + 0x80;
                const u32 packet = alias ? bucket : SCRATCH_BASE;
                // Exercise the register masks independently of the normal 24-bit address mask.
                const u32 tag_mask = alias ? 0xf000ff00 : 0xff000000;
                const u32 address_mask = alias ? 0x0ff0ffff : 0xffffff;
                input.r[4] = offset;
                input.r[21] = packet;
                input.r[paths[i].tag] = tag_mask;
                input.r[paths[i].address] = address_mask;
                input.r[29] = SCRATCH_BASE + 0x100;
                input.r[31] = paths[i].entry + 0x38;
                MEM_W32(input.r[29] + 0x10, bucket - offset);
                MEM_W32(packet, 0x07234567);
                MEM_W32(bucket, 0xde89abcd);
                u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
                memcpy(before, nc_scratch, sizeof before);
                input.budget = 22;
                CPUState native = input;
                if (nc_link_placement_packet(&native, paths[i].table, tag_mask, address_mask,
                                             paths[i].size) ||
                    memcmp(&native, &input, sizeof input) ||
                    memcmp(before, nc_scratch, sizeof before))
                    return false;
                native.budget = ++input.budget;
                if (!nc_link_placement_packet(&native, paths[i].table, tag_mask, address_mask,
                                              paths[i].size) ||
                    native.budget != 1 || native.r[4] != bucket ||
                    native.r[21] != packet + paths[i].size ||
                    MEM_R32(bucket) != ((0xde89abcd & tag_mask) | (packet & address_mask)) ||
                    (!alias && MEM_R32(packet) != 0x0789abcd))
                    return false;
                if (disc) {
                    memcpy(after, nc_scratch, sizeof after);
                    memcpy(nc_scratch, before, sizeof before);
                    interp_call(&input, paths[i].entry);
                    if (memcmp(&native, &input, sizeof input) ||
                        memcmp(after, nc_scratch, sizeof after))
                        return false;
                }
            }
        }
    }
    return true;
}

static bool textured_quad_packets(bool disc) {
    const u32 vertices = SCRATCH_BASE + 0x20, packet = vertices + 0x82;
    const s16 depths[] = {-32768, -1, 0, 1, 32767};
    for (unsigned i = 0; i < sizeof depths / sizeof depths[0]; ++i) {
        for (unsigned overlap = 0; overlap < 2; ++overlap) {
            CPUState input = initial();
            const u32 face = overlap ? packet - 14 - 0x11 : vertices + 0x3f;
            input.r[16] = packet;
            input.r[17] = face;
            input.r[18] = vertices;
            input.r[19] = vertices + 8;
            input.r[20] = vertices + 16;
            input.r[22] = vertices;
            input.r[29] = vertices + 0x100;
            input.r[30] = 0xffffffff;
            input.r[31] = 0x80027ae8;
            MEM_W32(vertices, 0x80000001);
            MEM_W32(vertices + 8, 0x7fffffff);
            MEM_W32(vertices + 16, 0x00008000);
            MEM_W32(vertices - 8, 0x11223344);
            MEM_W16(vertices + 4, 32767);
            MEM_W16(vertices + 12, -32768);
            MEM_W16(vertices + 20, 1);
            MEM_W16(vertices - 4, depths[i]);
            MEM_W32(face + 1, 0x12345678);
            MEM_W32(face + 5, 0x89abcdef);
            MEM_W16(face + 9, 0x7654);
            MEM_W16(face + 13, 0xfedc);
            MEM_W16(face + 0x11, -1);
            MEM_W32(input.r[29] + 0x18, 0x55aa55aa);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 65;
            CPUState native = input;
            if (nc_write_textured_quad(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_write_textured_quad(&native) || native.budget != 1 || native.r[30] ||
                native.r[4] != ((u32)depths[i] & 0x1ffc) ||
                MEM_R32(packet + 10) != (overlap ? 0x7fffffff : 0x11223344))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80027a5c);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool textured_triangle_packets(bool disc) {
    const u32 vertices = SCRATCH_BASE, packet = vertices + 0x82;
    const s16 depths[] = {-32768, -1, 0, 1, 32767};
    for (unsigned i = 0; i < sizeof depths / sizeof depths[0]; ++i) {
        for (unsigned overlap = 0; overlap <= 1; ++overlap) {
            CPUState input = initial();
            input.r[19] = vertices;
            input.r[17] = vertices + 8;
            input.r[18] = vertices + 16;
            input.r[16] = packet;
            input.r[20] = overlap ? packet - 15 : vertices + 0x3f;
            input.r[29] = vertices + 0x100;
            input.r[23] = 0xffffffff;
            input.r[31] = 0x800276bc;
            MEM_W32(input.r[20] + 1, 0x11223344);
            MEM_W32(input.r[20] + 5, 0x55667788);
            MEM_W16(input.r[20] + 9, 0xabcd);
            MEM_W32(input.r[29] + 0x18, 0x1199aabb);
            MEM_W32(vertices, 0x7fff8000);
            MEM_W32(vertices + 8, 0x0000ffff);
            MEM_W32(vertices + 16, 0x80000000);
            MEM_W16(vertices + 4, 32767);
            MEM_W16(vertices + 12, -32768);
            MEM_W16(vertices + 20, depths[i]);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 42;
            CPUState native = input;
            if (nc_write_textured_triangle(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_write_textured_triangle(&native) || native.budget != 1 || native.r[23] ||
                native.r[4] != ((u32)(2 * depths[i] - 1) & 0x1ffc) ||
                MEM_R32(packet - 10) != (overlap ? 0x7fff8000 : 0x11223344) ||
                MEM_R16(packet + 6) != (overlap ? 0xffff : 0xabcd))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80027664);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool projected_triangle_packets(bool disc) {
    const struct {
        s16 first_z, second_z, third_z;
        s32 depth;
    } cases[] = {{-1, 0, 0, -1},
                 {32767, -32768, 1, 0},
                 {0, 0, -32768, -16384},
                 {32767, 32767, 32767, 32767}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        for (unsigned lookup = 0; lookup <= 1; ++lookup) {
            CPUState input = initial();
            const u32 first = SCRATCH_BASE, second = first + 8, third = second + 8;
            input.r[18] = first;
            input.r[16] = second;
            input.r[17] = third;
            // Also exercise a packet whose first write overlaps the next vertex.
            input.r[19] = (lookup ? second : first + 0x40) - 1;
            input.r[9] = lookup;
            input.r[31] = lookup ? 0x80027880 : 0x8002789c;
            MEM_W32(first, 0x80007fff);
            MEM_W32(second, 0xffff0000);
            MEM_W32(third, 0x00008000);
            MEM_W16(first + 4, cases[i].first_z);
            MEM_W16(second + 4, cases[i].second_z);
            MEM_W16(third + 4, cases[i].third_z);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 27;
            CPUState native = input;
            if (nc_write_projected_triangle(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_write_projected_triangle(&native) || native.budget != 1 ||
                (s32)native.r[3] != cases[i].depth ||
                native.r[4] != ((u32)cases[i].depth & 0x7ff) ||
                MEM_R32(input.r[19] + 5) != (lookup ? 0x80007fff : 0xffff0000) ||
                MEM_R32(input.r[19] + 9) != 0x00008000)
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80027844);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool projected_quad_packets(bool disc) {
    const u32 vertices = SCRATCH_BASE + 0x40, packet = vertices + 0x3f;
    const s16 fourth_depths[] = {-32768, -1, 0, 32767};
    for (unsigned i = 0; i < sizeof fourth_depths / sizeof fourth_depths[0]; ++i) {
        for (unsigned overlap = 0; overlap < 2; ++overlap) {
            CPUState input = initial();
            input.r[16] = vertices;
            input.r[17] = vertices + 8;
            input.r[18] = vertices + 16;
            input.r[19] = packet;
            input.r[20] = overlap ? packet + 5 : vertices + 0x34;
            input.r[22] = vertices;
            input.r[10] = overlap;
            input.r[31] = overlap ? 0x80027cd0 : 0x80027cec;
            MEM_W32(vertices - 8, 0x80007fff);
            MEM_W32(vertices, 0x80000001);
            MEM_W32(vertices + 8, 0xffff0000);
            MEM_W32(vertices + 16, 0x00008000);
            MEM_W16(vertices - 4, fourth_depths[i]);
            MEM_W16(vertices + 4, 32767);
            MEM_W16(vertices + 12, -32768);
            MEM_W16(vertices + 20, 1);
            MEM_W16(input.r[20] - 4, -1);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 44;
            CPUState native = input;
            if (nc_write_projected_quad(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            const s32 depth = fourth_depths[i] >> 2;
            if (!nc_write_projected_quad(&native) || native.budget != 1 ||
                (s32)native.r[3] != depth || native.r[4] != ((u32)depth & 0x7ff) ||
                MEM_R32(packet + 13) != (overlap ? 0xffff0000 : 0x80007fff))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80027c70);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool placement_face_checks(bool disc) {
    const struct {
        u32 entry, reject, limit;
        unsigned first, counter;
    } paths[] = {{0x80027644, 0x80027710, 0x400, 7, 23},
                 {0x80027824, 0x800278fc, 0x80, 7, 22},
                 {0x80027a3c, 0x80027b3c, 0x200, 8, 30},
                 {0x80027c50, 0x80027d4c, 0x80, 8, 23}};
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        for (int area = -1; area <= 1; ++area) {
            for (int delta = -1; delta <= 1; ++delta) {
                CPUState input = initial();
                const u32 count = paths[i].limit + delta;
                const bool expected = area > 0 && delta != 0;
                input.r[2] = (u32)area;
                input.r[paths[i].counter] = count;
                input.r[29] = SCRATCH_BASE + 0x100;
                input.r[31] = expected ? paths[i].entry + 0x20 : paths[i].reject;
                for (unsigned j = 0; j < 4; ++j)
                    MEM_W32(input.r[29] + 0x30 + 4 * j, 0xdead0000 + j);
                u8 before[SCRATCH_SIZE];
                memcpy(before, nc_scratch, sizeof before);
                input.budget = area > 0 ? 16 : 14;
                CPUState native = input;
                bool accept = !expected;
                if (nc_check_placement_face(&native, paths[i].first, count, paths[i].limit,
                                            &accept) ||
                    accept != !expected || memcmp(&native, &input, sizeof input) ||
                    memcmp(before, nc_scratch, sizeof before))
                    return false;
                native.budget = ++input.budget;
                if (!nc_check_placement_face(&native, paths[i].first, count, paths[i].limit,
                                             &accept) ||
                    accept != expected || native.budget != 1 || native.r[2] != paths[i].limit)
                    return false;
                for (unsigned j = 0; j < 4; ++j)
                    if (native.r[paths[i].first + j] != 0xdead0000 + j)
                        return false;
                if (memcmp(before, nc_scratch, sizeof before))
                    return false;
                if (disc) {
                    interp_call(&input, paths[i].entry);
                    if (memcmp(&native, &input, sizeof input) ||
                        memcmp(before, nc_scratch, sizeof before))
                        return false;
                }
            }
        }
    }
    return true;
}

static bool pose_traversal_entries(bool disc) {
    for (unsigned small = 0; small < 2; ++small) {
        const u32 entry = small ? 0x80028c58 : 0x80028a70;
        const u32 size = small ? 0x180 : 0x300;
        const int cycles = small ? 15 : 17;
        for (unsigned project = 0; project < 2; ++project) {
            CPUState input = initial();
            input.r[29] = SCRATCH_BASE + 0x380;
            input.r[4] = SCRATCH_BASE;
            input.r[7] = project;
            input.r[31] =
                small ? (project ? 0x80028c8c : 0x80028db8) : (project ? 0x80028aac : 0x80028b38);
            MEM_W32(input.r[4] + 0x28, 0xfe80ff01);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = cycles;
            CPUState native = input;
            if (nc_begin_pose_traversal(&native, small) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_begin_pose_traversal(&native, small) || native.budget != 1 ||
                native.r[29] != input.r[29] - size || native.r[18] != input.r[5] ||
                native.r[20] != input.r[6] || native.r[19] ||
                MEM_R32(native.r[29] + (small ? 0x164 : 0x2dc)) != 0xfe80ff01)
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                /* Start after addiu sp so the branch destination has the interpreter's entry SP. */
                input.r[29] -= size;
                input.budget--;
                interp_call(&input, entry + 4);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool placement_lighting_entries(bool disc) {
    const u32 frame = SCRATCH_BASE + 0x180;
    for (unsigned alias = 0; alias < 2; ++alias) {
        CPUState input = initial();
        input.r[29] = frame + 0x18;
        input.r[31] = 0x80027e28;
        input.r[7] = alias ? frame + 0xc : frame + 0x24;
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 59;
        CPUState native = input;
        if (nc_begin_placement_lighting(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_begin_placement_lighting(&native) || native.budget != 1 || native.r[29] != frame ||
            native.r[15] != 0x800d7668 || MEM_R32(input.r[7]) != 0 ||
            MEM_R32(frame + 0xc) != (alias ? 0 : input.r[6]))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            // Account for the initial stack adjustment so interp_call stops at the continuation.
            input.r[29] = frame;
            --input.budget;
            interp_call(&input, 0x80027da8);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_projection_matrices(bool disc) {
    const u32 matrix = 0x800d7668, light = matrix + 0x40;
    u32 saved_matrix[8], saved_light[5];
    for (unsigned i = 0; i < 8; ++i) {
        saved_matrix[i] = MEM_R32(matrix + 4 * i);
        MEM_W32(matrix + 4 * i, 0x80000001u + 0x01010101u * i);
    }
    for (unsigned i = 0; i < 5; ++i) {
        saved_light[i] = MEM_R32(light + 4 * i);
        MEM_W32(light + 4 * i, 0xfffe8000u - 0x01010101u * i);
    }
    const u32 entries[] = {0x80028aac, 0x80028d2c};
    bool ok = placement_lighting_entries(disc);
    for (unsigned i = 0; i < 2 && ok; ++i) {
        CPUState input = initial();
        const u32 continuation = entries[i] + 0x24;
        input.r[31] = continuation;
        input.budget = 67;
        CPUState native = input;
        if (nc_install_placement_matrices(&native, continuation) ||
            memcmp(&native, &input, sizeof input)) {
            ok = false;
            break;
        }
        native.budget = ++input.budget;
        ok = nc_install_placement_matrices(&native, continuation) && native.budget == 1 &&
             native.r[4] == matrix && native.r[16] == matrix && native.r[31] == continuation &&
             native.r[8] == MEM_R32(matrix + 20) && native.r[12] == MEM_R32(matrix + 16);
        if (disc && ok) {
            interp_call(&input, entries[i]);
            ok = !memcmp(&native, &input, sizeof input);
        }
    }
    for (unsigned i = 0; i < 8; ++i)
        MEM_W32(matrix + 4 * i, saved_matrix[i]);
    for (unsigned i = 0; i < 5; ++i)
        MEM_W32(light + 4 * i, saved_light[i]);
    return ok;
}

static bool placement_depths(bool disc, u32 cue_pc, u32 depth_offset, unsigned pose_reg,
                             unsigned depth_reg) {
    const u32 frame = SCRATCH_BASE, pose = frame + 0x40;
    const u32 saved_limit = MEM_R32(0x8008f108);
    static const struct {
        s16 x, y, z;
        s32 limit;
        u32 depth;
    } cases[] = {{0, 0, 0, 0, 0},
                 {INT16_MIN, INT16_MAX, -1, 1, 0},
                 {0, -1, INT16_MAX, INT16_MAX - 1, INT16_MAX},
                 {INT16_MAX, 0, 1, INT32_MIN, 1}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[pose_reg] = pose;
        input.r[29] = frame;
        input.r[31] = (s32)cases[i].depth > cases[i].limit ? cue_pc : cue_pc + 0x2c;
        for (unsigned reg = 0; reg < 5; ++reg)
            gte_write_ctrl(&input, reg, reg % 2 == 0 ? 4096 : 0);
        MEM_W16(pose + 0x10, cases[i].x);
        MEM_W16(pose + 0x12, cases[i].y);
        MEM_W16(pose + 0x14, cases[i].z);
        MEM_W32(0x8008f108, (u32)cases[i].limit);
        MEM_W32(frame + depth_offset, 0xdeadbeef);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 23;
        CPUState native = input;
        if (nc_project_placement_depth(&native, native.r[pose_reg], depth_offset, depth_reg) ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_project_placement_depth(&native, native.r[pose_reg], depth_offset, depth_reg) ||
            native.budget != 1 || native.r[depth_reg] != cases[i].depth ||
            native.r[3] != (u32)cases[i].limit ||
            native.r[2] != cases[i].depth - (u32)cases[i].limit ||
            MEM_R32(frame + depth_offset) != cases[i].depth)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, cue_pc - 0x3c);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    MEM_W32(0x8008f108, saved_limit);
    return true;
}

static bool placement_depth_cues(bool disc, u32 cue_pc, u32 depth_offset, unsigned factor_reg) {
    const u32 frame = SCRATCH_BASE;
    static const struct {
        u32 excess_depth, color;
    } cases[] = {
        {512, 0x7e204060}, {1024, 0x7e000000}, {0x3c00, 0x7e80ffff}, {0x40000000, 0x7e4080c0}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[2] = cases[i].excess_depth;
        input.r[29] = frame;
        input.r[31] = cue_pc + 0x2c;
        MEM_W32(frame + depth_offset, 0x12345678);
        MEM_W32(frame + depth_offset + 4, 0x7e4080c0);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 15;
        CPUState native = input;
        if (nc_apply_placement_depth_cue(&native, depth_offset, factor_reg) ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_apply_placement_depth_cue(&native, depth_offset, factor_reg) ||
            native.budget != 1 || native.r[2] != frame + depth_offset + 4 ||
            native.r[factor_reg] != cases[i].excess_depth << 2 ||
            MEM_R32(frame + depth_offset) != native.r[factor_reg] ||
            MEM_R32(frame + depth_offset + 4) != cases[i].color)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, cue_pc);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placement_pose_stack(bool disc) {
    const u32 frame = SCRATCH_BASE, pose = frame + 0x80;
    const struct {
        u32 pc, index, link;
        int cycles;
    } cases[] = {{0x80028b38, 0, 0, 2}, {0x80028b38, 3, 0, 9}, {0x80028b44, 0, 0, 6},
                 {0x80028b44, 2, 0, 6}, {0x80028bf8, 3, 0, 6}, {0x80028bf8, 2, pose, 10},
                 {0x80028db8, 0, 0, 2}, {0x80028db8, 3, 0, 9}, {0x80028dc4, 0, 0, 6},
                 {0x80028dc4, 2, 0, 6}, {0x80028e5c, 3, 0, 6}, {0x80028e5c, 2, pose, 10}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        const bool save = cases[i].pc == 0x80028bf8 || cases[i].pc == 0x80028e5c;
        const bool index_ready = cases[i].pc == 0x80028b44 || cases[i].pc == 0x80028dc4;
        const bool linked = cases[i].pc >= 0x80028db8;
        const u32 output_offset = linked ? 0x58 : 0x38;
        input.r[18] = pose;
        input.r[19] = cases[i].index;
        input.r[29] = frame;
        input.r[31] = save ? cases[i].pc + 0x20 : linked ? 0x80028dd4 : 0x80028b54;
        MEM_W32(pose + 0xc, cases[i].link);
        for (unsigned j = 0; j < 4; ++j)
            MEM_W32(frame + 0x18 + 4 * j, pose + 0x20 * (j + 1));
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = cases[i].cycles;
        CPUState native = input;
        if ((save ? nc_save_linked_placement_pose(&native)
                  : nc_select_placement_pose(&native, index_ready, output_offset)) ||
            memcmp(&native, &input, sizeof input) || memcmp(nc_scratch, before, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!(save ? nc_save_linked_placement_pose(&native)
                   : nc_select_placement_pose(&native, index_ready, output_offset)) ||
            native.budget != 1)
            return false;
        if (save) {
            if (native.r[4] != cases[i].link || native.r[19] != cases[i].index + !!cases[i].link ||
                (cases[i].link && MEM_R32(frame + 0x18 + 4 * cases[i].index) != cases[i].link))
                return false;
        } else {
            const u32 index = cases[i].index - (!index_ready && cases[i].index != 0);
            if (native.r[19] != index || native.r[17] != frame + output_offset ||
                native.r[18] != (index_ready || cases[i].index ? pose + 0x20 * (index + 1) : pose))
                return false;
        }
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, cases[i].pc);
            if (memcmp(&native, &input, sizeof input) || memcmp(nc_scratch, after, sizeof after))
                return false;
        }
    }
    return true;
}

static bool projection_pose_flags(bool disc) {
    const u32 frame = SCRATCH_BASE, pose = frame + 0x40, actor = 0x80121000;
    for (unsigned i = 0; i < 4; ++i) {
        CPUState input = initial(), native;
        const u32 index = i & 1 ? 255 : 0;
        const bool root = i < 2;
        input.r[18] = pose;
        input.r[21] = actor;
        input.r[29] = frame;
        input.r[31] = 0x80028144;
        MEM_W32(actor + 0x20, root ? pose : 0);
        MEM_W32(pose + 0x38, frame + 0x80);
        MEM_W8(pose + 0x4c, index);
        MEM_W16(actor + 0x10a + 2 * index, 0x9f91);
        MEM_W32(frame + 0x2f0, 0x100);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = root ? 6 : 18;
        native = input;
        if (nc_set_projection_pose_mask(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_set_projection_pose_mask(&native) || native.budget != 1 ||
            MEM_R32(frame + 0x2e8) != input.r[2] ||
            MEM_R32(frame + 0x2f0) != (root ? 0x100 : 0x1f00))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            CPUState guest = input;
            interp_call(&guest, 0x80028114);
            if (memcmp(&native, &guest, sizeof guest) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
        MEM_W32(actor + 0x108, root ? pose : 0);
        input.budget = 64;
        input.r[31] = root ? 0x800282d8 : 0x8002817c;
        if (!compare(input, 0x8002816c, nc_load_projection_pose_descriptor, disc, &native) ||
            native.r[19] != frame + 0x80)
            return false;
        input.r[31] = 0x800281a0;
        if (!compare(input, 0x8002817c, nc_load_projection_pose_flags, disc, &native) ||
            native.r[17] != (index ? 0xffff9f91u : 0x81u))
            return false;
    }
    return true;
}

static bool projection_effect_flags(bool disc) {
    static const struct {
        u16 value;
        u32 half;
    } cases[] = {
        {0, 0}, {1, 0}, {2, 1}, {0x7fff, 0x3fff}, {0x8000, 0xffffc000}, {0xffff, 0xffffffff}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        for (unsigned flag = 0; flag <= 1; ++flag) {
            CPUState input = initial();
            const bool expected = flag && cases[i].value;
            input.r[21] = SCRATCH_BASE;
            input.r[31] = expected ? 0x80028080 : 0x800280d8;
            input.budget = cases[i].value ? 14 : 7;
            MEM_W16(SCRATCH_BASE + 0x362, cases[i].value);
            MEM_W32(SCRATCH_BASE + 0x2cc, flag ? 0xffffffff : 0xfffdffff);
            CPUState native = input;
            bool active = !expected;
            if (nc_check_projection_effect(&native, &active) || active == expected ||
                memcmp(&native, &input, sizeof input))
                return false;
            native.budget = ++input.budget;
            if (!nc_check_projection_effect(&native, &active) || active != expected ||
                native.budget != 1 || native.r[2] != cases[i].half || native.r[3] != 0x20000 ||
                native.r[4] != (u32)cases[i].value << 16)
                return false;
            if (disc) {
                interp_call(&input, 0x80028058);
                if (memcmp(&native, &input, sizeof input))
                    return false;
            }
        }
    }
    return true;
}

static bool projection_traversal_entries(bool disc) {
    const u32 frame = SCRATCH_BASE, actor = frame + 0x100;
    for (unsigned bit = 0; bit <= 1; ++bit) {
        CPUState input = initial();
        input.r[4] = actor;
        input.r[29] = frame + 0x320;
        input.budget = 23;
        memset(nc_scratch, 0xa5, SCRATCH_SIZE);
        MEM_W32(actor + 0x2c8, 0xfffffffeu | bit);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        if (nc_begin_projection_traversal(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_begin_projection_traversal(&native) || native.budget != 1 ||
            native.r[29] != frame || native.r[21] != actor || native.r[18] != input.r[5] ||
            native.r[30] != input.r[6] || native.r[22] || native.r[2] != bit ||
            MEM_R32(frame + 0x318) != input.r[30] || MEM_R32(frame + 0x31c) != input.r[31] ||
            MEM_R32(frame + 0x2e8) || MEM_R32(frame + 0x2f0) || MEM_R32(frame + 0x2e0))
            return false;
        for (unsigned reg = 16; reg <= 23; ++reg)
            if (MEM_R32(frame + 0x2f8 + 4 * (reg - 16)) != input.r[reg])
                return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_continue_call(&input, 0x80028004, bit ? 0x80028058 : 0x800280f8, frame);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool projection_face_calls(bool disc, bool textured) {
    bool (*call)(CPUState *, bool) =
        textured ? nc_call_projection_textured_faces : nc_call_projection_untextured_faces;
    for (unsigned quad = 0; quad <= 1; ++quad) {
        CPUState input = initial();
        input.r[29] = SCRATCH_BASE;
        const u32 entry =
            textured ? (quad ? 0x80028280 : 0x8002826c) : (quad ? 0x800282c0 : 0x800282a8);
        const u32 resume =
            textured ? (quad ? 0x80028298 : 0x80028280) : (quad ? 0x800282d8 : 0x800282c0);
        input.r[31] =
            textured ? (quad ? 0x80027954 : 0x8002755c) : (quad ? 0x80027b90 : 0x80027764);
        input.budget = textured && !quad ? 7 : 8;
        MEM_W32(SCRATCH_BASE + (textured ? 0x2e8 : 0x2e0), 0x81234568);
        MEM_W32(SCRATCH_BASE + 0x10, ~input.r[16]);
        CPUState native = input;
        if (call(&native, quad) || memcmp(&input, &native, sizeof input) ||
            MEM_R32(SCRATCH_BASE + 0x10) != ~input.r[16])
            return false;
        native.budget = ++input.budget;
        if (!call(&native, quad) || native.budget != 1 ||
            native.r[4] != input.r[textured && !quad ? 4 : 19] || native.r[5] != input.r[23] ||
            native.r[6] != input.r[30] || native.r[7] != 0x81234568 || native.r[31] != resume ||
            MEM_R32(SCRATCH_BASE + 0x10) != input.r[16])
            return false;
        if (disc) {
            MEM_W32(SCRATCH_BASE + 0x10, ~input.r[16]);
            interp_call(&input, entry);
            if (memcmp(&input, &native, sizeof input) ||
                MEM_R32(SCRATCH_BASE + 0x10) != input.r[16])
                return false;
        }
    }
    return true;
}

static bool untextured_projection_modes(bool disc) {
    const u32 modes[] = {0, 1, 2, 4, 0xffffffff};
    for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
        CPUState input = initial(), native;
        input.r[16] = modes[i];
        input.r[31] = modes[i] == 0 || modes[i] == 4 ? 0x800282a8 : 0x800282d8;
        if (!compare(input, 0x80028298, nc_check_untextured_projection_mode, disc, &native) ||
            native.r[2] != 4 || native.budget != input.budget - (modes[i] ? 4 : 2))
            return false;
    }
    return true;
}

static bool projection_packet_modes(bool disc) {
    const u32 actor = SCRATCH_BASE;
    for (unsigned bits = 0; bits < 32; ++bits) {
        const u32 base_mode = bits & 1;
        const bool pose_override = (bits & 2) != 0;
        const bool inhibit = (bits & 4) != 0;
        const bool actor_override = (bits & 8) != 0;
        const u32 group = (bits & 16) ? 0x1100 : 0;
        MEM_W32(actor + 0x2cc, (base_mode << 29) | (actor_override ? 0x10000 : 0));
        MEM_W32(actor + 0x2c8, inhibit ? 0x40000000 : 0);
        CPUState input = initial(), native;
        input.r[21] = actor;
        input.r[17] = group | (pose_override ? 0xffff8081 : 0x81);
        input.r[31] = 0x8002826c;
        const u32 expected = group                       ? 0
                             : actor_override            ? 4
                             : pose_override && !inhibit ? 2
                                                         : base_mode;
        if (!compare(input, 0x80028214, nc_select_projection_packet_mode, disc, &native) ||
            native.r[16] != expected || native.r[2] != group || native.r[3] != 0x10000 ||
            native.r[4] != input.r[19])
            return false;
    }
    return true;
}

static bool projection_traversal_return(bool disc) {
    CPUState input = initial();
    const u32 frame = SCRATCH_BASE;
    input.r[29] = frame;
    input.budget = 33;
    for (unsigned i = 0; i < 10; ++i)
        MEM_W32(frame + 0x2f8 + 4 * i, 0x80011000 + 4 * i);
    CPUState native = input;
    if (nc_return_projection_traversal(&native) || memcmp(&input, &native, sizeof input))
        return false;
    native.budget = ++input.budget;
    if (!nc_return_projection_traversal(&native) || native.budget != 1 ||
        native.r[29] != frame + 0x320 || native.r[30] != 0x80011020 || native.r[31] != 0x80011024)
        return false;
    for (unsigned reg = 16; reg <= 23; ++reg)
        if (native.r[reg] != 0x80011000 + 4 * (reg - 16))
            return false;
    if (disc) {
        interp_continue_call(&input, 0x8002830c, native.r[31], native.r[29]);
        return !memcmp(&native, &input, sizeof input);
    }
    return true;
}

static bool projection_pose_filters(bool disc) {
    const struct {
        u32 flags, mask;
        bool project;
        int cycles;
    } cases[] = {
        {0x81, 0, true, 14},           {0x181, 0, false, 11},     {0x81, 0x100, false, 8},
        {0xffff8191, 0x300, true, 13}, {0x1a1, 0x100, false, 10}, {0xa1, 0, false, 11},
        {0x80, 0, false, 14},          {0x101, 0x100, false, 13}, {0x201, 0x100, false, 8}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[29] = SCRATCH_BASE;
        input.r[17] = cases[i].flags;
        input.r[31] = cases[i].project ? 0x800281e0 : 0x800282d8;
        MEM_W32(SCRATCH_BASE + 0x2f0, cases[i].mask);
        input.budget = cases[i].cycles;
        CPUState native = input;
        bool project = !cases[i].project;
        if (nc_filter_projection_pose(&native, &project) || memcmp(&input, &native, sizeof input) ||
            project == cases[i].project)
            return false;
        native.budget = ++input.budget;
        if (!nc_filter_projection_pose(&native, &project) || native.budget != 1 ||
            project != cases[i].project)
            return false;
        if (disc) {
            interp_call(&input, 0x800281a0);
            if (memcmp(&native, &input, sizeof input))
                return false;
        }
    }
    return true;
}

static bool projection_pose_selection(bool disc) {
    const u32 frame = SCRATCH_BASE;
    for (unsigned path = 0; path < 4; ++path) {
        const bool ready = path >= 2;
        const u32 count = (path & 1) ? 3 : 0;
        const bool reload = ready || count;
        const u32 slot = count - (reload && !ready);
        const u32 pose = frame + 0x180 + 4 * path;
        CPUState input = initial();
        input.r[22] = count;
        input.r[29] = frame;
        input.r[31] = 0x80028160;
        MEM_W32(frame + 0x18 + 4 * slot, pose);
        input.budget = ready ? 6 : reload ? 9 : 2;
        CPUState native = input;
        if (nc_select_projection_pose(&native, ready) || memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (!nc_select_projection_pose(&native, ready) || native.budget != 1 ||
            native.r[22] != slot || native.r[18] != (reload ? pose : input.r[18]) ||
            native.r[23] != frame + 0x58)
            return false;
        if (disc) {
            interp_call(&input, ready ? 0x80028150 : 0x80028144);
            if (memcmp(&native, &input, sizeof input))
                return false;
        }
        input = native;
        input.r[31] = 0x8002816c;
        input.budget = 64;
        if (!compare(input, 0x80028160, nc_set_projection_pose_cursor, disc, &native) ||
            native.r[20] != frame + 0x18 + 4 * slot || native.budget != 61)
            return false;
    }
    return true;
}

static bool projection_pose_links(bool disc) {
    const u32 pose = SCRATCH_BASE + 0x80;
    for (unsigned path = 0; path < 4; ++path) {
        CPUState input = initial(), native;
        const bool defer = (path & 1) != 0, direct = (path & 2) != 0;
        input.r[18] = pose;
        input.r[20] = pose + 0x40;
        input.r[22] = UINT32_MAX;
        input.r[31] = 0x800282f4;
        MEM_W32(pose + 0xc, defer ? pose + 0x100 : 0);
        MEM_W32(input.r[20], 0xaabbccdd);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = defer ? 9 : 6;
        native = input;
        if (nc_push_projection_pose(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_push_projection_pose(&native) || native.budget != 1 ||
            native.r[22] != input.r[22] + defer || native.r[20] != input.r[20] + 4 * defer ||
            MEM_R32(input.r[20]) != (defer ? pose + 0x100 : 0xaabbccdd))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x800282d8);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
        input = initial();
        input.r[18] = pose;
        input.r[22] = defer;
        input.r[31] = direct ? 0x8002816c : defer ? 0x80028150 : 0x8002830c;
        MEM_W32(pose + 4, direct ? pose + 0x40 : 0);
        if (!compare(input, 0x800282f4, nc_advance_projection_pose, disc, &native) ||
            native.r[18] != (direct ? pose + 0x40 : 0) || native.r[22] != input.r[22] - !direct ||
            native.budget != input.budget - (direct ? 6 : 8))
            return false;
    }
    return true;
}

static bool placement_pose_exits(bool disc, bool small_frame) {
    const u32 frame = SCRATCH_BASE, pose = frame + 0x80;
    const u32 advance_pc = small_frame ? 0x80028e7c : 0x80028c18;
    const u32 return_pc = advance_pc + 0x18;
    const u32 frame_size = small_frame ? 0x180 : 0x300;
    const u32 saved_regs = small_frame ? 0x168 : 0x2e0;
    const unsigned last_reg = small_frame ? 20 : 21;
    for (unsigned path = 0; path < 4; ++path) {
        const bool linked = path == 0;
        const u32 pending = path == 2 ? 0 : path == 3 ? UINT32_MAX : 1;
        CPUState input = initial(), native;
        input.r[18] = pose;
        input.r[19] = pending;
        const u32 next_pose = small_frame ? 0x80028dd4 : 0x80028b54;
        input.r[31] = linked ? next_pose : pending ? next_pose - 0x10 : return_pc;
        MEM_W32(pose + 4, linked ? pose + 0x40 : 0);
        if (!compare(input, advance_pc, nc_advance_placement_pose, disc, &native) ||
            native.r[18] != (linked ? pose + 0x40 : 0) || native.r[19] != pending - !linked ||
            native.budget != input.budget - (linked ? 6 : 8))
            return false;
    }
    CPUState input = initial(), native;
    input.r[29] = frame;
    MEM_W32(frame + (small_frame ? 0x17c : 0x2f8), 0);
    for (unsigned reg = 16; reg <= last_reg; ++reg)
        MEM_W32(frame + saved_regs + 4 * (reg - 16), 0x80000000u + reg);
    u8 before[SCRATCH_SIZE];
    memcpy(before, nc_scratch, sizeof before);
    if (!compare(input, return_pc,
                 small_frame ? nc_return_linked_pose_traversal : nc_return_placement_projection,
                 false, &native) ||
        native.r[31] != 0 || native.r[29] != frame + frame_size ||
        native.budget != input.budget - (small_frame ? 21 : 24) ||
        memcmp(before, nc_scratch, sizeof before))
        return false;
    for (unsigned reg = 16; reg <= last_reg; ++reg)
        if (native.r[reg] != 0x80000000u + reg)
            return false;
    if (disc) {
        /* The epilogue changes SP. Resume until the saved zero return address. */
        interp_resume_exe(&input, return_pc);
        if (memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
    }
    return true;
}

static bool placement_vertex_matrices(bool disc) {
    const u32 descriptor = SCRATCH_BASE, pose = descriptor + 0x40;
    const u32 matrix[] = {0xf0000000, 0x00001000, 0, 0x10000000,
                          0xffff8000, 0x80000000, 0, 0x7fffffff};
    CPUState input = initial();
    input.r[4] = descriptor;
    input.r[5] = pose;
    input.r[6] = pose + 0x80;
    input.r[29] = SCRATCH_BASE + 0x200;
    input.r[31] = 0x800272d8;
    MEM_W32(descriptor + 8, pose + 0x100);
    for (unsigned i = 0; i < 8; ++i)
        MEM_W32(pose + 0x18 + 4 * i, matrix[i]);
    u8 before[SCRATCH_SIZE];
    memcpy(before, nc_scratch, sizeof before);
    CPUState native;
    if (!compare(input, 0x80027288, nc_begin_placement_vertex_projection, false, &native) ||
        native.budget != input.budget - 38 || native.r[29] != input.r[29] - 0x18 ||
        native.r[5] != pose + 0x18 || native.r[7] != input.r[6] || native.r[8] != pose + 0x100)
        return false;
    for (unsigned i = 0; i < 8; ++i)
        if (gte_read_ctrl(&native, i) != matrix[i])
            return false;
    if (disc) {
        /* Start after the stack adjustment so interp_call can stop at the
         * continuation with the same SP it observed at entry. */
        input.r[29] -= 0x18;
        input.budget--;
        interp_call(&input, 0x8002728c);
        if (memcmp(&native, &input, sizeof input))
            return false;
    }
    return !memcmp(before, nc_scratch, sizeof before);
}

static bool placement_vertex_dispatch(bool disc) {
    const u32 descriptor = SCRATCH_BASE, count = 255;
    const u32 table_entry = 0x8009e0d8 + count;
    const u8 saved = MEM_R8(table_entry);
    MEM_W8(descriptor + 4, count);
    for (unsigned path = 0; path < 2; ++path) {
        CPUState input = initial(), native;
        input.r[4] = descriptor;
        input.r[31] = path ? 0x80027300 : 0x80027388;
        MEM_W8(table_entry, path ? 252 : 0);
        if (!compare(input, 0x800272d8, nc_prepare_placement_vertex_groups, disc, &native) ||
            native.r[1] != 0x800a0000 + count || native.r[2] != count || native.r[5] ||
            native.r[9] != (path ? 252u : 0u) || native.r[6] != input.r[6] + (path ? 20 : 0) ||
            native.budget != input.budget - (path ? 14 : 11))
            return false;
        input.r[5] = path ? UINT32_MAX : count;
        input.r[31] = path ? 0x8002739c : 0x800273e4;
        if (!compare(input, 0x80027388, nc_check_placement_vertex_remainder, disc, &native) ||
            native.r[2] != path || native.r[9] != count || native.budget != input.budget - 7)
            return false;
    }
    MEM_W8(table_entry, saved);
    CPUState input = initial(), native;
    input.r[31] = 0;
    if (!compare(input, 0x800273e4, nc_return_placement_vertices, false, &native) ||
        native.r[29] != input.r[29] + 0x18 || native.budget != input.budget - 3)
        return false;
    if (disc) {
        interp_resume_exe(&input, 0x800273e4);
        if (memcmp(&native, &input, sizeof input))
            return false;
    }
    return true;
}

static bool placement_vertex_triples(bool disc, bool sixteenth) {
    const u32 vertices = SCRATCH_BASE, output = vertices + 0x80, frame = output + 0x80;
    const s16 coords[] = {INT16_MIN, -1, 0, 4, 127, INT16_MAX};
    CPUState input = initial();
    input.r[5] = 0;
    input.r[6] = output + 20;
    input.r[7] = output;
    input.r[8] = vertices;
    input.r[9] = 6;
    input.r[10] = frame + 8;
    input.r[11] = frame + 4;
    input.r[29] = frame;
    input.r[31] = sixteenth ? 0x800274f0 : 0x80027388;
    for (unsigned i = 0; i < 5; ++i)
        gte_write_ctrl(&input, i, i % 2 ? 0 : 4096);
    for (unsigned i = 0; i < 6; ++i) {
        MEM_W16(vertices + 8 * i, coords[5 - i]);
        MEM_W16(vertices + 8 * i + 2, coords[(i + 1) % 6]);
        MEM_W16(vertices + 8 * i + 4, coords[i]);
        MEM_W32(output + 8 * i + 4, 0xa5a55a5a);
    }
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    memcpy(before, nc_scratch, sizeof before);
    input.budget = 52;
    CPUState native = input;
    if (nc_project_placement_vertex_triple(&native, sixteenth ? 4 : 2) ||
        memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
        return false;
    native.budget = input.budget = 105;
    if (!nc_project_placement_vertex_triple(&native, sixteenth ? 4 : 2) || native.r[2] != 1 ||
        native.budget != 53 || !nc_project_placement_vertex_triple(&native, sixteenth ? 4 : 2) ||
        native.r[2] != 0 || native.budget != 1 || native.r[3] != (sixteenth ? 2047u : 8191u) ||
        native.r[5] != 6 || native.r[6] != output + 68 || native.r[7] != output + 48 ||
        native.r[8] != vertices + 48)
        return false;
    for (unsigned i = 0; i < 6; ++i) {
        const u32 depth = coords[i] > 0 ? (u32)coords[i] : 0;
        if (MEM_R32(output + 8 * i + 4) != (0xa5a50000u | (depth >> (sixteenth ? 4 : 2))))
            return false;
    }
    if (disc) {
        memcpy(after, nc_scratch, sizeof after);
        memcpy(nc_scratch, before, sizeof before);
        interp_call(&input, sixteenth ? 0x80027468 : 0x80027300);
        if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
            return false;
    }
    return true;
}

static bool indexed_face_selectors(bool disc, bool textured) {
    const u32 entry = textured ? 0x80028400 : 0x8002874c;
    const u32 actor = SCRATCH_BASE, face = actor + 0x83;
    const u32 slots[] = {0, 2, 1};
    MEM_W16(face + (textured ? 13 : 1), 0x00ff);
    MEM_W16(face + (textured ? 15 : 3), 0x0280);
    MEM_W16(face + (textured ? 11 : 5), 0x0100);
    for (unsigned missing = 0; missing < 4; ++missing) {
        CPUState input = initial();
        input.r[17] = face;
        input.r[19] = actor;
        input.r[31] = missing == 3 ? entry + 0x64 : textured ? 0x800286d8 : 0x80028a1c;
        for (unsigned i = 0; i < 3; ++i)
            MEM_W32(actor + 0x4c + 4 * slots[i], i == missing ? 0 : actor + 0x100 + 0x40 * i);
        u8 before[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 37;
        CPUState native = input;
        if (nc_select_indexed_face_poses(&native, textured) ||
            memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (!nc_select_indexed_face_poses(&native, textured) || native.r[6] != 255 ||
            native.r[7] != 128 || native.r[8] != 0 ||
            native.budget != (missing == 0   ? 16
                              : missing == 1 ? 8
                                             : 1) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        if (disc) {
            interp_call(&input, entry);
            if (memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool indexed_vertex_inputs(bool disc) {
    const struct {
        u32 pc;
        unsigned pose_reg, index_reg;
    } sites[] = {{0x80028464, 3, 6}, {0x800284f8, 5, 7}, {0x8002858c, 4, 8},
                 {0x800287b0, 3, 6}, {0x80028844, 5, 7}, {0x800288d8, 4, 8}};
    const u32 pose = SCRATCH_BASE, matrix = pose + 0x18, descriptor = pose + 0x80;
    MEM_W32(pose + 0x38, descriptor);
    MEM_W32(descriptor + 8, 0x80140000);
    for (unsigned i = 0; i < 8; ++i)
        MEM_W32(matrix + 4 * i, 0x80017fffu + i * 0x1010101u);
    u8 before[SCRATCH_SIZE];
    memcpy(before, nc_scratch, sizeof before);
    for (unsigned i = 0; i < sizeof sites / sizeof sites[0]; ++i) {
        CPUState input = initial();
        u32 index = i % 2 ? 255 : 0;
        input.r[2] = matrix;
        input.r[sites[i].pose_reg] = pose;
        input.r[sites[i].index_reg] = index;
        input.r[31] = sites[i].pc + 0x54;
        input.budget = 41;
        CPUState native = input;
        if (nc_prepare_indexed_vertex(&native, pose, index) ||
            memcmp(&input, &native, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (!nc_prepare_indexed_vertex(&native, pose, index) || native.budget != 1 ||
            native.r[2] != 0x80140000 + 8 * index || native.r[3] != 8 * index ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        if (disc) {
            interp_call(&input, sites[i].pc);
            if (memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool indexed_face_lists(bool disc, bool textured) {
    const u32 frame = SCRATCH_BASE, actor = frame, descriptor = frame + 0x100;
    const u32 entry = textured ? 0x800283bc : 0x800286f8;
    const int full_cost = textured ? 22 : 28;
    for (unsigned path = 0; path < 4; ++path) {
        const bool has_list = path != 0, has_colors = path != 1, has_faces = path == 3;
        CPUState input = initial();
        input.r[19] = actor;
        input.r[28] = input.r[29] = frame;
        input.r[30] = descriptor;
        MEM_W32(descriptor + (textured ? 0x18 : 0x14), has_list ? frame + 0x200 : 0);
        MEM_W32(actor + 0x2b4, has_colors ? frame + 0x300 : 0);
        MEM_W8(descriptor + (textured ? 1 : 0), has_faces ? 255 : 0);
        const bool skip = !has_list || (!textured && !has_colors);
        if (skip)
            input.r[31] = textured ? 0x800286f8 : 0x80028a3c;
        else if (!has_faces)
            input.r[31] = textured ? 0x800286f0 : 0x80028a34;
        else
            input.r[31] = textured ? 0x800283f4 : 0x80028740;
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = full_cost;
        CPUState native = input;
        if (nc_begin_indexed_face_list(&native, textured) ||
            memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        const int elapsed = !has_list ? 6 : skip ? 12 : full_cost - (has_faces ? 0 : 5);
        if (!nc_begin_indexed_face_list(&native, textured) ||
            native.budget != input.budget - elapsed)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, entry);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool indexed_face_frames(bool disc) {
    const u32 frame = SCRATCH_BASE + 0x100;
    for (unsigned present = 0; present < 2; ++present) {
        CPUState input = initial();
        input.r[29] = frame + 0x88;
        input.r[4] = SCRATCH_BASE;
        input.r[5] = frame; /* The descriptor load precedes the color store at the same address. */
        MEM_W32(input.r[4] + 0x2e4, 0xff801234);
        MEM_W32(frame + 0x38, present ? SCRATCH_BASE + 0x80 : 0);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 22;
        CPUState native = input;
        if (nc_begin_indexed_faces(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_begin_indexed_faces(&native) || native.budget != 1 || native.r[29] != frame ||
            native.r[30] != (present ? SCRATCH_BASE + 0x80 : 0) ||
            MEM_R32(frame + 0x38) != 0xff801234)
            return false;
        memcpy(after, nc_scratch, sizeof after);
        if (disc) {
            CPUState guest = input;
            memcpy(nc_scratch, before, sizeof before);
            interp_continue_call(&guest, 0x80028340, present ? 0x80028388 : 0x80028a3c, frame);
            if (memcmp(&native, &guest, sizeof guest) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
        for (unsigned reg = 16; reg <= 23; ++reg)
            native.r[reg] = 0;
        native.r[30] = native.r[31] = 0;
        native.budget = 33;
        CPUState guest = native;
        if (nc_return_indexed_faces(&native) || memcmp(&guest, &native, sizeof guest))
            return false;
        native.budget = ++guest.budget;
        if (!nc_return_indexed_faces(&native) || native.budget != 1 ||
            native.r[29] != input.r[29] || native.r[30] != input.r[30] ||
            native.r[31] != input.r[31] || memcmp(&native.r[16], &input.r[16], 8 * sizeof(u32)) ||
            memcmp(after, nc_scratch, sizeof after))
            return false;
        if (disc) {
            interp_continue_call(&guest, 0x80028a3c, input.r[31], input.r[29]);
            if (memcmp(&native, &guest, sizeof guest) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool indexed_face_advances(bool disc, bool textured) {
    const u32 entry = textured ? 0x800286d8 : 0x80028a1c;
    const u32 save = entry + 0x18;
    const struct {
        u32 index;
        u8 count;
        bool repeat;
    } cases[] = {{0, 2, true}, {254, 255, false}, {0, 0, false}, {0x7fffffff, 0, true}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[30] = SCRATCH_BASE;
        input.r[20] = cases[i].index;
        input.r[31] = cases[i].repeat ? (textured ? 0x800283f4 : 0x80028740) : save;
        MEM_W16(SCRATCH_BASE, textured ? (unsigned)cases[i].count << 8 : cases[i].count);
        input.budget = 8;
        CPUState native = input;
        if (nc_advance_indexed_face(&native, textured) || memcmp(&native, &input, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (!nc_advance_indexed_face(&native, textured) || native.budget != 1 ||
            native.r[2] != cases[i].repeat || native.r[20] != cases[i].index + 1 ||
            native.r[17] != input.r[17] + (textured ? 24 : 12))
            return false;
        if (disc) {
            interp_call(&input, entry);
            if (memcmp(&native, &input, sizeof input))
                return false;
        }
    }
    CPUState input = initial();
    input.r[28] = SCRATCH_BASE;
    input.r[31] = save + 8;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    memcpy(before, nc_scratch, sizeof before);
    input.budget = 2;
    CPUState native = input;
    if (nc_save_indexed_face_cursors(&native, textured) || memcmp(&native, &input, sizeof input) ||
        memcmp(before, nc_scratch, sizeof before))
        return false;
    native.budget = ++input.budget;
    if (!nc_save_indexed_face_cursors(&native, textured) || native.budget != 1 ||
        MEM_R32(SCRATCH_BASE + (textured ? 0x37c : 0x354)) != input.r[18] ||
        MEM_R32(SCRATCH_BASE + (textured ? 0x29c : 0x3d0)) != input.r[21])
        return false;
    if (disc) {
        memcpy(after, nc_scratch, sizeof after);
        memcpy(nc_scratch, before, sizeof before);
        interp_call(&input, save);
        if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
            return false;
    }
    return true;
}

static bool indexed_packet_links(bool disc, bool textured) {
    const u32 frame = SCRATCH_BASE, table = frame + 0x80;
    const u32 entry = textured ? 0x80028688 : 0x800289cc;
    for (unsigned alias = 0; alias < 2; ++alias) {
        CPUState input = initial();
        const u32 bucket = table + 4, packet = alias ? bucket : frame + 0xc0;
        input.r[29] = frame;
        input.r[31] = entry + 0x50;
        input.r[textured ? 4 : 3] = 4;
        input.r[16] = packet + 7;
        input.r[17] = frame + 0x60;
        input.r[18] = packet;
        input.r[22] = 0x00ffffff;
        MEM_W8(input.r[17], 0x20);
        MEM_W8(frame + 0x48, 2);
        MEM_W32(frame + 0x30, table);
        MEM_W32(packet, 0x07000000);
        MEM_W32(bucket, 0xab123456);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 32;
        CPUState native = input;
        if (nc_link_indexed_face_packet(&native, textured) ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_link_indexed_face_packet(&native, textured) || native.budget != 1 ||
            MEM_R32(bucket) != (0xab000000u | (packet & 0xffffff)) ||
            (!alias && MEM_R32(packet) != 0x07123456) || MEM_R8(packet + 7) != 0x22)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, entry);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool indexed_face_packets(bool disc, bool textured) {
    const int cycles = textured ? 42 : 38;
    const u32 frame = SCRATCH_BASE, colors = frame + 0x100;
    const u32 entry = textured ? 0x80028630 : 0x8002897c;
    for (unsigned alias = 0; alias < 2; ++alias) {
        CPUState input = initial();
        const u32 packet = frame + (alias ? 0x17 : 0x87);
        const u32 face = frame + 0xc3;
        input.r[29] = frame;
        input.r[16] = packet;
        input.r[17] = face;
        input.r[21] = 0xffffffff;
        input.r[31] = textured ? 0x80028688 : 0x800289cc;
        MEM_W32(frame + 0x10, 0xffff0000);
        MEM_W32(frame + 0x18, 0x00008000);
        MEM_W32(frame + 0x20, 0x7fff0001);
        MEM_W16(frame + 0x14, 0xfffb);
        MEM_W16(frame + 0x1c, 0x7fff);
        MEM_W16(frame + 0x24, 0x8000);
        MEM_W32(frame + 0x38, 0x11223344);
        MEM_W32(frame + 0x40, colors);
        MEM_W32(face + 1, 0x55667788);
        MEM_W32(face + 5, 0x000292ab);
        MEM_W16(face + 9, 0xface);
        MEM_W32(colors + 8, 0xaabbccdd);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = cycles;
        CPUState native = input;
        if (nc_write_indexed_face_packet(&native, textured) ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_write_indexed_face_packet(&native, textured) || native.budget != 1 ||
            native.r[21] != 0 || native.r[textured ? 4 : 3] != ((u32)-11 & 0x1ffc) ||
            MEM_R32(packet - 3) != (textured ? 0x11223344u : 0xaabbccddu))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, entry);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool indexed_face_windings(bool disc) {
    const u32 frame = SCRATCH_BASE;
    const struct {
        u32 pc, skip;
    } sites[] = {{0x8002860c, 0x800286d8}, {0x80028958, 0x80028a1c}};
    const struct {
        u32 second, third;
        s32 area;
    } cases[] = {{1, 0x10000, 3}, {0x10000, 1, -3}, {1, 1, 0}};
    for (unsigned i = 0; i < sizeof sites / sizeof sites[0]; ++i) {
        for (unsigned j = 0; j < sizeof cases / sizeof cases[0]; ++j) {
            CPUState input = initial();
            input.r[29] = frame;
            input.r[31] = cases[j].area > 0 ? sites[i].pc + 0x24 : sites[i].skip;
            const u32 return_pc = sites[i].pc + 0x1c;
            MEM_W32(frame + 0x10, 0xffffffff);
            MEM_W32(frame + 0x18, cases[j].second);
            MEM_W32(frame + 0x20, cases[j].third);
            MEM_W32(frame + 0x24, 0xa5a55a5a);
            MEM_W32(frame + 0x28, (u32)-5);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 26;
            CPUState native = input;
            if (nc_measure_indexed_face(&native, return_pc) ||
                memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_measure_indexed_face(&native, return_pc) || native.budget != 1 ||
                (s32)native.r[2] != cases[j].area || native.r[31] != return_pc ||
                MEM_R32(frame + 0x24) != 0xa5a5fffe)
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, sites[i].pc);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool indexed_depth_stores(bool disc) {
    const struct {
        u32 pc, offset;
        unsigned next_pose_reg;
    } sites[] = {
        {0x800284e4, 0x14, 5}, {0x80028578, 0x1c, 4}, {0x80028830, 0x14, 5}, {0x800288c4, 0x1c, 4}};
    const s32 depths[] = {-5, 0, 3, 4, 16383, INT32_MIN};
    const u32 frame = SCRATCH_BASE;
    for (unsigned i = 0; i < sizeof sites / sizeof sites[0]; ++i) {
        for (unsigned j = 0; j < sizeof depths / sizeof depths[0]; ++j) {
            CPUState input = initial();
            input.r[29] = frame;
            input.r[31] = sites[i].pc + 0x14;
            const u32 next_pose = input.r[sites[i].next_pose_reg];
            MEM_W32(frame + 0x28, (u32)depths[j]);
            MEM_W32(frame + sites[i].offset, 0xa5a55a5a);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 7;
            CPUState native = input;
            if (nc_store_indexed_vertex_depth(&native, sites[i].offset, next_pose) ||
                memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_store_indexed_vertex_depth(&native, sites[i].offset, next_pose) ||
                native.budget != 1 || native.r[2] != next_pose + 0x18 ||
                MEM_R32(frame + sites[i].offset) != (0xa5a50000u | (u16)(depths[j] >> 2)))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, sites[i].pc);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool indexed_vertex_projections(bool disc) {
    const u32 vertex = SCRATCH_BASE, frame = vertex + 0x80;
    const struct {
        u32 pc, offset;
    } sites[] = {{0x800284b8, 0x10}, {0x8002854c, 0x18}, {0x800285e0, 0x20},
                 {0x80028804, 0x10}, {0x80028898, 0x18}, {0x8002892c, 0x20}};
    const s16 points[][3] = {{INT16_MIN, 0, -1}, {0, INT16_MAX, INT16_MAX}, {0, 0, 0}, {-2, 3, 7}};
    for (unsigned i = 0; i < sizeof sites / sizeof sites[0]; ++i) {
        for (unsigned j = 0; j < sizeof points / sizeof points[0]; ++j) {
            CPUState input = initial();
            input.r[2] = vertex;
            input.r[29] = frame;
            input.r[31] = sites[i].pc + 0x2c;
            const u32 screen = frame + sites[i].offset;
            input.r[23] = j == 3 ? screen : frame + 0x40;
            for (unsigned k = 0; k < 5; ++k)
                gte_write_ctrl(&input, k, k % 2 ? 0 : 4096);
            gte_write_ctrl(&input, 26, 256);
            for (unsigned k = 0; k < 3; ++k)
                MEM_W16(vertex + 2 * k, (u16)points[j][k]);
            MEM_W32(screen, 0xdeadbeef);
            MEM_W32(input.r[23], 0x11223344);
            u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 15;
            CPUState native = input;
            if (nc_project_indexed_vertex(&native, sites[i].offset) ||
                memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            const u32 depth = points[j][2] > 0 ? (u32)points[j][2] >> 2 : 0;
            if (!nc_project_indexed_vertex(&native, sites[i].offset) || native.budget != 1 ||
                native.r[2] != screen || native.r[12] != depth || MEM_R32(input.r[23]) != depth ||
                (input.r[23] != screen && MEM_R32(screen) != gte_read_data(&native, 14)))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, sites[i].pc);
                if (memcmp(&input, &native, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool remaining_placement_vertices(bool disc, bool sixteenth) {
    const u32 vertices = SCRATCH_BASE, output = vertices + 0x40, frame = output + 0x40;
    CPUState input = initial();
    input.r[5] = 3;
    input.r[9] = 5;
    input.r[7] = output;
    input.r[8] = vertices;
    input.r[29] = frame;
    input.r[31] = sixteenth ? 0x80027550 : 0x800273e4;
    for (unsigned i = 0; i < 5; ++i)
        gte_write_ctrl(&input, i, i % 2 ? 0 : 4096);
    MEM_W32(vertices, 0x7fff8000);
    MEM_W32(vertices + 4, 0xffff);
    MEM_W32(vertices + 8, 0x0000ffff);
    MEM_W32(vertices + 12, 32767);
    MEM_W32(output + 4, 0xa5a55a5a);
    MEM_W32(output + 12, 0xa5a55a5a);
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    memcpy(before, nc_scratch, sizeof before);
    const int cycles = sixteenth ? 25 : 24;
    input.budget = cycles;
    CPUState native = input;
    if (nc_project_remaining_placement_vertex(&native, sixteenth) ||
        memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
        return false;
    input.budget = native.budget = 2 * cycles + 1;
    if (!nc_project_remaining_placement_vertex(&native, sixteenth) || native.r[2] != 1 ||
        native.budget != cycles + 1 || MEM_R32(output + 4) != 0xa5a50000 ||
        !nc_project_remaining_placement_vertex(&native, sixteenth) || native.r[2] ||
        native.budget != 1 || native.r[8] != vertices + 16 || native.r[7] != output + 16 ||
        native.r[5] != 5 || native.r[12] != 8191 || MEM_R32(frame) != 8191 ||
        MEM_R32(output + 12) != (sixteenth ? 0xa5a507ffu : 0xa5a51fffu))
        return false;
    if (disc) {
        memcpy(after, nc_scratch, sizeof after);
        memcpy(nc_scratch, before, sizeof before);
        interp_call(&input, sixteenth ? 0x80027504 : 0x8002739c);
        if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
            return false;
    }
    return true;
}

static bool indexed_pose_projection_calls(bool disc) {
    const u32 actor = SCRATCH_BASE, pose = actor + 0x80;
    for (unsigned i = 0; i < 3; ++i) {
        const u32 index = i == 2 ? 255 : 0;
        const u32 flags = actor + 0x10a + 2 * index;
        CPUState input = initial();
        input.r[2] = i ? 0x10 : 0;
        input.r[18] = pose;
        input.r[19] = actor + 0x180;
        input.r[21] = actor;
        input.r[29] = actor + 0x40;
        input.r[31] = 0x800273f0;
        MEM_W8(pose + 0x4c, index);
        MEM_W16(flags, 0xffff);
        u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        input.budget = i ? 17 : 5;
        CPUState native = input;
        if (nc_call_indexed_pose_projection(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_call_indexed_pose_projection(&native) || native.budget != 1 ||
            native.r[31] != 0x80028214 || native.r[4] != input.r[19] || native.r[5] != pose ||
            native.r[6] != input.r[29] + 0x58 || MEM_R16(flags) != (i ? 0xff6f : 0xffff))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x800281e0);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool pose_vertex_projection_calls(bool disc) {
    const u32 pose = SCRATCH_BASE, descriptor = pose + 0x80;
    const struct {
        u32 entry, skip, resume;
    } sites[] = {{0x80028b54, 0x80028bf8, 0x80028b7c}, {0x80028dd4, 0x80028e5c, 0x80028dfc}};
    MEM_W32(pose + 0x38, descriptor);
    for (unsigned site = 0; site < 2; ++site) {
        for (unsigned project = 0; project < 2; ++project) {
            CPUState input = initial();
            input.r[18] = pose;
            input.r[17] = pose + 0x100;
            input.r[31] = project ? 0x80027288 : sites[site].skip;
            MEM_W8(descriptor + 5, project ? 0xff : 0xfd);
            u8 before[SCRATCH_SIZE];
            memcpy(before, nc_scratch, sizeof before);
            input.budget = project ? 14 : 11;
            CPUState native = input;
            if (nc_call_pose_vertex_projection(&native, sites[site].resume) ||
                memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_call_pose_vertex_projection(&native, sites[site].resume) ||
                native.budget != 1 || native.r[2] != 2 * project || native.r[16] != descriptor ||
                native.r[4] != descriptor || native.r[5] != (project ? pose : input.r[5]) ||
                native.r[6] != (project ? input.r[17] : input.r[6]) ||
                native.r[31] != (project ? sites[site].resume : sites[site].skip))
                return false;
            if (disc) {
                interp_call(&input, sites[site].entry);
                if (memcmp(&input, &native, sizeof input))
                    return false;
            }
            if (memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool placement_transform_flags(bool disc) {
    const u32 record = SCRATCH_BASE, owner = record + 0x80, descriptor = owner + 0x80;
    for (unsigned flags = 0; flags < 4; ++flags) {
        const bool enabled = flags & 1, selected = flags & 2;
        const bool call = enabled && selected;
        CPUState input = initial(), native;
        input.r[16] = record;
        input.r[31] = call ? 0x8002a674 : 0x80029460;
        MEM_W32(record, owner);
        MEM_W32(record + 0x2c, enabled ? 0xffffffff : 0xffffffbf);
        MEM_W32(owner + 0x14, descriptor);
        MEM_W32(descriptor + 0x10, selected ? 0xffffffff : 0xfffeffff);
        u8 before[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        if (!compare(input, 0x80029420, nc_check_placement_transform, disc, &native) ||
            native.r[2] != (call ? 0x10000u : 0u) || native.r[3] != 0x10000 ||
            native.r[4] != (call ? record : input.r[4]) || native.r[31] != 0x80029460 ||
            native.budget != input.budget - (call      ? 24
                                             : enabled ? 22
                                                       : 7) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
    }
    return true;
}

static bool placement_projection_args(bool disc) {
    const u32 gp = SCRATCH_BASE, record = gp + 0x80, pose = record + 0x40;
    MEM_W32(record + 4, pose);
    MEM_W32(gp + 0x2bc, 0x80123000);
    MEM_W32(gp + 0x364, 0xffffff80);
    for (unsigned alternate = 0; alternate <= 1; ++alternate) {
        CPUState input = initial(), native;
        input.r[16] = record;
        input.r[28] = gp;
        input.r[31] = 0x80029418;
        MEM_W32(record + 0x30, alternate ? 0xffffffff : 0xffefffff);
        u8 before[SCRATCH_SIZE];
        memcpy(before, nc_scratch, sizeof before);
        if (!compare(input, 0x800293e4, nc_select_placement_projection_args, disc, &native) ||
            native.r[2] != (alternate ? 0x100000u : 0u) || native.r[3] != 0x100000 ||
            native.r[4] != record || native.r[5] != pose ||
            native.r[6] != (alternate ? 0x20u : 0x80123000u) || native.r[7] != 1 ||
            native.budget != input.budget - (alternate ? 16 : 14) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
    }
    return true;
}

static bool placement_angle_entries(bool disc) {
    const u32 record = SCRATCH_BASE, pose = record + 0x80;
    static const struct {
        u32 direction, value, next, next_direction;
        int cycles;
    } cases[] = {{0x20000000, 0x808080, 0x808080, 0, 13},
                 {1, 0x808081, 0x848485, 1, 15},
                 {0xffffffff, 0xfffffffe, 0x40402, 0xffffffff, 15},
                 {0, 0x20201f, 0x20201f, 1, 15},
                 {0, 0x202020, 0x1c1c1c, 0, 16},
                 {0, 0x80000000, 0x80000000, 1, 15}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[4] = record;
        input.r[6] = record + 0x28;
        input.r[7] = cases[i].direction;
        input.r[31] = 0x800294f0;
        input.budget = cases[i].cycles;
        MEM_W32(record + 4, pose);
        MEM_W32(input.r[6], cases[i].value);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        if (nc_begin_placement_angle_update(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_begin_placement_angle_update(&native) || native.budget != 1 ||
            native.r[8] != pose || native.r[3] != cases[i].value ||
            native.r[7] != cases[i].next_direction || MEM_R32(input.r[6]) != cases[i].next)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x80029498);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool point_strides(bool disc) {
    static const struct {
        u32 position, sample;
        s32 step;
    } cases[] = {{0xfffffd00, 0, 0},
                 {0x7fffffff, 16384, 5120},
                 {0x80000000, 0xffffc000, -5120},
                 {0, 0xabcdffff, -1},
                 {10, 3, 0},
                 {10, 4, 1}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        for (unsigned axis = 0; axis < 2; ++axis) {
            CPUState input = initial();
            const u32 coord = SCRATCH_BASE + (axis ? 0x284 : 0x27c);
            const u32 flags = i % 2 ? 0x40001 : 1;
            const u32 pc = axis ? 0x8001adcc : 0x8001ada4;
            input.r[31] = axis ? (flags & 0x40000 ? 0x8001adfc : 0x8001ae0c) : 0x8003531c;
            input.r[19] = SCRATCH_BASE;
            input.r[2] = cases[i].sample;
            input.budget = axis ? 16 : 12;
            bool (*step)(CPUState *) = axis ? nc_step_point_z : nc_step_point_x;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(coord, cases[i].position);
            MEM_W32(SCRATCH_BASE + 0x2c8, flags);
            memcpy(before, nc_scratch, sizeof before);
            CPUState native = input;
            if (step(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!step(&native) || native.budget != 1 ||
                MEM_R32(coord) != cases[i].position + cases[i].step)
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, pc);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool point_heading_turns(bool disc) {
    const u32 headings = 0x800d4c9a;
    const u16 gaps[] = {0, 7, 8, 0x7ffe, 0x7fff, 0x8000, 0xfff7, 0xffff};
    for (unsigned i = 0; i < sizeof gaps / sizeof gaps[0]; ++i) {
        CPUState input = initial();
        input.r[2] = (i % 4) * 0x400;
        input.r[20] = i % 4;
        input.r[18] = input.r[20] * 2;
        input.r[31] = 0x80035338;
        const u16 gap = gaps[i];
        u16 before[4] = {11, 22, 33, 44}, expected[4];
        before[input.r[20]] = (input.r[2] << 4) - gap;
        memcpy(expected, before, sizeof before);
        const s32 turn = gap >= 0x7fff ? (s32)gap - 65536 : gap;
        expected[input.r[20]] += turn >> 3;
        for (unsigned j = 0; j < 4; ++j)
            MEM_W16(headings + 2 * j, before[j]);
        input.budget = 29 + (gap >= 0x7fff);
        CPUState native = input;
        if (nc_turn_toward_point(&native) || memcmp(&native, &input, sizeof input))
            return false;
        for (unsigned j = 0; j < 4; ++j)
            if (MEM_R16(headings + 2 * j) != before[j])
                return false;
        native.budget = ++input.budget;
        if (!nc_turn_toward_point(&native) || native.budget != 1 ||
            (s32)native.r[4] != (s16)expected[input.r[20]])
            return false;
        for (unsigned j = 0; j < 4; ++j) {
            if (MEM_R16(headings + 2 * j) != expected[j])
                return false;
            MEM_W16(headings + 2 * j, before[j]);
        }
        if (disc) {
            interp_call(&input, 0x8001ad3c);
            if (memcmp(&native, &input, sizeof input))
                return false;
            for (unsigned j = 0; j < 4; ++j)
                if (MEM_R16(headings + 2 * j) != expected[j])
                    return false;
        }
    }
    return true;
}

static bool point_distance_bearings(bool disc) {
    const u32 point = SCRATCH_BASE, actor = 0x80121000;
    static const struct {
        s32 x, z;
        s16 stored_x, stored_z;
        s32 dx, dz;
    } cases[] = {{-1, 255, 1, 0, 0, 0},
                 {-513, 0, 0, -4, 3, -4},
                 {256, -257, -1, 7, 0, 9},
                 {INT32_MIN, INT32_MAX, INT16_MIN, INT16_MAX, 8421376, -8355840}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial(), native;
        input.r[3] = point;
        input.r[19] = actor;
        input.r[31] = 0x800475e4;
        MEM_W32(actor + 0x27c, cases[i].x);
        MEM_W32(actor + 0x284, cases[i].z);
        MEM_W16(point + 0xa, cases[i].stored_x);
        MEM_W16(point + 0xc, cases[i].stored_z);
        const u32 dx = cases[i].dx, dz = cases[i].dz;
        if (!compare(input, 0x8001acf0, nc_prepare_point_distance_root, disc, &native) ||
            native.r[16] != dx || native.r[17] != dz || native.r[4] != dx * dx + dz * dz)
            return false;
        input = native;
        if (!nc_distance_root(&input))
            return false;
        input.r[31] = 0x80047684;
        if (!compare(input, 0x8001ad2c, nc_call_point_distance_bearing, disc, &native) ||
            native.r[4] != dx || native.r[5] != dz || native.r[21] != input.r[2])
            return false;
    }
    return true;
}

static bool displacements(bool disc) {
    CPUState c = initial(), result;
    c.r[4] = 0x80120000;
    c.r[5] = 0x80121000;
    c.r[31] = 0x8004CE58;
    const u32 coords[] = {0, 1, 255, 256, 257, 0x7fffffff, 0x80000000, 0xffffffff};
    for (unsigned i = 0; i < 8; i++)
        for (unsigned j = 0; j < 8; j++) {
            MEM_W32(c.r[4] + 0x27C, coords[i]);
            MEM_W32(c.r[5] + 0x27C, coords[j]);
            MEM_W32(c.r[4] + 0x284, coords[j]);
            MEM_W32(c.r[5] + 0x284, coords[i]);
            if (!compare(c, 0x8004CE24, nc_actor_distance_terms, disc, &result))
                return false;
        }
    return true;
}

static bool headings(bool disc) {
    CPUState c = initial(), result;
    const s32 axes[] = {-2147483647, -2097151, -1024, -17, -1, 0, 1, 17, 1024, 2097151, 2147483647};
    /* Non-disc runs use a synthetic ramp. Disc runs read the game's table. */
    if (!disc)
        for (unsigned i = 0; i <= 1024; i++)
            MEM_W16(0x80085C4C + i * 2, i / 2);
    for (unsigned i = 0; i < sizeof axes / sizeof axes[0]; i++)
        for (unsigned j = 0; j < sizeof axes / sizeof axes[0]; j++) {
            c.r[4] = (u32)axes[i];
            c.r[5] = (u32)axes[j];
            bool wrapped = (axes[i] > 2097151 || axes[i] < -2097151) &&
                           (axes[j] > 2097151 || axes[j] < -2097151);
            if (wrapped) {
                CPUState before = c;
                if (nc_vector_heading(&c) || memcmp(&before, &c, sizeof c))
                    return false;
            } else if (!compare(c, 0x80047684, nc_vector_heading, disc, &result))
                return false;
        }
    c.r[4] = c.r[5] = 0;
    return compare(c, 0x80047684, nc_vector_heading, disc, &result) && result.r[2] == 512;
}

static bool heading_samples(bool disc) {
    const s16 cardinal[] = {0, 16384, 0, -16384};
    CPUState c = initial(), result;
    for (unsigned table = 0; table < 2; table++) {
        bool (*sample)(CPUState *) = table ? nc_heading_sine : nc_heading_cosine;
        u32 address = table ? 0x80083be4 : 0x80081be4;
        u32 pc = table ? 0x80035338 : 0x8003531c;
        for (unsigned i = 0; i < 4096; i++) {
            if (!disc) {
                s16 value = i % 1024 ? (s16)(i * 31) : cardinal[(i / 1024 + !table) % 4];
                MEM_W16(address + 2 * i, value);
            }
            c.r[4] = 0xfffe0000u + i * 16 + i % 16;
            if (!compare(c, pc, sample, disc, &result) ||
                result.r[2] != (u32)(s32)(s16)MEM_R16(address + 2 * i))
                return false;
            if (i % 1024 == 0 && (s32)result.r[2] != cardinal[(i / 1024 + !table) % 4])
                return false;
        }
    }
    return true;
}

static bool projected_points(bool disc) {
    const u32 samples[] = {0, 1, 0x4000, 0xffffc000, 0x7fff, 0x8000, 0xffffffff};
    for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; i++)
        for (unsigned j = 0; j < sizeof samples / sizeof samples[0]; j++) {
            CPUState c = initial();
            c.r[31] = 0x800471AC;
            c.r[2] = samples[i];
            c.r[16] = samples[i] * 0x10001u;
            c.r[17] = samples[j] * 0x12345u;
            c.r[18] = SCRATCH_BASE + 0x140;
            MEM_W32(0x800D7780, 0xfffffcff);
            MEM_W32(0x800D7788, 0x000154ff);
            MEM_W32(c.r[18], 0x11002200);
            CPUState native = c;
            if (!nc_project_heading_offset(&native))
                return false;
            u32 output = MEM_R32(c.r[18]);
            u32 product = (u32)(s32)(s16)samples[i] * c.r[17];
            u16 x = (u16)(((s32)product >> 14) - 4);
            if (output != (0x11000000u | x) || native.r[2] != c.r[16] - 0x154 ||
                native.budget != c.budget - 20)
                return false;
            MEM_W32(c.r[18], 0x11002200);
            CPUState cutoff = c;
            cutoff.budget = 20;
            CPUState before = cutoff;
            if (nc_project_heading_offset(&cutoff) || memcmp(&cutoff, &before, sizeof before) ||
                MEM_R32(c.r[18]) != 0x11002200)
                return false;
            if (disc) {
                CPUState guest = c;
                interp_call(&guest, 0x8004716C);
                if (memcmp(&guest, &native, sizeof guest) || MEM_R32(c.r[18]) != output)
                    return false;
            }
        }
    return true;
}

static bool actor_coordinates(bool disc) {
    const u32 record = SCRATCH_BASE + 0x80;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    const u32 offsets[] = {0, 0xffffffff, 0x80001234};
    for (unsigned i = 0; i < 3; ++i) {
        CPUState input = initial();
        input.r[4] = i == 2 ? record + 0x18 - 0x2dc : 0x80120000;
        input.r[5] = record;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(input.r[4] + 0x2dc, 0xfffeffff);
        MEM_W32(record + 0x138, offsets[i]);
        MEM_W16(0x8008f0c0, 0x8123);
        MEM_W16(0x8008f0bc, 0x3456);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        native.budget = 27;
        CPUState cutoff = native;
        if (nc_init_actor_coordinates(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native = input;
        native.budget = input.budget = 28;
        u32 y = (i == 2 ? 0xfffe3456u : 0xfffeffffu) + offsets[i];
        if (!nc_init_actor_coordinates(&native) || native.r[3] != y || native.budget != 1 ||
            MEM_R16(record + 2) != (u16)y || MEM_R32(record + 0x160) != input.r[4])
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x800470dc);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool heading_projections(bool disc) {
    const u32 angles[] = {0, 0x3fff, 0x4000, 0x8000, 0xc000, 0xffff};
    const s32 radii[] = {0, 1, -257, 32767, -32768};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i)
        for (unsigned j = 0; j < sizeof radii / sizeof radii[0]; ++j) {
            CPUState input = initial();
            input.r[4] = (u32)radii[j];
            input.r[5] = angles[i];
            input.r[29] = SCRATCH_BASE + 0x100;
            /* Also project onto the stack's saved s0/s1 words. */
            input.r[6] = j & 1 ? input.r[29] - 16 : SCRATCH_BASE;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(0x800D7780, 0xfffffeff);
            MEM_W32(0x800D7788, 0x7ff);
            memcpy(before, nc_scratch, sizeof before);
            CPUState cutoff = input;
            cutoff.budget = 57;
            CPUState native = cutoff;
            if (nc_prepare_heading_projection(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            u32 offset = (angles[i] >> 4) * 2;
            s32 sine = (s16)MEM_R16(0x80083be4 + offset);
            s32 cosine = (s16)MEM_R16(0x80081be4 + offset);
            s32 dx = (sine * radii[j]) >> 14;
            s32 dz = (cosine * radii[j]) >> 14;
            native.budget++;
            if (!nc_prepare_heading_projection(&native) || native.budget != 1 ||
                native.r[31] != 0x800471b4 || native.r[4] != (u32)dx || native.r[5] != (u32)dz ||
                MEM_R16(input.r[6]) != (u16)(dx - 2) || MEM_R16(input.r[6] + 4) != (u16)(dz - 7))
                return false;
            native.budget = input.budget - 57;
            if (!nc_vector_heading(&native))
                return false;
            memcpy(after, nc_scratch, sizeof after);
            cutoff = native;
            cutoff.budget = 19;
            CPUState epilogue = cutoff;
            if (nc_finish_heading_projection(&epilogue) ||
                memcmp(&epilogue, &cutoff, sizeof cutoff) ||
                memcmp(after, nc_scratch, sizeof after))
                return false;
            epilogue.budget++;
            if (!nc_finish_heading_projection(&epilogue) || epilogue.budget != 1 ||
                !nc_finish_heading_projection(&native))
                return false;
            if (disc) {
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80047128);
                if (memcmp(&input, &native, sizeof input) ||
                    memcmp(nc_scratch, after, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool actor_headings(bool disc) {
    const u32 angles[] = {0,      1,      0x3fff,     0x4000,     0x7fff,    0x8000,
                          0xc000, 0xffff, 0xffff8000, 0xffffffff, 0x80000000};
    const u32 actor = SCRATCH_BASE, pose = actor + 128, linked = pose + 128;
    CPUState input = initial(), result;
    input.r[4] = actor;
    MEM_W32(actor + 0x20, pose);
    MEM_W32(pose + 4, linked);
    u8 before[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i)
        for (unsigned j = 0; j < sizeof angles / sizeof angles[0]; ++j) {
            MEM_W32(pose + 0x48, angles[i]);
            MEM_W32(linked + 0x40, angles[j]);
            memcpy(before, nc_scratch, sizeof before);
            if (!compare(input, 0x800472D8, nc_actor_heading, disc, &result) ||
                result.r[2] != (u32)(s32)(s16)(angles[i] + angles[j]) || result.r[3] != angles[j] ||
                result.budget != input.budget - 18 || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    return true;
}

static bool record_projections(bool disc) {
    const u32 angles[] = {0, 1024, 2048, 3072, 4095};
    const s32 radii[] = {0, 257, -257};
    const u32 record = SCRATCH_BASE + 128;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i)
        for (unsigned j = 0; j < sizeof radii / sizeof radii[0]; ++j) {
            CPUState input = initial();
            input.r[5] = record;
            input.r[29] = SCRATCH_BASE + 896;
            const u32 actor = j == 2 ? record + 0x150 - 0x284 : 0x80120000;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(0x800d7780, (u32)-769);
            MEM_W32(0x800d7788, 1023);
            MEM_W32(record + 0x160, actor);
            MEM_W32(record + 0x128, angles[i]);
            MEM_W32(record + 0x130, radii[j]);
            MEM_W32(actor + 0x27c, 0xffff89ab);
            MEM_W32(actor + 0x284, 0x123456);
            memcpy(before, nc_scratch, sizeof before);
            CPUState cutoff = input;
            cutoff.budget = 29;
            CPUState native = cutoff;
            if (nc_prepare_record_projection(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget++;
            if (!nc_prepare_record_projection(&native) || native.budget != 1 ||
                native.r[4] != (u32)radii[j] || native.r[5] != angles[i] * 16 ||
                native.r[6] != record + 0x18 || native.r[31] != 0x800473c0 ||
                MEM_R32(record + 0x150) != 0xffff89ab ||
                MEM_R32(record + 0x158) != (j == 2 ? 0xffff89abu : 0x123456u))
                return false;
            native.budget = input.budget - 29;
            if (disc) {
                interp_call(&native, 0x80047128);
                cutoff = native;
                cutoff.budget = 2;
                CPUState call = cutoff;
                if (nc_call_projected_point_heading(&call) || memcmp(&cutoff, &call, sizeof call) ||
                    !nc_call_projected_point_heading(&native))
                    return false;
                interp_call(&native, 0x80047220);
            } else {
                native.r[2] = angles[i];
            }
            cutoff = native;
            cutoff.budget = 14;
            CPUState finish = cutoff;
            memcpy(after, nc_scratch, sizeof after);
            if (nc_store_projected_heading(&finish) || memcmp(&finish, &cutoff, sizeof cutoff) ||
                memcmp(after, nc_scratch, sizeof after))
                return false;
            finish.budget++;
            if (!nc_store_projected_heading(&finish) || finish.budget != 1 ||
                !nc_store_projected_heading(&native) ||
                MEM_R16(record + 0x12) != (u16)native.r[2] ||
                MEM_R16(record + 0x0a) != (u16)native.r[2] || native.r[31] != input.r[31] ||
                native.r[29] != input.r[29])
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x8004737c);
                if (memcmp(&input, &native, sizeof input) ||
                    memcmp(nc_scratch, after, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool projection_entries(bool disc) {
    const u32 record = SCRATCH_BASE, actor = 0x80121000, linked_actor = 0x80122000;
    const u32 pose = 0x80123000, linked_pose = 0x80124000;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned state = 0; state <= 2; ++state)
        for (unsigned flag = 0; flag <= 1; ++flag) {
            const bool expected_point = state < 2 && flag;
            CPUState input = initial();
            input.r[4] = actor;
            input.r[5] = record;
            input.r[29] = SCRATCH_BASE + 896;
            input.r[31] = expected_point ? 0x80047220 : 0x80047488;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(actor + 0x20, pose);
            MEM_W32(record + 0x160, linked_actor);
            MEM_W32(pose + 4, linked_pose);
            MEM_W32(pose + 0x48, 0xffff8000);
            MEM_W32(linked_pose + 0x40, 0x4000);
            MEM_W8(linked_actor + 0x388, state);
            MEM_W32(linked_actor + 0x330, flag ? 0xc0 : 0x80);
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 40;
            CPUState native = input;
            bool use_point = !expected_point;
            if (nc_begin_projected_point_update(&native, &use_point) ||
                memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_begin_projected_point_update(&native, &use_point) ||
                use_point != expected_point || native.r[17] != 0xffffc000 ||
                native.r[16] != record ||
                native.budget != 41 - (state >= 2 ? 26 : (flag ? 40 : 33)) ||
                MEM_R16(record + 0x16c) != (expected_point ? 0xa5a4 : 0xa5a5))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                /* Start the oracle with the post-prologue stack pointer so it
                 * stops at either continuation. Undo that adjustment in the slot. */
                const u32 stub = 0x80120000;
                MEM_W32(stub, 0x08000000u | ((0x80047420u >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, 0x27bd0028); /* addiu sp, sp, 40 */
                input.r[29] -= 40;
                input.budget += 2;
                interp_call(&input, stub);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool rotated_vector_setup(bool disc) {
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned negative_z = 0; negative_z < 2; ++negative_z) {
        const u32 source = negative_z ? 0x8008eefc : 0x8008ef04;
        if (!disc) {
            MEM_W32(source, 0x80017fff + negative_z);
            MEM_W32(source + 4, 0x1234abcd + negative_z);
        }
        for (unsigned alignment = 0; alignment < 4; ++alignment) {
            CPUState input = initial();
            input.r[4] = (u32)-513;
            input.r[5] = SCRATCH_BASE;
            input.r[29] = SCRATCH_BASE + 512 + alignment;
            input.r[31] = 0x8007a320; /* stop at ApplyMatrix */
            memset(nc_scratch, 0xa5, sizeof before);
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 28;
            CPUState native = input;
            if (nc_prepare_rotated_point_offset(&native, negative_z) ||
                memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            const u32 frame = input.r[29] - 56;
            const u32 first = MEM_R32(source), second = MEM_R32(source + 4);
            if (!nc_prepare_rotated_point_offset(&native, negative_z) || native.budget != 1 ||
                native.r[29] != frame || native.r[4] != input.r[5] + 0x38 ||
                native.r[5] != frame + 16 || native.r[6] != frame + 24 || native.r[2] != first ||
                native.r[3] != second)
                return false;
            for (unsigned b = 0; b < 8; ++b)
                if (MEM_R8(frame + 16 + b) != MEM_R8(source + b))
                    return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                /* Match interp_call's expected stack at the nested call. */
                const u32 stub = 0x80120000, pc = negative_z ? 0x80046e18 : 0x80046edc;
                MEM_W32(stub, 0x08000000u | ((pc >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, 0x27bd0038); /* addiu sp, sp, 56 */
                input.r[29] = frame;
                input.budget += 2;
                interp_call(&input, stub);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool rotated_point_offsets(bool disc) {
    const s32 components[][2] = {
        {0, 0}, {511, -511}, {-512, 513}, {-513, -1025}, {0x40000001, -0x40000001}};
    const s16 scales[] = {1, -1, 32767};
    const u32 frame = SCRATCH_BASE + 128;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned dir = 0; dir < 2; ++dir)
        for (unsigned i = 0; i < sizeof components / sizeof components[0]; ++i)
            for (unsigned j = 0; j < sizeof scales / sizeof scales[0]; ++j) {
                CPUState input = initial();
                const u32 record = j == 2 ? frame + 8 : SCRATCH_BASE + 512;
                input.r[29] = frame;
                input.r[16] = 0xabcd0000u | (u16)scales[j];
                input.r[17] = record;
                memset(nc_scratch, 0xa5, sizeof before);
                MEM_W16(record + 0x18, 1);
                MEM_W16(record + 0x1c, 0xffff);
                MEM_W32(frame + 0x18, components[i][0]);
                MEM_W32(frame + 0x20, components[i][1]);
                MEM_W32(frame + 0x30, input.r[31]);
                MEM_W32(frame + 0x2c, 0x12345678);
                MEM_W32(frame + 0x28, 0x87654321);
                memcpy(before, nc_scratch, sizeof before);
                input.budget = 43;
                CPUState native = input;
                if (nc_apply_rotated_point_offset(&native, dir) ||
                    memcmp(&native, &input, sizeof input) ||
                    memcmp(before, nc_scratch, sizeof before))
                    return false;
                native.budget = ++input.budget;
                const s32 dx = (s32)((u32)components[i][0] * (u32)scales[j]) / 512;
                const u16 x = MEM_R16(record + 0x18);
                if (!nc_apply_rotated_point_offset(&native, dir) ||
                    MEM_R16(record + 0x18) != (u16)(dir ? x - dx : x + dx) ||
                    native.r[29] != frame + 56 || native.r[31] != input.r[31])
                    return false;
                const s32 dz = (s32)(MEM_R32(frame + 0x20) * (u32)scales[j]) / 512;
                if (native.r[3] != (u32)dz ||
                    MEM_R16(record + 0x1c) != (u16)(dir ? 65535 + dz : 65535 - dz))
                    return false;
                if (disc) {
                    memcpy(after, nc_scratch, sizeof after);
                    memcpy(nc_scratch, before, sizeof before);
                    /* Enter with the caller's sp so the oracle recognizes the return. */
                    const u32 stub = 0x80120000, pc = dir ? 0x80046e68 : 0x80046f2c;
                    MEM_W32(stub, 0x08000000u | ((pc >> 2) & 0x03ffffffu));
                    MEM_W32(stub + 4, 0x27bdffc8); /* addiu sp, sp, -56 */
                    input.r[29] += 56;
                    input.budget += 2;
                    interp_call(&input, stub);
                    if (memcmp(&native, &input, sizeof input) ||
                        memcmp(after, nc_scratch, sizeof after))
                        return false;
                }
            }
    return true;
}

static bool actor_point_placements(bool disc) {
    const u32 angles[] = {0, 1024, 2048, 3072, 4095, 0xffffffff};
    const u32 actor = 0x80121000, pose = 0x80122000, linked = 0x80123000;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i) {
        CPUState input = initial();
        input.r[4] = actor;
        input.r[5] = SCRATCH_BASE;
        input.r[6] = angles[i];
        input.r[7] = i & 1 ? (u32)-257 : 257;
        input.r[29] = SCRATCH_BASE + 896;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(actor + 0x20, pose);
        MEM_W32(pose + 4, linked);
        MEM_W32(pose + 0x48, 0xffff8000);
        MEM_W32(linked + 0x40, 0x4000);
        MEM_W32(actor + 0x27c, (u32)-769);
        MEM_W32(actor + 0x280, (u32)-7);
        MEM_W32(actor + 0x284, 1023);
        MEM_W32(input.r[29] + 16, 33);
        memcpy(before, nc_scratch, sizeof before);
        CPUState cutoff = input;
        cutoff.budget = 49;
        CPUState native = cutoff;
        if (nc_begin_actor_point_placement(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget++;
        if (!nc_begin_actor_point_placement(&native) || native.budget != 1 || native.r[19] != 33 ||
            native.r[20] != (u32)-4 || native.r[16] != 3 ||
            native.r[4] != 0xffffc000u - (angles[i] << 4) || native.r[31] != 0x8004701c)
            return false;
        native.budget = input.budget - 49;
        if (!nc_heading_cosine(&native) || !nc_place_actor_points(&native))
            return false;
        if (disc)
            interp_call(&native, 0x80047128);
        cutoff = native;
        cutoff.budget = 34;
        CPUState finish = cutoff;
        memcpy(after, nc_scratch, sizeof after);
        if (nc_finish_actor_point_placement(&finish) || memcmp(&finish, &cutoff, sizeof cutoff) ||
            memcmp(after, nc_scratch, sizeof after))
            return false;
        finish.budget++;
        if (!nc_finish_actor_point_placement(&finish) || finish.budget != 1 ||
            !nc_finish_actor_point_placement(&native) || native.r[29] != input.r[29] ||
            native.r[31] != input.r[31] || MEM_R16(SCRATCH_BASE + 10) != (u16)native.r[2])
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x80046fa0);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool placed_actor_points(bool disc) {
    const u32 record = SCRATCH_BASE + 128;
    const u32 angles[] = {0, 0x3fff, 0x4000, 0x8000, 0xc000, 0xffff};
    const s32 radii[] = {0, 257, -257, 0x40000001};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i)
        for (unsigned j = 0; j < sizeof radii / sizeof radii[0]; ++j) {
            CPUState input = initial();
            const u32 offset = (angles[i] >> 4) * 2;
            const u32 cosine = (u32)(s32)(s16)MEM_R16(0x80081be4 + offset);
            const u32 sine = (u32)(s32)(s16)MEM_R16(0x80083be4 + offset);
            const u32 actor = j == 2 ? record + 0x150 - 0x284 : 0x80120000;
            input.r[2] = cosine | 0xaaaa0000;
            input.r[16] = (u32)-31;
            input.r[17] = record;
            input.r[18] = actor;
            input.r[19] = 16;
            input.r[20] = (u32)-4;
            input.r[21] = angles[i];
            input.r[22] = 0xffff8000;
            input.r[23] = radii[j];
            input.r[29] = SCRATCH_BASE + 768;
            input.r[30] = 37;
            input.r[31] = 0x80047128;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(actor + 0x27c, (u32)-769);
            MEM_W32(actor + 0x284, 1023);
            MEM_W32(input.r[29] + 16, 0x1230);
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 51;
            CPUState native = input;
            if (nc_place_actor_points(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            const s32 dx = (s32)(sine * (u32)radii[j]) >> 14;
            const s32 dz = (s32)(cosine * (u32)radii[j]) >> 14;
            if (!nc_place_actor_points(&native) || native.budget != 1 ||
                native.r[31] != 0x800470a4 || native.r[5] != 0x9230 ||
                MEM_R16(record) != (u16)(-4 + dx) || MEM_R16(record + 4) != (u16)(31 - dz) ||
                MEM_R16(record + 2) != (u16)-21 || MEM_R32(record + 0x160) != actor ||
                memcmp(nc_scratch + 128, nc_scratch + 128 + 24, 6) ||
                MEM_R32(record + 0x158) != (j == 2 ? (u32)-769 : 1023))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x8004701c);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool part_transform_setup(bool disc) {
    const u32 frame = SCRATCH_BASE + 256, matrix = frame + 0x98;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    CPUState input = initial();
    input.r[29] = frame + 0xf8;
    input.r[31] = 0x800343d8;
    for (unsigned reg = 0; reg < 8; ++reg)
        gte_write_ctrl(&input, reg, 0x8000ffffu + reg);
    memset(nc_scratch, 0xa5, sizeof before);
    memcpy(before, nc_scratch, sizeof before);
    input.budget = 55;
    CPUState native = input;
    if (nc_init_part_transform(&native) || memcmp(&input, &native, sizeof input) ||
        memcmp(before, nc_scratch, sizeof before))
        return false;
    native.budget = ++input.budget;
    if (!nc_init_part_transform(&native) || native.budget != 1 || native.r[29] != frame ||
        native.r[17] != matrix || MEM_R16(matrix + 18) != 0xa5a5)
        return false;
    for (unsigned i = 0; i < 9; ++i)
        if (MEM_R16(matrix + 2 * i) != (i % 4 == 0 ? 4096 : 0))
            return false;
    if (disc) {
        memcpy(after, nc_scratch, sizeof after);
        memcpy(nc_scratch, before, sizeof before);
        /* The oracle stops at the continuation with the new frame active. */
        const u32 stub = 0x80120000;
        MEM_W32(stub, 0x08000000u | ((0x8003433cu >> 2) & 0x03ffffffu));
        MEM_W32(stub + 4, 0x27bd00f8); /* addiu sp, sp, 248 */
        input.r[29] = frame;
        input.budget += 2;
        interp_call(&input, stub);
        if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
            return false;
    }
    return true;
}

static bool part_chain_traversal(bool disc) {
    const u32 frame = SCRATCH_BASE, part = frame + 512;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned count = 0; count <= 3; ++count) {
        CPUState input = initial();
        input.r[29] = frame;
        input.r[5] = part;
        input.r[7] = frame + 384;
        input.r[16] = 0;
        input.r[31] = count ? 0x80034430 : 0x800345bc;
        memset(nc_scratch, 0xa5, sizeof before);
        for (unsigned i = 0; i < count; ++i)
            MEM_W32(part + 32 * i, i + 1 < count ? part + 32 * (i + 1) : 0);
        memcpy(before, nc_scratch, sizeof before);
        CPUState native = input;
        if (count) {
            if (!nc_begin_part_chain(&native))
                return false;
            for (unsigned i = 1; i < count; ++i)
                if (!native.r[2] || !nc_append_part_parent(&native))
                    return false;
            if (native.r[2] || native.r[16] != count)
                return false;
        }
        if (!nc_prepare_part_chain_rotation(&native) || native.r[16] != (count ? count - 1 : 0))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, count ? 0x800343d8 : 0x80034410);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    CPUState input = initial();
    input.r[5] = input.r[4] = part;
    input.r[29] = input.r[3] = frame;
    input.r[7] = frame + 384;
    memcpy(before, nc_scratch, sizeof before);
    bool (*const stages[])(CPUState *) = {nc_begin_part_chain, nc_append_part_parent,
                                          nc_prepare_part_chain_rotation,
                                          nc_prepare_part_chain_rotation};
    const int cutoffs[] = {9, 11, 8, 5};
    for (unsigned i = 0; i < 4; ++i) {
        input.r[16] = i == 3 ? 0 : 1;
        input.budget = cutoffs[i];
        CPUState native = input;
        if (stages[i](&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
    }
    return true;
}

static const u32 rotation_entry_pc[] = {0x80025fe4, 0x800261c8, 0x80026394};
static const u32 rotation_matrix_pc[] = {0x8002607c, 0x80026248, 0x80026414};

static bool part_rotation_samples(bool disc, NcRotationInput variant) {
    const u32 frame = SCRATCH_BASE + 256, part = SCRATCH_BASE;
    const u32 angles[][3] = {{0, 0x4000, 0x8000}, {-3, -2, -1}, {0x3ffd, 0x7ffe, 0xbfff}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i) {
        CPUState input = initial();
        input.r[4] = part;
        input.r[5] = SCRATCH_BASE + 512;
        input.r[29] = frame + 48;
        input.r[31] = rotation_matrix_pc[variant];
        memset(nc_scratch, 0xa5, sizeof before);
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (variant == NC_ROTATION_PACKED)
                MEM_W16(part + 2 * axis, angles[i][axis]);
            else
                MEM_W32(part + 0x40 + 4 * axis, angles[i][axis]);
        }
        memcpy(before, nc_scratch, sizeof before);
        input.budget = variant == NC_ROTATION_PART ? 104 : 98;
        CPUState native = input;
        if (nc_sample_part_rotation(&native, variant) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_sample_part_rotation(&native, variant) || native.budget != 1 ||
            native.r[29] != frame || native.r[19] != input.r[5] || native.r[31] != input.r[31])
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            /* Match the oracle's stack at the continuation after the sixth lookup. */
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((rotation_entry_pc[variant] >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bd0030); /* addiu sp, sp, 48 */
            input.r[29] = frame;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool part_rotation_matrices(bool disc, NcRotationInput variant) {
    const u32 frame = SCRATCH_BASE + 256;
    const s32 samples[][6] = {{4096, 4096, 4096, 0, 0, 0},
                              {0, 4096, 0, -4096, 0, 16384},
                              {-1, 2371, -3241, 4095, -3340, -10003}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
        CPUState input = initial();
        input.r[29] = frame;
        input.r[19] = i == 2 ? frame : SCRATCH_BASE;
        input.r[21] = samples[i][0];
        input.r[20] = samples[i][1];
        input.r[variant == NC_ROTATION_PART ? 17 : 16] = samples[i][2];
        input.r[18] = samples[i][3];
        input.r[variant == NC_ROTATION_PART ? 16 : 17] = samples[i][4];
        input.r[2] = samples[i][5];
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(frame + 40, input.r[31]);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 97;
        CPUState native = input;
        if (nc_finish_part_rotation(&native, variant) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_finish_part_rotation(&native, variant) || native.budget != 1 ||
            native.r[29] != frame + 48)
            return false;
        if (!i)
            for (unsigned k = 0; k < 9; ++k)
                if (MEM_R16(input.r[19] + 2 * k) != (k % 4 == 0 ? 4096 : 0))
                    return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((rotation_matrix_pc[variant] >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bdffd0); /* addiu sp, sp, -48 */
            input.r[29] += 48;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool part_transform_calls(bool disc) {
    const u32 node = SCRATCH_BASE, descriptor = node + 128;
    static const struct {
        bool translation, branch, child, flagged;
        u32 stop;
        int cycles;
    } cases[] = {
        {true, true, false, false, 0x8007a530, 14},  {true, false, false, false, 0x8007a530, 10},
        {false, true, false, false, 0x80025fe4, 8},  {false, false, true, false, 0x80025fe4, 14},
        {false, false, false, true, 0x80025fe4, 25}, {false, false, false, false, 0x800267ec, 23}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[17] = node;
        input.r[18] = node + 256;
        input.r[31] = cases[i].stop;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(node + 12, cases[i].branch ? node + 64 : 0);
        MEM_W32(node + 4, cases[i].child ? node + 96 : 0);
        MEM_W32(node + 0x38, descriptor);
        MEM_W8(descriptor + 5, cases[i].flagged ? 0xff : 0xfd);
        MEM_W16(node + 0x10, -32768);
        MEM_W16(node + 0x12, 0);
        MEM_W16(node + 0x14, 32767);
        memcpy(before, nc_scratch, sizeof before);
        bool (*run)(CPUState *) =
            cases[i].translation ? nc_prepare_part_translation : nc_select_part_rotation;
        input.budget = cases[i].translation ? 14 : 25;
        CPUState native = input;
        if (run(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!run(&native) || native.budget != input.budget - cases[i].cycles ||
            (!cases[i].translation && (native.r[2] != 0) != (cases[i].stop == 0x80025fe4)))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, cases[i].translation ? 0x80026754 : 0x80026784);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool node_transform_selection(bool disc) {
    const u32 node = SCRATCH_BASE, descriptor = node + 256;
    static const struct {
        bool branch, child, flagged, overlap;
        int cycles;
    } cases[] = {{true, false, false, false, 19}, {false, true, false, false, 16},
                 {false, false, true, false, 27}, {false, false, false, false, 23},
                 {true, false, true, true, 36},   {true, false, false, true, 32}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[16] = node;
        input.r[18] = 0;
        input.r[19] = cases[i].overlap ? node + 12 : node + 128;
        const bool transform =
            cases[i].child || cases[i].flagged || (cases[i].branch && !cases[i].overlap);
        input.r[31] = transform ? 0x8007a530 : 0x800266b4;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(node + 4, cases[i].child ? node + 64 : 0);
        MEM_W32(node + 12, cases[i].branch ? node + 96 : 0);
        MEM_W32(node + 0x38, descriptor);
        MEM_W8(descriptor + 5, cases[i].flagged ? 0xff : 0xfd);
        MEM_W16(node + 0x10, -32768);
        MEM_W16(node + 0x12, 0);
        MEM_W16(node + 0x14, 32767);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 36;
        CPUState native = input;
        if (nc_select_node_transform(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_select_node_transform(&native) || native.budget != 37 - cases[i].cycles ||
            (native.r[2] != 0) != transform || (transform && native.r[4] != node + 0x10))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x800265cc);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool node_continuations(bool disc, bool part) {
    const u32 node = SCRATCH_BASE, frame = node + 256;
    static const struct {
        u32 link, pending, stop;
        NcNodeContinuation result;
        int cycles;
    } cases[] = {{SCRATCH_BASE + 64, 1, 0x800265cc, NC_NODE_NEXT, 6},
                 {0, 1, 0x800265ac, NC_NODE_POP, 8},
                 {0, 0, 0x80010000, NC_NODE_RETURN, 32}};
    u8 before[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[part ? 17 : 16] = node;
        input.r[part ? 18 : 19] = frame + 24;
        input.r[part ? 19 : 20] = cases[i].pending;
        input.r[29] = frame;
        input.r[31] = cases[i].stop;
        if (part && cases[i].result == NC_NODE_NEXT)
            input.r[31] = 0x80026754;
        if (part && cases[i].result == NC_NODE_POP)
            input.r[31] = 0x80026738;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(node + 4, cases[i].link);
        for (unsigned reg = 16; reg <= (part ? 20u : 21u); ++reg)
            MEM_W32(frame + (part ? 0xb8 : 0xc0) + 4 * (reg - 16), 0x80000000u + reg);
        MEM_W32(frame + (part ? 0xcc : 0xd8), input.r[31]);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = part ? 29 : 32;
        CPUState native = input;
        if (nc_advance_node_transform(&native, part) != NC_NODE_FALLBACK ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (nc_advance_node_transform(&native, part) != cases[i].result ||
            native.budget != input.budget - (part && i == 2 ? 29 : cases[i].cycles) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        if (disc) {
            u32 entry = part ? 0x800267ec : 0x800266b4;
            if (cases[i].result == NC_NODE_RETURN) {
                const u32 stub = 0x80120000;
                MEM_W32(stub, 0x08000000u | ((entry >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, part ? 0x27bdff30 : 0x27bdff20); /* allocate guest frame */
                input.r[29] += part ? 208 : 224;
                input.budget += 2;
                entry = stub;
            }
            interp_call(&input, entry);
            if (memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool part_composition(bool disc) {
    CPUState input = initial(), result;
    input.r[16] = SCRATCH_BASE;
    input.r[17] = SCRATCH_BASE + 128;
    input.r[20] = SCRATCH_BASE + 256;
    input.r[31] = 0x8007a0e0;
    return compare(input, 0x800267c8, nc_prepare_part_composition, disc, &result) &&
           result.r[4] == input.r[16] && result.r[5] == input.r[20] &&
           result.r[6] == input.r[17] + 24 && result.r[31] == 0x800267dc &&
           result.budget == input.budget - 5;
}

static bool node_matrix_installation(bool disc) {
    const u32 node = SCRATCH_BASE, matrix = node + 64, frame = node + 256;
    static const struct {
        bool (*run)(CPUState *);
        u32 entry, stop;
        int cycles;
        u32 frame_size;
    } paths[] = {{nc_start_node_traversal, 0x80026560, 0x800265cc, 60, 224},
                 {nc_restore_node_branch, 0x800265ac, 0x800265cc, 48, 0},
                 {nc_apply_node_matrix, 0x800266a4, 0x800266b4, 40, 0},
                 {nc_apply_part_matrix, 0x800267dc, 0x800267ec, 40, 0},
                 {nc_start_part_traversal, 0x800266f4, 0x80026754, 53, 208},
                 {nc_restore_part_branch, 0x80026738, 0x80026754, 47, 0}};
    const u32 words[] = {0x80001000, 0x7fff0000, 0x0001f000, 0xffff0800,
                         0x1000,     (u32)-17,   0,          32767};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        CPUState input = initial();
        input.r[4] = node;
        input.r[5] = matrix;
        if (paths[i].run == nc_apply_part_matrix)
            input.r[16] = matrix;
        else if (paths[i].run == nc_restore_part_branch)
            input.r[18] = frame + 16;
        else
            input.r[18] = matrix;
        input.r[19] = frame + 16;
        input.r[29] = frame + paths[i].frame_size;
        input.r[31] = paths[i].stop;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(node + 4, node + 128);
        MEM_W32(node + 8, node + 160);
        MEM_W32(frame + 16, matrix);
        MEM_W32(frame + 20, node + 128);
        for (unsigned reg = 0; reg < 8; ++reg)
            MEM_W32(matrix + 4 * reg, words[reg]);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = paths[i].cycles;
        CPUState native = input;
        if (paths[i].run(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!paths[i].run(&native) || native.budget != 1 || native.r[29] != frame)
            return false;
        for (unsigned reg = 0; reg < 8; ++reg)
            if (gte_read_ctrl(&native, reg) != words[reg])
                return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            u32 entry = paths[i].entry;
            if (paths[i].frame_size) {
                const u32 stub = 0x80120000;
                MEM_W32(stub, 0x08000000u | ((entry >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, 0x27bd0000u | paths[i].frame_size); /* addiu sp, sp, frame_size */
                input.r[29] = frame;
                input.budget += 2;
                entry = stub;
            }
            interp_call(&input, entry);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool node_rotations(bool disc) {
    const u32 source = SCRATCH_BASE + 64;
    const s16 values[] = {-32768, 0, 32767, -4096, 4096, 0, 1, -1, -13};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < 3; ++i) {
        const u32 output = i == 1 ? source + 2 : SCRATCH_BASE + 256;
        CPUState input = initial();
        input.r[16] = output - 0x18;
        input.r[17] = source;
        input.r[18] = SCRATCH_BASE + 512;
        input.r[21] = i == 2 ? source + 4 : source;
        input.r[31] = 0x8007a0e0;
        memset(nc_scratch, 0xa5, sizeof before);
        for (unsigned k = 0; k < 9; ++k)
            MEM_W16(source + 2 * k, values[k]);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 46;
        CPUState native = input;
        if (nc_prepare_node_rotation(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_prepare_node_rotation(&native) || native.budget != 1 ||
            native.r[4] != input.r[18] || native.r[5] != source || native.r[6] != output ||
            native.r[31] != 0x800266a4)
            return false;
        for (unsigned k = 0; k < 9; ++k) {
            const unsigned index = i == 1 ? 0 : (i == 2 && k == 0 ? 2 : k);
            if (MEM_R16(output + 2 * k) != (u16)values[index])
                return false;
        }
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x80026634);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool traversal_rotation_composition(bool disc) {
    const u32 frame = SCRATCH_BASE, local = frame + 0x80;
    const u32 root = frame + 256, current = root + 32;
    const s16 inputs[] = {-32768, 0, 32767, 0, 4096, 0, -1, 0, 1};
    const s16 quarter_turn[] = {0, 0, -4096, 0, 4096, 0, 4096, 0, 0};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned root_flag = 0; root_flag < 2; ++root_flag) {
        for (unsigned overlap = 0; overlap < 2; ++overlap) {
            const u32 output = overlap ? local + 4 : current + 32;
            CPUState input = initial();
            input.r[10] = output - 0x18;
            input.r[13] = 0x80 | (root_flag << 3);
            input.r[15] = current;
            input.r[30] = root;
            input.r[29] = frame;
            input.r[31] = 0x80026f60;
            input.budget = root_flag ? 127 : 125;
            memset(nc_scratch, 0xa5, sizeof before);
            for (unsigned k = 0; k < 9; ++k) {
                MEM_W16(root + 2 * k, quarter_turn[k]);
                MEM_W16(current + 2 * k, k % 4 == 0 ? 4096 : 0);
                MEM_W16(local + 2 * k, inputs[k]);
            }
            for (unsigned axis = 0; axis < 3; ++axis)
                MEM_W32(output + 20 + 4 * axis, axis - 1);
            memcpy(before, nc_scratch, sizeof before);
            CPUState native = input;
            if (nc_compose_traversal_rotation(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_compose_traversal_rotation(&native) || native.budget != 1 ||
                native.r[15] != output)
                return false;
            if (!root_flag && !overlap)
                for (unsigned k = 0; k < 9; ++k)
                    if (MEM_R16(output + 2 * k) != (u16)inputs[k])
                        return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80026cdc);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    }
    return true;
}

static bool scaled_walk_continuations(bool disc) {
    const u32 part = SCRATCH_BASE, frame = part + 256;
    static const struct {
        u32 next, pending, stop;
        NcNodeContinuation result;
        int cycles;
    } paths[] = {{SCRATCH_BASE + 64, 1, 0x800269b4, NC_NODE_NEXT, 6},
                 {0, 1, 0x8002695c, NC_NODE_POP, 8},
                 {0, 0, 0x80010000, NC_NODE_RETURN, 38}};
    u8 before[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        CPUState input = initial();
        input.r[10] = part;
        input.r[17] = frame + 8;
        input.r[19] = paths[i].pending;
        input.r[29] = frame;
        input.r[31] = paths[i].stop;
        MEM_W32(part + 4, paths[i].next);
        for (unsigned reg = 16; reg <= 23; ++reg)
            MEM_W32(frame + 0xc8 + 4 * (reg - 16), 0x80000000u + reg);
        MEM_W32(frame + 0xe8, 0x7fffffff);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 38;
        CPUState native = input;
        if (nc_advance_scaled_part_walk(&native) != NC_NODE_FALLBACK ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (nc_advance_scaled_part_walk(&native) != paths[i].result ||
            native.budget != input.budget - paths[i].cycles ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        if (disc) {
            u32 entry = 0x80026f60;
            if (paths[i].result == NC_NODE_RETURN) {
                const u32 stub = 0x80120000;
                MEM_W32(stub, 0x08000000u | ((entry >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, 0x27bdff10); /* allocate the traversal frame */
                input.r[29] += 0xf0;
                input.budget += 2;
                entry = stub;
            }
            interp_call(&input, entry);
            if (memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool traversal_matrices(bool disc) {
    const s32 samples[][6] = {{4096, 4096, 4096, 0, 0, 0},
                              {0, 4096, 0, -4096, 0, 4096},
                              {-3123, 2751, -4095, 173, -2231, -2001}};
    const u32 frame = SCRATCH_BASE;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
        CPUState input = initial();
        input.r[11] = samples[i][0];
        input.r[7] = samples[i][1];
        input.r[6] = samples[i][2];
        input.r[9] = samples[i][3];
        input.r[4] = samples[i][4];
        input.r[8] = samples[i][5];
        input.r[29] = frame;
        input.r[31] = 0x80026cdc;
        input.budget = 75;
        CPUState native = input;
        memset(nc_scratch, 0xa5, sizeof before);
        memcpy(before, nc_scratch, sizeof before);
        if (nc_build_traversal_matrix(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_build_traversal_matrix(&native) || native.budget != 1)
            return false;
        if (i == 0)
            for (unsigned k = 0; k < 9; ++k)
                if (MEM_R16(frame + 0x80 + 2 * k) != (k % 4 == 0 ? 4096 : 0))
                    return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x80026bb8);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool traversal_angle_samples(bool disc, bool shared) {
    const u32 angles[][3] = {{0, 0x4000, 0x8000}, {-18, -2, -1}, {0xfffe, 0x7ffe, 0xbfff}};
    const u32 cosine = 0x80140000, sine = cosine + 0x4000;
    /* Give the words just outside the ordinary cosine table distinct values. */
    for (int k = -1; k <= 4096; ++k) {
        MEM_W16(cosine + 2 * k, 0x9303 + 37 * k);
        MEM_W16(sine + 2 * k, 0xcafe - 17 * k);
    }
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i) {
        CPUState input = initial();
        input.r[10] = SCRATCH_BASE;
        input.r[23] = SCRATCH_BASE + 256;
        input.r[20] = sine;
        input.r[21] = cosine;
        input.r[31] = 0x80026bb8;
        const u32 source = shared ? input.r[23] : input.r[10] + 0x40;
        for (unsigned axis = 0; axis < 3; ++axis)
            MEM_W32(source + 4 * axis, angles[i][axis]);
        input.budget = shared ? 57 : 54;
        CPUState native = input;
        if (nc_sample_traversal_angles(&native, shared) || memcmp(&input, &native, sizeof input))
            return false;
        native.budget = ++input.budget;
        if (!nc_sample_traversal_angles(&native, shared) || native.budget != 1)
            return false;
        const int x_indices[] = {0, -1, 4096};
        const int x_index = shared ? x_indices[i] : (x_indices[i] & 4095);
        const u16 sample = MEM_R16(cosine + 2 * x_index);
        const u32 expected = shared ? (u32)(sample & 4095) >> 2 : (u32)((s32)(s16)sample >> 2);
        if (native.r[11] != expected)
            return false;
        if (disc) {
            interp_call(&input, shared ? 0x80026aa0 : 0x80026b28);
            if (memcmp(&input, &native, sizeof input))
                return false;
        }
    }
    return true;
}

static bool part_heading_sources(bool disc) {
    const u32 part = SCRATCH_BASE + 64;
    static const struct {
        bool branch, child;
        u32 flags, stop;
        NcHeadingSource source;
        int cycles;
    } cases[] = {{true, false, 0, 0x80026b28, NC_HEADING_FROM_PART, 8},
                 {true, true, 8, 0x80026aa0, NC_HEADING_FROM_SHARED, 8},
                 {false, true, 8, 0x80026aa0, NC_HEADING_FROM_SHARED, 14},
                 {false, false, 0x80, 0x80026b28, NC_HEADING_FROM_PART, 17},
                 {false, false, 0xffffff88, 0x80026aa0, NC_HEADING_FROM_SHARED, 17},
                 {false, false, 8, 0x80026cdc, NC_HEADING_SKIP, 15}};
    u8 before[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[10] = part;
        input.r[13] = cases[i].flags;
        input.r[31] = cases[i].stop;
        MEM_W32(part + 12, cases[i].branch ? part + 64 : 0);
        MEM_W32(part + 4, cases[i].child ? part + 96 : 0);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 17;
        CPUState native = input;
        if (nc_select_part_heading_source(&native) != NC_HEADING_FALLBACK ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (nc_select_part_heading_source(&native) != cases[i].source ||
            native.budget != 18 - cases[i].cycles || native.r[2] != (cases[i].flags & 8))
            return false;
        if (disc) {
            interp_call(&input, 0x80026a6c);
            if (memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool child_positions(bool disc) {
    const u32 frame = SCRATCH_BASE, part = frame + 320;
    const s16 point[] = {-32768, 0, 32767};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned root = 0; root < 2; ++root) {
        CPUState input = initial();
        input.r[29] = frame;
        input.r[10] = part;
        input.r[31] = 0x80026a6c;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(frame + 0xb8, root ? part : part + 64);
        for (unsigned reg = 0; reg < 5; ++reg)
            gte_write_ctrl(&input, reg, reg % 2 == 0 ? 4096 : 0);
        for (unsigned axis = 0; axis < 3; ++axis) {
            MEM_W16(part + 0x10 + 2 * axis, point[axis]);
            gte_write_ctrl(&input, 5 + axis, axis - 1);
        }
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 23;
        CPUState native = input;
        if (nc_update_child_position(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_update_child_position(&native) || native.budget != (root ? 18 : 1))
            return false;
        for (unsigned axis = 0; axis < 3; ++axis)
            if (MEM_R32(part + 0x2c + 4 * axis) != (root ? 0xa5a5a5a5u : point[axis] + axis - 1))
                return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x80026a28);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool scaled_part_filters(bool disc) {
    const u32 frame = SCRATCH_BASE, actor = frame + 128, part = frame + 928;
    static const struct {
        u8 index;
        u16 flags, mask;
        bool branch, selected;
        int cycles;
    } cases[] = {{0, 0xffff, 0, false, true, 18},     {0, 0, 0x100, true, false, 22},
                 {255, 0x80ff, 0, false, true, 27},   {1, 0x8100, 0, true, false, 34},
                 {3, 0x8100, 0x100, false, true, 26}, {7, 0x8400, 0x100, true, false, 31}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[10] = part;
        input.r[17] = input.r[18] = frame + 8;
        input.r[22] = cases[i].mask;
        input.r[29] = frame;
        input.r[31] = cases[i].selected ? 0x80026a28 : 0x80026f60;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(frame + 0xb0, actor);
        MEM_W32(part + 12, cases[i].branch ? part + 64 : 0);
        MEM_W8(part + 0x4c, cases[i].index);
        MEM_W16(actor + 0x10a + 2 * cases[i].index, cases[i].flags);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 34;
        CPUState native = input;
        if (nc_filter_scaled_part(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_filter_scaled_part(&native) || native.budget != 35 - cases[i].cycles ||
            (native.r[22] ? native.r[2] != 0 : native.r[2] == 0) != cases[i].selected)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x800269b4);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool scaled_part_walk(bool disc) {
    const u32 frame = SCRATCH_BASE, actor = frame + 256, part = frame + 768;
    const u32 matrix = frame + 64;
    const u32 words[] = {0x80001000, 0x7fff0000, 0x0001f000, 0xffff0800,
                         0x1000,     (u32)-17,   0,          32767};
    u8 before[SCRATCH_SIZE];
    for (unsigned path = 0; path < 3; ++path) {
        const bool resume = path == 2;
        CPUState input = initial();
        input.r[10] = part;
        input.r[17] = frame + 16;
        input.r[19] = resume ? 3 : 0;
        input.r[22] = 0;
        input.r[29] = frame;
        input.r[31] = 0x800269b4;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(frame + 0xb0, actor);
        MEM_W32(actor + 0x20, path == 0 ? part : part + 80);
        MEM_W8(part + 0x4c, 7);
        MEM_W16(actor + 0x10a + 14, 0xf5ff);
        MEM_W32(frame + 16, matrix);
        MEM_W32(frame + 20, part + 80);
        for (unsigned reg = 0; reg < 8; ++reg)
            MEM_W32(matrix + 4 * reg, words[reg]);
        memcpy(before, nc_scratch, sizeof before);
        bool (*run)(CPUState *) = resume ? nc_resume_scaled_part_walk : nc_begin_scaled_part_walk;
        input.budget = resume ? 42 : 31;
        CPUState native = input;
        if (run(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!run(&native) || native.budget != (path == 0 ? 12 : 1) ||
            native.r[22] != (path == 1 ? 0x1500u : 0) || native.r[18] != frame + (resume ? 16 : 0))
            return false;
        if (disc) {
            interp_call(&input, resume ? 0x8002695c : 0x80026908);
            if (memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool scaled_part_positions(bool disc) {
    const u32 frame = SCRATCH_BASE, matrix = frame + 512;
    const s16 points[][3] = {{-32768, 0, 32767}, {0, -1, 0}, {32767, -32768, 0}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof points / sizeof points[0]; ++i) {
        CPUState input = initial();
        const u32 part = i == 2 ? frame + 0xa8 - 0x2c : frame + 320;
        input.r[5] = part;
        input.r[6] = matrix;
        input.r[29] = frame + 240;
        input.r[31] = 0x80026908;
        memset(nc_scratch, 0xa5, sizeof before);
        for (unsigned k = 0; k < 9; ++k)
            MEM_W16(matrix + 2 * k, k % 4 == 0 ? 4096 : 0);
        for (unsigned axis = 0; axis < 3; ++axis) {
            MEM_W16(part + 0x10 + 2 * axis, points[i][axis]);
            MEM_W32(matrix + 20 + 4 * axis, axis - 1);
        }
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 82;
        CPUState native = input;
        if (nc_start_scaled_part_position(&native) || memcmp(&input, &native, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_start_scaled_part_position(&native) || native.budget != 1)
            return false;
        for (unsigned axis = 0; axis < 3; ++axis) {
            u32 value = points[i][axis] + axis - 1;
            if (i == 2 && axis == 0)
                value = native.r[12];
            if (MEM_R32(part + 0x2c + 4 * axis) != value * 4)
                return false;
        }
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((0x80026828u >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bd00f0); /* enter with the caller's stack */
            input.r[29] = frame;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool part_offsets(bool disc) {
    const u32 frame = SCRATCH_BASE, part = frame + 512;
    const s16 points[][3] = {{-32768, 0, 0}, {0, 32767, 0}, {0, 0, -1}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof points / sizeof points[0]; ++i) {
        CPUState input = initial();
        input.r[16] = i;
        input.r[29] = frame;
        input.r[31] = 0x80025fe4;
        for (unsigned reg = 0; reg < 5; ++reg)
            gte_write_ctrl(&input, reg, (reg % 2 == 0) ? 4096 : 0);
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(frame + 0x1c + 8 * i, part);
        for (unsigned axis = 0; axis < 3; ++axis) {
            gte_write_ctrl(&input, 5 + axis, axis + 1);
            MEM_W16(part + 0x10 + 2 * axis, points[i][axis]);
        }
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 26;
        CPUState native = input;
        if (nc_transform_part_offset(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_transform_part_offset(&native) || native.budget != 1 || native.r[4] != part ||
            native.r[5] != input.r[18] || native.r[31] != 0x80034480)
            return false;
        for (unsigned axis = 0; axis < 3; ++axis)
            if (MEM_R32(frame + 0xac + 4 * axis) != (u32)points[i][axis] + axis + 1)
                return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x80034430);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool part_matrix_composition(bool disc) {
    const u32 frame = SCRATCH_BASE, parent = frame + 512;
    const s16 rotations[][9] = {{4096, 0, 0, 0, 4096, 0, 0, 0, 4096},
                                {0, 0, -4096, 0, 4096, 0, 4096, 0, 0}};
    const s16 part[] = {-32768, 0, 32767, 0, 4096, 0, -1, 0, 1};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < 2; ++i)
        for (unsigned overlap = 0; overlap < 2; ++overlap) {
            CPUState input = initial();
            input.r[16] = i;
            input.r[17] = parent;
            input.r[18] = frame + 0xb8;
            input.r[19] = overlap ? frame + 0xbc : frame + 0x98;
            input.r[29] = frame;
            input.r[31] = i ? 0x80034430 : 0x800345bc;
            memset(nc_scratch, 0xa5, sizeof before);
            for (unsigned k = 0; k < 9; ++k) {
                MEM_W16(parent + 2 * k, rotations[i][k]);
                MEM_W16(frame + 0xb8 + 2 * k, part[k]);
            }
            memcpy(before, nc_scratch, sizeof before);
            input.budget = i ? 122 : 123;
            CPUState native = input;
            if (nc_compose_part_matrix(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_compose_part_matrix(&native) || native.budget != 1 ||
                native.r[17] != input.r[19] || native.r[16] != 0)
                return false;
            if (!i && !overlap)
                for (unsigned k = 0; k < 9; ++k)
                    if (MEM_R16(frame + 0x98 + 2 * k) != (u16)part[k])
                        return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80034480);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool part_translation(bool disc) {
    const u32 matrix = SCRATCH_BASE, frame = SCRATCH_BASE + 256;
    const u32 outputs[] = {SCRATCH_BASE + 64, matrix + 0x18, frame + 0xe0};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof outputs / sizeof outputs[0]; ++i) {
        CPUState input = initial();
        input.r[17] = matrix;
        input.r[20] = outputs[i];
        input.r[29] = frame;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(matrix + 0x14, -5);
        MEM_W32(matrix + 0x18, 0);
        MEM_W32(matrix + 0x1c, INT32_MIN);
        MEM_W32(frame + 0xf4, input.r[31]);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 39;
        CPUState native = input;
        if (nc_finish_part_translation(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_finish_part_translation(&native) || native.budget != 1 ||
            MEM_R32(outputs[i]) != (u32)-2 || MEM_R32(outputs[i] + 4) != (i == 1 ? (u32)-1 : 0) ||
            MEM_R32(outputs[i] + 8) != (i == 1 ? (u32)-1 : (u32)(INT32_MIN / 4)))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            /* Enter with caller sp so the oracle recognizes the return. */
            const u32 stub = 0x80120000;
            MEM_W32(stub, 0x08000000u | ((0x800345bcu >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bdff08); /* addiu sp, sp, -248 */
            input.r[29] += 248;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool heading_matrix_setup(bool disc) {
    const s16 rotations[][9] = {{4096, 0, 0, 0, 4096, 0, 0, 0, 4096},
                                {0, 0, -4096, 0, 4096, 0, 4096, 0, 0},
                                {32767, -32768, 32767, -32768, 32767, -32768, 0, 1, -1}};
    const s16 values[] = {4096, -32768, 32767, 1, -1, 0, 2048, -2048, 4096};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof rotations / sizeof rotations[0]; ++i)
        for (unsigned alias = 0; alias < 2; ++alias) {
            CPUState input = initial();
            input.r[29] = SCRATCH_BASE + 512;
            const u32 frame = input.r[29] - 88;
            input.r[4] = alias ? frame + 14 : SCRATCH_BASE;
            input.r[5] = SCRATCH_BASE + 704;
            input.r[31] = 0x8007a1f0; /* stop at ScaleMatrixL */
            memset(nc_scratch, 0xa5, sizeof before);
            for (unsigned k = 0; k < 9; ++k) {
                MEM_W16(0x800d7688 + 2 * k, rotations[i][k]);
                MEM_W16(input.r[4] + 2 * k, values[k]);
            }
            memcpy(before, nc_scratch, sizeof before);
            input.budget = 119;
            CPUState native = input;
            if (nc_prepare_heading_matrix(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_prepare_heading_matrix(&native) || native.budget != 1 ||
                native.r[4] != frame + 16 || native.r[5] != frame + 48 ||
                native.r[19] != input.r[5] || native.r[31] != 0x8003475c)
                return false;
            if (!i && !alias)
                for (unsigned k = 0; k < 9; ++k)
                    if (MEM_R16(frame + 16 + 2 * k) != (u16)values[k])
                        return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                /* Match the oracle's stack at the nested call. */
                const u32 stub = 0x80120000;
                MEM_W32(stub, 0x08000000u | ((0x80034610u >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, 0x27bd0058); /* addiu sp, sp, 88 */
                input.r[29] = frame;
                input.budget += 2;
                interp_call(&input, stub);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool matrix_planar_lengths(bool disc) {
    static const struct {
        s16 y, z;
        s32 root_input;
    } cases[] = {
        {0, 0, 0},
        {63, -1, 0},
        {-64, 0, 1},
        {0, 4096, 4096},
        {4096, -4096, 8192},
        {-32768, 32767, 524272},
        {-32768, -32768, -524288},
    };
    CPUState input = initial(), native;
    input.r[29] = SCRATCH_BASE + 64;
    input.r[31] = 0x800475e4; /* stop at the root call */
    u8 before[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        MEM_W16(input.r[29] + 0x18, cases[i].y);
        MEM_W16(input.r[29] + 0x1a, cases[i].z);
        memcpy(before, nc_scratch, sizeof before);
        if (!compare(input, 0x8003475c, nc_prepare_matrix_planar_length, disc, &native) ||
            native.r[4] != (u32)cases[i].root_input || native.r[31] != 0x80034788 ||
            native.budget != input.budget - 15 || memcmp(before, nc_scratch, sizeof before))
            return false;
    }
    return true;
}

static bool matrix_headings(bool disc) {
    const s16 matrices[][9] = {
        {4096, 0, 0, 0, 4096, 0, 0, 0, 4096},
        {4096, 0, 0, -4096, 0, 0, 0, 4096, 0},
        {-32768, 0, 0, 32767, -1, -32768, 32767, -32768, -1},
    };
    const u32 lengths[] = {0, 1, 64, 0x7fffffff};
    const u32 frame = SCRATCH_BASE + 128;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof matrices / sizeof matrices[0]; ++i)
        for (unsigned j = 0; j < sizeof lengths / sizeof lengths[0]; ++j) {
            CPUState input = initial();
            input.r[2] = lengths[j];
            input.r[29] = frame;
            /* Include output overlapping the saved registers. */
            input.r[19] = j == 2 ? frame + 0x40 : SCRATCH_BASE;
            memset(nc_scratch, 0xa5, sizeof before);
            for (unsigned k = 0; k < 9; ++k)
                MEM_W16(frame + 16 + k * 2, matrices[i][k]);
            MEM_W32(frame + 0x50, input.r[31]);
            memcpy(before, nc_scratch, sizeof before);
            input.budget = lengths[j] ? 155 : 111;
            CPUState native = input;
            if (nc_finish_matrix_headings(&native) || memcmp(&input, &native, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_finish_matrix_headings(&native) || native.r[29] != frame + 88 ||
                native.r[31] != input.r[31] || native.budget <= 0 ||
                (!lengths[j] && MEM_R16(input.r[19] + 2) != 0))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                /* Enter with the caller's stack pointer for interp_call's return check. */
                const u32 stub = 0x80120000;
                MEM_W32(stub, 0x08000000u | ((0x80034788u >> 2) & 0x03ffffffu));
                MEM_W32(stub + 4, 0x27bdffa8); /* addiu sp, sp, -88 */
                input.r[29] += 88;
                input.budget += 2;
                interp_call(&input, stub);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    CPUState exceptional = initial();
    exceptional.r[2] = 0x80000000;
    CPUState unchanged = exceptional;
    return !nc_finish_matrix_headings(&exceptional) &&
           !memcmp(&exceptional, &unchanged, sizeof unchanged);
}

static bool facing_offsets(bool disc) {
    const u32 record = SCRATCH_BASE + 0x80;
    const u32 angles[] = {0, 0x3fff, 0x4000, 0x8000, 0xc000, 0xffff};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof angles / sizeof angles[0]; ++i)
        for (unsigned alias = 0; alias < 2; ++alias) {
            CPUState input = initial();
            input.r[16] = record;
            input.r[17] = angles[i];
            input.r[29] = SCRATCH_BASE + 0x300;
            input.r[31] = 0x80047128; /* compare up to entry into the projection call */
            const u32 actor = alias ? record + 0x150 - 0x284 : 0x80120000;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(record + 0x160, actor);
            MEM_W32(record + 0x130, (u32)-257);
            MEM_W32(record + 0x128, 0xffff0123);
            MEM_W32(actor + 0x27c, (u32)-769);
            MEM_W32(actor + 0x284, 1023);
            memcpy(before, nc_scratch, sizeof before);
            const u32 offset = (angles[i] >> 4) * 2;
            const s32 x = -4 + (((s16)MEM_R16(0x80083be4 + offset) * 130) >> 14);
            const s32 z = (alias ? x >> 8 : 3) + (((s16)MEM_R16(0x80081be4 + offset) * 130) >> 14);
            input.budget = 70;
            CPUState native = input;
            if (nc_prepare_facing_offset(&native) || memcmp(&native, &input, sizeof input) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget = ++input.budget;
            if (!nc_prepare_facing_offset(&native) || native.budget != 1 ||
                native.r[31] != 0x80047518 || native.r[4] != (u32)-257 ||
                native.r[5] != 0xfff01230u - angles[i] || native.r[6] != input.r[29] + 16 ||
                native.r[7] != (u32)z || MEM_R32(record + 0x150) != ((u32)x << 8) ||
                MEM_R32(record + 0x158) != ((u32)z << 8))
                return false;
            if (disc) {
                memcpy(after, nc_scratch, sizeof after);
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80047488);
                if (memcmp(&native, &input, sizeof input) ||
                    memcmp(after, nc_scratch, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool saved_bearing_projections(bool disc) {
    const s16 bearings[] = {-32768, -1, 0, 1024, 2048, 3072, 4095, 4096, 32767};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof bearings / sizeof bearings[0]; ++i) {
        CPUState input = initial();
        input.r[16] = SCRATCH_BASE;
        input.r[6] = SCRATCH_BASE + 24;
        input.r[31] = 0x80047128; /* stop the interpreter at the projection call */
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W16(SCRATCH_BASE + 10, bearings[i]);
        MEM_W32(SCRATCH_BASE + 0x130, (u32)-257);
        MEM_W16(SCRATCH_BASE + 0x16c, 0xabd0 | (i & 1));
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 15;
        CPUState native = input;
        if (nc_prepare_saved_bearing_projection(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_prepare_saved_bearing_projection(&native) || native.budget != 1 ||
            native.r[4] != (u32)-257 || native.r[5] != (u32)(4096 - bearings[i]) * 16 ||
            native.r[31] != 0x800475cc || MEM_R16(SCRATCH_BASE + 0x16c) != 0xabd0)
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x800475a8);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    for (unsigned store = 0; store < 2; ++store) {
        CPUState input = initial();
        input.r[29] = SCRATCH_BASE + 512;
        MEM_W32(input.r[29] + 32, input.r[31]);
        MEM_W32(input.r[29] + 28, 0x12345678);
        MEM_W32(input.r[29] + 24, 0x87654321);
        input.r[16] = SCRATCH_BASE;
        input.r[2] = 0xfffffc00;
        MEM_W16(SCRATCH_BASE + 0x12, 0x1234);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = store ? 14 : 12;
        CPUState native = input;
        if (nc_finish_projected_point_update(&native, store) ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_finish_projected_point_update(&native, store) || native.budget != 1 ||
            native.r[16] != 0x87654321 || native.r[17] != 0x12345678 ||
            native.r[29] != input.r[29] + 40 ||
            MEM_R16(SCRATCH_BASE + 0x12) != (store ? 0xfc00 : 0x1234))
            return false;
        if (disc) {
            /* Enter with the caller's stack so interp_call recognizes the return.
             * The jump and its stack-allocation delay slot cost two cycles. */
            const u32 stub = 0x80120000;
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            const u32 entry = store ? 0x80047480 : 0x800475cc;
            MEM_W32(stub, 0x08000000u | ((entry >> 2) & 0x03ffffffu));
            MEM_W32(stub + 4, 0x27bdffd8); /* addiu sp, sp, -40 */
            input.r[29] += 40;
            input.budget += 2;
            interp_call(&input, stub);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool projected_heading_selection(bool disc) {
    static const struct {
        s32 gap;
        u16 flags;
        bool reproject;
        int cycles;
    } cases[] = {
        {569, 0xa500, false, 9},  {4095, 0xa501, false, 9}, {568, 0xa500, true, 10},
        {568, 0xa501, false, 13}, {456, 0xa501, false, 13}, {455, 0xa501, true, 11},
        {-1, 0xa501, true, 11},   {0, 0xa500, true, 10},
    };
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[2] = cases[i].gap < 569;
        input.r[3] = cases[i].gap;
        input.r[16] = SCRATCH_BASE;
        input.r[17] = 0xffff8123;
        input.r[31] = cases[i].reproject ? 0x800475a8 : 0x800475cc;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W16(SCRATCH_BASE + 0x16c, cases[i].flags);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 13;
        CPUState native = input;
        bool reproject = !cases[i].reproject;
        if (nc_select_projected_heading(&native, &reproject) ||
            memcmp(&native, &input, sizeof input) || memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_select_projected_heading(&native, &reproject) || reproject != cases[i].reproject ||
            native.budget != input.budget - cases[i].cycles ||
            MEM_R16(SCRATCH_BASE + 0x12) != (reproject ? 0xa5a5 : 0x8123) ||
            MEM_R16(SCRATCH_BASE + 0x16c) != (cases[i].flags | (cases[i].gap >= 569)))
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, 0x80047564);
            if (memcmp(&native, &input, sizeof input) || memcmp(after, nc_scratch, sizeof after))
                return false;
        }
    }
    return true;
}

static bool projected_heading_gaps(bool disc) {
    static const struct {
        s16 previous, projected;
        s32 gap, cycles;
    } cases[] = {
        {0, 0, 0, 12},
        {0, 568, 568, 11},
        {569, 0, 569, 12},
        {0, 2048, 2048, 11},
        {2048, 0, 2048, 12},
        {0, 2049, 2047, 15},
        {4095, 0, 1, 15},
        {-1, 4095, 0, 15},
        {4096, 0, 0, 15},
        {-32768, 32767, -61439, 15},
        {32767, -32768, -61439, 15},
    };
    u8 before[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[16] = SCRATCH_BASE;
        input.r[2] = 0xdead0000u | (u16)cases[i].projected;
        input.r[31] = 0x80047564;
        MEM_W16(SCRATCH_BASE + 10, cases[i].previous);
        memcpy(before, nc_scratch, sizeof before);
        input.budget = 15;
        CPUState native = input;
        if (nc_projected_heading_gap(&native) || memcmp(&native, &input, sizeof input) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget = ++input.budget;
        if (!nc_projected_heading_gap(&native) || native.r[2] != (u32)(cases[i].gap < 569) ||
            native.r[3] != (u32)cases[i].gap || native.r[4] != (u32)(s32)cases[i].previous ||
            native.r[17] != (u32)(s32)cases[i].projected ||
            native.budget != input.budget - cases[i].cycles ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        if (disc) {
            interp_call(&input, 0x80047518);
            if (memcmp(&input, &native, sizeof input) || memcmp(before, nc_scratch, sizeof before))
                return false;
        }
    }
    return true;
}

static bool relative_headings(bool disc) {
    const u32 actor = SCRATCH_BASE, pose = actor + 128, linked = actor + 256;
    const u32 point = actor + 384;
    const s16 points[][2] = {{-4, -3}, {-5, -3}, {-3, -3}, {-4, -4}, {-4, -2}, {-32768, 32767}};
    const u32 offsets[] = {0x2000, 0x1fff, 0x2010, 0x80000000, 0xfffffff0};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof points / sizeof points[0]; ++i)
        for (unsigned j = 0; j < sizeof offsets / sizeof offsets[0]; ++j) {
            CPUState input = initial();
            input.r[4] = point;
            input.r[29] = SCRATCH_BASE + 768;
            memset(nc_scratch, 0xa5, sizeof before);
            MEM_W32(0x800D7790, actor);
            MEM_W32(0x800D7780, (u32)-769);
            MEM_W32(0x800D7788, 1023);
            MEM_W32(actor + 0x20, pose);
            MEM_W32(pose + 4, linked);
            MEM_W32(pose + 0x48, 0xfffffff0);
            MEM_W32(linked + 0x40, offsets[j] + 16);
            MEM_W16(point, points[i][0]);
            MEM_W16(point + 4, points[i][1]);
            memcpy(before, nc_scratch, sizeof before);
            CPUState cutoff = input;
            cutoff.budget = 16;
            CPUState native = cutoff;
            if (nc_prepare_relative_heading(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
                memcmp(before, nc_scratch, sizeof before))
                return false;
            native.budget++;
            if (!nc_prepare_relative_heading(&native) || native.budget != 1 ||
                native.r[2] != pose || native.r[16] != linked || native.r[31] != 0x80047328)
                return false;
            native.budget = input.budget - 16;
            if (!nc_prepare_point_heading(&native) || !nc_vector_heading(&native) ||
                !nc_return_point_heading(&native))
                return false;
            u32 relative = ((offsets[j] >> 4) - native.r[2]) & 4095;
            cutoff = native;
            cutoff.budget = 33;
            CPUState finish = cutoff;
            memcpy(after, nc_scratch, sizeof after);
            if (nc_finish_relative_heading(&finish) || memcmp(&finish, &cutoff, sizeof cutoff) ||
                memcmp(after, nc_scratch, sizeof after))
                return false;
            finish.budget++;
            if (!nc_finish_relative_heading(&finish) || finish.budget != 1 ||
                !nc_finish_relative_heading(&native) ||
                native.r[2] != (relative ? relative : 4096) || native.r[4] != offsets[j] + 16 ||
                native.r[31] != input.r[31] || native.r[16] != input.r[16] ||
                native.r[29] != input.r[29])
                return false;
            if (disc) {
                memcpy(nc_scratch, before, sizeof before);
                interp_call(&input, 0x80047300);
                if (memcmp(&input, &native, sizeof input) ||
                    memcmp(nc_scratch, after, sizeof after))
                    return false;
            }
        }
    return true;
}

static bool reference_distances(bool disc) {
    static const struct {
        s32 x, z;
        s16 ref_x, ref_z;
        s32 dx, dz;
    } cases[] = {
        {-1, -1, -1, 1, 0, 0},
        {-257, 255, -5, 4, 3, 4},
        {-257, 255, -2, -1, 0, -1},
        {255, -257, 1, 2, -1, 0},
        {0x800000, 0x800100, -32768, 32767, 65536, 65536},
        {0, -0x800100, -32768, -32768, 32768, -65537},
    };
    const u32 sentinel = 0xa55a5aa5;
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[29] = SCRATCH_BASE + 128;
        MEM_W32(0x800D7780, cases[i].x);
        MEM_W32(0x800D7788, cases[i].z);
        MEM_W16(0x800D7648, cases[i].ref_x);
        MEM_W16(0x800D764C, cases[i].ref_z);
        MEM_W32(0x800D7760, sentinel);
        MEM_W32(0x800D7764, sentinel);
        memset(nc_scratch, 0xa5, sizeof before);
        memcpy(before, nc_scratch, sizeof before);
        CPUState cutoff = input;
        cutoff.budget = 28;
        CPUState native = cutoff;
        if (nc_prepare_reference_distance(&native) || memcmp(&native, &cutoff, sizeof cutoff) ||
            memcmp(before, nc_scratch, sizeof before))
            return false;
        native.budget++;
        u32 dx = (u32)cases[i].dx, dz = (u32)cases[i].dz;
        if (!nc_prepare_reference_distance(&native) || native.budget != 1 || native.r[2] != dz ||
            native.r[3] != dz * dz || native.r[4] != dx * dx + dz * dz ||
            native.r[31] != 0x800472b8 || MEM_R32(input.r[29] - 8) != input.r[31])
            return false;
        native.budget = input.budget - 28;
        if (disc)
            interp_call(&native, 0x8007a050);
        else
            native.r[2] = i;
        cutoff = native;
        cutoff.budget = 10;
        CPUState finish = cutoff;
        if (nc_store_reference_distance(&finish) || memcmp(&finish, &cutoff, sizeof cutoff) ||
            MEM_R32(0x800D7760) != sentinel || MEM_R32(0x800D7764) != sentinel)
            return false;
        finish.budget++;
        if (!nc_store_reference_distance(&finish) || finish.budget != 1 ||
            !nc_store_reference_distance(&native) || native.r[31] != input.r[31] ||
            native.r[29] != input.r[29] || MEM_R32(0x800D7760) != native.r[2] ||
            MEM_R32(0x800D7764) != native.r[2])
            return false;
        if (disc) {
            memcpy(after, nc_scratch, sizeof after);
            memcpy(nc_scratch, before, sizeof before);
            MEM_W32(0x800D7760, sentinel);
            MEM_W32(0x800D7764, sentinel);
            interp_call(&input, 0x80047268);
            if (memcmp(&input, &native, sizeof input) || memcmp(after, nc_scratch, sizeof after) ||
                MEM_R32(0x800D7760) != native.r[2] || MEM_R32(0x800D7764) != native.r[2])
                return false;
        }
    }
    return true;
}

static bool point_headings(bool disc, bool local) {
    const int cycles = local ? 19 : 21;
    const u32 entry = local ? 0x800471e0 : 0x80047220;
    const u32 resume = local ? 0x8004720c : 0x80047254;
    bool (*prepare)(CPUState *) = local ? nc_prepare_local_point_heading : nc_prepare_point_heading;
    const s16 points[][2] = {{-4, -3}, {-4, -4},        {-5, -3},       {-3, -3},
                             {-4, -2}, {-32768, 32767}, {32767, -32768}};
    u8 before[SCRATCH_SIZE], after[SCRATCH_SIZE];
    for (unsigned i = 0; i < sizeof points / sizeof points[0]; ++i) {
        CPUState input = initial();
        input.r[29] = SCRATCH_BASE + 128;
        /* Last point overlaps the return-address save. */
        u32 save = input.r[29] - 8;
        u32 point = i + 1 == sizeof points / sizeof points[0] ? save : SCRATCH_BASE;
        input.r[4] = point;
        memset(nc_scratch, 0xa5, sizeof before);
        MEM_W32(0x800D7780, (u32)-769); /* -4 after arithmetic shift */
        MEM_W32(0x800D7788, 1023);      /* +3 */
        MEM_W16(0x800D7630, (u16)-4);
        MEM_W16(0x800D7634, (u16)-3);
        MEM_W16(point, points[i][0]);
        MEM_W16(point + 4, points[i][1]);
        memcpy(before, nc_scratch, sizeof before);
        CPUState cutoff = input;
        cutoff.budget = cycles;
        CPUState native = cutoff;
        if (prepare(&native) || memcmp(&native, &cutoff, sizeof native) ||
            memcmp(nc_scratch, before, sizeof before))
            return false;
        native = input;
        native.budget = cycles + 1;
        s32 x = point == save ? (s16)input.r[31] : points[i][0];
        s32 dx = local ? x + 4 : -4 - x;
        s32 v1 = local ? -3 : x;
        s32 a2 = local ? x : points[i][1];
        if (!prepare(&native) || native.budget != 1 || native.r[4] != (u32)dx ||
            native.r[5] != (u32)(3 + points[i][1]) || native.r[3] != (u32)v1 ||
            native.r[6] != (u32)a2 || native.r[2] != (u32)-4 || native.r[31] != resume ||
            native.r[29] != input.r[29] - 24 || MEM_R32(save) != input.r[31])
            return false;
        native.budget = input.budget - cycles;
        if (!nc_vector_heading(&native))
            return false;
        cutoff = native;
        cutoff.budget = 7;
        CPUState epilogue = cutoff;
        memcpy(after, nc_scratch, sizeof after);
        if (nc_return_point_heading(&epilogue) || memcmp(&cutoff, &epilogue, sizeof cutoff) ||
            memcmp(nc_scratch, after, sizeof after))
            return false;
        epilogue.budget++;
        epilogue.r[2] = 0x12348001;
        if (!nc_return_point_heading(&epilogue) || epilogue.budget != 1 ||
            epilogue.r[2] != 0xffff8001 || !nc_return_point_heading(&native) ||
            native.r[31] != input.r[31] || native.r[29] != input.r[29])
            return false;
        if (disc) {
            memcpy(nc_scratch, before, sizeof before);
            interp_call(&input, entry);
            if (memcmp(&input, &native, sizeof input) || memcmp(nc_scratch, after, sizeof after))
                return false;
        }
    }
    return true;
}

static bool part_positions(bool disc) {
    const u32 matrices[][5] = {
        {0x1000, 0, 0x1000, 0, 0x1000}, /* identity */
        {0x10000000, 0xf0000000, 0, 0, 0x1000},
        {0xf8ab1234, 0x7fff4321, 0x8000fffb, 0x2468aaa0, 0xabcd1357},
    };
    const u32 coords[] = {0, 1, 0xfffffff7, 0x7fff, 0xffff8000, 0x7fffffff, 0x80000000};
    const unsigned count = sizeof coords / sizeof coords[0];
    u8 original[SCRATCH_SIZE], expected[SCRATCH_SIZE];
    for (unsigned m = 0; m < sizeof matrices / sizeof matrices[0]; ++m) {
        for (unsigned i = 0; i < 5; ++i)
            MEM_W32(0x800d7688 + i * 4, matrices[m][i]);
        for (unsigned axis = 0; axis < 3; ++axis)
            MEM_W16(0x800d7630 + axis * 2, 0xfffd + axis);
        for (unsigned quarter = 0; quarter < 2; ++quarter)
            for (unsigned sample = 0; sample < count; ++sample)
                for (unsigned alias = 0; alias < 2; ++alias) {
                    CPUState c = initial();
                    c.r[4] = SCRATCH_BASE + 0x40;
                    c.r[29] = SCRATCH_BASE + 0x180;
                    c.r[5] = alias ? c.r[29] - 12 : SCRATCH_BASE + 0x240;
                    memset(nc_scratch, 0xa5, SCRATCH_SIZE);
                    for (unsigned axis = 0; axis < 3; ++axis)
                        MEM_W32(c.r[4] + 0x2c + 4 * axis, coords[(sample + axis) % count]);
                    memcpy(original, nc_scratch, SCRATCH_SIZE);
                    c.budget = quarter ? 85 : 82;
                    CPUState native = c;
                    if (nc_transform_part_position(&native, quarter) ||
                        memcmp(&native, &c, sizeof c) || memcmp(original, nc_scratch, SCRATCH_SIZE))
                        return false;
                    native.budget = ++c.budget;
                    if (!nc_transform_part_position(&native, quarter) || native.budget != 1)
                        return false;
                    if (!m && !alias) {
                        for (unsigned axis = 0; axis < 3; ++axis) {
                            s32 v = (s32)coords[(sample + axis) % count];
                            s16 narrowed = (s16)(quarter ? v >> 2 : v);
                            if (MEM_R16(c.r[5] + axis * 2) !=
                                (u16)((narrowed >> 1) - (0xfffd + axis)))
                                return false;
                        }
                    }
                    if (disc) {
                        memcpy(expected, nc_scratch, SCRATCH_SIZE);
                        memcpy(nc_scratch, original, SCRATCH_SIZE);
                        interp_call(&c, quarter ? 0x80034828 : 0x800348fc);
                        if (memcmp(&c, &native, sizeof c) ||
                            memcmp(expected, nc_scratch, SCRATCH_SIZE)) {
                            fprintf(stderr,
                                    "part transform mismatch: matrix %u, scale %u, sample %u, "
                                    "alias %u\n",
                                    m, quarter, sample, alias);
                            return false;
                        }
                    }
                }
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc > 2)
        return 1;
    bool disc = argc == 2;
    if (disc) {
        FILE *f = fopen(argv[1], "rb");
        if (!f)
            return 77;
        fclose(f);
        ExecInfo exe;
        if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX2.EXE;1", &exe))
            return 1;
    }
    if (!placement_tick_latch(disc) || !placement_clocks(disc) || !placement_reset(disc) ||
        !placement_refreshes(disc) || !placement_counters(disc) || !placement_presets(disc) ||
        !placement_update_entry(disc) || !placement_mode_entry(disc) ||
        !placement_step_selection(disc) || !actor_placement_paths(disc) ||
        !actor_placement_targets(disc) || !placement_matrix_return(disc) ||
        !placement_matrix_calls(disc) || !placement_matrix_scales(disc) ||
        !placement_axis_samples(disc) || !placement_axes(disc) ||
        !placement_vector_frame(disc, true) || !placement_vector_frame(disc, false) ||
        !placement_vector_pairs(disc) || !placement_heading_calls(disc) ||
        !transposed_placement_scales(disc) || !placement_pose_initializers(disc) ||
        !placement_record_steps(disc))
        return 1;
    bool ok =
        spatial_frames(disc, true) && spatial_frames(disc, false) && record_angle_pairs(disc) &&
        record_angle_steps(disc) && paired_point_return(disc) && paired_point_placement(disc) &&
        actor_level_selection(disc) && actor_level_calls(disc) && point_level_selection(disc) &&
        spatial_levels(disc, true) && spatial_levels(disc, false) && spatial_attenuation(disc) &&
        spatial_distances(disc, true) && spatial_distances(disc, false) &&
        spatial_bearings(disc, true) && spatial_bearings(disc, false) &&
        target_vertical_velocities(disc) && target_displacements(disc) && planar_velocities(disc) &&
        roots(disc) && part_heading_entry(disc) && part_turn_resets(disc) &&
        part_heading_turns(disc) && part_turn_completion(disc) && part_pose_heading_steps(disc) &&
        part_search_exits(disc) && part_heading_windows(disc) && part_bearing_calls(disc) &&
        part_point_bearings(disc) && bearing_point_selection(disc) &&
        selected_point_returns(disc) && selected_point_bearings(disc) &&
        reference_bearing_gaps(disc) && actor_distances(disc) && point_pose_and_query(disc) &&
        point_index_advances(disc) && point_update_returns(disc) && point_proximity(disc) &&
        point_row_records(disc) && point_update_entries(disc) && record_point_bearings(disc) &&
        linked_heading_offsets(disc) && linked_heading_traversal(disc) &&
        unlinked_pose_headings(disc) && record_heading_frames(disc) &&
        projection_bypass_flags(disc) && record_point_projections(disc) &&
        placement_point_scans(disc) && placement_point_flags(disc) && placement_angle_steps(disc) &&
        placement_quad_windings(disc) && placement_triangle_windings(disc) &&
        placement_face_checks(disc) && textured_face_entries(disc) && textured_face_cursors(disc) &&
        textured_face_flags(disc) && untextured_face_entry(disc, false) &&
        untextured_face_entry(disc, true) && placement_face_returns(disc) &&
        placement_face_advances(disc) && untextured_packet_finishes(disc) &&
        textured_packet_finishes(disc) && placement_packet_links(disc) &&
        textured_quad_packets(disc) && textured_triangle_packets(disc) &&
        projected_triangle_packets(disc) && projected_quad_packets(disc) &&
        pose_traversal_entries(disc) && placement_projection_matrices(disc) &&
        placement_depths(disc, 0x80028b0c, 0x2d8, 18, 4) &&
        placement_depths(disc, 0x80028d8c, 0x160, 18, 4) &&
        projection_effect_flags(disc) &&
        projection_traversal_entries(disc) &&
        projection_face_calls(disc, true) && projection_face_calls(disc, false) &&
        untextured_projection_modes(disc) &&
        projection_packet_modes(disc) &&
        projection_traversal_return(disc) &&
        placement_depths(disc, 0x80027f0c, 8, 5, 5) &&
        placement_depth_cues(disc, 0x80027f0c, 8, 15) &&
        placement_depth_cues(disc, 0x80028b0c, 0x2d8, 8) &&
        placement_depth_cues(disc, 0x80028d8c, 0x160, 8) && projection_pose_filters(disc) &&
        projection_pose_flags(disc) && projection_pose_selection(disc) &&
        projection_pose_links(disc) && placement_pose_stack(disc) &&
        placement_pose_exits(disc, false) && placement_pose_exits(disc, true) &&
        placement_vertex_dispatch(disc) && placement_vertex_triples(disc, false) &&
        placement_vertex_triples(disc, true) && indexed_face_lists(disc, true) &&
        indexed_face_lists(disc, false) && indexed_face_frames(disc) &&
        indexed_face_advances(disc, true) && indexed_face_advances(disc, false) &&
        indexed_packet_links(disc, true) && indexed_packet_links(disc, false) &&
        indexed_face_packets(disc, true) && indexed_face_packets(disc, false) &&
        indexed_face_windings(disc) && indexed_depth_stores(disc) &&
        indexed_face_selectors(disc, true) && indexed_face_selectors(disc, false) &&
        indexed_vertex_inputs(disc) && indexed_vertex_projections(disc) &&
        remaining_placement_vertices(disc, false) && remaining_placement_vertices(disc, true) &&
        placement_vertex_matrices(disc) && indexed_pose_projection_calls(disc) &&
        pose_vertex_projection_calls(disc) && placement_transform_flags(disc) &&
        placement_projection_args(disc) && placement_angle_entries(disc) &&
        type7_pose_angles(disc) && placement_angle_calls(disc) && placement_traversal_calls(disc) &&
        record_pose_coordinates(disc) && point_strides(disc) && point_heading_turns(disc) &&
        point_distance_bearings(disc) && displacements(disc) && headings(disc) &&
        heading_samples(disc) && projected_points(disc) && part_positions(disc) &&
        actor_coordinates(disc) && point_headings(disc, false) && point_headings(disc, true) &&
        actor_headings(disc) && heading_projections(disc) && reference_distances(disc) &&
        relative_headings(disc) && record_projections(disc) && projected_heading_gaps(disc) &&
        projected_heading_selection(disc) && saved_bearing_projections(disc) &&
        facing_offsets(disc) && projection_entries(disc) && rotated_point_offsets(disc) &&
        rotated_vector_setup(disc) && placed_actor_points(disc) && actor_point_placements(disc) &&
        matrix_headings(disc) && matrix_planar_lengths(disc) && heading_matrix_setup(disc) &&
        part_translation(disc) && part_transform_calls(disc) && node_transform_selection(disc) &&
        node_matrix_installation(disc) && part_composition(disc) &&
        node_continuations(disc, false) && node_continuations(disc, true) && node_rotations(disc) &&
        traversal_angle_samples(disc, true) && traversal_angle_samples(disc, false) &&
        scaled_walk_continuations(disc) && traversal_rotation_composition(disc) &&
        traversal_matrices(disc) && part_heading_sources(disc) && child_positions(disc) &&
        scaled_part_filters(disc) && scaled_part_walk(disc) && scaled_part_positions(disc) &&
        part_offsets(disc) && part_matrix_composition(disc) && part_transform_setup(disc) &&
        part_chain_traversal(disc);
    for (NcRotationInput variant = NC_ROTATION_PART; ok && variant <= NC_ROTATION_PACKED; ++variant)
        ok = part_rotation_samples(disc, variant) && part_rotation_matrices(disc, variant);
    if (disc)
        disc_close();
    return ok ? 0 : 1;
}
