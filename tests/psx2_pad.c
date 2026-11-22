#include "decomp/pad.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/hw/hw.h"
#include "port/interp.h"
#include "port/runtime.h"
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
        interp_call(&guest, entry);
        memcpy(nc_ram, before, RAM_SIZE);
        nc_set_module(NC_MOD_PSX2);
        NcFunc routine = nc_lookup(entry);
        if (!routine)
            return false;
        routine(&cpu);
        nc_set_module(NC_MOD_NONE);
        if (memcmp(&cpu, &guest, sizeof cpu) || memcmp(nc_ram, after, RAM_SIZE))
            return false;
    }
    return true;
}
#endif

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
    bool ok = pad_start_edge() && pad_inactivity() && l2_direction_edges() && button_dispatch() &&
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
    disc_close();
    return ok ? 0 : 1;
}
