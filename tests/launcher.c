#include "vblank_wait.h"
#include "cd_init.h"
#include "cd_reset.h"
#include "jump_context.h"
#include "decomp/jump_context.h"
#include "decomp/byte_scan.h"
#include "decomp/cd_state.h"
#include "decomp/cd_flush.h"
#include "decomp/cd_volume.h"
#include "decomp/cd_position.h"
#include "decomp/bios_service.h"
#include "decomp/launcher.h"
#include "decomp/sdk_printf.h"
#include "decomp/sdk_putchar.h"
#include "sdk_putchar.h"
#include "decomp/sdk_puts.h"
#include "sdk_puts.h"
#include "decomp/interrupt_driver.h"
#include "port/bios.h"
#include "port/disc.h"
#include "port/hw/hw.h"
#include "port/exe.h"

#include <stdio.h>

static void install_recorder(u32 entry, u32 body, const u32 *actions, unsigned count) {
    /* Jump to a guest stub, log the caller's ra at s1, then perform its actions. */
    MEM_W32(entry, 0x08000000 | ((body >> 2) & 0x03ffffff));
    MEM_W32(entry + 4, 0xae3f0000); /* sw ra, 0(s1) */
    for (unsigned i = 0; i < count; i++)
        MEM_W32(body + 4 * i, actions[i]);
    MEM_W32(body + 4 * count, 0x03e00008);
    MEM_W32(body + 4 * count + 4, 0x26310004); /* advance trace */
}

static bool check_shared_record(void) {
    ExecInfo info;
    if (!exe_load("cdrom:\\SLUS_005.82;1", &info))
        return false;
    const u32 record = 0x80016400;
    const u32 locations[] = {record, 0xa0016400, SCRATCH_BASE + 32};
    const int budgets[] = {2, 7, 31, 4096};
    for (unsigned i = 0; i < sizeof locations / sizeof locations[0]; i++) {
        u32 base = locations[i];
        for (unsigned j = 0; j < sizeof budgets / sizeof budgets[0]; j++) {
            memset(nc_fast_ptr(base - 1), 0xa5, 42);
            CPUState start = {0};
            start.r[28] = 0x80016300;
            start.r[29] = 0x801fff00;
            start.r[31] = 0x80010000;
            start.budget = budgets[j];
            MEM_W32(start.r[28], base);
            if (!compare(start, 0x8001078c, nc_launcher_init_shared_record, 1) ||
                MEM_R32(base + 4) != 0x08000000 || MEM_R32(base + 0x24) != 0 ||
                MEM_R8(base - 1) != 0xa5 || MEM_R8(base + 0x23) != 0xa5 ||
                MEM_R8(base + 0x28) != 0xa5)
                return false;
        }
    }
    /* The store at offset 0x20 redirects subsequent pointer reads. */
    CPUState start = {0};
    start.r[28] = record + 0x20;
    start.r[29] = 0x801fff00;
    start.r[31] = 0x80010000;
    start.budget = 16;
    MEM_W32(start.r[28], record);
    return compare(start, 0x8001078c, nc_launcher_init_shared_record, 1);
}

static bool run_constructors(CPUState *c) {
    return nc_launcher_global_callbacks(c, true);
}

static bool run_destructors(CPUState *c) {
    return nc_launcher_global_callbacks(c, false);
}

static bool check_wait_vblank(void) {
    const u32 drivers[] = {0x80013dd8, 0x80011a8c, 0x80011a9c};
    return compare_vblank_wait(0x80013f70, nc_launcher_wait_vblank, 0x80015760, 0x800105c4, drivers);
}

static bool check_cd_init(void) {
    const u32 slots[] = {0x80015818, 0x8001581c, 0x80015b44};
    const u32 handlers[] = {0x80011f0c, 0x80011f34, 0x80011f5c};
    return check_cd_init_retries(0x80011e7c, nc_launcher_cd_init, 0x80011fd4,
                                 0x80011160, 0x8001034c, slots, handlers);
}

static bool launcher_putchar(CPUState *c) {
    return sdk_putchar(c, 0x800118d8, 0x80011a7c, 0x80015b7c, 0x800156bd);
}

static bool launcher_puts(CPUState *c) {
    return sdk_puts(c, 0x800105bc, 0x800118d8, 0x80013e04);
}

static bool launcher_printf(CPUState *c) {
    return sdk_printf(c, 0x8001119c, 0x8001118c);
}

static bool check_printf(void) {
    const u32 format = 0x80060000, count = format + 32;
    strcpy((char *)nc_fast_ptr(format), "%d%x%c%n");
    const int budgets[] = {12, 4096};
    for (unsigned i = 0; i < sizeof budgets / sizeof budgets[0]; i++) {
        CPUState state = arguments(format, (u32)-12, 0xcafe, budgets[i]);
        state.r[7] = 'A';
        MEM_W32(state.r[29] + 16, count); /* fourth variadic argument */
        MEM_W32(count, UINT32_MAX);
        if (!compare(state, 0x80011160, launcher_printf, 11) ||
            MEM_R32(count) != 8 || nc_cpu.r[2] != 8)
            return false;
    }
    return true;
}

static bool launcher_toupper(CPUState *c) {
    return nc_launcher_convert_case(c, true);
}

static bool launcher_tolower(CPUState *c) {
    return nc_launcher_convert_case(c, false);
}

static bool check_case_conversion(void) {
    const struct { u32 input, upper, lower; } cases[] = {
        {'a', 'A', 'a'}, {'Z', 'Z', 'z'}, {'9', '9', '9'},
        {0xffffff61, 'A', 'a'}, {0x141, 'A', 'a'}, {0xffffffff, 255, 255},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        CPUState start = {0};
        start.r[4] = cases[i].input;
        start.r[29] = 0x801fff00;
        start.r[31] = 0x80010000;
        start.budget = NC_LOAD_CYCLES + 12;
        if (!compare(start, 0x80011828, launcher_toupper, NC_LOAD_CYCLES + 11) ||
            nc_cpu.r[2] != cases[i].upper ||
            !compare(start, 0x80011858, launcher_tolower, NC_LOAD_CYCLES + 11) ||
            nc_cpu.r[2] != cases[i].lower)
            return false;
    }
    return true;
}

static u32 cd_state_address;

static bool read_cd_byte(CPUState *c) {
    return cd_state_byte(c, cd_state_address);
}

static bool get_cd_position(CPUState *c) {
    return cd_last_position(c, 0x80015834);
}

static bool set_cd_word(CPUState *c) {
    return exchange_cd_word(c, cd_state_address);
}

static bool cd_command_name(CPUState *c) {
    return cd_lookup_name(c, 0x8001583c, 28, 0x80010364);
}

static bool cd_interrupt_name(CPUState *c) {
    return cd_lookup_name(c, 0x800158bc, 7, 0x80010364);
}

static u32 cd_event_spec, cd_event_resume;

static bool deliver_cd_callback(CPUState *c) {
    return cd_default_event(c, cd_event_spec, cd_event_resume, 0x80011f84);
}

static u32 cd_driver, cd_driver_resume;

static enum CdDriverResult cd_driver_result;

static bool forward_cd_call(CPUState *c) {
    return call_cd_status_driver(c, cd_driver, cd_driver_resume, cd_driver_result);
}

static bool register_cd_data(CPUState *c) {
    return cd_data_callback(c, 0x80010ab0, 0x80012554);
}

static bool check_cd_init_interrupt(void) {
    const u32 mask_slot = 0x800156b0, mask = 0x80061000, handler = 0x8001462c;
    const u32 cleared[] = {0x8001581c, 0x80015818, 0x8001582c, 0x80015828};
    const u32 previous[] = {0, 0x80016200, 0x80013cf8};
    u32 saved_mask = MEM_R32(mask_slot);
    MEM_W32(mask_slot, mask);
    MEM_W16(0x80014620, 1); /* callback subsystem already started */
    for (unsigned i = 0; i < sizeof previous / sizeof previous[0]; i++) {
        MEM_W32(handler, previous[i]);
        MEM_W16(mask, 0x41);
        MEM_W16(0x80014650, 0x20);
        for (unsigned j = 0; j < 4; j++)
            MEM_W32(cleared[j], UINT32_MAX);
        if (!compare(arguments(7, 2, 3, i ? 13 : 4096), 0x80013840,
                     nc_launcher_cd_init_interrupt, 12) ||
            nc_cpu.r[2] != previous[i] || MEM_R32(handler) != 0x80013cf8 ||
            MEM_R16(mask) != (previous[i] == 0x80013cf8 ? 0x41 : 0x45))
            return false;
        for (unsigned j = 0; j < 4; j++)
            if (MEM_R32(cleared[j]) != 0)
                return false;
    }
    MEM_W32(mask_slot, saved_mask);
    return true;
}

static bool check_cd_data_callback(void) {
    const u32 driver = 0x80016700, port = driver + 32;
    const u32 slot = 0x8001576c + 3 * 4;
    MEM_W32(0x800156a8, driver);
    MEM_W32(driver + 4, 0x80011da8); /* setIntrDMA */
    MEM_W32(0x80015768, port);
    MEM_W32(port, 0);
    MEM_W32(slot, 0);
    const u32 handlers[] = {0x80016200, 0x80016300, 0};
    for (unsigned i = 0; i < sizeof handlers / sizeof handlers[0]; i++) {
        u32 previous = MEM_R32(slot);
        CPUState start = arguments(handlers[i], 0, 0, 6);
        if (!compare(start, 0x80012540, register_cd_data, 5) ||
            nc_cpu.r[2] != previous || MEM_R32(slot) != handlers[i] ||
            MEM_R32(port) != (handlers[i] ? 0x00880000u : 0x00800000u))
            return false;
    }
    return true;
}

static bool check_cd_driver_calls(void) {
    const struct {
        u32 entry, driver;
        enum CdDriverResult result;
        u32 input, expected;
    } calls[] = {
        {0x80012040, 0x8001366c, CD_RETURN_STATUS, 11, 11},
        {0x800120e0, 0x80012c80, CD_RETURN_STATUS, 22, 22},
        {0x80012100, 0x80012f00, CD_RETURN_STATUS, 33, 33},
        {0x80012564, 0x80013a7c, CD_RETURN_STATUS, 44, 44},
        {0x800124f8, 0x800135e4, CD_RETURN_ONE, 0, 1},
        {0x800124f8, 0x800135e4, CD_RETURN_ONE, UINT32_MAX, 1},
        {0x8001251c, 0x80013be8, CD_ZERO_IS_SUCCESS, 0, 1},
        {0x8001251c, 0x80013be8, CD_ZERO_IS_SUCCESS, UINT32_MAX, 0},
    };
    const u32 trace = 0x80016200;
    for (unsigned i = 0; i < sizeof calls / sizeof calls[0]; i++) {
        cd_driver = calls[i].driver;
        cd_driver_result = calls[i].result;
        cd_driver_resume = calls[i].entry + 16;
        u32 saved[3];
        memcpy(saved, nc_fast_ptr(cd_driver), sizeof saved);
        MEM_W32(cd_driver, 0xacbf0000);     /* sw ra, 0(a1) */
        MEM_W32(cd_driver + 4, 0x03e00008); /* jr ra */
        MEM_W32(cd_driver + 8, 0x00801021); /* move v0, a0 */
        MEM_W32(trace, 0);
        CPUState start = arguments(calls[i].input, trace, 0, 5);
        bool ok = compare(start, calls[i].entry, forward_cd_call, 4) &&
                  nc_cpu.r[2] == calls[i].expected && MEM_R32(trace) == cd_driver_resume;
        memcpy(nc_fast_ptr(cd_driver), saved, sizeof saved);
        if (!ok)
            return false;
    }
    return true;
}

static bool check_cd_events(void) {
    const struct { u32 entry, spec; } callbacks[] = {
        {0x80011f0c, 0x20}, {0x80011f34, 0x40}, {0x80011f5c, 0x40},
    };
    for (unsigned i = 0; i < sizeof callbacks / sizeof callbacks[0]; i++) {
        cd_event_spec = callbacks[i].spec;
        cd_event_resume = callbacks[i].entry + 24;
        CPUState event = arguments(0xf0000003, cd_event_spec, 0x2000, 4096);
        event.r[9] = 8; /* OpenEvent in polling mode */
        bios_call(&event, 0xb0);
        event.r[4] = event.r[2];
        if (event.r[4] == UINT32_MAX)
            return false;
        event.r[9] = 12; /* EnableEvent */
        bios_call(&event, 0xb0);
        CPUState start = arguments(1, 2, 3, 7);
        nc_cpu = start;
        if (!deliver_cd_callback(&nc_cpu))
            return false;
        event.r[9] = 11; /* TestEvent consumes the notification. */
        bios_call(&event, 0xb0);
        if (!event.r[2] || !compare(start, callbacks[i].entry, deliver_cd_callback, 6))
            return false;
        bios_call(&event, 0xb0);
        if (!event.r[2])
            return false;
        event.r[9] = 9; /* CloseEvent */
        bios_call(&event, 0xb0);
    }
    return true;
}

static bool check_cd_names(void) {
    const struct { u32 entry, table, count; bool (*routine)(CPUState *); } lookups[] = {
        {0x80012078, 0x8001583c, 28, cd_command_name},
        {0x800120ac, 0x800158bc, 7, cd_interrupt_name},
    };
    for (unsigned i = 0; i < sizeof lookups / sizeof lookups[0]; i++) {
        const u32 count = lookups[i].count;
        const u32 inputs[] = {0, 0x100 + count - 1, count, UINT32_MAX};
        for (unsigned j = 0; j < sizeof inputs / sizeof inputs[0]; j++) {
            u32 index = inputs[j] & 0xff;
            u32 expected = index < count ? MEM_R32(lookups[i].table + 4 * index) : 0x80010364;
            CPUState start = arguments(inputs[j], 0, 0, 14);
            if (!compare(start, lookups[i].entry, lookups[i].routine, 13) || nc_cpu.r[2] != expected)
                return false;
        }
    }
    return true;
}

static bool check_cd_setters(void) {
    const struct { u32 entry, address; } setters[] = {
        {0x80012060, 0x80015824}, {0x80012120, 0x80015818},
        {0x80012138, 0x8001581c}, {0x800145dc, 0x80015b44},
    };
    const u32 replacements[] = {0x80016234, 0};
    for (unsigned i = 0; i < sizeof setters / sizeof setters[0]; i++) {
        cd_state_address = setters[i].address;
        u32 saved = MEM_R32(cd_state_address);
        MEM_W32(cd_state_address, 0x87654321);
        for (unsigned j = 0; j < sizeof replacements / sizeof replacements[0]; j++) {
            u32 previous = MEM_R32(cd_state_address);
            CPUState start = arguments(replacements[j], 0, 0, 9);
            if (!compare(start, setters[i].entry, set_cd_word, 8) ||
                nc_cpu.r[2] != previous || MEM_R32(cd_state_address) != replacements[j])
                return false;
        }
        MEM_W32(cd_state_address, saved);
    }
    return true;
}

static bool set_test_parameter_count(CPUState *c) {
    return cd_set_test_parameter_count(c, 0x80015ac0);
}

static bool check_test_parameter_count(void) {
    const u32 address = 0x80015ac0, saved = MEM_R32(address);
    const u32 counts[] = {0x87654321, 0};
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        CPUState start = arguments(counts[i], 0, 0, 5);
        if (!compare(start, 0x80013ce8, set_test_parameter_count, 4) ||
            MEM_R32(address) != counts[i] || nc_cpu.r[2] != start.r[2])
            return false;
    }
    MEM_W32(address, saved);
    return true;
}

static bool set_cd_volume(CPUState *c) {
    return cd_volume(c, 0x80015adc);
}

static bool check_cd_volume(void) {
    const u32 slots = 0x80015adc, ports = SCRATCH_BASE + 0x40;
    u32 saved[4];
    memcpy(saved, nc_fast_ptr(slots), sizeof saved);
    for (unsigned i = 0; i < 4; i++)
        MEM_W32(slots + i * 4, ports + i);
    for (unsigned alias = 0; alias < 2; alias++) {
        u32 input = alias ? ports : 0x80016200;
        MEM_W32(ports, 0);
        MEM_W32(input, 0x81234567);
        if (!compare(arguments(input, 0, 0, 57), 0x800135e4, set_cd_volume, 56) ||
            nc_cpu.r[2] != 0 || MEM_R32(ports) != (alias ? 0x20450203u : 0x20812303u))
            return false;
    }
    memcpy(nc_fast_ptr(slots), saved, sizeof saved);
    return true;
}

static bool reset_cd_controller(CPUState *c) {
    return cd_reset(c, 0x80011fd4, 0x80013840, 0x80013890, 0x8001374c);
}

static bool flush_cd_controller(CPUState *c) {
    return cd_flush_controller(c, 0x80015adc);
}

static bool check_cd_flush_controller(void) {
    const u32 slots = 0x80015adc, ram = 0x80061000, dma = SCRATCH_BASE + 4;
    u8 saved[28];
    memcpy(saved, nc_fast_ptr(slots), sizeof saved);
    bool ok = true;
    for (unsigned pending = 0; ok && pending < 2; pending++) {
        MEM_W32(slots, ram);
        /* Writing the interrupt enable redirects the next status read to ram + 7. */
        MEM_W32(slots + 8, pending ? slots + 12 : ram + 2);
        MEM_W32(slots + 12, ram + 3);
        MEM_W32(slots + 16, dma);
        MEM_W32(slots + 24, UINT32_MAX);
        MEM_W32(dma, 0);
        MEM_W32(ram, 0);
        MEM_W8(ram + 3, pending);
        MEM_W8(ram + 7, 0);
        ok = compare(arguments(9, 2, 3, pending ? 7 : 4096), 0x8001366c, flush_cd_controller, 6) &&
             MEM_R32(slots + 24) == 0xff000002 && MEM_R32(dma) == 0x1325 &&
             MEM_R8(ram + 3) == (pending ? 7 : 0);
    }
    memcpy(nc_fast_ptr(slots), saved, sizeof saved);
    return ok;
}

static bool init_cd_volume(CPUState *c) {
    return cd_init_volume(c, 0x80015adc);
}

static bool check_cd_init_volume(void) {
    const u32 slots = 0x80015adc, spu = 0x80061000, ports = SCRATCH_BASE + 0x40;
    u8 saved[24];
    memcpy(saved, nc_fast_ptr(slots), sizeof saved);
    for (unsigned i = 0; i < 4; i++)
        MEM_W32(slots + i * 4, ports + i);
    MEM_W32(slots + 20, spu);
    bool ok = true;
    for (unsigned active = 0; active < 4 && ok; active++) {
        MEM_W16(spu + 0x1b8, active & 1);
        MEM_W16(spu + 0x1ba, active & 2);
        MEM_W32(spu + 0x180, 0x81234567);
        MEM_W32(spu + 0x1b0, 0);
        MEM_W16(spu + 0x1aa, 0);
        MEM_W32(ports, UINT32_MAX);
        ok = compare(arguments(1, 2, 3, 92), 0x8001374c, init_cd_volume, 91) &&
             MEM_R32(spu + 0x180) == (active ? 0x81234567 : 0x3fff3fff) &&
             MEM_R32(spu + 0x1b0) == 0x3fff3fff && MEM_R16(spu + 0x1aa) == 0xc001;
    }
    memcpy(nc_fast_ptr(slots), saved, sizeof saved);
    return ok;
}

static bool check_cd_positions(void) {
    const u32 position = 0x80016201;
    const struct { s32 sector; u8 minute, second, frame; } cases[] = {
        {-150, 0, 0, 0}, {-1, 0, 0x01, 0x74}, {0, 0, 0x02, 0},
        {4349, 0, 0x59, 0x74}, {4350, 0x01, 0, 0}, {449849, 0x99, 0x59, 0x74},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const u32 sector = (u32)cases[i].sector;
        memset(nc_fast_ptr(position - 1), 0xcc, 5);
        if (!compare(arguments(sector, position, 0, 66), 0x80012584, cd_sector_to_position, 65) ||
            nc_cpu.r[2] != position || MEM_R8(position) != cases[i].minute ||
            MEM_R8(position + 1) != cases[i].second || MEM_R8(position + 2) != cases[i].frame ||
            MEM_R8(position - 1) != 0xcc || MEM_R8(position + 3) != 0xcc)
            return false;
        if (!compare(arguments(position, 0, 0, 39), 0x80012688, cd_position_to_sector, 38) ||
            nc_cpu.r[2] != sector)
            return false;
    }
    return true;
}

static bool check_cd_state(void) {
    const struct { u32 entry, address; u8 value; } bytes[] = {
        {0x80011f94, 0x80015828, 0x81},
        {0x80011fa4, 0x80015838, 0xfe},
        {0x80011fb4, 0x80015839, 0x9c},
    };
    CPUState start = arguments(1, 2, 3, 7);
    for (unsigned i = 0; i < sizeof bytes / sizeof bytes[0]; i++) {
        cd_state_address = bytes[i].address;
        MEM_W8(cd_state_address, bytes[i].value);
        if (!compare(start, bytes[i].entry, read_cd_byte, 6) || nc_cpu.r[2] != bytes[i].value)
            return false;
    }
    MEM_W32(0x80015834, 0x12345678);
    return compare(start, 0x80011fc4, get_cd_position, 4) && nc_cpu.r[2] == 0x80015834;
}

static bool check_byte_scans(void) {
    const u32 base = 0x80016201;
    const u8 bytes[] = {0x78, 0xff, 0, 0xff};
    memcpy(nc_fast_ptr(base), bytes, sizeof bytes);
    const struct { u32 address, key, count, result; } searches[] = {
        {0, 0, 4, 0}, {base, 0x78, 0, 0}, {base, 0x78, UINT32_MAX, 0},
        {base, 0x1ff, 1, 0}, {base, 0x1ff, 4, base + 1}, {base, 0, 3, base + 2},
    };
    for (unsigned i = 0; i < sizeof searches / sizeof searches[0]; i++) {
        CPUState start = arguments(searches[i].address, searches[i].key, searches[i].count, 10);
        if (!compare(start, 0x80011888, sdk_find_byte, 9) || nc_cpu.r[2] != searches[i].result)
            return false;
    }
    const u32 strings[] = {0, base + 2, base, base + 0x20000000};
    for (unsigned i = 0; i < sizeof strings / sizeof strings[0]; i++) {
        if (!compare(arguments(strings[i], 0, 0, 3), 0x8001198c, sdk_string_length, 2) ||
            nc_cpu.r[2] != (i < 2 ? 0u : 2u))
            return false;
    }
    return true;
}

static bool check_global_callbacks(void) {
    const u32 flags[] = {0, 1, 0x80000000};
    for (unsigned ctor = 0; ctor < 2; ctor++)
        for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; i++) {
            CPUState start = {0};
            start.r[1] = 0x3456;
            start.r[8] = 0x1234;
            start.r[16] = 0x6789;
            start.r[17] = 0x9876;
            start.r[29] = 0x801ffe00;
            start.r[31] = 0x80010000;
            start.budget = i ? 4096 : 31;
            MEM_W32(0x800145f4, flags[i]);
            u32 entry = ctor ? 0x800108f8 : 0x80010968;
            if (!compare(start, entry, ctor ? run_constructors : run_destructors, 30) ||
                MEM_R32(0x800145f4) != (ctor && !flags[i] ? 1 : flags[i]))
                return false;
        }
    return true;
}

static u32 bios_table, bios_function;

static bool call_bios_wrapper(CPUState *c) {
    return bios_service(c, bios_table, bios_function);
}

static bool check_bios_wrappers(void) {
    const u32 header = 0x80016200, probe = 0x80016300, trace = probe + 64;
    const struct { u32 entry, table, service, first_arg, result; } cases[] = {
        {0x800109d0, 0xa0, 0x39, header, 0},
        {0x800109e0, 0xa0, 0x42, 0x80010000, 1}, /* original PSX.EXE path */
        {0x800109e0, 0xa0, 0x42, trace, 0},      /* empty path */
        {0x800109f0, 0xa0, 0x43, header, 1},
        {0x80010a00, 0xa0, 0x71, 0, 0},
        {0x80010a18, 0xa0, 0x72, 0, 0},
        {0x80011a3c, 0xb0, 0x17, 0, 0},     /* outside an exception */
        {0x80011a5c, 0xb0, 0x19, probe, 0}, /* HookEntryInt */
        {0x80011a4c, 0xb0, 0x18, 0, 0},     /* ResetEntryInt */
        {0x80011a7c, 0xb0, 0x35, 1, 2},      /* write to the TTY */
        {0x80011a7c, 0xb0, 0x35, 99, ~0u},   /* invalid descriptor */
        {0x80011f84, 0xb0, 0x07, 0xf0000003, 0}, /* DeliverEvent */
        {0x80011a8c, 0xb0, 0x5b, 1, 0},     /* ChangeClearPad */
        {0x80011a9c, 0xc0, 0x0a, 2, 1},     /* ChangeClearRCnt */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        CPUState start = {0};
        start.r[4] = cases[i].first_arg;
        start.r[5] = header;
        start.r[6] = 17;
        start.r[16] = trace;
        start.r[29] = 0x801fff00;
        start.r[31] = 0x80010000;
        start.budget = i & 1 ? 4 : 1024;
        bios_table = cases[i].table;
        bios_function = cases[i].service;
        MEM_W32(trace, 0);
        if (bios_table == 0xb0 && bios_function == 0x35) {
            start.r[5] = probe;
            start.r[6] = 2;
            MEM_W8(probe, 'x');
            MEM_W8(probe + 1, '\n');
        }
        if (bios_function == 0x43) {
            ExecInfo info = {.pc0 = probe};
            exe_write_info(&info, header);
            MEM_W32(probe, 0xae040000); /* sw a0, 0(s0) */
            MEM_W32(probe + 4, 0x03e00008);
            MEM_W32(probe + 8, 0xae050004); /* sw a1, 4(s0) */
        }
        if (!compare(start, cases[i].entry, call_bios_wrapper, 3) ||
            nc_cpu.r[2] != cases[i].result || nc_cpu.r[9] != bios_function ||
            nc_cpu.r[10] != bios_table)
            return false;
        if (bios_function == 0x42 && cases[i].result && MEM_R32(header) != 0x80025c70)
            return false;
        if (bios_function == 0x43 && (MEM_R32(trace) != header || MEM_R32(trace + 4) != 17))
            return false;
    }
    return true;
}

static const struct {
    u32 entry, offset, resume;
    bool single;
} irq_wrappers[] = {
    {0x80010a50, 12, 0x80010a70, false}, {0x80010a80, 8, 0x80010aa0, false},
    {0x80010ab0, 4, 0x80010ad0, false}, {0x80010ae0, 20, 0x80010b04, true},
    {0x80010b14, 20, 0x80010b34, false}, {0x80010b44, 16, 0x80010b64, false},
    {0x80010b74, 24, 0x80010b94, false},
};
static unsigned irq_wrapper;

static bool call_irq_wrapper(CPUState *c) {
    return interrupt_driver_call(c, 0x800156a8, irq_wrappers[irq_wrapper].offset,
                                 irq_wrappers[irq_wrapper].resume, irq_wrappers[irq_wrapper].single);
}

static bool launcher_set_vsync_slot(CPUState *c) {
    return set_vsync_slot(c, 0x80015740);
}

static bool check_start_interrupt(bool dma) {
    const u32 slots = dma ? 0x8001576c : 0x80015740, driver = 0x80016700, stub = driver + 32;
    const u32 trace = stub + 32, port = trace + 8;
    MEM_W32(0x800156a8, driver);
    MEM_W32(driver + 8, stub);
    MEM_W32(stub, 0xae040000);     /* sw a0, 0(s0) */
    MEM_W32(stub + 4, 0x03e00008); /* jr ra */
    MEM_W32(stub + 8, 0xae050004); /* sw a1, 4(s0) */
    MEM_W32(dma ? slots - 4 : slots + 36, port);
    const unsigned words = dma ? 8 : 9;
    const int threshold = dma ? 6 : 14;
    const int budgets[] = {threshold + 1, 4096};
    for (unsigned i = 0; i < sizeof budgets / sizeof budgets[0]; i++) {
        memset(nc_fast_ptr(slots), 0xff, words * 4);
        memset(nc_fast_ptr(trace), 0xff, 12);
        CPUState start = {0};
        start.r[16] = trace;
        start.r[29] = 0x801fff00;
        start.r[31] = 0x80010000;
        start.budget = budgets[i];
        if (!compare(start, dma ? 0x80011bd4 : 0x80011aac,
                     dma ? nc_launcher_start_dma : nc_launcher_start_vsync, threshold) ||
            MEM_R32(port) != (dma ? 0u : 0x107u) || MEM_R32(trace) != (dma ? 3u : 0u) ||
            MEM_R32(trace + 4) != (dma ? 0x80011c24u : 0x80011b04u) ||
            nc_cpu.r[2] != (dma ? 0x80011da8u : 0x80011b7cu))
            return false;
        for (unsigned j = 0; j < words; j++)
            if (MEM_R32(slots + j * 4) != 0)
                return false;
    }
    return true;
}

static bool check_vblank_dispatch(void) {
    const u32 slots = 0x80015740, counter = slots + 32;
    const u32 handler = 0x80016300, trace = handler + 16;
    MEM_W32(handler, 0x03e00008);      /* jr ra */
    MEM_W32(handler + 4, 0xacdf0000);  /* sw ra, 0(a2) */
    for (unsigned active = 0; active < 2; active++) {
        memset(nc_fast_ptr(slots), 0, 32);
        MEM_W32(counter, UINT32_MAX);
        MEM_W32(trace, 0);
        if (active)
            MEM_W32(slots, 0x80011b7c); /* install handler in slot seven */
        CPUState start = {0};
        start.r[4] = 7;
        start.r[5] = handler;
        start.r[6] = trace;
        start.r[29] = 0x801fff00;
        start.r[31] = 0x80010000;
        start.budget = active ? 19 : 4096;
        if (!compare(start, 0x80011b04, nc_launcher_dispatch_vblank, 18) ||
            MEM_R32(counter) != 0 || MEM_R32(trace) != (active ? 0x80011b54u : 0))
            return false;
    }
    return true;
}

static bool launcher_set_dma_slot(CPUState *c) {
    return set_dma_slot(c, 0x8001576c);
}

static bool check_dma_slots(void) {
    const u32 table = 0x8001576c, port = 0x80016200, handler = 0x80016300;
    const u32 initial = 0xa541002a;
    const struct { u32 channel, previous, next, control; } cases[] = {
        {2, 0, handler, 0x00c5002a},
        {4, handler, handler, initial},
        {0, handler, handler + 16, 0x00c1002a},
        {6, handler, 0, 0x0081002a},
    };
    MEM_W32(table - 4, port);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        u32 slot = table + 4 * cases[i].channel;
        MEM_W32(slot, cases[i].previous);
        MEM_W32(port, initial);
        CPUState start = {0};
        start.r[4] = cases[i].channel;
        start.r[5] = cases[i].next;
        start.r[29] = 0x801fff00;
        start.r[31] = 0x80010000;
        start.budget = 35;
        if (!compare(start, 0x80011da8, launcher_set_dma_slot, 34) ||
            nc_cpu.r[2] != cases[i].previous || MEM_R32(slot) != cases[i].next ||
            MEM_R32(port) != cases[i].control)
            return false;
    }
    return true;
}

static bool check_vsync_slots(void) {
    const u32 table = 0x80015740, handler = 0x80016200;
    const struct { u32 slot, previous, next; } cases[] = {
        {0, 0, handler}, {3, handler, handler}, {7, handler, 0},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        CPUState start = {0};
        start.r[4] = cases[i].slot;
        start.r[5] = cases[i].next;
        start.r[29] = 0x801fff00;
        start.r[31] = 0x80010000;
        start.budget = NC_LOAD_CYCLES + 11;
        u32 address = table + 4 * cases[i].slot;
        MEM_W32(address, cases[i].previous);
        if (!compare(start, 0x80011b7c, launcher_set_vsync_slot, NC_LOAD_CYCLES + 10) ||
            nc_cpu.r[2] != cases[i].previous || MEM_R32(address) != cases[i].next)
            return false;
    }
    return true;
}

static bool check_irq_wrappers(void) {
    const u32 trace = 0x80016800;
    for (unsigned driver = 0; driver < 2; driver++) {
        u32 table = trace + 64 + driver * 128, body = table + 32;
        MEM_W32(0x800156a8, table);
        MEM_W32(body, 0xae040000);      /* sw a0, 0(s0) */
        MEM_W32(body + 4, 0xae050004);  /* sw a1, 4(s0) */
        MEM_W32(body + 8, 0xae1f0008);  /* sw ra, 8(s0) */
        MEM_W32(body + 12, 0x03e00008);
        MEM_W32(body + 16, 0x34020001 + driver); /* return driver identifier */
        for (irq_wrapper = 0; irq_wrapper < sizeof irq_wrappers / sizeof irq_wrappers[0]; irq_wrapper++) {
            memset(nc_fast_ptr(table), 0, 28);
            MEM_W32(table + irq_wrappers[irq_wrapper].offset, body);
            bool single = irq_wrappers[irq_wrapper].single;
            int threshold = single ? 13 : 12;
            CPUState start = {0};
            start.r[4] = 7;
            start.r[5] = 0x80016700;
            start.r[16] = trace;
            start.r[29] = 0x801ffe00;
            start.r[31] = 0x80010000;
            start.budget = driver ? 4096 : threshold + 1;
            if (!compare(start, irq_wrappers[irq_wrapper].entry, call_irq_wrapper, threshold) ||
                MEM_R32(trace) != (single ? 0 : start.r[4]) ||
                MEM_R32(trace + 4) != (single ? start.r[4] : start.r[5]) ||
                MEM_R32(trace + 8) != irq_wrappers[irq_wrapper].resume ||
                nc_cpu.r[2] != driver + 1)
                return false;
        }
    }
    return true;
}

static bool read_callback_status(CPUState *c) {
    return interrupt_callback_status(c, 0x80014622);
}

static bool read_mask(CPUState *c) {
    return interrupt_mask(c, 0x800156b0, false);
}

static bool replace_mask(CPUState *c) {
    return interrupt_mask(c, 0x800156b0, true);
}

static bool check_interrupt_mask(void) {
    const u32 slot = 0x800156b0, status = 0x80014622;
    const u32 ports[] = {0x80016702, SCRATCH_BASE + 6};
    const u32 hardware = MEM_R32(slot);
    if (nc_phys(hardware) != 0x1f801074)
        return false;
    const u16 previous_mask = MEM_R16(hardware);
    CPUState start = {0};
    start.r[3] = 0x76543210;
    start.r[4] = 0x12345678;
    start.r[29] = 0x801ffe00;
    start.r[31] = 0x80010000;
    for (unsigned i = 0; i < sizeof ports / sizeof ports[0]; i++) {
        start.budget = i ? 4096 : 12;
        u16 old = i ? 0x8001 : 0;
        MEM_W16(status, old);
        if (!compare(start, 0x80010ba4, read_callback_status, 6) || nc_cpu.r[2] != old)
            return false;
        MEM_W32(slot, ports[i]);
        MEM_W32(ports[i] - 2, 0xabcd);
        MEM_W16(ports[i], old);
        MEM_W16(ports[i] + 2, 0xef01);
        if (!compare(start, 0x80010bb4, read_mask, 10) || nc_cpu.r[2] != old ||
            !compare(start, 0x80010bcc, replace_mask, 11) || nc_cpu.r[2] != old ||
            nc_cpu.r[3] != ports[i] || MEM_R16(ports[i]) != 0x5678 ||
            MEM_R16(ports[i] - 2) != 0xabcd || MEM_R16(ports[i] + 2) != 0xef01)
            return false;
    }
    MEM_W32(slot, hardware);
    /* The CPU comparison restores RAM. Seed the hardware mask separately
     * before each run so both executions observe the same previous value. */
    bool ok = true;
    for (unsigned run = 0; run < 2; run++) {
        MEM_W16(hardware, 0x41);
        nc_cpu = start;
        nc_cpu.r[4] = 0xff000102;
        if (run)
            interp_call(&nc_cpu, 0x80010bcc);
        else
            ok = replace_mask(&nc_cpu);
        ok = ok && nc_cpu.r[2] == 0x41 && nc_cpu.r[3] == hardware &&
             MEM_R16(hardware) == 0x102 && nc_cpu.budget == start.budget - 11;
    }
    MEM_W16(hardware, previous_mask);
    return ok;
}

static void save_interrupt_state(StateIO *io) {
    cpu_serialize(io);
    timers_serialize(io);
    dma_serialize(io);
    irq_serialize(io);
    bios_serialize(io);
}

static bool check_interrupt_control(bool restart, bool active, int budget) {
    const u32 state = 0x80014620, mask = 0x1f801074, dpcr = 0x1f8010f0;
    bool (*routine)(CPUState *) = restart ? nc_launcher_restart_interrupts : nc_launcher_stop_interrupts;
    StateIO initial = {0}, native = {0}, guest = {0};
    save_interrupt_state(&initial);
    bool ok = !initial.error;
    for (unsigned run = 0; ok && run < 2; run++) {
        MEM_W16(state, active ? 0x8000 : 0);
        MEM_W16(state + 0x32, 0x50);
        MEM_W32(state + 0x34, 0x13572468);
        MEM_W16(mask, 0x40);
        MEM_W32(dpcr, 0xfaceb00c);
        irq_raise(IRQ_DMA);
        nc_cpu = (CPUState){0};
        if (!restart) {
            nc_cpu.r[4] = state + 0x38;
            nc_cpu.r[9] = 0x19;
            bios_call(&nc_cpu, 0xb0); /* seed the hook that stopIntr removes */
        }
        nc_cpu.r[16] = 0xcafe;
        nc_cpu.r[29] = 0x801ffe00;
        nc_cpu.r[31] = 0x80010000;
        nc_cpu.cop0[12] = restart ? 0 : 0x401;
        nc_cpu.budget = 11;
        if (!run && routine(&nc_cpu))
            ok = false;
        nc_cpu.budget = budget;
        if (run)
            interp_call(&nc_cpu, restart ? 0x800110ac : 0x80011000);
        else
            ok = routine(&nc_cpu) && ok;
        bool stopped = !restart && active;
        bool restarted = restart && !active;
        u32 expected_dpcr = 0xfaceb00c;
        u16 expected_mask = 0x40, expected_state = active ? 0x8000 : 0;
        if (stopped) {
            expected_dpcr &= 0x77777777;
            expected_mask = expected_state = 0;
        } else if (restarted) {
            expected_dpcr = 0x13572468;
            expected_mask = 0x50;
            expected_state = 1;
        }
        ok = ok && nc_cpu.r[2] == (stopped || restarted ? state : 0) &&
             MEM_R16(mask) == expected_mask && MEM_R32(dpcr) == expected_dpcr &&
             MEM_R16(state) == expected_state;
        save_interrupt_state(run ? &guest : &native);
        initial.loading = true;
        initial.pos = 0;
        save_interrupt_state(&initial);
    }
    ok = ok && !initial.error && !native.error && !guest.error && native.len == guest.len &&
         !memcmp(native.buf, guest.buf, guest.len);
    free(initial.buf);
    free(native.buf);
    free(guest.buf);
    return ok;
}

static bool check_irq_word_clear(void) {
    const u32 entries[] = {0x80011134, 0x80011ba8, 0x80011e50};
    const struct { u32 dest, count; int budget; } cases[] = {
        {0x80016200, 0, 4}, {SCRATCH_BASE + 4, 1, 4},
        {0xa0016200, 8, 4096}, {0x80016200, 11, 13},
    };
    for (unsigned i = 0; i < sizeof entries / sizeof entries[0]; i++) {
        for (unsigned j = 0; j < sizeof cases / sizeof cases[0]; j++) {
            const u32 dest = cases[j].dest, bytes = 4 * cases[j].count;
            memset(nc_fast_ptr(dest - 4), 0xcd, bytes + 8);
            CPUState start = {0};
            start.r[3] = 0x1234;
            start.r[4] = dest;
            start.r[5] = cases[j].count;
            start.r[29] = 0x801ffe00;
            start.r[31] = 0x80010000;
            start.budget = cases[j].budget;
            if (!compare(start, entries[i], nc_launcher_clear_irq_words, 3) ||
                nc_cpu.r[4] != dest + bytes || MEM_R32(dest - 4) != 0xcdcdcdcd ||
                MEM_R32(dest + bytes) != 0xcdcdcdcd)
                return false;
            for (u32 offset = 0; offset < bytes; offset += 4)
                if (MEM_R32(dest + offset))
                    return false;
        }
    }
    return true;
}

static bool check_stack_switch(void) {
    CPUState start = {0};
    start.r[4] = 0x801ffe00;
    start.r[29] = 0x801ffe00;
    start.r[31] = 0x80010000;
    start.budget = 4;
    if (!compare(start, 0x80010a40, nc_launcher_switch_stack, 3))
        return false;

    /* Returning from a stack switch crosses the interpreter's frame boundary
     * with the new sp. Supply that boundary explicitly. */
    start.r[4] = 0x801fff00;
    CPUState native = start;
    if (!nc_launcher_switch_stack(&native))
        return false;
    nc_cpu = start;
    interp_continue_call(&nc_cpu, 0x80010a40, start.r[31], start.r[4]);
    return !memcmp(&native, &nc_cpu, sizeof native) &&
           native.r[2] == start.r[29] && native.r[29] == start.r[4];
}

static bool enter_critical(CPUState *c) {
    return critical_section_syscall(c, 1);
}

static bool exit_critical(CPUState *c) {
    return critical_section_syscall(c, 2);
}

static bool check_critical_sections(void) {
    const u32 enable = 0x401;
    const u32 flags[] = {0, 1, 0x400, enable};
    for (unsigned enter = 0; enter < 2; enter++) {
        for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; i++) {
            CPUState start = {0};
            start.r[2] = 0x12345678;
            start.r[4] = 0x76543210;
            start.r[29] = 0x801ffe00;
            start.r[31] = 0x80010000;
            start.cop0[12] = 0x00408000 | flags[i];
            start.budget = i & 1 ? 5 : 4096;
            u32 status = enter ? start.cop0[12] & ~enable : start.cop0[12] | enable;
            if (!compare(start, enter ? 0x80010a30 : 0x80011a6c,
                         enter ? enter_critical : exit_critical, 4) ||
                nc_cpu.cop0[12] != status || nc_cpu.r[4] != (enter ? 1u : 2u) ||
                nc_cpu.r[2] != (enter ? (u32)(flags[i] == enable) : start.r[2]))
                return false;
        }
    }
    return true;
}

static bool check_handoff(unsigned selection, bool missing) {
    const u32 trace = 0x80016200, shared = trace + 128, args = shared + 64;
    const u32 path_offsets[] = {0, 0x14, 0x2c, 0x40, 0x58, 0x70};
    const NcModule modules[] = {NC_MOD_PSX, NC_MOD_STREAM1, NC_MOD_PSX2,
                                NC_MOD_STREAM2, NC_MOD_STREAM3, NC_MOD_CREDITS};
    ExecInfo info;
    if (!exe_load("cdrom:\\SLUS_005.82;1", &info))
        return false;
    nc_set_module(NC_MOD_NONE);
    /* Use the real stack switch, Load and Exec. Replace the loaded program's
     * entry with a returning probe so the resident loop runs for one handoff. */
    const u32 probe = 0x800160f0;
    MEM_W32(probe, 0xae440000);     /* sw a0, 0(s2) */
    MEM_W32(probe + 4, 0xae450004); /* sw a1, 4(s2) */
    MEM_W32(probe + 8, 0x03e00008);
    MEM_W32(probe + 12, 0x25080001); /* count executions in t0 */
    install_recorder(0x800108f8, 0x80016000, NULL, 0);
    install_recorder(0x80010a50, 0x80016010, NULL, 0);
    install_recorder(0x80011e7c, 0x80016020, NULL, 0);
    install_recorder(0x8001078c, 0x80016030, NULL, 0);
    install_recorder(0x80010a00, 0x80016040, NULL, 0);
    const u32 finish[] = {0xad8d0000}; /* next selection uses the original epilogue */
    install_recorder(0x80010a18, 0x80016050, finish, 1);
    const u32 redirect[] = {0xae0e0000}; /* sw t6, 0(s0): replace exec_header.pc0 */
    install_recorder(0x80010a30, 0x80016060, redirect, 1);
    install_recorder(0x80010b44, 0x80016070, NULL, 0);

    CPUState start = {0};
    start.r[12] = 0x80010088 + 4 * selection;
    start.r[13] = 0x80010778;
    start.r[14] = probe;
    start.r[16] = 0x1234;
    start.r[17] = trace;
    start.r[18] = args;
    start.r[28] = shared + 48;
    start.r[29] = 0x801fff18;
    start.r[31] = 0x80010000;
    start.budget = selection & 1 ? 2 : 4096;
    MEM_W32(start.r[28], shared);
    MEM_W32(shared + 0x24, selection);
    MEM_W32(args, UINT32_MAX);
    MEM_W32(args + 4, UINT32_MAX);
    memset(nc_fast_ptr(trace), 0, 128);
    if (missing)
        MEM_W8(0x80010000 + path_offsets[selection] + 7, '!');

    const u32 arm = MEM_R32(start.r[12]);
    if (!compare(start, 0x80010618, nc_launcher_run_modules, 1))
        return false;
    const u32 expected[] = {0x80010628, 0x8001063c, 0x80010644, 0x8001064c, arm + 8,
                            missing ? 0x80010770 : 0x80010738, 0x80010740, 0x80010758, 0x80010760};
    const unsigned calls = missing ? 6 : 9;
    for (unsigned i = 0; i < calls; i++)
        if (MEM_R32(trace + 4 * i) != expected[i])
            return false;
    return nc_cpu.r[17] == trace + 4 * calls && nc_cpu.r[8] == (missing ? 0u : 1u) &&
           nc_active_module() == (missing ? NC_MOD_NONE : modules[selection]) &&
           MEM_R32(args) == (missing ? UINT32_MAX : 0) &&
           MEM_R32(args + 4) == (missing ? UINT32_MAX : 0);
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 77;
    fclose(file);
    if (!disc_open(argv[1]))
        return 1;
    if (!check_shared_record()) {
        fprintf(stderr, "launcher shared record failed\n");
        return 1;
    }
    if (!check_sdk_putchar(0x800118d8, launcher_putchar, 0x80015b7c)) {
        fprintf(stderr, "launcher putchar failed\n");
        return 1;
    }
    if (!check_sdk_puts(0x80013dd8, launcher_puts, 0x80015b7c)) {
        fprintf(stderr, "launcher puts failed\n");
        return 1;
    }
    if (!check_printf()) {
        fprintf(stderr, "launcher printf failed\n");
        return 1;
    }
    if (!check_case_conversion()) {
        fprintf(stderr, "launcher character case conversion failed\n");
        return 1;
    }
    if (!check_wait_vblank()) {
        fprintf(stderr, "launcher VBlank wait failed\n");
        return 1;
    }
    if (!check_cd_init()) {
        fprintf(stderr, "launcher CD initialization failed\n");
        return 1;
    }
    if (!check_cd_init_interrupt()) {
        fprintf(stderr, "launcher CD interrupt initialization failed\n");
        return 1;
    }
    if (!check_cd_data_callback()) {
        fprintf(stderr, "launcher CD data callback failed\n");
        return 1;
    }
    if (!check_cd_driver_calls()) {
        fprintf(stderr, "launcher CD driver calls failed\n");
        return 1;
    }
    if (!check_cd_events()) {
        fprintf(stderr, "launcher CD event callbacks failed\n");
        return 1;
    }
    if (!check_cd_names()) {
        fprintf(stderr, "launcher CD name lookups failed\n");
        return 1;
    }
    if (!check_cd_setters()) {
        fprintf(stderr, "launcher CD state setters failed\n");
        return 1;
    }
    if (!check_test_parameter_count()) {
        fprintf(stderr, "launcher CD test parameter count failed\n");
        return 1;
    }
    if (!check_cd_volume()) {
        fprintf(stderr, "launcher CD volume failed\n");
        return 1;
    }
    const u32 reset_drivers[] = {0x80013840, 0x80013890, 0x8001374c};
    if (!check_cd_reset_modes(0x80011fd4, reset_cd_controller, reset_drivers)) {
        fprintf(stderr, "launcher CD reset failed\n");
        return 1;
    }
    if (!check_cd_flush_controller()) {
        fprintf(stderr, "launcher CD controller flush failed\n");
        return 1;
    }
    if (!check_cd_init_volume()) {
        fprintf(stderr, "launcher CD volume initialization failed\n");
        return 1;
    }
    if (!check_cd_positions()) {
        fprintf(stderr, "launcher CD position conversions failed\n");
        return 1;
    }
    if (!check_cd_state()) {
        fprintf(stderr, "launcher CD state accessors failed\n");
        return 1;
    }
    if (!check_byte_scans()) {
        fprintf(stderr, "launcher byte scans failed\n");
        return 1;
    }
    if (!check_global_callbacks()) {
        fprintf(stderr, "launcher global callbacks failed\n");
        return 1;
    }
    if (!check_bios_wrappers()) {
        fprintf(stderr, "launcher BIOS wrappers failed\n");
        return 1;
    }
    if (!check_start_interrupt(false) || !check_start_interrupt(true)) {
        fprintf(stderr, "launcher interrupt initialization failed\n");
        return 1;
    }
    if (!check_vblank_dispatch()) {
        fprintf(stderr, "launcher VBlank dispatch failed\n");
        return 1;
    }
    if (!check_dma_slots()) {
        fprintf(stderr, "launcher DMA callback slots failed\n");
        return 1;
    }
    if (!check_save_irq_context(0x800119bc, save_irq_context) ||
        !check_restore_jump_context(0x800119f8, restore_jump_context)) {
        fprintf(stderr, "launcher jump context failed\n");
        return 1;
    }
    if (!check_vsync_slots()) {
        fprintf(stderr, "launcher VSync callback slots failed\n");
        return 1;
    }
    if (!check_irq_wrappers()) {
        fprintf(stderr, "launcher callback wrappers failed\n");
        return 1;
    }
    if (!check_interrupt_mask()) {
        fprintf(stderr, "launcher interrupt mask failed\n");
        return 1;
    }
    for (unsigned restart = 0; restart < 2; restart++)
        for (unsigned active = 0; active < 2; active++)
            if (!check_interrupt_control(restart, active, 12) ||
                !check_interrupt_control(restart, active, 4096)) {
                fprintf(stderr, "launcher interrupt control failed (restart %u, active %u)\n", restart, active);
                return 1;
            }
    if (!check_irq_word_clear()) {
        fprintf(stderr, "launcher interrupt table clear failed\n");
        return 1;
    }
    if (!check_stack_switch()) {
        fprintf(stderr, "launcher stack switch failed\n");
        return 1;
    }
    if (!check_critical_sections()) {
        fprintf(stderr, "launcher critical sections failed\n");
        return 1;
    }
    for (unsigned selection = 0; selection < 6; selection++)
        for (unsigned missing = 0; missing < 2; missing++)
            if (!check_handoff(selection, missing)) {
                fprintf(stderr, "launcher selector %u, missing %u failed\n", selection, missing);
                return 1;
            }
    disc_close();
    return 0;
}
