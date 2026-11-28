#include "decomp/psx_spu.h"
#include "decomp/twins/psx_exe.h"
#include "port/disc.h"
#include "port/exe.h"
#include "spu_driver.h"
#include <stdio.h>

enum {
    REVERB_ON = 0x80019d2c,
    REVERB_OFF = 0x80021b08,
    SPU_INIT = 0x80021b28,
    SPU_START = 0x80021c20,
    DATA_CALLBACK = 0x80022858,
    SPU_QUIT = 0x8002287c,
    STARTED = 0x80027480,
    EVENT = 0x80027018,
};

static const SpuDriverFixture psx_spu = {
    .layout =
        {
            .init = 0x80021c9c,
            .write_by_io = 0x80021f30,
            .transfer = 0x80022288,
            .wait = 0x800227e8,
            .print = 0x8001a640,
            .write_label = 0x8001891c,
            .restore_label = 0x80018930,
            .registers = 0x80027484,
            .dma_address = 0x80027488,
            .dma_blocks = 0x8002748c,
            .dma_control = 0x80027490,
            .dma_priority = 0x80027494,
            .transfer_address = 0x8002749c,
            .address_shift = 0x800274ac,
            .initialized = 0x800274b8,
            .direction = 0x800274d4,
            .dma_count = 0x800274dc,
            .voice_state = 0x8002a248,
        },
    .delay_register = 0x80027498,
    .init = nc_credits_initialize_spu__psx_exe_80021C9C,
    .write_by_io = nc_credits_spu_write_by_io__psx_exe_80021F30,
    .transfer = nc_credits_spu_transfer_command__psx_exe_80022288,
};

/* Callee stubs log their first argument through t8 and return a per-stub value. */
static const u32 trace = 0x80061000;

static void stub(u32 entry, u16 result) {
    MEM_W32(entry, 0xaf040000);               /* sw a0,0(t8) */
    MEM_W32(entry + 4, 0x27180004);           /* addiu t8,t8,4 */
    MEM_W32(entry + 8, 0x03e00008);           /* jr ra */
    MEM_W32(entry + 12, 0x24020000 | result); /* addiu v0,zero,result */
}

static u32 logged(unsigned i) {
    return MEM_R32(trace + 4 * i);
}

/* Native run with the disc comparison when the original instructions are loaded. Both runs
 * start from the same CPU, RAM and budget; a budget of `threshold` must be declined. */
static bool run(u32 entry, bool (*routine)(CPUState *), CPUState start, int threshold, bool disc) {
    start.r[24] = trace;
    memset(nc_fast_ptr(trace), 0xcc, 64);
    if (disc)
        return compare(start, entry, routine, threshold);
    StateIO initial = {0}, declined = {0};
    nc_cpu = start;
    nc_cpu.budget = threshold;
    cpu_serialize(&initial);
    bool ok = !routine(&nc_cpu);
    cpu_serialize(&declined);
    ok = ok && !initial.error && !declined.error && initial.len == declined.len &&
         !memcmp(initial.buf, declined.buf, initial.len);
    free(initial.buf);
    free(declined.buf);
    nc_cpu = start;
    return ok && routine(&nc_cpu) && nc_cpu.r[29] == start.r[29] && nc_cpu.r[31] == start.r[31];
}

static bool check_wrappers(bool disc) {
    stub(0x8001d79c, 7);
    stub(SPU_INIT, 8);
    stub(0x8001bc60, 9); /* DMACallback */
    CPUState start = arguments(0x8001234, 2, 3, 4096);
    bool ok = run(REVERB_ON, nc_psx_reverb_on, start, 4, disc) && nc_cpu.r[4] == 1 &&
              logged(0) == 1 && nc_cpu.r[2] == 7;
    ok = ok && run(REVERB_OFF, nc_psx_reverb_off, start, 4, disc) && nc_cpu.r[4] == 0 &&
         logged(0) == 0 && nc_cpu.r[2] == 8;
    ok = ok && run(DATA_CALLBACK, nc_psx_spu_data_callback, start, 5, disc) && nc_cpu.r[4] == 4 &&
         nc_cpu.r[5] == 0x8001234 && logged(0) == 4 && nc_cpu.r[2] == 9;
    return ok;
}

static void stub_events(void) {
    stub(0x8001b7a4, 1);    /* EnterCriticalSection */
    stub(0x8001b7b4, 2);    /* ExitCriticalSection */
    stub(0x8001b744, 0x77); /* OpenEvent */
    stub(0x8001b754, 3);    /* CloseEvent */
    stub(0x8001b774, 4);    /* EnableEvent */
    stub(0x8001b784, 5);    /* DisableEvent */
    stub(DATA_CALLBACK, 6);
}

static bool check_start_quit(bool disc) {
    stub_events();
    bool ok = true;
    for (u32 state = 0; ok && state < 3; state++) {
        MEM_W32(STARTED, state);
        MEM_W32(EVENT, 0x1234);
        MEM_W32(0x800274bc, 0xaaaa);
        MEM_W32(0x800274c0, 0xbbbb);
        ok = run(SPU_START, nc_psx_spu_start, arguments(1, 2, 3, 4096), 7, disc);
        if (state == 0)
            ok = ok && MEM_R32(STARTED) == 1 && MEM_R32(EVENT) == 0x77 && logged(1) == 0x800220fc &&
                 logged(2) == 0xf0000009 && logged(3) == 0x77 && nc_cpu.r[2] == 2 &&
                 nc_cpu.r[5] == 0x20 && nc_cpu.r[6] == 0x2000 && nc_cpu.r[7] == 0;
        else
            ok = ok && MEM_R32(STARTED) == state && MEM_R32(EVENT) == 0x1234 &&
                 logged(0) == 0xcccccccc && nc_cpu.r[2] == state;

        MEM_W32(STARTED, state);
        MEM_W32(EVENT, 0x1234);
        ok = ok && run(SPU_QUIT, nc_psx_spu_quit, arguments(1, 2, 3, 4096), 8, disc);
        if (state == 1)
            ok = ok && MEM_R32(STARTED) == 0 && MEM_R32(0x800274bc) == 0 &&
                 MEM_R32(0x800274c0) == 0 && logged(1) == 0 && logged(2) == 0x1234 &&
                 logged(3) == 0x1234 && nc_cpu.r[2] == 2;
        else
            ok = ok && MEM_R32(STARTED) == state && MEM_R32(0x800274bc) == 0xaaaa &&
                 logged(0) == 0xcccccccc && nc_cpu.r[2] == 1 && nc_cpu.r[3] == state;
    }
    return ok;
}

static bool check_init(bool disc) {
    stub(0x8001bc00, 1); /* ResetCallback */
    stub(0x80021c9c, 2); /* _spu_init */
    stub(SPU_START, 3);
    stub(0x80022608, 4); /* _spu_FsetRXX */
    const u32 cleared[] = {0x80027024, 0x80027028, 0x80027034, 0x80027038, 0x8002703c,
                           0x80027040, 0x800274e0, 0x800274e4, 0x800274e8, 0x80027020,
                           0x800274a0, 0x8002701c, 0x80027048, 0x80027044, 0x8002747c};
    bool ok = true;
    for (u32 mode = 0; ok && mode < 3; mode++) {
        memset(nc_fast_ptr(0x80027048), 0xdd, 0x36); /* the voice table and its neighbours */
        for (unsigned i = 0; i < sizeof cleared / sizeof cleared[0]; i++)
            MEM_W32(cleared[i], 0xeeeeeeee);
        MEM_W32(0x800274ec, 0x5a5a + mode);
        u32 argument = mode == 2 ? 0x80000000u : mode;
        ok = run(SPU_INIT, nc_psx_spu_init, arguments(argument, 2, 3, 4096), 5, disc) &&
             nc_cpu.r[2] == 4 && nc_cpu.r[4] == 0xd1 && nc_cpu.r[5] == 0x5a5a + mode &&
             nc_cpu.r[6] == 0 && MEM_R32(0x8002702c) == 0x5a5a + mode && logged(1) == argument &&
             logged(3) == 0xd1;
        for (unsigned i = 0; ok && i < sizeof cleared / sizeof cleared[0]; i++)
            ok = MEM_R32(cleared[i]) == 0;
        for (unsigned i = 0; ok && i < 24; i++)
            ok = MEM_R16(0x8002707a - 2 * i) == (mode ? 0xdddd : 0xc000);
        ok = ok && MEM_R16(0x8002707c) == 0xdddd;
    }
    return ok;
}

int main(int argc, char **argv) {
    if (argc > 2)
        return 1;
    bool disc = argc == 2;
    if (disc) {
        FILE *file = fopen(argv[1], "rb");
        if (!file)
            return 77;
        fclose(file);
        ExecInfo exe;
        if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX.EXE;1", &exe))
            return 1;
    }
    static const struct {
        bool (*check)(bool);
        const char *name;
    } checks[] = {
        {check_wrappers, "libspu wrappers"},
        {check_start_quit, "SpuStart/SpuQuit"},
        {check_init, "_SpuInit"},
    };
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE); /* the stubs are interpreted */
    bool ok = true;
    for (unsigned i = 0; ok && i < sizeof checks / sizeof checks[0]; i++) {
        StateIO saved = {0};
        cpu_serialize(&saved);
        ok = checks[i].check(disc);
        if (!ok)
            fprintf(stderr, "psx %s failed\n", checks[i].name);
        saved.loading = true;
        cpu_serialize(&saved); /* restore the callees the stubs replaced */
        ok = ok && !saved.error;
        free(saved.buf);
    }
    nc_set_module(module);
    if (ok && disc) {
        ok = check_spu_initialization(&psx_spu) && check_spu_fifo_write(&psx_spu) &&
             check_spu_transfer_command(&psx_spu);
        if (!ok)
            fprintf(stderr, "psx SPU driver failed\n");
    }
    if (disc)
        disc_close();
    return ok ? 0 : 1;
}
