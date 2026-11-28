#include "decomp/psx_intr.h"
#include "cd_irq.h"
#include "interrupt_dispatch.h"
#include "interrupt_start.h"
#include "port/bios.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/hw/hw.h"
#include <stdio.h>

enum {
    STATE = 0x80025da0,
    PORTS = 0x80026e2c,
    START = 0x8001bd98,
    RESUME = 0x8001be00,
    TRAP = 0x8001be74,
    SET_HANDLER = 0x8001c05c,
    STOP = 0x8001c1b0,
    RESTART = 0x8001c25c,
    GETINTR = 0x80023174,
};

static const CdEventFixture psx_events = {
    .getintr = GETINTR,
    .puts = 0x8001bba0,
    .print = 0x8001a640,
    .ports = 0x80027b30,
    .callbacks = 0x8002786c,
    .results = 0x80027e24,
    .status = 0x80027b48,
};

static const struct {
    u32 entry;
    bool (*routine)(CPUState *);
    int threshold; /* the largest budget the routine declines */
} routines[] = {
    {START, nc_psx_start_interrupts, 8 * NC_INSN_CYCLES + NC_LOAD_CYCLES},
    {SET_HANDLER, nc_psx_set_interrupt_handler, 16 * NC_INSN_CYCLES + NC_LOAD_CYCLES},
    {STOP, nc_psx_stop_interrupts, 11},
    {RESTART, nc_psx_restart_interrupts, 11},
    {GETINTR, nc_psx_get_cd_interrupt, 1},
};

/* A declined call leaves the CPU, RAM and the cycle budget untouched. */
static bool declines(bool (*routine)(CPUState *), int threshold) {
    StateIO before = {0}, after = {0};
    nc_cpu = arguments(1, 2, 3, threshold);
    cpu_serialize(&before);
    bool ok = !routine(&nc_cpu);
    cpu_serialize(&after);
    ok = ok && !before.error && !after.error && before.len == after.len &&
         !memcmp(before.buf, after.buf, before.len);
    free(before.buf);
    free(after.buf);
    return ok;
}

/* Without the disc: the budget declines and the driver-state early returns. */
static bool check_rules(void) {
    for (unsigned i = 0; i < sizeof routines / sizeof routines[0]; i++)
        if (!declines(routines[i].routine, routines[i].threshold))
            return false;
    /* startIntr and restartIntr return 0 while the driver runs, stopIntr while it is off.
     * The stack frame is restored and the cycle count matches the instructions run. */
    const struct {
        bool (*routine)(CPUState *);
        u16 state;
        int cycles;
    } early[] = {
        /* Eight instructions and a load to the branch, then two loads and three more. */
        {nc_psx_start_interrupts, 1, 11 + 9},
        {nc_psx_restart_interrupts, 1, 11 + 1 + 9}, /* restartIntr also clears v0 */
        {nc_psx_stop_interrupts, 0, 11 + 9},
    };
    for (unsigned i = 0; i < sizeof early / sizeof early[0]; i++) {
        MEM_W16(STATE, early[i].state);
        CPUState start = arguments(1, 2, 3, 4096);
        nc_cpu = start;
        if (!early[i].routine(&nc_cpu) || nc_cpu.r[2] != 0 || nc_cpu.r[29] != start.r[29] ||
            nc_cpu.r[31] != start.r[31] || nc_cpu.r[16] != start.r[16] ||
            nc_cpu.budget != start.budget - early[i].cycles) {
            fprintf(stderr, "psx interrupt early return %u failed\n", i);
            return false;
        }
    }
    return true;
}

/* The setjmp return site of startIntr, entered with v0 = 0 as setjmp itself returns. */
static bool check_resume(void) {
    StateIO saved = {0};
    interrupt_start_state(&saved);
    bios_serialize(&saved);
    bool ok = !saved.error;
    for (unsigned tight = 0; ok && tight < 2; tight++) {
        StateIO native = {0}, guest = {0};
        for (unsigned run = 0; ok && run < 2; run++) {
            nc_cpu = arguments(1, 2, 3, tight ? 3 : 4096);
            nc_cpu.r[2] = 0;
            nc_cpu.r[16] = STATE;
            nc_cpu.r[29] -= 24;
            MEM_W32(nc_cpu.r[29] + 16, 0xcafe);
            MEM_W32(nc_cpu.r[29] + 20, 0x80010000);
            if (run) /* return to the caller of startIntr, with its stack pointer */
                interp_continue_call(&nc_cpu, RESUME, 0x80010000, nc_cpu.r[29] + 24);
            else
                ok = nc_psx_resume_interrupt_start(&nc_cpu);
            ok = ok && nc_cpu.r[2] == STATE && nc_cpu.r[16] == 0xcafe && MEM_R16(STATE) == 1;
            StateIO *result = run ? &guest : &native;
            interrupt_start_state(result);
            bios_serialize(result);
            saved.loading = true;
            saved.pos = 0;
            interrupt_start_state(&saved);
            bios_serialize(&saved);
        }
        ok = ok && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    free(saved.buf);
    return ok && !saved.error;
}

int main(int argc, char **argv) {
    if (argc > 2)
        return 1;
    if (argc == 1)
        return check_rules() ? 0 : 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 77;
    fclose(file);
    ExecInfo exe;
    if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX.EXE;1", &exe))
        return 1;
    if (!check_rules()) {
        fprintf(stderr, "psx interrupt rules failed\n");
        return 1;
    }
    if (!compare_interrupt_start(START, nc_psx_start_interrupts, STATE, PORTS - 4, 0x8001c4b0,
                                 0x8001c6dc)) {
        fprintf(stderr, "psx startIntr failed\n");
        return 1;
    }
    if (!check_resume()) {
        fprintf(stderr, "psx startIntr setjmp return failed\n");
        return 1;
    }
    if (!compare_set_interrupt_handler(SET_HANDLER, nc_psx_set_interrupt_handler, STATE)) {
        fprintf(stderr, "psx setIntr failed\n");
        return 1;
    }
    if (!compare_interrupt_dispatch(TRAP, nc_psx_dispatch_interrupts, STATE, PORTS, 0x8001a640,
                                    0x8001c3b0, 0x8001845c, 0x80018478)) {
        fprintf(stderr, "psx trapIntr failed\n");
        return 1;
    }
    for (unsigned stop = 0; stop < 2; stop++)
        if (!compare_interrupt_control(stop, stop ? STOP : RESTART,
                                       stop ? nc_psx_stop_interrupts : nc_psx_restart_interrupts,
                                       STATE)) {
            fprintf(stderr, "psx %s failed\n", stop ? "stopIntr" : "restartIntr");
            return 1;
        }
    if (!check_get_cd_interrupt(&psx_events, nc_psx_get_cd_interrupt) ||
        !check_cd_interrupt_fifo(&psx_events, nc_psx_get_cd_interrupt)) {
        fprintf(stderr, "psx getintr failed\n");
        return 1;
    }
    disc_close();
    return 0;
}
