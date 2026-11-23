#include "credits_compare.h"
#include "decomp/bios_service.h"
#include "decomp/launcher.h"
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

static u32 a0_service;

static bool call_a0_wrapper(CPUState *c) {
    return bios_service(c, 0xa0, a0_service);
}

static bool check_a0_wrappers(void) {
    const u32 header = 0x80016200, probe = 0x80016300, trace = probe + 64;
    const struct { u32 entry, service, first_arg, result; } cases[] = {
        {0x800109d0, 0x39, header, 0},
        {0x800109e0, 0x42, 0x80010000, 1}, /* original PSX.EXE path */
        {0x800109e0, 0x42, trace, 0},      /* empty path */
        {0x800109f0, 0x43, header, 1},
        {0x80010a00, 0x71, 0, 0},
        {0x80010a18, 0x72, 0, 0},
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
        a0_service = cases[i].service;
        MEM_W32(trace, 0);
        if (a0_service == 0x43) {
            ExecInfo info = {.pc0 = probe};
            exe_write_info(&info, header);
            MEM_W32(probe, 0xae040000); /* sw a0, 0(s0) */
            MEM_W32(probe + 4, 0x03e00008);
            MEM_W32(probe + 8, 0xae050004); /* sw a1, 4(s0) */
        }
        if (!compare(start, cases[i].entry, call_a0_wrapper, 3) ||
            nc_cpu.r[2] != cases[i].result || nc_cpu.r[9] != a0_service ||
            nc_cpu.r[10] != 0xa0)
            return false;
        if (a0_service == 0x42 && cases[i].result && MEM_R32(header) != 0x80025c70)
            return false;
        if (a0_service == 0x43 && (MEM_R32(trace) != header || MEM_R32(trace + 4) != 17))
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
    if (!check_global_callbacks()) {
        fprintf(stderr, "launcher global callbacks failed\n");
        return 1;
    }
    if (!check_a0_wrappers()) {
        fprintf(stderr, "launcher A0 wrappers failed\n");
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
    for (unsigned selection = 0; selection < 6; selection++)
        for (unsigned missing = 0; missing < 2; missing++)
            if (!check_handoff(selection, missing)) {
                fprintf(stderr, "launcher selector %u, missing %u failed\n", selection, missing);
                return 1;
            }
    disc_close();
    return 0;
}
