#include <stdlib.h>

#include "decomp/pad.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/hw/hw.h"
#include "port/interp.h"
#include "port/runtime.h"
#include "port/savestate.h"
#include "port/settings.h"

static u8 before[RAM_SIZE], after[RAM_SIZE];
static const u32 gp = 0x80120000, packet = 0x8008f1c0, config = 0x80130000;

static CPUState initial(void) {
    CPUState cpu = {0};
    for (unsigned i = 1; i < 32; ++i)
        cpu.r[i] = i * 0x13579u;
    cpu.r[28] = gp;
    cpu.r[29] = 0x801ff000;
    cpu.r[31] = 0x80010000;
    cpu.budget = 4096;
    return cpu;
}

static bool compare(CPUState cpu, u32 entry, bool (*native)(CPUState *), int cutoff) {
    memcpy(before, nc_ram, RAM_SIZE);
    CPUState actual = cpu;
    actual.budget = cutoff;
    CPUState low = actual;
    if (native(&low) || memcmp(&actual, &low, sizeof low) || memcmp(nc_ram, before, RAM_SIZE))
        return false;
    actual = cpu;
    if (!native(&actual))
        return false;
    memcpy(after, nc_ram, RAM_SIZE);
    memcpy(nc_ram, before, RAM_SIZE);
    interp_call(&cpu, entry);
    if (memcmp(&actual, &cpu, sizeof cpu) || memcmp(nc_ram, after, RAM_SIZE)) {
        fprintf(stderr, "%08x budget native/disc %d/%d, packet %02x/%04x\n", entry, actual.budget,
                cpu.budget, MEM_R8(packet + 1), MEM_R16(packet + 2));
        return false;
    }
    return true;
}

static bool player_source(u32 flags) {
    const u32 actor = 0x80160000, records = 0x80170000;
    bool sequence = (flags & 0x30) != 0;
    CPUState input = initial();
    input.r[3] = flags;
    input.r[31] = sequence ? 0x80044784 : 0x80044780;
    MEM_W32(gp + 0x244, config);
    MEM_W32(config, flags);
    MEM_W32(packet, 0xbfef4100);      /* port one: 0x4010 */
    MEM_W32(packet + 34, 0xfd5f4100); /* port two: 0x02a0 */
    MEM_W32(gp + 0x184, records);
    MEM_W32(gp + 0x188, 0);
    MEM_W8(gp + 0x3f8, 3);
    MEM_W32(records, 0x77048080);
    MEM_W16(0x800d7c26, 0); /* save buttons and return before movement processing */
    memcpy(before, nc_ram, RAM_SIZE);

    CPUState low = input;
    low.budget = 7;
    CPUState native = low;
    if (nc_player_input_source(&native) != 0x80044754 || memcmp(&native, &low, sizeof low) ||
        memcmp(before, nc_ram, RAM_SIZE))
        return false;
    native = input;
    u32 buttons = sequence ? 0x8080 : 0x02a04010;
    if (nc_player_input_source(&native) != input.r[31] || native.r[2] != buttons ||
        native.r[17] != (sequence ? buttons : input.r[17]))
        return false;
    memcpy(after, nc_ram, RAM_SIZE);
    memcpy(nc_ram, before, RAM_SIZE);
    CPUState guest = input;
    interp_call(&guest, 0x80044754);
    if (memcmp(&guest, &native, sizeof guest) || memcmp(nc_ram, after, RAM_SIZE))
        return false;

    /* With a centered modern stick the hook clears live direction bits.
     * Sequence direction bits pass through untouched. Exercise the caller
     * at both sides of the budget cutoff after its 16-cycle prologue. */
    bios_set_pad(0);
    for (unsigned modern = 0; modern < 2; ++modern) {
        settings.controls = modern ? CONTROLS_MODERN : CONTROLS_TANK;
        for (int budget = 23; budget <= 24; ++budget) {
            memcpy(nc_ram, before, RAM_SIZE);
            CPUState cpu = initial();
            cpu.r[4] = actor;
            cpu.budget = budget;
            nc_set_module(NC_MOD_PSX2);
            nc_call(&cpu, 0x80044724);
            nc_set_module(NC_MOD_NONE);
            u32 expected = modern && !sequence ? buttons & ~0xf0u : buttons;
            if (MEM_R32(actor + 0x2f8) != expected)
                return false;
        }
    }
    settings.controls = CONTROLS_TANK;
    return true;
}

static bool button_dispatch(void) {
    const u32 actor = 0x80160000;
    const struct {
        u32 buttons, actor, continuation;
        u16 state;
    } cases[] = {
        {0x00000001, actor, 0x800451ec, 1},      {0xbeef0101, 0, 0x800451ec, 0},
        {0x00000100, 0, 0x800451e8, 1},          {0, actor, 0x800451e8, 0},
        {0x00000100, actor, 0x800451e8, 0},      {0x01010000, actor, 0x800448d4, 1},
        {0x00000100, actor, 0x800447b8, 0xffff}, {0x12344180, actor, 0x800447b8, 1},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState input = initial();
        input.r[16] = cases[i].actor;
        input.r[17] = cases[i].buttons;
        input.r[31] = cases[i].continuation; /* stop the interpreter at the branch destination */
        MEM_W16(0x800d7c26, cases[i].state);
        MEM_W32(actor + 0x2f8, 0xabcd40c0);
        memcpy(before, nc_ram, RAM_SIZE);
        input.budget = 17;
        CPUState native = input;
        if (nc_dispatch_player_buttons(&native) != 0x80044784 ||
            memcmp(&native, &input, sizeof input) || memcmp(nc_ram, before, RAM_SIZE))
            return false;
        input.budget = 18;
        native = input;
        if (nc_dispatch_player_buttons(&native) != cases[i].continuation ||
            memcmp(nc_ram, before, RAM_SIZE))
            return false;
        interp_call(&input, 0x80044784);
        if (memcmp(&native, &input, sizeof input) || memcmp(nc_ram, before, RAM_SIZE))
            return false;
    }
    return true;
}

static bool check_start_edge(u32 buttons, u32 previous, u32 flags, u32 state, u32 resume) {
    CPUState input = initial();
    input.r[2] = buttons;
    input.r[31] = resume;
    MEM_W32(gp + 0x144, previous);
    MEM_W32(gp + 0x244, config);
    MEM_W32(config, flags);
    MEM_W32(config + 4, state);
    memcpy(before, nc_ram, RAM_SIZE);
    input.budget = 34;
    CPUState native = input;
    if (nc_pad_start_edge(&native) != 0x80042e8c || memcmp(&native, &input, sizeof input) ||
        memcmp(before, nc_ram, RAM_SIZE))
        return false;
    input.budget = 35;
    native = input;
    if (nc_pad_start_edge(&native) != input.r[31] || memcmp(before, nc_ram, RAM_SIZE))
        return false;
    CPUState guest = input;
    interp_call(&guest, 0x80042e8c);
    if (memcmp(&native, &guest, sizeof guest) || memcmp(before, nc_ram, RAM_SIZE))
        return false;
    return true;
}

static bool pad_start_edge(void) {
    const u32 words[] = {0, 1, 8, 9, 0x00090000, 0xdead4088};
    for (unsigned i = 0; i < sizeof words / sizeof words[0]; ++i) {
        for (unsigned held = 0; held < 2; ++held) {
            u32 resume = (words[i] & 9) == 8 && !held ? 0x80042ef4 : 0x80042efc;
            if (!check_start_edge(words[i], 0x00080001 | (held << 3), 0, 0, resume))
                return false;
        }
    }
    for (unsigned bits = 0; bits < 16; ++bits) {
        u32 flags = 0x80000020 | ((bits & 1) << 6) | ((bits & 2) << 26);
        u32 state = 0x40000010 | ((bits & 12) << 22);
        u32 resume = bits & 1 ? 0x80042f18 : bits ? 0x80042efc : 0x80042ef4;
        if (!check_start_edge(0xdead4088, 0x80000000, flags, state, resume))
            return false;
    }
    return true;
}

static bool dispatch_read(CPUState *cpu) {
    return nc_call_live_pad(cpu, 0x80042e8c);
}

static bool live_pad_dispatch(void) {
    CPUState cpu = initial();
    cpu.r[31] = 0x80042e8c;
    MEM_W32(gp + 0x244, config);
    MEM_W32(config, 0x8000); /* suppress the disconnect action when neither port responds */
    MEM_W32(config + 4, 0);
    const u32 words[] = {0, 1, 0x00080000, 0x00404008};
    for (unsigned i = 0; i < sizeof words / sizeof words[0]; ++i) {
        MEM_W32(packet, (~words[i] << 16) | 0x4100);
        MEM_W32(packet + 34, (~words[i] & 0xffff0000) | 0x4100);
        if (!compare(cpu, 0x80042e7c, dispatch_read, 4))
            return false;
    }
    MEM_W8(packet, 0xff);
    MEM_W8(packet + 34, 0xff);
    return compare(cpu, 0x80042e7c, dispatch_read, 4);
}

static bool dispatch_start_action(CPUState *cpu) {
    return nc_call_pad_action(cpu, false);
}

static bool dispatch_select_action(CPUState *cpu) {
    return nc_call_pad_action(cpu, true);
}

static bool pad_dispatch_actions(void) {
    CPUState cpu = initial();
    MEM_W32(gp + 0x244, config);
    MEM_W32(gp + 0x2d0, 0);
    MEM_W32(config, 0);
    MEM_W32(config + 4, 0);
    cpu.r[31] = 0x80042efc;
    if (!compare(cpu, 0x80042ef4, dispatch_start_action, 2))
        return false;
    MEM_W16(0x800cc00c, 0);
    MEM_W16(0x800cbff8, 0xa540);
    cpu.r[31] = 0x80042f54;
    if (!compare(cpu, 0x80042f40, dispatch_select_action, 2))
        return false;

    MEM_W32(cpu.r[29] + 0x20, 0x12345678);
    MEM_W32(cpu.r[29] + 0x24, 0); /* stop the disc interpreter after jr ra */
    cpu.budget = 9;
    CPUState native = cpu;
    memcpy(before, nc_ram, RAM_SIZE);
    if (nc_leave_pad_dispatch(&native) || memcmp(&native, &cpu, sizeof cpu) ||
        memcmp(before, nc_ram, RAM_SIZE))
        return false;
    cpu.budget = 10;
    native = cpu;
    if (!nc_leave_pad_dispatch(&native))
        return false;
    interp_resume_exe(&cpu, 0x80042f54);
    return !memcmp(&native, &cpu, sizeof cpu) && !memcmp(before, nc_ram, RAM_SIZE);
}

static bool playback_read_destination(CPUState *cpu) {
    u32 expected = cpu->r[31];
    return nc_poll_playback_input(cpu) == expected;
}

static bool playback_read(void) {
    const u32 records = 0x80170000;
    CPUState cpu = initial();
    cpu.r[4] = 0;
    cpu.budget = 2;
    if (nc_poll_playback_input(&cpu) != 0x80042e18)
        return false;
    MEM_W32(gp + 0x244, config);
    MEM_W32(config, 0);
    MEM_W32(config + 4, 0);
    MEM_W32(gp + 0x184, records);
    MEM_W32(gp + 0x188, 0);
    const u32 words[] = {0, 1, 0x00400000, 0x00204001};
    for (unsigned i = 0; i < sizeof words / sizeof words[0]; ++i) {
        for (unsigned ended = 0; ended < 2; ++ended) {
            MEM_W32(records, ended ? 0x4e414c41 : 0x12345678);
            MEM_W32(packet, (~words[i] << 16) | 0x4100);
            MEM_W32(packet + 34, (~words[i] & 0xffff0000) | 0x4100);
            cpu.r[31] = words[i] ? 0x80042e38 : 0x80042e30;
            cpu.budget = 4096;
            if (!compare(cpu, 0x80042e18, playback_read_destination, 2))
                return false;
        }
    }
    return true;
}

static bool pad_dispatch_entry(void) {
    CPUState input = initial();
    input.r[31] = 0; /* stop at the common return, after restoring the dispatch frame */
    MEM_W32(gp + 0x244, config);
    MEM_W32(gp + 0x190, 0x21);
    MEM_W32(packet, 0xffff4100);
    MEM_W32(packet + 34, 0xffff4100);
    for (unsigned mode = 0; mode <= 0x30; mode += 0x10) {
        for (unsigned bits = 0; bits < 4; ++bits) {
            MEM_W32(config, mode | ((bits & 1) << 15));
            MEM_W32(config + 4, 0x02000010 | ((bits & 2) << 17));
            MEM_W32(input.r[29] - 4, 0x12345678);
            MEM_W32(input.r[29] - 8, 0x87654321);
            memcpy(before, nc_ram, RAM_SIZE);
            CPUState low = input;
            low.budget = 24;
            CPUState native = low;
            if (nc_enter_pad_dispatch(&native) != 0x80042d9c || memcmp(&native, &low, sizeof low) ||
                memcmp(before, nc_ram, RAM_SIZE))
                return false;
            native = input;
            u32 resume = nc_enter_pad_dispatch(&native);
            if (resume != (mode ? 0x80042de4u : 0x80042e7cu) ||
                MEM_R32(input.r[29] - 4) != input.r[31] || MEM_R32(input.r[29] - 8) != input.r[16])
                return false;
            if (!mode || bits) {
                u32 expected_v0 = bits & 1 ? 0x8000 : mode ? 0x40000 : 0;
                int cycles = !mode ? 13 : bits & 1 ? 15 : 22;
                if (native.r[2] != expected_v0 || native.r[3] != (mode ? 0x40000 : expected_v0) ||
                    native.r[4] != config || native.budget != input.budget - cycles)
                    return false;
            }
            interp_resume_exe(&native, resume);
            memcpy(after, nc_ram, RAM_SIZE);
            memcpy(nc_ram, before, RAM_SIZE);
            CPUState guest = input;
            interp_resume_exe(&guest, 0x80042d9c);
            if (memcmp(&native, &guest, sizeof guest) || memcmp(after, nc_ram, RAM_SIZE))
                return false;
        }
    }
    return true;
}

static bool playback_poll_destination(CPUState *cpu) {
    return nc_pad_playback_poll_gate(cpu) == cpu->r[31];
}

static bool playback_poll_gate(void) {
    CPUState cpu = initial();
    cpu.budget = 19;
    if (nc_pad_playback_poll_gate(&cpu) != 0x80042de4)
        return false;
    cpu.budget = 20;
    MEM_W32(gp + 0x244, config);
    for (unsigned bits = 0; bits < 4; ++bits) {
        MEM_W32(config, 0x80000020 | ((bits & 1) << 4));
        MEM_W32(config + 4, 0x81000008 | ((bits & 2) << 24));
        cpu.r[31] = bits ? 0x80042f54 : 0x80042e18;
        if (!compare(cpu, 0x80042de4, playback_poll_destination, 19))
            return false;
    }
    return true;
}

static bool playback_sequence_result(CPUState *cpu) {
    return nc_pad_playback_interrupt(cpu, true);
}

static bool playback_live_press(CPUState *cpu) {
    return nc_pad_playback_interrupt(cpu, false);
}

static bool playback_interrupt(void) {
    CPUState cpu = initial();
    cpu.r[16] = 0x02000000;
    cpu.r[31] = 0x80042f54;
    MEM_W32(gp + 0x244, config);
    const u32 results[] = {0, 1, 0x10000, 0xabcd0041};
    for (unsigned i = 0; i < sizeof results / sizeof results[0]; ++i) {
        cpu.r[2] = results[i];
        for (unsigned set = 0; set < 2; ++set) {
            for (unsigned sequence = 0; sequence < 2; ++sequence) {
                MEM_W32(config + 4, 0x80345678 | (set << 25));
                MEM_W32(0x800d7c30, 0xbeefdead);
                MEM_W32(0x8008f164, 0x11223344);
                cpu.budget = sequence ? 24 : 22;
                if (!compare(cpu, sequence ? 0x80042e30 : 0x80042e38,
                             sequence ? playback_sequence_result : playback_live_press,
                             cpu.budget - 1))
                    return false;
            }
        }
    }
    return true;
}

static bool select_path_matches(CPUState *cpu) {
    return nc_pad_select_edge(cpu) == cpu->r[31];
}

static bool pad_select_edge(void) {
    const u32 words[] = {0, 1, 8, 9, 0x00010000, 0xabcd4011};
    CPUState cpu = initial();
    cpu.budget = 20;
    if (nc_pad_select_edge(&cpu) != 0x80042efc)
        return false;
    cpu.budget = 21;
    MEM_W32(gp + 0x244, config);
    for (unsigned i = 0; i < sizeof words / sizeof words[0]; ++i) {
        cpu.r[16] = words[i];
        for (unsigned held = 0; held < 2; ++held) {
            for (unsigned flagged = 0; flagged < 2; ++flagged) {
                bool new_press = !flagged && (words[i] & 1) && !held;
                cpu.r[31] = new_press ? 0x80042f40 : 0x80042f54;
                MEM_W32(config, 0x80000020 | (flagged << 6));
                MEM_W32(gp + 0x144, 0x12340008 | held);
                if (!compare(cpu, 0x80042efc, select_path_matches, 20))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_scan_destination(CPUState *cpu) {
    u32 buttons = cpu->r[4];
    bool scan = (buttons & 0xf01) && (buttons & ~MEM_R32(gp + 0x18c));
    u32 destination = nc_menu_button_scan_entry(cpu);
    if (destination != (scan ? 0x8006718c : 0x80067308))
        return false;
    /* Finish the disc routine with its stack frame still allocated. */
    interp_resume_exe(cpu, destination);
    return true;
}

static bool menu_button_scan_edges(void) {
    const u32 buttons[] = {0, 1, 0x100, 0x200, 0x400, 0x800, 0x4000, 0xf01, 0x80004001};
    for (unsigned i = 0; i < sizeof buttons / sizeof buttons[0]; ++i) {
        const u32 held[] = {0, buttons[i], 1, 0x80004000};
        for (unsigned j = 0; j < sizeof held / sizeof held[0]; ++j) {
            CPUState cpu = initial();
            cpu.r[4] = buttons[i];
            cpu.r[31] = 0;
            MEM_W32(gp + 0x18c, held[j]);
            memset(nc_ram + ((cpu.r[29] - 0x38) & (RAM_SIZE - 1)), 0xa5, 0x38);
            if (!compare(cpu, 0x80067130, menu_scan_destination, 25))
                return false;
        }
    }
    return true;
}

static bool menu_sequence_destination(CPUState *cpu) {
    return nc_menu_sequence_match(cpu) == cpu->r[31];
}

static bool menu_sequence_matches(void) {
    const struct {
        u32 buttons, destination;
        u16 expected;
        u8 type, progress;
    } cases[] = {
        {0, 0x8006725c, 0, 0, 0},
        {1, 0x8006725c, 1, 255, 0},
        {1, 0x80067200, 1, 10, 3},
        {0x300, 0x800672e0, 0x300, 10, 0},
        {0x301, 0x800672ec, 0x300, 10, 1},
        {0x8000, 0x800672ec, 0x8000, 10, 2},
        {0xffff8000, 0x800672e0, 0x8000, 10, 255},
    };
    for (unsigned index = 0; index <= 10; ++index) {
        u32 record = 0x80088594 + index * 50;
        for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            CPUState cpu = initial();
            cpu.budget = 40;
            cpu.r[19] = index;
            cpu.r[2] = 2 * index;
            cpu.r[20] = cases[i].buttons;
            cpu.r[21] = 0x80088594;
            cpu.r[22] = 0x80088596;
            cpu.r[31] = cases[i].destination;
            u32 step = record + 2 + 2 * cases[i].progress;
            u16 saved_header = MEM_R16(record), saved_step = MEM_R16(step);
            MEM_W8(record, cases[i].progress);
            MEM_W8(record + 1, cases[i].type);
            MEM_W16(step, cases[i].expected);
            MEM_W32(gp + 0x244, config);
            MEM_W32(config + 4, 0xabcd0001);
            bool ok = compare(cpu, 0x8006718c, menu_sequence_destination, 39);
            MEM_W16(record, saved_header);
            MEM_W16(step, saved_step);
            if (!ok)
                return false;
        }
    }
    return true;
}

static bool menu_record_destination(CPUState *cpu) {
    return nc_menu_record_sequence(cpu) == cpu->r[31];
}

static bool menu_record_sequences(void) {
    const struct {
        u32 buttons, destination;
        u16 expected;
    } cases[] = {
        {0, 0x800672ec, 1},         {1, 0x800672b4, 1},           {0x300, 0x800672e0, 0x300},
        {0x301, 0x800672ec, 0x300}, {0x8000, 0x800672ec, 0x8000}, {0xffff8000, 0x800672e0, 0x8000},
    };
    for (unsigned index = 0; index <= 10; ++index) {
        u32 type = index == 10 ? 255 : index;
        u32 descriptor = 0x80088178 + 28 * type;
        u32 record = 0x80088594 + 50 * index;
        u32 progress = index == 10 ? 255 : index;
        u32 step = record + 2 + 2 * progress;
        u32 saved_flags = MEM_R32(descriptor);
        u8 saved_progress = MEM_R8(record);
        u16 saved_step = MEM_R16(step);
        for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            for (unsigned enabled = 0; enabled < 2; ++enabled) {
                CPUState cpu = initial();
                cpu.budget = 29;
                cpu.r[2] = type * 8;
                cpu.r[3] = type;
                cpu.r[4] = index * 50;
                cpu.r[20] = cases[i].buttons;
                cpu.r[21] = 0x80088594;
                cpu.r[22] = 0x80088596;
                cpu.r[23] = 0x02000000;
                cpu.r[31] = enabled ? cases[i].destination : 0x800672f8;
                MEM_W32(descriptor, enabled ? 0x82000001 : 0x80000001);
                MEM_W8(record, progress);
                MEM_W16(step, cases[i].expected);
                if (!compare(cpu, 0x8006725c, menu_record_destination, 28))
                    return false;
            }
        }
        MEM_W32(descriptor, saved_flags);
        MEM_W8(record, saved_progress);
        MEM_W16(step, saved_step);
    }
    return true;
}

static bool store_menu_match(CPUState *c) {
    return nc_menu_store_progress(c, true);
}

static bool clear_menu_mismatch(CPUState *c) {
    return nc_menu_store_progress(c, false);
}

static bool advance_menu_scan(CPUState *c) {
    return nc_menu_next_record(c, true) == c->r[31];
}

static bool resume_menu_scan(CPUState *c) {
    return nc_menu_next_record(c, false) == c->r[31];
}

static bool menu_enabled_record_destination(CPUState *cpu) {
    return nc_menu_advance_enabled_record(cpu) == cpu->r[31];
}

static bool menu_enabled_records(void) {
    CPUState cpu = initial();
    cpu.r[23] = 0x02000000;
    cpu.r[31] = 0x80067240;
    MEM_W32(gp + 0x244, config);
    MEM_W32(config + 4, 0);
    for (unsigned index = 0; index < 10; ++index) {
        cpu.r[16] = index * 50;
        cpu.r[17] = 0x80088594 + cpu.r[16];
        u32 type = MEM_R8(cpu.r[17] + 1);
        u32 flags = 0x80088178 + type * 28;
        u32 saved = MEM_R32(flags);
        for (unsigned enabled = 0; enabled < 2; ++enabled) {
            MEM_W32(flags, enabled ? 0x82000001 : 0x80000001);
            if (!compare(cpu, 0x80067200, nc_menu_apply_enabled_record, 20))
                return false;
        }
        MEM_W32(flags, saved);
    }
    const u32 indices[] = {0, 8, 9, 10, 0x7fffffff, 0xffffffff};
    cpu.budget = 8;
    for (unsigned i = 0; i < sizeof indices / sizeof indices[0]; ++i) {
        cpu.r[18] = indices[i];
        cpu.r[31] = (s32)(indices[i] + 1u) < 10 ? 0x80067200 : 0x800672fc;
        if (!compare(cpu, 0x80067240, menu_enabled_record_destination, 7))
            return false;
    }
    return true;
}

static bool menu_completed_sequence(void) {
    CPUState cpu = initial();
    cpu.r[4] = 0x80160000;
    cpu.r[31] = 0x800672bc;
    MEM_W32(gp + 0x244, config);
    MEM_W32(config + 4, 0);
    MEM_W8(cpu.r[4] + 1, 3);
    if (!compare(cpu, 0x800672b4, nc_menu_apply_sequence, 2))
        return false;

    cpu.r[31] = 0x800672f8;
    cpu.budget = 70;
    const u32 records = 0x80088594;
    u8 saved[11];
    for (unsigned i = 0; i < 11; ++i) {
        saved[i] = MEM_R8(records + 50 * i);
        MEM_W8(records + 50 * i, 0xf0 + i);
    }
    bool ok = compare(cpu, 0x800672bc, nc_menu_reset_sequences, 69);
    for (unsigned i = 0; i < 11; ++i)
        MEM_W8(records + 50 * i, saved[i]);
    return ok;
}

static bool menu_record_progress(void) {
    const u32 positions[] = {1, 255, 256};
    CPUState cpu = initial();
    cpu.budget = 4;
    cpu.r[4] = 50 * 7;
    cpu.r[21] = 0x80160000;
    cpu.r[31] = 0x800672f8;
    u32 fixed = 0x80088594 + cpu.r[4], saved = MEM_R8(fixed);
    for (unsigned i = 0; i < sizeof positions / sizeof positions[0]; ++i) {
        cpu.r[3] = positions[i];
        MEM_W8(cpu.r[21] + cpu.r[4], 0xa5);
        MEM_W8(fixed, 0x5a);
        if (!compare(cpu, 0x800672e0, store_menu_match, 3) ||
            !compare(cpu, 0x800672ec, clear_menu_mismatch, 3))
            return false;
    }
    MEM_W8(fixed, saved);
    const u32 indices[] = {0, 9, 10, 11, 0x7fffffff, 0xffffffff};
    for (unsigned increment = 0; increment < 2; ++increment) {
        int cost = increment ? 4 : 3;
        cpu.budget = cost + 1;
        for (unsigned i = 0; i < sizeof indices / sizeof indices[0]; ++i) {
            cpu.r[19] = indices[i];
            cpu.r[31] = (s32)(indices[i] + increment) < 11 ? 0x8006718c : 0x80067308;
            if (!compare(cpu, increment ? 0x800672f8 : 0x800672fc,
                         increment ? advance_menu_scan : resume_menu_scan, cost))
                return false;
        }
    }
    return true;
}

static bool menu_scan_history(void) {
    const u32 records = 0x80088594;
    u16 headers[11];
    for (unsigned i = 0; i < 11; ++i) {
        headers[i] = MEM_R16(records + 50 * i);
        MEM_W16(records + 50 * i, 0);
    }
    u16 saved_step = MEM_R16(records + 4);
    u32 saved_flags = MEM_R32(0x80088178);
    MEM_W32(0x80088178, 0); /* skip ordinary records */
    MEM_W16(records + 4, 0x100);
    const struct {
        u32 previous, buttons;
        u8 progress;
    } cases[] = {
        {0xffffffff, 0, 1}, {0, 0x100, 2}, {0x100, 0x100, 1}, {0x100, 0x300, 0}, {0, 0x80000100, 0},
    };
    CPUState cpu = initial();
    cpu.r[31] = 0x800687d8;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        MEM_W16(records, 0x0a01); /* type 10, waiting for sequence step one */
        MEM_W32(gp + 0x18c, cases[i].previous);
        cpu.r[30] = cases[i].buttons;
        if (!compare(cpu, 0x800687cc, nc_menu_scan_and_save_buttons, 2) ||
            MEM_R8(records) != cases[i].progress || MEM_R32(gp + 0x18c) != cases[i].buttons)
            return false;
    }
    MEM_W32(0x80088178, saved_flags);
    MEM_W16(records + 4, saved_step);
    for (unsigned i = 0; i < 11; ++i)
        MEM_W16(records + 50 * i, headers[i]);
    return true;
}

static bool menu_shoulder_destination(CPUState *cpu) {
    return nc_menu_record_shoulders(cpu) == cpu->r[31];
}

static bool menu_triangle_destination(CPUState *cpu) {
    return nc_menu_record_triangle(cpu) == cpu->r[31];
}

static bool menu_record_select_destination(CPUState *cpu) {
    return nc_menu_record_select(cpu) == cpu->r[31];
}

static bool menu_apply_record_select(void) {
    CPUState cpu = initial();
    const u32 record = 0x80088438;
    u16 saved_target = MEM_R16(record + 6);
    cpu.r[5] = record;
    cpu.r[31] = 0x80068cf8;
    MEM_W32(gp + 0x244, config);
    MEM_W32(gp + 0x190, 10);
    MEM_W16(0x800d7c6c, 0);    /* no sound assigned */
    MEM_W16(record + 6, 0xff); /* stay on this menu record */
    MEM_W32(config, 0x100000); /* the action clears this bit */
    MEM_W32(gp + 0x18c, 0x80004001);
    MEM_W32(0x8008eee4, 0xffffffff);
    bool ok = compare(cpu, 0x80068980, nc_menu_apply_record_select, 2) &&
              MEM_R32(gp + 0x18c) == 0 && MEM_R32(0x8008eee4) == 1 && MEM_R32(config) == 0;
    MEM_W16(record + 6, saved_target);
    return ok;
}

static bool menu_record_dispatch_and_select(void) {
    CPUState cpu = initial();
    const u32 record = 0x80160000, item = 0x80170000;
    cpu.r[2] = 0x800883c0;
    cpu.r[5] = record - cpu.r[2];
    cpu.r[6] = 0x80;
    cpu.r[31] = 0x80068944;
    MEM_W32(record, item);
    MEM_W16(record + 4, 1);
    MEM_W16(record + 10, 0);
    MEM_W32(item + 4, 0);
    if (!compare(cpu, 0x8006893c, nc_menu_dispatch_record_action, 2))
        return false;

    const u32 words[] = {0, 1, 0x1000, 0xabcd1001};
    cpu.budget = 20;
    for (unsigned record_index = 9; record_index <= 11; ++record_index) {
        MEM_W32(gp + 0x190, record_index);
        for (unsigned i = 0; i < sizeof words / sizeof words[0]; ++i) {
            cpu.r[30] = words[i];
            for (unsigned held = 0; held < 2; ++held) {
                MEM_W32(gp + 0x18c, 0x1200 | held);
                bool fresh = record_index == 10 && (words[i] & 1) && !held;
                cpu.r[31] = fresh ? 0x80068980 : 0x80068d00;
                if (!compare(cpu, 0x80068944, menu_record_select_destination, 19))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_record_repeat_destination(CPUState *cpu) {
    return nc_menu_record_direction_repeat(cpu) == cpu->r[31];
}

static bool menu_record_direction_repeats(void) {
    const u32 counts[] = {0, 4, 5, 0x7fffffff, 0xffffffff};
    const u32 previous[] = {0, 0x50, 0xa0, 0xf0};
    for (unsigned mask = 0; mask < 16; ++mask) {
        u32 buttons = mask << 4;
        for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; ++i) {
            for (unsigned j = 0; j < sizeof previous / sizeof previous[0]; ++j) {
                CPUState cpu = initial();
                cpu.budget = 31;
                cpu.r[30] = buttons | (j & 1 ? 0x80000400 : 0);
                cpu.r[3] = i * 12;
                MEM_W32(gp + 0x194, counts[i]);
                MEM_W32(gp + 0x18c, previous[j]);
                u32 selected = buttons & 0x10   ? 0x10
                               : buttons & 0x40 ? 0x40
                               : buttons & 0x80 ? 0x80
                                                : buttons & 0x20;
                bool suppressed = (s32)(counts[i] + 1u) < 6 && (selected & previous[j]);
                cpu.r[31] = !selected ? 0x80068adc : suppressed ? 0x80068cf8 : 0x80068ce0;
                if (!compare(cpu, 0x800689a8, menu_record_repeat_destination, 30))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_shoulder_repeat_destination(CPUState *cpu) {
    return nc_menu_record_shoulder_repeat(cpu) == cpu->r[31];
}

static bool menu_record_shoulder_repeats(void) {
    const u32 counts[] = {0, 4, 5, 0x7fffffff, 0xffffffff};
    for (unsigned mask = 0; mask < 4; ++mask) {
        for (unsigned history = 0; history < 4; ++history) {
            for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; ++i) {
                CPUState cpu = initial();
                cpu.budget = 26;
                cpu.r[30] = (mask << 10) | (i & 1 ? 0xabcd4000 : 0);
                cpu.r[2] = cpu.r[30] & 0x400;
                cpu.r[3] = 12 * i;
                MEM_W32(gp + 0x194, counts[i]);
                MEM_W32(gp + 0x18c, history << 10);
                u32 first = mask & 1 ? 1 : mask & 2;
                bool held = (history & first) && (s32)(counts[i] + 1u) < 6;
                cpu.r[31] = !first ? 0x80068b74 : held ? 0x80068cf8 : 0x80068ce0;
                if (!compare(cpu, 0x80068adc, menu_shoulder_repeat_destination, 25))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_face_edge_destination(CPUState *cpu) {
    return nc_menu_record_face_edges(cpu) == cpu->r[31];
}

static bool menu_record_face_edges(void) {
    const u32 pairs[] = {0, 0x4000, 0x1000, 0x5000};
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned j = 0; j < 4; ++j) {
            for (unsigned square = 0; square < 2; ++square) {
                CPUState cpu = initial();
                cpu.budget = 31;
                cpu.r[30] = pairs[i] | (square ? 0xabcd8000 : 0);
                cpu.r[2] = cpu.r[30] & 0x4000;
                cpu.r[3] = 12 * 7;
                MEM_W32(gp + 0x190, square ? 0xffffffff : 10);
                MEM_W32(gp + 0x18c, pairs[j]);
                cpu.r[31] = (pairs[i] & ~pairs[j]) ? 0x80068ce0 : 0x80068bec;
                if (!compare(cpu, 0x80068b74, menu_face_edge_destination, 30))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_face_repeat_destination(CPUState *cpu) {
    return nc_menu_record_face_repeat(cpu) == cpu->r[31];
}

static bool menu_record_face_repeats(void) {
    const u32 words[] = {0, 0x8000, 0x2000, 0xa000};
    const u32 counts[] = {0, 4, 5, 0x7fffffff, 0xffffffff};
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned j = 0; j < 4; ++j) {
            for (unsigned k = 0; k < sizeof counts / sizeof counts[0]; ++k) {
                CPUState cpu = initial();
                cpu.budget = 32;
                cpu.r[30] = words[i] | (k & 1 ? 0xabcd0008 : 0);
                cpu.r[2] = words[i] & 0x8000;
                MEM_W32(gp + 0x190, k & 1 ? 0xffffffff : 7);
                MEM_W32(gp + 0x194, counts[k]);
                MEM_W32(gp + 0x18c, words[j]);
                u32 first = words[i] & 0x8000 ? 0x8000 : words[i] & 0x2000;
                bool held = (words[j] & first) && (s32)(counts[k] + 1u) < 6;
                cpu.r[31] = !first ? 0x80068ca4 : held ? 0x80068cf8 : 0x80068ce0;
                if (!compare(cpu, 0x80068bec, menu_face_repeat_destination, 31))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_record_start_destination(CPUState *cpu) {
    return nc_menu_record_start(cpu) == cpu->r[31];
}

static bool menu_record_start_and_dispatch(void) {
    CPUState cpu = initial();
    cpu.budget = 20;
    for (unsigned index = 0; index < 3; ++index) {
        MEM_W32(gp + 0x190, index);
        for (unsigned bits = 0; bits < 4; ++bits) {
            cpu.r[30] = 0x80001000 | ((bits & 1) << 3);
            cpu.r[2] = cpu.r[30] & 8;
            MEM_W32(gp + 0x18c, 0x40 | ((bits & 2) << 2));
            cpu.r[31] = bits != 1 ? 0x80068cf4 : index == 1 ? 0x80068ce0 : 0x80068d00;
            if (!compare(cpu, 0x80068ca4, menu_record_start_destination, 19))
                return false;
        }
    }
    cpu = initial();
    cpu.r[5] = 0x800883cc;
    cpu.r[6] = 0x20;
    cpu.r[30] = 0xbeef0008;
    cpu.r[31] = 0x80068cf8;
    MEM_W32(gp + 0x190, 1);
    u16 saved_target = MEM_R16(cpu.r[5] + 6);
    MEM_W16(cpu.r[5] + 6, 0xff); /* stay on the current record */
    bool ok = compare(cpu, 0x80068ce0, nc_menu_dispatch_and_save_buttons, 2);
    MEM_W16(cpu.r[5] + 6, saved_target);
    MEM_W32(gp + 0x194, 6);
    return ok && MEM_R32(gp + 0x18c) == cpu.r[30] &&
           compare(cpu, 0x80068cf4, nc_menu_clear_repeat, 1);
}

static bool menu_load_record_history(CPUState *cpu) {
    return nc_menu_save_record_buttons(cpu, true) == cpu->r[31];
}

static bool menu_save_record_history(CPUState *cpu) {
    return nc_menu_save_record_buttons(cpu, false) == cpu->r[31];
}

static bool menu_record_button_history(void) {
    const u32 actor = 0x80160000;
    u32 saved_actor = MEM_R32(0x800ce9c4);
    for (unsigned load = 0; load < 2; ++load) {
        int cutoff = load ? 20 : 16;
        for (unsigned match = 0; match < 2; ++match) {
            for (unsigned present = 0; present < 2; ++present) {
                CPUState cpu = initial();
                cpu.budget = cutoff + 1;
                cpu.r[2] = 100;
                cpu.r[3] = load ? 42 : 99 + match;
                cpu.r[30] = present ? 0xbeef4010 : 0;
                cpu.r[31] = match ? 0x80068d30 : 0x80068d3c;
                MEM_W32(gp + 0x190, load ? 99 + match : 42);
                MEM_W32(gp + 0x18c, 0x12345678);
                MEM_W32(0x800ce9c4, present ? actor : 0);
                MEM_W32(actor + 0x2f8, 0xabcdef01);
                if (!compare(cpu, load ? 0x80068cf8 : 0x80068d00,
                             load ? menu_load_record_history : menu_save_record_history, cutoff))
                    return false;
            }
        }
    }
    MEM_W32(0x800ce9c4, saved_actor);
    return true;
}

static bool menu_record_101_destination(CPUState *cpu) {
    u32 stop = cpu->r[31];
    return nc_menu_check_record_101(cpu) == stop;
}

static bool menu_record_history_resets(void) {
    CPUState cpu = initial();
    MEM_W32(gp + 0x244, config);
    MEM_W32(config, 0); /* take the guest call's short return */
    cpu.r[31] = 0x80068d3c;
    MEM_W32(gp + 0x190, 100);
    MEM_W32(gp + 0x18c, 0xabcd4008);
    if (!compare(cpu, 0x80068d30, nc_menu_reset_record_100, 3))
        return false;
    for (unsigned index = 100; index <= 102; ++index) {
        MEM_W32(gp + 0x190, index);
        MEM_W32(gp + 0x18c, 0xabcd4008);
        cpu.r[31] = index == 101 ? 0x80068fe4 : 0x80068d60;
        if (!compare(cpu, 0x80068d3c, menu_record_101_destination, 9))
            return false;
    }
    return true;
}

static bool menu_record_four_destination(CPUState *cpu) {
    return nc_menu_record_four_timing(cpu) == cpu->r[31];
}

static bool menu_record_four_timing(void) {
    const u32 flags[] = {0x40, 0x800, 0x1000, 0x80000000};
    const s32 elapsed[] = {-1, 0, 599, 600, 24000, 24001};
    MEM_W32(gp + 0x244, config);
    for (unsigned mode = 0; mode < 4; ++mode) {
        MEM_W32(config, flags[mode]);
        for (unsigned index = 3; index <= 4; ++index) {
            MEM_W32(gp + 0x190, index);
            for (unsigned i = 0; i < sizeof elapsed / sizeof elapsed[0]; ++i) {
                MEM_W32(gp + 0x3e8, 0xfffffff0);
                MEM_W32(0x8008ef10, 0xfffffff0u + (u32)elapsed[i]);
                for (unsigned cross = 0; cross < 2; ++cross) {
                    CPUState cpu = initial();
                    cpu.budget = 34;
                    cpu.r[30] = cross ? 0xabcd4010 : 0;
                    cpu.r[31] = 0x80068dd4;
                    if (!mode)
                        cpu.r[31] = 0x80068e84;
                    else if (index == 4 && elapsed[i] < 600)
                        cpu.r[31] = 0x80068fe4;
                    else if (index == 4 && elapsed[i] > 24000)
                        cpu.r[31] = 0x80068dbc;
                    if (!compare(cpu, 0x80068d60, menu_record_four_destination, 33))
                        return false;
                }
            }
        }
    }
    return true;
}

static bool menu_record_cross(void) {
    const u32 buttons[] = {0, 0x10, 0x4000, 0xabcd4010};
    for (unsigned i = 0; i < sizeof buttons / sizeof buttons[0]; ++i) {
        CPUState cpu = initial();
        cpu.r[30] = buttons[i];
        cpu.r[31] = 0x80068dd4;
        MEM_W32(gp + 0x190, 4);
        if (!compare(cpu, 0x80068dbc, nc_menu_dispatch_timed_record, 5))
            return false;
        cpu.r[2] = buttons[i] & 0x4000;
        cpu.r[31] = 0x80068e18;
        for (unsigned record = 3; record <= 4; ++record) {
            for (unsigned held = 0; held < 2; ++held) {
                MEM_W32(gp + 0x190, record);
                MEM_W32(gp + 0x18c, 0x40 | (held << 14));
                if (!compare(cpu, 0x80068dd4, nc_menu_record_cross, 20))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_record_103_destination(CPUState *cpu) {
    return nc_menu_check_record_103(cpu) == cpu->r[31];
}

static bool menu_record_modes(void) {
    const u32 flags[] = {0, 0x80001800, 0xa5a5ffff};
    for (unsigned record = 101; record <= 104; ++record) {
        for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; ++i) {
            CPUState cpu = initial();
            MEM_W32(gp + 0x244, config);
            MEM_W32(config, flags[i]);
            MEM_W32(gp + 0x18c, 0xabcd4010);
            MEM_W32(gp + 0x190, record);
            cpu.r[31] = 0x80068e50;
            cpu.budget = 23;
            if (!compare(cpu, 0x80068e18, nc_menu_check_record_102, 22))
                return false;
            cpu.r[3] = record;
            cpu.r[2] = 103;
            cpu.r[31] = record == 103 ? 0x80068fe4 : 0x80068e84;
            cpu.budget = 18;
            if (!compare(cpu, 0x80068e50, menu_record_103_destination, 17))
                return false;
        }
    }
    return true;
}

static bool menu_mode_up_destination(CPUState *cpu) {
    return nc_menu_mode_up(cpu) == cpu->r[31];
}

static bool menu_mode_up(void) {
    const u32 buttons[] = {0, 0x10, 0x40, 0x4050};
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
        MEM_W32(gp + 0x244, config);
        MEM_W32(config, 0x80000000 | (enabled << 15));
        for (unsigned held = 0; held < 2; ++held) {
            MEM_W32(gp + 0x18c, 0x40 | (held << 4));
            for (unsigned i = 0; i < sizeof buttons / sizeof buttons[0]; ++i) {
                CPUState cpu = initial();
                cpu.budget = 35;
                cpu.r[30] = buttons[i];
                MEM_W32(gp + 0x190, i);
                MEM_W32(0x800cbff8, held ? 0xabcd8008 : 0xabcd0010);
                cpu.r[31] = 0x80068ee8;
                if (!enabled)
                    cpu.r[31] = 0x80068fe4;
                else if ((buttons[i] & 0x10) && !held)
                    cpu.r[31] = 0x80068f0c;
                if (!compare(cpu, 0x80068e84, menu_mode_up_destination, 34))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_mode_down_destination(CPUState *cpu) {
    return nc_menu_mode_down(cpu) == cpu->r[31];
}

static bool menu_mode_directions(void) {
    const u32 buttons[] = {0, 0x4000, 0x40, 0x4050};
    for (unsigned i = 0; i < sizeof buttons / sizeof buttons[0]; ++i) {
        for (unsigned held = 0; held < 2; ++held) {
            CPUState cpu = initial();
            cpu.r[30] = buttons[i];
            cpu.r[2] = buttons[i] & 0x40;
            cpu.budget = 14;
            MEM_W32(gp + 0x190, i);
            MEM_W32(gp + 0x18c, 0x10 | (held << 6));
            cpu.r[31] = !cpu.r[2] ? 0x80068f38 : held ? 0x80068f34 : 0x80068f0c;
            if (!compare(cpu, 0x80068ee8, menu_mode_down_destination, 13))
                return false;
        }
    }
    for (unsigned record = 3; record <= 4; ++record) {
        for (unsigned action = 4; action <= 8; action *= 2) {
            CPUState cpu = initial();
            cpu.r[2] = record;
            cpu.r[4] = cpu.r[22];
            cpu.r[6] = action;
            cpu.r[30] = action == 4 ? 0x4010 : 0x4040;
            cpu.r[31] = 0x80068fe4;
            MEM_W32(gp + 0x190, record);
            if (!compare(cpu, 0x80068f0c, nc_menu_dispatch_direction, 7))
                return false;
        }
    }
    return true;
}

static bool menu_face_mask_cross(CPUState *cpu) {
    return nc_menu_mode_face_buttons(cpu, true) == cpu->r[31];
}

static bool menu_face_use_cross(CPUState *cpu) {
    return nc_menu_mode_face_buttons(cpu, false) == cpu->r[31];
}

static bool menu_mode_face_buttons(void) {
    const u32 buttons[] = {0, 0x4000, 0x1000, 0x5000};
    for (unsigned entry = 0; entry < 2; ++entry) {
        int cutoff = entry ? 22 : 23;
        for (unsigned current = 0; current < 4; ++current) {
            for (unsigned previous = 0; previous < 4; ++previous) {
                CPUState cpu = initial();
                cpu.budget = cutoff + 1;
                cpu.r[30] = buttons[current] | (entry ? 0 : 0x40);
                cpu.r[2] = entry ? buttons[current] & 0x4000 : 0x40;
                cpu.r[31] = (buttons[current] & ~buttons[previous]) ? 0x80068f88 : 0x80068fa8;
                MEM_W32(gp + 0x190, current + 5);
                MEM_W32(gp + 0x18c, buttons[previous] | (entry ? 0 : 0x40));
                if (!compare(cpu, entry ? 0x80068f38 : 0x80068f34,
                             entry ? menu_face_use_cross : menu_face_mask_cross, cutoff))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_mode_face_result(void) {
    const u32 buttons[] = {0, 0x1000, 0x4000, 0x5000};
    const u32 records[] = {0, 102, 103, 104, 105, 106, 107, 0xffffffff};
    for (unsigned i = 0; i < sizeof buttons / sizeof buttons[0]; ++i) {
        CPUState cpu = initial();
        cpu.r[30] = buttons[i];
        cpu.r[31] = 0x80068fe4;
        cpu.budget = 14;
        for (unsigned j = 0; j < sizeof records / sizeof records[0]; ++j) {
            MEM_W32(gp + 0x190, records[j]);
            MEM_W32(gp + 0x18c, 0xabcd4010);
            if (!compare(cpu, 0x80068fa8, nc_menu_mode_record_result, 13))
                return false;
        }
    }
    for (unsigned action = 0x10; action <= 0x20; action *= 2) {
        CPUState cpu = initial();
        cpu.r[2] = 1;
        cpu.r[4] = cpu.r[22];
        cpu.r[6] = action;
        cpu.r[30] = action == 0x10 ? 0x5000 : 0x1000;
        cpu.r[31] = 0x80068fa8;
        MEM_W32(gp + 0x190, 1);
        u32 target = 0x800883cc + 6;
        u16 saved = MEM_R16(target);
        MEM_W16(target, 0xff);
        bool ok = compare(cpu, 0x80068f88, nc_menu_dispatch_face_button, 7);
        MEM_W16(target, saved);
        if (!ok)
            return false;
    }
    return true;
}

static bool menu_input_entry(void) {
    const u32 buttons[] = {0, 0x4000, 0x1000, 0x5010};
    const u32 flags[] = {0, 0x10, 0x40, 0x50, 0x8000, 0x40000000};
    for (unsigned i = 0; i < sizeof buttons / sizeof buttons[0]; ++i) {
        for (unsigned j = 0; j < sizeof flags / sizeof flags[0]; ++j) {
            CPUState cpu = initial();
            cpu.r[5] = buttons[i];
            cpu.budget = 31;
            u32 primary = j < 4 ? 1 : flags[j];
            u32 destination = (primary & 0xc0009840) || (flags[j] & 0x40) ? 0x800673bc : 0x80067400;
            MEM_W32(gp + 0x244, config);
            MEM_W32(config, primary);
            MEM_W32(config + 4, flags[j]);
            memset(nc_ram + ((cpu.r[29] - 0x48) & (RAM_SIZE - 1)), 0xa5, 0x48);
            memcpy(before, nc_ram, RAM_SIZE);
            CPUState native = cpu;
            if (nc_enter_menu_input(&native) != 0x80067358 || memcmp(&native, &cpu, sizeof cpu) ||
                memcmp(before, nc_ram, RAM_SIZE))
                return false;
            cpu.budget = 64;
            native = cpu;
            if (nc_enter_menu_input(&native) != destination)
                return false;
            memcpy(after, nc_ram, RAM_SIZE);
            memcpy(nc_ram, before, RAM_SIZE);
            /* Stop immediately after the selected branch with jr zero; nop.
             * Both instructions are outside the range under test. */
            u32 saved_jump = MEM_R32(destination), saved_delay = MEM_R32(destination + 4);
            MEM_W32(destination, 8);
            MEM_W32(destination + 4, 0);
            interp_resume_exe(&cpu, 0x80067358);
            MEM_W32(destination, saved_jump);
            MEM_W32(destination + 4, saved_delay);
            cpu.budget += 2;
            if (memcmp(&cpu, &native, sizeof cpu) || memcmp(nc_ram, after, RAM_SIZE))
                return false;
        }
    }
    return true;
}

static bool menu_mode_record_call(void) {
    const u32 modes[] = {0, 0x10, 0x80000040, 0xffffffff};
    const u32 buttons[] = {0, 0x1000, 0x4000, 0x5010};
    for (unsigned record = 1; record <= 2; ++record) {
        u32 count_address = 0x800883c0 + 12 * record + 4;
        u16 saved_count = MEM_R16(count_address);
        MEM_W16(count_address, 0); /* empty record in the action 2 callee */
        for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
            CPUState cpu = initial();
            cpu.r[22] = 0x80150000;
            cpu.r[30] = buttons[i];
            cpu.r[31] = 0x800674a0;
            MEM_W32(gp + 0x190, record);
            MEM_W32(gp + 0x244, config);
            MEM_W32(config + 4, modes[i]);
            if (!compare(cpu, 0x800673bc, nc_menu_mode_record_call, 12) ||
                MEM_R32(config + 4) != (modes[i] & ~0x10u)) {
                MEM_W16(count_address, saved_count);
                return false;
            }
        }
        MEM_W16(count_address, saved_count);
    }
    return true;
}

static bool menu_mode_record_destination(CPUState *cpu) {
    return nc_menu_select_mode_record(cpu) == cpu->r[31];
}

static bool menu_mode_record_selection(void) {
    const u16 selectors[] = {19, 20, 21, 0xffff};
    const u32 buttons[] = {0, 0x1000, 0x4000, 0x5010};
    for (unsigned i = 0; i < sizeof selectors / sizeof selectors[0]; ++i) {
        MEM_W16(0x800d7c20, selectors[i]);
        for (unsigned index = 0; index < 4; ++index) {
            MEM_W32(gp + 0x190, index);
            u32 slot = 0x800d4d10 + 4 * index;
            u32 saved = MEM_R32(slot);
            MEM_W32(slot, 0x80150000 + index * 8);
            for (unsigned enabled = 0; enabled < 2; ++enabled) {
                CPUState cpu = initial();
                cpu.budget = 22;
                cpu.r[2] = enabled ? 0x10 : 0;
                cpu.r[30] = buttons[index];
                cpu.r[31] = enabled ? 0x8006745c : 0x800674a0;
                if (!compare(cpu, 0x80067400, menu_mode_record_destination, 21))
                    return false;
            }
            MEM_W32(slot, saved);
        }
    }
    return true;
}

static bool menu_record_timeout(void) {
    CPUState cpu = initial();
    cpu.r[4] = 0x80150000;
    cpu.r[5] = 0x80170000;
    cpu.r[6] = 2;
    cpu.r[31] = 0x80067464;
    MEM_W16(cpu.r[5] + 4, 0); /* empty record */
    if (!compare(cpu, 0x8006745c, nc_menu_call_selected_record, 2))
        return false;

    const u32 elapsed[] = {0xffffffff, 0, 11999, 12000, 12001, 12002, 0x7fffffff, 0x80000000};
    const u32 buttons[] = {0, 0x1000, 0x4000, 0x5010};
    cpu.r[31] = 0x800674a0;
    cpu.budget = 24;
    MEM_W32(gp + 0x244, config);
    MEM_W32(0x800cc040, 0xfffffff0);
    for (unsigned i = 0; i < sizeof elapsed / sizeof elapsed[0]; ++i) {
        MEM_W32(0x8008ef10, 0xfffffff0u + elapsed[i]);
        for (unsigned j = 0; j < sizeof buttons / sizeof buttons[0]; ++j) {
            u32 flags = 0x80000040 | ((j & 1) << 4);
            MEM_W32(config + 4, flags);
            cpu.r[30] = buttons[j];
            if (!compare(cpu, 0x80067464, nc_menu_record_timeout, 23) ||
                MEM_R32(config + 4) != ((s32)elapsed[i] > 12000 ? flags & ~0x10u : flags))
                return false;
        }
    }
    return true;
}

static bool menu_input_return(void) {
    const u32 results[] = {0, 1, 2, 4};
    for (unsigned i = 0; i < sizeof results / sizeof results[0]; ++i) {
        CPUState cpu = initial();
        cpu.r[2] = results[i];
        cpu.r[29] -= i * 0x48;
        for (unsigned offset = 0x20; offset <= 0x44; offset += 4)
            MEM_W32(cpu.r[29] + offset, 0x87654321u + i + offset);
        CPUState native = cpu;
        if (!nc_leave_menu_input(&native) || native.r[31] != MEM_R32(cpu.r[29] + 0x44))
            return false;

        MEM_W32(cpu.r[29] + 0x44, 0); /* stop the interpreter on return */
        memcpy(before, nc_ram, RAM_SIZE);
        cpu.budget = 33;
        native = cpu;
        if (nc_leave_menu_input(&native) || memcmp(&cpu, &native, sizeof cpu) ||
            memcmp(before, nc_ram, RAM_SIZE))
            return false;
        cpu.budget++;
        native = cpu;
        if (!nc_leave_menu_input(&native) || native.budget != 1 || native.r[2] != results[i] ||
            memcmp(before, nc_ram, RAM_SIZE))
            return false;
        interp_resume_exe(&cpu, 0x80068fe4);
        if (memcmp(&cpu, &native, sizeof cpu) || memcmp(before, nc_ram, RAM_SIZE))
            return false;
    }
    return true;
}

static bool menu_record_triangle(void) {
    const u32 buttons[] = {0, 0x1000, 0x8000, 0xb0009090};
    const u32 actions[] = {0, 0x405};
    for (unsigned i = 0; i < sizeof buttons / sizeof buttons[0]; ++i) {
        for (unsigned held = 0; held < 2; ++held) {
            for (unsigned j = 0; j < sizeof actions / sizeof actions[0]; ++j) {
                CPUState cpu = initial();
                cpu.budget = 29;
                cpu.r[2] = buttons[i] & 0x1000;
                cpu.r[30] = buttons[i];
                bool fresh = cpu.r[2] && !held;
                cpu.r[31] = fresh || actions[j] ? 0x8006893c : 0x80068944;
                MEM_W32(gp + 0x18c, held ? 0x3010 : 0x2000);
                MEM_W32(gp + 0x190, j ? 0xffffffff : 7);
                MEM_W32(cpu.r[29] + 0x18, actions[j]);
                if (!compare(cpu, 0x800688ec, menu_triangle_destination, 28))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_record_button_pairs(void) {
    const u32 bits[] = {0x10, 0x40, 0x80, 0x20, 0x8000, 0x2000};
    for (unsigned mask = 0; mask < 64; ++mask) {
        CPUState cpu = initial();
        cpu.budget = 34;
        cpu.r[31] = 0x800688ec;
        cpu.r[30] = mask & 1 ? 0xbeef1000 : 0xabcd0000;
        for (unsigned i = 0; i < 6; ++i) {
            if (mask & (1u << i))
                cpu.r[30] |= bits[i];
        }
        cpu.r[2] = cpu.r[30] & 0x10; /* Up mask prepared by the previous block */
        MEM_W32(cpu.r[29] + 0x18, 0x12340301);
        if (!compare(cpu, 0x8006885c, nc_menu_record_button_pairs, 33))
            return false;
    }
    return true;
}

static bool menu_record_shoulders(void) {
    const u32 words[] = {0, 0x400, 0x800, 0xc00, 0xf0000c10};
    const u32 index = 7, record = 0x800883c0 + index * 12, item = 0x80170000;
    u32 saved = MEM_R32(record);
    MEM_W32(record, item);
    MEM_W32(gp + 0x190, index);
    MEM_W32(gp + 0x244, config);
    for (unsigned mode = 0; mode < 3; ++mode) {
        MEM_W32(config, mode ? 0x41 : 1);
        MEM_W32(item + 4, mode == 2 ? 0x4001 : 1);
        for (unsigned i = 0; i < sizeof words / sizeof words[0]; ++i) {
            CPUState cpu = initial();
            cpu.budget = 43;
            cpu.r[30] = words[i];
            cpu.r[31] = mode == 0 ? 0x80068d60 : mode == 1 ? 0x800689a8 : 0x8006885c;
            MEM_W32(cpu.r[29] + 0x18, 0xabcd0001);
            if (!compare(cpu, 0x800687d8, menu_shoulder_destination, 42))
                return false;
        }
    }
    MEM_W32(record, saved);
    return true;
}

static bool menu_navigation_history(void) {
    const u32 record = 0x80160000, item = 0x80170000;
    const u32 buttons[] = {0, 0x80, 0x20, 0xbeef00a0};
    CPUState cpu = initial();
    cpu.r[31] = 0x800687cc;
    cpu.r[4] = record;
    MEM_W32(record, item);
    MEM_W16(record + 4, 1);
    MEM_W16(record + 10, 0);
    MEM_W32(item + 4, 0);
    for (unsigned i = 0; i < sizeof buttons / sizeof buttons[0]; ++i) {
        cpu.r[30] = buttons[i];
        cpu.r[5] = buttons[i] & 0x80 ? 0x80 : 0x40;
        MEM_W32(gp + 0x18c, 0x12345678);
        MEM_W32(gp + 0x194, 5);
        if (!buttons[i]) {
            if (!compare(cpu, 0x800687c8, nc_menu_clear_repeat, 1))
                return false;
        } else if (!compare(cpu, 0x800687b4, nc_menu_apply_direction, 2)) {
            return false;
        }
    }
    return true;
}

static bool menu_left_selection(CPUState *cpu) {
    return nc_menu_direction_selection(cpu, false) == cpu->r[31];
}

static bool menu_right_selection(CPUState *cpu) {
    return nc_menu_direction_selection(cpu, true) == cpu->r[31];
}

static bool menu_direction_selections(void) {
    CPUState cpu = initial();
    const u32 indices[] = {0, 11, 12, 13, 0x40000001, 0xffffffff};
    const u16 values[] = {0, 1, 2, 0x8000};
    for (unsigned right = 0; right < 2; ++right) {
        u32 entry = right ? 0x8006870c : 0x8006865c;
        int cutoff = right ? 22 : 21;
        cpu.budget = cutoff;
        if (nc_menu_direction_selection(&cpu, right) != entry)
            return false;
        cpu.budget = cutoff + 1;
        cpu.r[30] = right ? 0x00a00020 : 0x00a000a0;
        for (unsigned i = 0; i < sizeof indices / sizeof indices[0]; ++i) {
            MEM_W32(gp + 0x190, indices[i]);
            for (unsigned j = 0; j < sizeof values / sizeof values[0]; ++j) {
                MEM_W32(0x800d7c20, ((u32)values[j] << 16) | 0x1234);
                MEM_W16(0x800d7c6e, j & 1 ? 0x8001 : 0x4321);
                MEM_W32(gp + 0x18c, 0x87654321);
                if (indices[i] != 12)
                    cpu.r[31] = 0x800687b4;
                else if (values[j] == right)
                    cpu.r[31] = 0x800687cc;
                else
                    cpu.r[31] = right ? 0x8006874c : 0x80068698;
                if (!compare(cpu, entry, right ? menu_right_selection : menu_left_selection,
                             cutoff))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_horizontal_destination(CPUState *cpu) {
    return nc_menu_horizontal_repeat(cpu) == cpu->r[31];
}

static bool menu_horizontal_inputs(void) {
    CPUState cpu = initial();
    cpu.budget = 20;
    if (nc_menu_horizontal_repeat(&cpu) != 0x80068624)
        return false;
    cpu.budget = 21;
    const u32 directions[] = {0, 0x80, 0x20, 0xa0};
    const u32 counts[] = {0, 4, 5, 0x7fffffff, 0xffffffff};
    for (unsigned now = 0; now < 4; ++now) {
        cpu.r[30] = 0x00a00000 | directions[now];
        cpu.r[2] = directions[now] & 0x80;
        for (unsigned prev = 0; prev < 4; ++prev) {
            MEM_W32(gp + 0x18c, 0x00a00000 | directions[prev]);
            for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; ++i) {
                MEM_W32(gp + 0x194, counts[i]);
                u32 direction = cpu.r[2] ? 0x80 : directions[now];
                if (!direction)
                    cpu.r[31] = 0x800687c8;
                else if ((directions[prev] & direction) && (s32)(counts[i] + 1u) < 6)
                    cpu.r[31] = 0x800687cc;
                else
                    cpu.r[31] = direction == 0x80 ? 0x8006865c : 0x8006870c;
                if (!compare(cpu, 0x80068624, menu_horizontal_destination, 20))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_vertical_destination(CPUState *cpu) {
    return nc_menu_vertical_repeat(cpu) == cpu->r[31];
}

static bool menu_vertical_inputs(void) {
    CPUState cpu = initial();
    cpu.budget = 23;
    if (nc_menu_vertical_repeat(&cpu) != 0x80068578)
        return false;
    cpu.budget = 24;
    MEM_W32(gp + 0x190, 0x12345678);
    const u32 buttons[] = {0, 0x10, 0x40, 0x50, 0x00500080};
    const u32 counts[] = {0, 4, 5, 0x7fffffff, 0xffffffff};
    for (unsigned i = 0; i < sizeof buttons / sizeof buttons[0]; ++i) {
        cpu.r[30] = buttons[i];
        cpu.r[2] = buttons[i] & 0x10;
        for (unsigned j = 0; j < sizeof counts / sizeof counts[0]; ++j) {
            for (unsigned held = 0; held < 2; ++held) {
                MEM_W32(gp + 0x194, counts[j]);
                MEM_W32(gp + 0x18c, 0x00500040 | (held << 4));
                if (cpu.r[2]) {
                    bool suppress = held && (s32)(counts[j] + 1u) < 6;
                    cpu.r[31] = suppress ? 0x800687cc : 0x800685fc;
                } else {
                    cpu.r[31] = buttons[i] & 0x40 ? 0x800685d8 : 0x80068624;
                }
                if (!compare(cpu, 0x80068578, menu_vertical_destination, 23))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_face_destination(CPUState *cpu) {
    return nc_menu_face_press(cpu) == cpu->r[31];
}

static bool menu_face_inputs(void) {
    CPUState cpu = initial();
    cpu.budget = 25;
    if (nc_menu_face_press(&cpu) != 0x8006851c)
        return false;
    cpu.budget = 26;
    MEM_W32(gp + 0x190, 0x12345678);
    const u32 faces[] = {0, 0x4000, 0x1000, 0x5000};
    for (unsigned now = 0; now < 4; ++now) {
        for (unsigned prev = 0; prev < 4; ++prev) {
            MEM_W32(gp + 0x18c, faces[prev] | 0x50000008);
            for (unsigned up = 0; up < 2; ++up) {
                cpu.r[30] = faces[now] | 0x50000000 | (up << 4);
                cpu.r[31] = faces[now] & ~faces[prev] ? 0x800685fc : 0x80068578;
                if (!compare(cpu, 0x8006851c, menu_face_destination, 25))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_start_down_destination(CPUState *cpu) {
    return nc_menu_start_down(cpu) == cpu->r[31];
}

static bool menu_down_repeat_destination(CPUState *cpu) {
    return nc_menu_down_repeat(cpu) == cpu->r[31];
}

static bool menu_start_down_inputs(void) {
    CPUState cpu = initial();
    cpu.budget = 19;
    if (nc_menu_start_down(&cpu) != 0x800684d0)
        return false;
    cpu.budget = 13;
    if (nc_menu_down_repeat(&cpu) != 0x800685d8)
        return false;
    MEM_W32(gp + 0x190, 0x10001);
    const u32 words[] = {0, 8, 0x40, 0x48, 0x00480000};
    const u32 counts[] = {0, 9, 10, 0x7fffffff, 0xffffffff};
    for (unsigned now = 0; now < sizeof words / sizeof words[0]; ++now) {
        for (unsigned prev = 0; prev < 4; ++prev) {
            u32 previous = (prev & 1 ? 8u : 0u) | (prev & 2 ? 0x40u : 0u);
            MEM_W32(gp + 0x18c, previous);
            for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; ++i) {
                cpu.r[30] = words[now];
                cpu.r[2] = words[now] & 8;
                bool fresh_start = cpu.r[2] && !(previous & 8);
                cpu.r[31] = fresh_start ? 0x800685fc : words[now] & 0x40 ? 0x800685d8 : 0x8006851c;
                MEM_W32(gp + 0x194, counts[i]);
                cpu.budget = 20;
                if (!compare(cpu, 0x800684d0, menu_start_down_destination, 19))
                    return false;
                if (cpu.r[31] == 0x800685d8) {
                    cpu.r[2] = (s32)(counts[i] + 1u) < 11;
                    cpu.r[31] = cpu.r[2] && (previous & 0x40) ? 0x800687cc : 0x800685fc;
                    cpu.budget = 14;
                    if (!compare(cpu, 0x800685d8, menu_down_repeat_destination, 13))
                        return false;
                }
            }
        }
    }
    return true;
}

static bool menu_up_destination(CPUState *cpu) {
    return nc_menu_up_repeat(cpu) == cpu->r[31];
}

static bool menu_up_inputs(void) {
    CPUState cpu = initial();
    cpu.budget = 23;
    if (nc_menu_up_repeat(&cpu) != 0x8006848c)
        return false;
    cpu.budget = 24;
    MEM_W32(gp + 0x190, 0x12345678);
    const u32 buttons[] = {0, 8, 0x10, 0x58, 0x00100008};
    const u32 counts[] = {0, 9, 10, 11, 0x7fffffff, 0xffffffff};
    for (unsigned i = 0; i < sizeof buttons / sizeof buttons[0]; ++i) {
        cpu.r[30] = buttons[i];
        cpu.r[2] = buttons[i] & 0x10;
        for (unsigned j = 0; j < sizeof counts / sizeof counts[0]; ++j) {
            for (unsigned held = 0; held < 2; ++held) {
                MEM_W32(gp + 0x18c, 0x00100008 | (held << 4));
                MEM_W32(gp + 0x194, counts[j]);
                bool suppress = held && (s32)(counts[j] + 1u) < 11;
                cpu.r[31] = !cpu.r[2] ? 0x800684d0 : suppress ? 0x800687cc : 0x800685fc;
                if (!compare(cpu, 0x8006848c, menu_up_destination, 23))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_right_destination(CPUState *cpu) {
    return nc_menu_right_input(cpu) == cpu->r[31];
}

static bool menu_right_inputs(void) {
    CPUState cpu = initial();
    cpu.budget = 24;
    if (nc_menu_right_input(&cpu) != 0x80068438)
        return false;
    cpu.budget = 25;
    MEM_W32(gp + 0x244, config);
    const u32 buttons[] = {0, 0x20, 0x80, 0xb0, 0x00200010};
    const u32 indices[] = {0, 0x1a, 0xffffffff};
    for (unsigned i = 0; i < sizeof indices / sizeof indices[0]; ++i) {
        MEM_W32(gp + 0x190, indices[i]);
        for (unsigned j = 0; j < sizeof buttons / sizeof buttons[0]; ++j) {
            cpu.r[30] = buttons[j];
            for (unsigned set = 0; set <= 1; ++set) {
                MEM_W32(config + 4, 0x81000004 | (set << 27));
                cpu.r[31] = !(buttons[j] & 0x20) ? 0x80068488 : set ? 0x8006848c : 0x80068480;
                if (!compare(cpu, 0x80068438, menu_right_destination, 24))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_left_destination(CPUState *cpu) {
    return nc_menu_left_input(cpu) == cpu->r[31];
}

static bool menu_left_inputs(void) {
    CPUState cpu = initial();
    cpu.budget = 29;
    if (nc_menu_left_input(&cpu) != 0x800683d0)
        return false;
    cpu.budget = 30;
    MEM_W32(gp + 0x244, config);
    const u16 fields[] = {6, 0, 0x8006};
    const u32 buttons[] = {0, 0x80, 0x20, 0xb0, 0x00800010};
    for (unsigned i = 0; i < sizeof fields / sizeof fields[0]; ++i) {
        u32 index = 0x1a + i;
        MEM_W32(gp + 0x190, index);
        MEM_W16(0x800883ca + 12 * index, fields[i]);
        for (unsigned j = 0; j < sizeof buttons / sizeof buttons[0]; ++j) {
            cpu.r[30] = buttons[j];
            for (unsigned set = 0; set <= 1; ++set) {
                MEM_W32(config + 4, 0x81000004 | (set << 27));
                cpu.r[31] = fields[i] != 6               ? 0x8006848c
                            : set && (buttons[j] & 0x80) ? 0x80068424
                                                         : 0x80068438;
                if (!compare(cpu, 0x800683d0, menu_left_destination, 29))
                    return false;
            }
        }
    }
    return true;
}

static bool menu_pad_type_flags(void) {
    CPUState cpu = initial();
    cpu.r[31] = 0x800683d0;
    cpu.budget = 29;
    const u8 types[] = {0x41, 0x53, 0x70, 0x73, 0x7f, 0xff};
    for (unsigned i = 0; i < sizeof types; ++i) {
        MEM_W8(packet + 1, types[i]);
        for (unsigned bits = 0; bits < 16; ++bits) {
            u32 flags = 0xa5a00018 | ((bits & 3) << 1) | ((bits & 12) << 13);
            MEM_W32(0x80087a40, flags);
            if (!compare(cpu, 0x80068344, nc_menu_pad_type_flags, 28))
                return false;
        }
    }
    return true;
}

static bool menu_confirm_destination(CPUState *cpu) {
    return nc_menu_confirm_edge(cpu) == cpu->r[31];
}

static bool menu_confirm_edges(void) {
    const u32 buttons[] = {0, 8, 0x4000, 0x4008};
    CPUState cpu = initial();
    cpu.budget = 18;
    if (nc_menu_confirm_edge(&cpu) != 0x80068154)
        return false;
    cpu.budget = 19;
    for (unsigned now = 0; now < 4; ++now) {
        for (unsigned prev = 0; prev < 4; ++prev) {
            for (unsigned triangle = 0; triangle < 2; ++triangle) {
                cpu.r[30] = 0x40080000 | buttons[now] | (triangle << 12);
                cpu.r[2] = buttons[now] & 8;
                MEM_W32(gp + 0x18c, 0xabcd1000 | buttons[prev]);
                cpu.r[31] = buttons[now] & ~buttons[prev] ? 0x8006818c : 0x80068304;
                if (!compare(cpu, 0x80068154, menu_confirm_destination, 18))
                    return false;
            }
        }
    }
    return true;
}

static bool pad_inactivity(void) {
    const u32 buttons[] = {0, 1, 0x00400000, 0x80804010};
    CPUState cpu = initial();
    cpu.r[31] = 0x8006d5a8;
    cpu.budget = 30;
    MEM_W32(gp + 0x244, config);
    for (unsigned i = 0; i < 4; ++i) {
        cpu.r[2] = buttons[i];
        for (unsigned count = 0; count <= 2; ++count) {
            cpu.r[19] = count;
            for (unsigned state = 10; state <= 11; ++state) {
                MEM_W32(0x8008ef30, state);
                for (unsigned set = 0; set < 2; ++set) {
                    MEM_W32(config + 4, 0x81345678 | (set << 25));
                    MEM_W32(0x800d7c30, 0xabcd4321);
                    if (!compare(cpu, 0x8006d548, nc_pad_inactivity, 29))
                        return false;
                }
            }
        }
    }
    return true;
}

static bool l2_held(CPUState *c) {
    return nc_player_l2_direction(c, true);
}

static bool l2_released(CPUState *c) {
    return nc_player_l2_direction(c, false);
}

static bool l2_direction_edges(void) {
    const u32 directions[] = {0, 0x20, 0x80, 0xa0};
    CPUState cpu = initial();
    cpu.r[31] = 0x800448e0;
    for (unsigned now = 0; now < 4; ++now) {
        for (unsigned prev = 0; prev < 4; ++prev) {
            for (unsigned bits = 0; bits < 8; bits += 2) {
                cpu.r[2] = 0x50;
                cpu.r[3] = 0xf0000000 | directions[prev];
                cpu.r[17] = 0x01004100 | directions[now];
                MEM_W16(0x800cc002, 0xbeef);
                MEM_W16(0x800cbff8, 0x8100 | bits);
                MEM_W16(0x800cbffa, bits & 4 ? 0xffff : 7);
                if (!compare(cpu, 0x800447b8, l2_held, 37) || MEM_R16(0x800cc002) != 0x50)
                    return false;
            }
        }
    }
    return compare(cpu, 0x800448d4, l2_released, 3) && MEM_R16(0x800cc002) == 0x3c;
}

static bool ports(void) {
    const u8 types[] = {0xff, 0x41, 0x73, 0x53};
    for (unsigned a = 0; a < 4; ++a)
        for (unsigned b = 0; b < 4; ++b) {
            MEM_W8(packet, a ? 0 : 0xff);
            MEM_W8(packet + 1, types[a]);
            MEM_W8(packet + 34, b ? 0x80 : 0xff);
            MEM_W8(packet + 35, types[b]);
            MEM_W32(gp + 0x238, 0xabcdffff);
            if (!compare(initial(), 0x8006ff34, nc_select_pad, 70))
                return false;
            const unsigned mask = (a == 1 || a == 2) + 2 * (b == 1 || b == 2);
            if (MEM_R16(gp + 0x238) != mask || MEM_R16(gp + 0x23a) != (mask & 1    ? 0
                                                                       : mask == 2 ? 1
                                                                                   : 0xabcd))
                return false;
        }
    return true;
}

static bool decode(u32 type, u32 pressed, bool both, u32 flags, u8 x, u8 y, u8 right) {
    CPUState cpu = initial();
    cpu.r[2] = cpu.r[3] = 0;
    cpu.r[31] = 0x800705d8;
    MEM_W32(gp + 0x238, both ? 3 : 1);
    MEM_W32(gp + 0x244, config);
    MEM_W32(config, flags);
    MEM_W32(config + 0x1a, 0xc0c04040);
    MEM_W32(config + 0x1e, 0xe0e02020);
    MEM_W32(packet, ((u32)(u16)~pressed << 16) | type << 8);
    MEM_W8(packet + 4, right);
    MEM_W8(packet + 6, x);
    MEM_W8(packet + 7, y);
    MEM_W32(packet + 34, 0x76544100);
    MEM_W8(gp + 0x410, 0x99);
    MEM_W8(gp + 0x414, 0x77);
    return compare(cpu, 0x800700fc, nc_decode_pad, 550);
}

/* Exercise the normal function entry with enough budget for both native
 * paths, then with enough to return but too little for the decoder shortcut. */
static bool caller(u32 budget, u8 first, u8 second, u32 argument, u32 flags, u32 state) {
    CPUState cpu = initial();
    cpu.r[4] = argument;
    cpu.r[5] = 1;
    cpu.budget = budget;
    MEM_W32(gp + 0x244, config);
    MEM_W32(config, flags);
    MEM_W32(config + 4, state);
    MEM_W32(gp + 0x2d0, 0);
    MEM_W32(0x8008ef30, 0x11223344);
    MEM_W32(packet, 0xf6ee4100u | first);
    MEM_W32(packet + 34, 0x12344100u | second);
    memcpy(before, nc_ram, RAM_SIZE);
    CPUState guest = cpu;
    nc_set_module(NC_MOD_NONE);
    interp_call(&guest, 0x80070050);
    memcpy(after, nc_ram, RAM_SIZE);
    memcpy(nc_ram, before, RAM_SIZE);
#ifdef NC_TEST_GENERATED_PAD
    nc_set_module(NC_MOD_PSX2);
    NcFunc read_pad = nc_lookup(0x80070050);
    if (!read_pad)
        return false;
    read_pad(&cpu);
    nc_set_module(NC_MOD_NONE);
#else
    CPUState input = cpu;
    u32 resume = nc_read_pad(&cpu);
    if (resume) {
        if (resume != 0x80070050 || memcmp(&cpu, &input, sizeof cpu) ||
            memcmp(before, nc_ram, RAM_SIZE))
            return false;
        interp_call(&cpu, resume);
    }
#endif
    bool notify = first == 0xff && argument && !(flags & 0xc0009840) && !(state & 0x03000040);
    if (MEM_R32(0x8008ef30) != (notify ? 0x10u : 0x11223344u) ||
        MEM_R32(config + 4) != (notify ? state | 0x40 : state))
        return false;
    u32 expected = first == 0xff ? 0 : 0x0911u | (second == 0xff ? 0 : 0xedcb0000u);
    return cpu.r[2] == expected && !memcmp(&cpu, &guest, sizeof cpu) &&
           !memcmp(nc_ram, after, RAM_SIZE);
}

#ifdef NC_TEST_GENERATED_PAD
/* Replay the same RAM fixture through the generated entry on either side of
 * its budget guard. compare() saved the original and expected RAM images. */
static bool compare_generated(CPUState input, u32 entry, int cutoff) {
    const int budgets[] = {cutoff, cutoff + 1};
    for (unsigned i = 0; i < 2; ++i) {
        CPUState cpu = input;
        cpu.budget = budgets[i];
        CPUState guest = cpu;
        memcpy(nc_ram, before, RAM_SIZE);
        /* A low-budget run advances the global poll clock. Restore its
         * starting value along with RAM before executing the same input. */
        StateIO snapshot = {0};
        cpu_serialize(&snapshot);
        if (snapshot.error) {
            free(snapshot.buf);
            return false;
        }
        interp_call(&guest, entry);
        snapshot.loading = true;
        cpu_serialize(&snapshot);
        free(snapshot.buf);
        if (snapshot.error)
            return false;
        nc_set_module(NC_MOD_PSX2);
        NcFunc routine = nc_lookup(entry);
        if (!routine)
            return false;
        routine(&cpu);
        nc_set_module(NC_MOD_NONE);
        if (memcmp(&cpu, &guest, sizeof cpu) || memcmp(nc_ram, after, RAM_SIZE)) {
            fprintf(stderr, "entry %08x budget %d -> native %d/disc %d ram %d\n", entry, budgets[i],
                    cpu.budget, guest.budget, memcmp(nc_ram, after, RAM_SIZE));
            return false;
        }
    }
    return true;
}
#endif

static bool menu_scan_return(void) {
    CPUState cpu = initial();
    for (unsigned i = 0; i < 8; ++i)
        MEM_W32(cpu.r[29] + 0x10 + 4 * i, 0x81234560 + i);
    MEM_W32(cpu.r[29] + 0x30, 0); /* stop the interpreter after the return */
    memcpy(before, nc_ram, RAM_SIZE);
    cpu.budget = 30;
    CPUState native = cpu;
    if (nc_leave_menu_button_scan(&native) || memcmp(&native, &cpu, sizeof cpu) ||
        memcmp(before, nc_ram, RAM_SIZE))
        return false;
    cpu.budget = 31;
    native = cpu;
    if (!nc_leave_menu_button_scan(&native))
        return false;
    interp_resume_exe(&cpu, 0x80067308);
    if (memcmp(&native, &cpu, sizeof cpu) || memcmp(before, nc_ram, RAM_SIZE))
        return false;

#ifdef NC_TEST_GENERATED_PAD
    /* Idle takes 13 cycles to reach the return, a held shoulder word takes 20. */
    const u32 held[] = {0, 0x100, 0x301};
    for (unsigned i = 0; i < sizeof held / sizeof held[0]; ++i) {
        CPUState input = initial();
        input.r[4] = held[i];
        MEM_W32(gp + 0x18c, held[i]);
        memset(nc_ram + ((input.r[29] - 0x38) & (RAM_SIZE - 1)), 0xa5, 0x38);
        memcpy(before, nc_ram, RAM_SIZE);
        CPUState guest = input;
        interp_call(&guest, 0x80067130);
        memcpy(after, nc_ram, RAM_SIZE);
        if (!compare_generated(input, 0x80067130, held[i] ? 50 : 43))
            return false;
    }
#endif
    return true;
}

static bool full_pad_dispatch(void) {
    const u32 words[] = {0, 1, 8, 9, 0x00080001};
    MEM_W32(gp + 0x244, config);
    MEM_W32(gp + 0x2d0, 0);
    MEM_W32(config, 0);
    MEM_W32(config + 4, 0);
    MEM_W16(0x800cc00c, 0);
    for (unsigned i = 0; i < sizeof words / sizeof words[0]; ++i) {
        MEM_W32(gp + 0x144, 0x12340000);
        MEM_W32(packet, (~words[i] << 16) | 0x4100);
        MEM_W32(packet + 34, (~words[i] & 0xffff0000) | 0x4100);
        CPUState guest = initial();
        memcpy(before, nc_ram, RAM_SIZE);
        interp_call(&guest, 0x80042d9c);
        if (guest.r[2] != 0xffffffff ||
            MEM_R32(gp + 0x144) != ((words[i] & 1) ? 0x12340000 : words[i]))
            return false;
        memcpy(after, nc_ram, RAM_SIZE);
#ifdef NC_TEST_GENERATED_PAD
        if (!compare_generated(initial(), 0x80042d9c, 24) ||
            !compare_generated(initial(), 0x80042d9c, 4095))
            return false;
#endif
    }
    return true;
}

static bool pad_bios_services(void) {
    const struct {
        u32 entry;
        NcPadBiosService service;
    } cases[] = {{0x80073f44, NC_BIOS_INIT_PAD},
                 {0x80073f54, NC_BIOS_START_PAD},
                 {0x80073e3c, NC_BIOS_CHANGE_CLEAR_PAD},
                 {0x80073d7c, NC_BIOS_STOP_PAD}};
    bios_init();
    CPUState input = initial();
    input.r[4] = packet;
    input.r[5] = 34;
    input.r[6] = packet + 34;
    input.r[7] = 34;
    memcpy(before, nc_ram, RAM_SIZE);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        CPUState low = input;
        low.budget = 3;
        CPUState native = low;
        if (nc_pad_bios_call(&native, cases[i].service) || memcmp(&native, &low, sizeof low))
            return false;
        CPUState guest = input;
        interp_call(&guest, cases[i].entry);
        memcpy(after, nc_ram, RAM_SIZE);
        native = input;
        memcpy(nc_ram, before, RAM_SIZE);
        if (!nc_pad_bios_call(&native, cases[i].service) || native.budget != input.budget - 3 ||
            memcmp(&native, &guest, sizeof guest) || memcmp(nc_ram, after, RAM_SIZE))
            return false;
#ifdef NC_TEST_GENERATED_PAD
        if (!compare_generated(input, cases[i].entry, 3))
            return false;
#endif
    }
    return true;
}

static bool sdk_pad_patch(void) {
    CPUState cpu = initial();
    bios_init();
    cpu.r[9] = 0x57;
    nc_call(&cpu, 0xb0);
    u32 table_entry = cpu.r[2] + 0x16c;
    const u32 targets[] = {MEM_R32(table_entry), 0x80150000};
    for (unsigned i = 0; i < 2; ++i) {
        cpu = initial();
        cpu.cop0[12] = i ? 0x405 : 4;
        MEM_W32(table_entry, targets[i]);
        u32 start = targets[i] + 0x594;
        MEM_W32(start - 4, 0xaabbccdd);
        for (unsigned word = 0; word < 12; ++word)
            MEM_W32(start + 4 * word, 0x12345670 + word);
        if (!compare(cpu, 0x80073f74, nc_sdk_patch_pad, 71) || MEM_R32(0x8008efe8) != cpu.r[31] ||
            MEM_R32(start - 4) != 0xaabbccdd || MEM_R32(start + 44) != 0x1234567b)
            return false;
        for (unsigned word = 0; word < 11; ++word)
            if (MEM_R32(start + 4 * word))
                return false;
#ifdef NC_TEST_GENERATED_PAD
        if (!compare_generated(cpu, 0x80073f74, 71))
            return false;
#endif
    }
    return true;
}

static bool sdk_pad(u32 first_size, u32 second_size) {
    CPUState cpu = initial();
    const u32 first = 0x80140100, second = 0x80141004;
    bios_init();
    cpu.r[4] = first;
    cpu.r[5] = first_size;
    cpu.r[6] = second;
    cpu.r[7] = second_size;
    cpu.cop0[12] = 0x405;
    if (!compare(cpu, 0x80073eb4, nc_sdk_init_pad, 11))
        return false;
#ifdef NC_TEST_GENERATED_PAD
    if (!compare_generated(cpu, 0x80073eb4, 11))
        return false;
#endif
    if (!nc_sdk_init_pad(&cpu) || (cpu.cop0[12] & 0x401))
        return false;
    if (!compare(cpu, 0x80073f1c, nc_sdk_start_pad, 4))
        return false;
#ifdef NC_TEST_GENERATED_PAD
    if (!compare_generated(cpu, 0x80073f1c, 4))
        return false;
#endif
    if (!nc_sdk_start_pad(&cpu) || cpu.cop0[12] != 0x405)
        return false;
    /* A VBlank exposes which addresses and lengths the SDK passed to BIOS. */
    MEM_W32(first, 0x12345678);
    MEM_W32(second, 0x87654321);
    bios_set_pad(0x6010);
    irq_raise(IRQ_VBLANK);
    bios_exception(&cpu);
    irq_write(0, 0);
    return MEM_R32(first) == (first_size >= 4 ? 0x9fef4100u : 0x12345678u) &&
           MEM_R32(second) == (second_size >= 4 ? 0x876543ffu : 0x87654321u);
}

static bool pad_driver(void) {
    CPUState cpu = initial();
    bios_init();
    /* Give the disc VSync stable status/timer words. At INT32_MAX its
     * signed next-frame comparison completes without an external clock. */
    MEM_W32(0x80088aa4, 0x80150000);
    MEM_W32(0x80088aa8, 0x80150004);
    MEM_W32(0x80089b78, 0x7fffffff);
    MEM_W32(0x80088ab0, 0);
    MEM_W32(packet, 0x12347300);
    MEM_W32(packet + 34, 0x56784100);
    MEM_W32(gp + 0x238, 0xaaaabbbb);
    if (!compare(cpu, 0x8006febc, nc_start_pad_driver, 8) || MEM_R32(gp + 0x238) != 3)
        return false;
#ifdef NC_TEST_GENERATED_PAD
    if (!compare_generated(cpu, 0x8006febc, 8))
        return false;
#endif
    /* Check the actual BIOS registrations with an interrupt after startup. */
    if (!nc_start_pad_driver(&cpu))
        return false;
    bios_set_pad(0x4010);
    irq_raise(IRQ_VBLANK);
    bios_exception(&cpu);
    irq_write(0, 0);
    if (MEM_R32(packet) != 0xbfef4100 || MEM_R8(packet + 34) != 0xff)
        return false;

    cpu = initial();
    MEM_W32(gp + 0x238, 0x00010002);
    if (!compare(cpu, 0x8006ff0c, nc_stop_pad_driver, 4) || MEM_R32(gp + 0x238))
        return false;
#ifdef NC_TEST_GENERATED_PAD
    if (!compare_generated(cpu, 0x8006ff0c, 4))
        return false;
#endif
    if (!nc_start_pad_driver(&cpu) || !nc_stop_pad_driver(&cpu))
        return false;
    bios_set_pad(0);
    MEM_W8(packet + 34, 0);
    irq_raise(IRQ_VBLANK);
    bios_exception(&cpu);
    irq_write(0, 0);
    return MEM_R32(packet) == 0xbfef4100 && MEM_R8(packet + 34) == 0;
}

static bool packet_readers(void) {
    CPUState cpu = initial();
    /* A disconnected status and the second port's ID do not gate the raw read. */
    MEM_W8(packet, 0xff);
    MEM_W8(packet + 35, 0x73);
    const u8 ids[] = {0, 0x41, 0x4f, 0x73, 0x79, 0xff};
    for (unsigned i = 0; i < sizeof ids; ++i) {
        MEM_W8(packet + 1, ids[i]);
        CPUState result = cpu;
        if (!nc_pad_packet_type(&result) || result.r[2] != (u32)(ids[i] >> 4) ||
            !compare(cpu, 0x8007001c, nc_pad_packet_type, 6))
            return false;
#ifdef NC_TEST_GENERATED_PAD
        if (!compare_generated(cpu, 0x8007001c, 6))
            return false;
#endif
    }

    const u32 stack_byte = cpu.r[29] + 0x10;
    const u32 destinations[][2] = {
        {stack_byte, stack_byte + 1},
        {stack_byte, stack_byte},
        {packet + 7, stack_byte},
    };
    const u8 axes[][2] = {{0x80, 0x80}, {0, 0xff}, {0xff, 0}};
    for (unsigned i = 0; i < sizeof destinations / sizeof destinations[0]; ++i)
        for (unsigned j = 0; j < sizeof axes / sizeof axes[0]; ++j) {
            cpu.r[4] = destinations[i][0];
            cpu.r[5] = destinations[i][1];
            MEM_W32(stack_byte, 0x11223344);
            MEM_W8(packet + 6, axes[j][0]);
            MEM_W8(packet + 7, axes[j][1]);
            if (!compare(cpu, 0x8007002c, nc_read_pad_stick, 13))
                return false;
            u8 y = cpu.r[4] == packet + 7 ? axes[j][0] : axes[j][1];
            if (MEM_R8(cpu.r[4]) != (cpu.r[4] == cpu.r[5] ? y : axes[j][0]) ||
                MEM_R8(cpu.r[5]) != y)
                return false;
#ifdef NC_TEST_GENERATED_PAD
            if (!compare_generated(cpu, 0x8007002c, 13))
                return false;
#endif
        }
    return true;
}

/* For a blocked read the entry and early return are the entire disc path. */
static bool blocked_player_input(u32 flags) {
    CPUState input = initial();
    input.r[4] = 0x80160000;
    MEM_W32(gp + 0x244, config);
    MEM_W32(config, flags);
    MEM_W32(input.r[4] + 0x2f8, 0x12345678);
    memcpy(before, nc_ram, RAM_SIZE);
    CPUState low = input;
    low.budget = 16;
    CPUState native = low;
    if (nc_enter_player_input(&native) || memcmp(&native, &low, sizeof low) ||
        memcmp(before, nc_ram, RAM_SIZE))
        return false;
    native = input;
    if (!nc_enter_player_input(&native) || native.r[2] != (flags & 0x80009800) ||
        native.r[3] != flags || native.r[16] != input.r[4] ||
        !nc_leave_player_input(&native, false) || native.budget != input.budget - 31 ||
        MEM_R32(input.r[4] + 0x2f8) != 0x12345678)
        return false;
    memcpy(after, nc_ram, RAM_SIZE);
    memcpy(nc_ram, before, RAM_SIZE);
    CPUState guest = input;
    interp_call(&guest, 0x80044724);
    if (memcmp(&native, &guest, sizeof guest) || memcmp(nc_ram, after, RAM_SIZE))
        return false;
#ifdef NC_TEST_GENERATED_PAD
    /* 16 cycles to enter, then 15 to restore the frame. */
    return compare_generated(input, 0x80044724, 16) && compare_generated(input, 0x80044724, 31);
#else
    return true;
#endif
}

static bool player_input_return(bool store_buttons) {
    CPUState input = initial();
    const u32 sp = input.r[29];
    MEM_W32(sp + 0x1c, 0); /* stop interp_resume_exe at the restored return address */
    MEM_W32(sp + 0x18, 0x12345678);
    MEM_W32(sp + 0x14, 0xcafebabe);
    MEM_W32(sp + 0x10, 0x87654321);
    input.r[16] = sp + 0x14 - 0x2f8; /* destination aliases the saved s1 */
    memcpy(before, nc_ram, RAM_SIZE);
    CPUState low = input;
    low.budget = store_buttons ? 16 : 15;
    CPUState native = low;
    if (nc_leave_player_input(&native, store_buttons) || memcmp(&native, &low, sizeof low) ||
        memcmp(before, nc_ram, RAM_SIZE))
        return false;
    native = input;
    if (!nc_leave_player_input(&native, store_buttons) ||
        native.r[17] != (store_buttons ? input.r[17] : 0xcafebabe))
        return false;
    memcpy(after, nc_ram, RAM_SIZE);
    memcpy(nc_ram, before, RAM_SIZE);
    interp_resume_exe(&input, store_buttons ? 0x800451e8 : 0x800451ec);
    return !memcmp(&input, &native, sizeof input) && !memcmp(nc_ram, after, RAM_SIZE);
}

static bool sequence_init(u32 flags, bool alternate, bool interrupts_enabled) {
    CPUState cpu = initial();
    cpu.cop0[12] = interrupts_enabled ? 0x405 : 4;
    MEM_W32(gp + 0x244, config);
    MEM_W32(config, flags);
    MEM_W32(config + 4, alternate ? 0x40000 : 0);
    MEM_W32(gp + 0x184, 0xaabbccdd);
    MEM_W32(gp + 0x188, 0x73);
    MEM_W8(gp + 0x3f8, 20);
    MEM_W32(gp + 0x344, 0xdead);
    MEM_W32(0x8008f04c, 0x7b);
    MEM_W32(0x800d4190, 0x80140000);
    MEM_W32(0x8008ef18, 0x1248bdef);
    MEM_W32(0x8008ef1c, 0xfeedbeef);
    bool enabled = (flags & 0x30) != 0;
    if (!compare(cpu, 0x80062c28, nc_init_pad_sequence, 54) ||
        MEM_R32(gp + 0x184) != (enabled ? 0x80140000u : 0) ||
        MEM_R32(gp + 0x188) != (enabled ? 0 : 0x73) || MEM_R8(gp + 0x3f8) != (enabled ? 0 : 20) ||
        MEM_R32(gp + 0x344) != (enabled ? (alternate ? 0x8000 : 0x200) : 0xdead) ||
        MEM_R32(0x8008f04c) != (enabled ? 0 : 0x7b) ||
        MEM_R32(0x8008ef1c) != (enabled ? 0x1248bdefu : 0xfeedbeefu))
        return false;
#ifdef NC_TEST_GENERATED_PAD
    return compare_generated(cpu, 0x80062c28, 54);
#else
    return true;
#endif
}

static bool sequence_step(u16 buttons, u8 remaining, u8 side_byte) {
    CPUState cpu = initial();
    if (!compare(cpu, 0x80062d2c, nc_read_pad_sequence, 100) || MEM_R8(gp + 0x3f8) != remaining ||
        MEM_R32(0x8008f04c) != side_byte)
        return false;
    memcpy(nc_ram, before, RAM_SIZE);
    if (!nc_read_pad_sequence(&cpu) || cpu.r[2] != buttons)
        return false;
#ifdef NC_TEST_GENERATED_PAD
    return compare_generated(initial(), 0x80062d2c, 100);
#else
    return true;
#endif
}

static bool sequences(void) {
    const u32 records = 0x80140000;
    CPUState cpu = initial();
    MEM_W32(gp + 0x184, 0);
    if (!compare(cpu, 0x80062cc8, nc_pad_sequence_ended, 40))
        return false;
    MEM_W32(gp + 0x184, records);
    if (!compare(cpu, 0x80062cb0, nc_stop_pad_sequence, 6) || MEM_R32(gp + 0x184) ||
        MEM_R32(0x8008f164) != 5)
        return false;
    MEM_W32(gp + 0x184, records);
    MEM_W32(gp + 0x188, 0xffffffff);
    MEM_W8(gp + 0x3f8, 0);
    MEM_W32(records, 0x11030000);      /* idle for three reads */
    MEM_W32(records + 4, 0x22010020);  /* single button */
    MEM_W32(records + 8, 0x3302c080);  /* simultaneous buttons */
    MEM_W32(records + 12, 0x44014c41); /* first marker half alone */
    MEM_W32(records + 16, 0x4e414c41);
    MEM_W32(0x8008f164, 0xdeadbeef);
    MEM_W32(records + 20, 0x55000000);
    if (!sequence_step(0, 2, 0x11) || !sequence_step(0, 1, 0x11) || !sequence_step(0, 0, 0x22) ||
        !sequence_step(0x20, 0, 0x33) || !sequence_step(0xc080, 1, 0x33) ||
        !sequence_step(0xc080, 0, 0x44) || !sequence_step(0x4c41, 0, 0x4e) ||
        !sequence_step(0x4c41, 0, 0x55) || MEM_R32(gp + 0x184) != 0 || MEM_R32(gp + 0x188) != 4 ||
        !sequence_step(0, 0, 0x55))
        return false;
    MEM_W32(gp + 0x184, records);
    MEM_W32(gp + 0x188, 0xffffffff);
    MEM_W32(records, 0x11000080); /* zero duration wraps to 255 */
    if (!sequence_step(0x80, 255, 0x11))
        return false;
    for (unsigned i = 0; i < 3; ++i) {
        MEM_W32(gp + 0x188, i + 2);
        if (!compare(cpu, 0x80062cc8, nc_pad_sequence_ended, 40))
            return false;
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 77;
    fclose(file);
    ExecInfo exe;
    if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX2.EXE;1", &exe))
        return 1;
    bool ok =
        pad_dispatch_actions() && live_pad_dispatch() && pad_dispatch_entry() && playback_read() &&
        playback_poll_gate() && playback_interrupt() && pad_select_edge() && pad_start_edge() &&
        menu_button_scan_edges() && menu_sequence_matches() && menu_record_sequences() &&
        menu_record_progress() && menu_completed_sequence() && menu_enabled_records() &&
        menu_scan_return() && menu_navigation_history() && menu_scan_history() &&
        menu_record_shoulders() && menu_record_button_pairs() && menu_record_triangle() &&
        menu_record_dispatch_and_select() && menu_apply_record_select() &&
        menu_record_direction_repeats() && menu_record_shoulder_repeats() &&
        menu_record_face_edges() && menu_record_face_repeats() &&
        menu_record_start_and_dispatch() && menu_record_button_history() &&
        menu_record_history_resets() && menu_record_four_timing() && menu_record_cross() &&
        menu_record_modes() && menu_mode_up() && menu_mode_directions() &&
        menu_mode_face_buttons() && menu_mode_face_result() && menu_input_entry() &&
        menu_mode_record_call() && menu_mode_record_selection() && menu_record_timeout() &&
        menu_input_return() && menu_direction_selections() && menu_horizontal_inputs() &&
        menu_vertical_inputs() && menu_face_inputs() && menu_start_down_inputs() &&
        menu_up_inputs() && menu_right_inputs() && menu_left_inputs() && menu_pad_type_flags() &&
        menu_confirm_edges() && pad_inactivity() && l2_direction_edges() && button_dispatch() &&
        sdk_pad_patch() && pad_bios_services() && sdk_pad(34, 4) && sdk_pad(3, 34) &&
        sdk_pad(4, 0) && pad_driver() && packet_readers() && sequences() && ports();
    for (unsigned i = 0; ok && i < 16; ++i)
        ok = sequence_init(0x80 | (i & 3) << 4, (i & 4) != 0, (i & 8) != 0);
    for (unsigned i = 0; ok && i < 18; ++i) {
        u32 pressed = i == 16 ? 0 : i == 17 ? 0xfafa : 1u << i;
        ok = decode(0x41, pressed, false, 0, 128, 128, 128) &&
             decode(0x41, pressed, true, 0, 128, 128, 128);
    }
    const u8 axes[] = {0, 31, 32, 33, 63, 64, 128, 192, 193, 223, 224, 225, 255};
    const u8 right[] = {15, 16, 240, 241};
    const u32 flags[] = {0, 0x00100000, 0x40000000};
    for (unsigned i = 0; ok && i < sizeof axes; ++i)
        for (unsigned j = 0; ok && j < 4; ++j)
            for (unsigned k = 0; ok && k < 3; ++k)
                ok = decode(0x73, j * 2, true, flags[k], axes[i], axes[sizeof axes - 1 - i],
                            right[j]);
    ok = ok && decode(0x53, 0x9876, false, 0, 0, 0, 0);
    for (unsigned i = 0; ok && i < 4; ++i) {
        u8 first = i & 1 ? 0xff : 0, second = i & 2 ? 0xff : 0;
        ok = caller(4096, first, second, 0x800d7c04, 0x40000000, 0) &&
             caller(150, first, second, 0x800d7c04, 0x40000000, 0) &&
             caller(4096, first, second, 0, 0, 0) &&
             caller(4096, first, second, 0x800d7c04, 0, 0x03000000) &&
             caller(4096, first, second, 0x800d7c04, 0, 0);
    }
    for (unsigned flags = 0; ok && flags <= 0x30; flags += 0x10)
        ok = player_source(flags);
    const u32 blocked_flags[] = {0x80000000, 0x8000, 0x1000, 0x800, 0x80009830};
    for (unsigned i = 0; ok && i < sizeof blocked_flags / sizeof blocked_flags[0]; ++i)
        ok = blocked_player_input(blocked_flags[i]);
    ok = ok && player_input_return(false) && player_input_return(true);
    ok = ok && full_pad_dispatch();
    disc_close();
    return ok ? 0 : 1;
}
