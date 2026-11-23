#include "decomp/launcher.h"

static void call_launcher_service(CPUState *c, u32 address, u32 resume) {
    R(31) = resume;
    NC_POLL(c, 2);
    nc_call(c, address);
}

/* Each switch arm calls _96_init before selecting its ISO path. The
 * credits arm has its own Load call and falls through on success. */
static void load_launcher_module(CPUState *c, u32 arm, u32 path) {
    bool credits = arm == 0x80010710;
    call_launcher_service(c, 0x80010a00, arm + 8);
    R(4) = 0x80010000;
    NC_POLL(c, 1);
    R(4) += path;
    NC_POLL(c, 1);
    if (!credits && arm != 0x80010688)
        NC_POLL(c, 2);
    R(31) = credits ? 0x80010728 : 0x800106a0;
    R(5) = R(16);
    NC_POLL(c, 2);
    nc_call(c, 0x800109e0); /* Load(path, exec_header) */
    bool loaded = R(2) != 0;
    NC_POLL(c, 2);
    if (!loaded) {
        call_launcher_service(c, 0x80010a18, 0x80010770); /* _96_remove */
    } else {
        if (!credits)
            NC_POLL(c, 2);
        call_launcher_service(c, 0x80010a18, 0x80010738);
        call_launcher_service(c, 0x80010a30, 0x80010740); /* enter critical section */
        R(4) = R(16);
        NC_POLL(c, 1);
        R(5) = 0;
        NC_POLL(c, 1);
        R(31) = 0x80010750;
        R(6) = 0;
        NC_POLL(c, 2);
        nc_call(c, 0x800109f0); /* Exec(exec_header, 0, 0) */
        call_launcher_service(c, 0x80010a50, 0x80010758); /* ResetCallback */
        call_launcher_service(c, 0x80010b44, 0x80010760); /* StopCallback */
    }
    NC_POLL(c, 2);
}

/* Resident launcher main. Returning overlays select the next executable by
 * writing its index into the shared record at offset 0x24. */
bool nc_launcher_run_modules(CPUState *c) {
    if (c->budget <= 1)
        return false;
    R(29) -= 0x18;
    NC_POLL(c, 1);
    MEM_W32(R(29) + 0x14, R(31));
    NC_POLL(c, 1);
    R(31) = 0x80010628;
    MEM_W32(R(29) + 0x10, R(16));
    NC_POLL(c, 2);
    nc_call(c, 0x800108f8); /* __main */
    R(4) = 0x801f0000;
    NC_POLL(c, 1);
    R(31) = 0x80010634;
    R(4) |= 0xff00;
    NC_POLL(c, 2);
    nc_call(c, 0x80010a40); /* switch stack */
    call_launcher_service(c, 0x80010a50, 0x8001063c);
    call_launcher_service(c, 0x80011e7c, 0x80010644); /* CdInit */
    call_launcher_service(c, 0x8001078c, 0x8001064c); /* initialize shared record */
    R(16) = 0x80010000;
    NC_POLL(c, 1);
    R(16) += 0x5ba4;
    NC_POLL(c, 1);
    for (;;) {
        R(2) = MEM_R32(R(28));
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        R(3) = MEM_R32(R(2) + 0x24);
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        R(2) = R(3) < 6;
        NC_POLL(c, 1);
        bool valid = R(2) != 0;
        R(2) = R(3) << 2;
        NC_POLL(c, 2);
        if (!valid)
            continue;
        R(1) = 0x80010000;
        NC_POLL(c, 1);
        R(1) += R(2);
        NC_POLL(c, 1);
        R(2) = MEM_R32(R(1) + 0x88);
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        u32 arm = R(2);
        NC_POLL(c, 2);
        u32 path;
        switch (arm) {
        case 0x80010688: path = 0x00; break; /* PSX */
        case 0x800106b0: path = 0x14; break; /* STREAM1 */
        case 0x800106c8: path = 0x2c; break; /* PSX2 */
        case 0x800106e0: path = 0x40; break; /* STREAM2 */
        case 0x800106f8: path = 0x58; break; /* STREAM3 */
        case 0x80010710: path = 0x70; break; /* CREDITS */
        case 0x80010778: /* epilogue after the retry loop */
            R(31) = MEM_R32(R(29) + 0x14);
            NC_POLL(c, NC_LOAD_CYCLES);
            R(16) = MEM_R32(R(29) + 0x10);
            NC_POLL(c, NC_LOAD_CYCLES);
            R(29) += 0x18;
            NC_POLL(c, 1);
            NC_POLL(c, 2);
            return true;
        default:
            nc_jump(c, arm);
            return true;
        }
        load_launcher_module(c, arm, path);
    }
}
