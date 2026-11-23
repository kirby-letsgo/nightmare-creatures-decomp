#include "decomp/cd_interrupt_callback.h"
#include "decomp/cd_read.h"
#include "decomp/cd_read_sync.h"
#include "decomp/vsync.h"
#include "decomp/vblank_wait.h"
#include "decomp/cd_init.h"
#include "decomp/cd_state.h"
#include "decomp/interrupt_control.h"
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

/* The SDK indexes its character-class table with the low byte of the argument. */
bool nc_launcher_convert_case(CPUState *c, bool upper) {
    if (c->budget <= NC_LOAD_CYCLES + 11)
        return false;
    R(3) = R(4);
    R(1) = 0x80010000u + (R(4) & 0xff);
    c->budget -= 4;
    u32 flags = MEM_R8(R(1) + 0x56bd);
    c->budget -= NC_LOAD_CYCLES + 4;
    if (flags & (upper ? 2u : 1u)) {
        R(3) = upper ? R(4) - 32 : R(4) + 32;
        c->budget--;
    }
    R(2) = R(3) & 0xff;
    c->budget -= 2;
    return true;
}

/* Return the previous stack pointer and install the caller's stack. */
bool nc_launcher_switch_stack(CPUState *c) {
    if (c->budget <= 3)
        return false;
    R(2) = R(29);
    R(29) = R(4);
    c->budget -= 3;
    return true;
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

static void load_record_pointer(CPUState *c, unsigned reg) {
    R(reg) = MEM_R32(R(28));
    NC_POLL(c, NC_LOAD_CYCLES);
}

static void set_record_byte(CPUState *c, u32 offset, u8 value) {
    load_record_pointer(c, 3);
    R(2) = value;
    NC_POLL(c, 1);
    MEM_W8(R(3) + offset, (u8)R(2));
    NC_POLL(c, 1);
}

/* Initialize the shared record before the PSX.EXE handoff. The guest
 * reloads its pointer between byte stores and leaves several gaps intact. */
bool nc_launcher_init_shared_record(CPUState *c) {
    if (c->budget <= 1)
        return false;
    load_record_pointer(c, 2);
    NC_POLL(c, 1);
    MEM_W8(R(2) + 0x22, 0);
    NC_POLL(c, 1);
    load_record_pointer(c, 3);
    R(4) = 10;
    NC_POLL(c, 1);
    MEM_W32(R(2), 0);
    NC_POLL(c, 1);
    MEM_W32(R(2) + 4, 0);
    NC_POLL(c, 1);
    R(2) = 0x80;
    NC_POLL(c, 1);
    MEM_W16(R(3) + 0x0c, 0);
    NC_POLL(c, 1);
    const unsigned offsets[] = {0x08, 0x0a, 0x10, 0x16};
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
        MEM_W16(R(3) + offsets[i], (u16)R(2));
        NC_POLL(c, 1);
    }
    MEM_W8(R(3) + 0x18, (u8)R(4));
    NC_POLL(c, 1);
    set_record_byte(c, 0x19, 5);
    set_record_byte(c, 0x1a, 0x4d);
    set_record_byte(c, 0x1e, 0x13);
    set_record_byte(c, 0x1b, 0x4c);
    load_record_pointer(c, 2);
    NC_POLL(c, 1);
    MEM_W8(R(2) + 0x1f, (u8)R(4));
    NC_POLL(c, 1);
    set_record_byte(c, 0x1c, 0xb2);
    load_record_pointer(c, 2);
    R(4) = 0xec;
    NC_POLL(c, 1);
    MEM_W8(R(2) + 0x20, (u8)R(4));
    NC_POLL(c, 1);
    set_record_byte(c, 0x1d, 0xb1);
    load_record_pointer(c, 2);
    NC_POLL(c, 1);
    MEM_W8(R(2) + 0x21, (u8)R(4));
    NC_POLL(c, 1);
    load_record_pointer(c, 4);
    NC_POLL(c, 1);
    R(2) = MEM_R32(R(4) + 4);
    NC_POLL(c, NC_LOAD_CYCLES);
    R(3) = 0x08000000;
    NC_POLL(c, 1);
    MEM_W32(R(4) + 0x24, 0); /* first module selector */
    NC_POLL(c, 1);
    R(2) |= R(3);
    NC_POLL(c, 1);
    MEM_W32(R(4) + 4, R(2));
    NC_POLL(c, 2);
    return true;
}

/* The linker encoded a zero callback count in both SN runtime tables.
 * __main sets the guard on first use. The destructor entry only reads it. */
bool nc_launcher_global_callbacks(CPUState *c, bool constructors) {
    if (c->budget <= 30)
        return false;
    R(8) = MEM_R32(0x800145f4);
    R(29) -= 16;
    MEM_W32(R(29) + 4, R(16));
    MEM_W32(R(29) + 8, R(17));
    MEM_W32(R(29) + 12, R(31));
    bool initialized = R(8) != 0;
    int cycles = 22;
    if (constructors)
        R(8) = 1;
    if (constructors != initialized) {
        if (constructors) {
            R(1) = 0x80010000;
            MEM_W32(0x800145f4, R(8));
            cycles += 2;
        }
        R(16) = 0x80010000;
        R(17) = 0;
        cycles += 6;
    }
    R(31) = MEM_R32(R(29) + 12);
    R(17) = MEM_R32(R(29) + 8);
    R(16) = MEM_R32(R(29) + 4);
    R(29) += 16;
    c->budget -= cycles;
    return true;
}

static const InterruptControlLayout interrupt_control = {
    .state = 0x80014620,
    .ports = 0x800156ac,
    .stop = 0x80011000,
    .restart = 0x800110ac,
    .enter_critical = 0x80010a30,
    .exit_critical = 0x80011a6c,
    .reset_hook = 0x80011a4c,
    .install_hook = 0x80011a5c,
};

bool nc_launcher_stop_interrupts(CPUState *c) {
    return stop_driver_interrupts(c, &interrupt_control);
}

bool nc_launcher_restart_interrupts(CPUState *c) {
    return restart_driver_interrupts(c, &interrupt_control);
}

bool nc_launcher_start_dma(CPUState *c) {
    static const InterruptInitLayout layout = {
        .slots = 0x8001576c, .entry = 0x80011bd4, .clear = 0x80011e50,
        .register_irq = 0x80010a80, .handler = 0x80011c24, .setter = 0x80011da8,
    };
    return start_dma(c, &layout);
}

bool nc_launcher_start_vsync(CPUState *c) {
    static const InterruptInitLayout layout = {
        .slots = 0x80015740, .entry = 0x80011aac, .clear = 0x80011ba8,
        .register_irq = 0x80010a80, .handler = 0x80011b04, .setter = 0x80011b7c,
    };
    return start_vsync(c, &layout);
}

bool nc_launcher_dispatch_vblank(CPUState *c) {
    return dispatch_vblank(c, 0x80015740, 0x80011b54);
}

bool nc_launcher_clear_irq_words(CPUState *c) {
    return clear_interrupt_words(c);
}

bool nc_launcher_cd_init_interrupt(CPUState *c) {
    return cd_init_interrupt(c, 0x80015818, 0x80013840, 0x80010a50, 0x80010a80, 0x80013cf8);
}

bool nc_launcher_cd_init(CPUState *c) {
    static const CdInitLayout layout = {
        .entry = 0x80011e7c, .reset = 0x80011fd4,
        .sync_handler = 0x80011f0c, .set_sync = 0x80012120,
        .ready_handler = 0x80011f34, .set_ready = 0x80012138,
        .read_handler = 0x80011f5c, .set_read = 0x800145dc,
        .message = 0x8001034c, .print = 0x80011160,
    };
    return cd_init(c, &layout);
}

bool nc_launcher_wait_vblank(CPUState *c) {
    static const VBlankWaitLayout layout = {
        .entry = 0x80013f70, .frame = 0x80015760, .message = 0x800105c4,
        .puts_entry = 0x80013dd8, .clear_pad = 0x80011a8c, .clear_rcnt = 0x80011a9c,
    };
    return wait_vblank(c, &layout);
}

bool nc_launcher_vsync(CPUState *c) {
    return sdk_vsync(c, 0x80015b34, 0x80015760, 0x80013e28, 0x80013f70);
}

bool nc_launcher_cd_read_sync(CPUState *c) {
    static const CdReadSyncLayout layout = {
        .entry = 0x80014510, .start_frame = 0x80015b64,
        .vsync = 0x80013e28, .retry = 0x80014230, .ready = 0x80012100,
    };
    return cd_read_sync(c, &layout);
}

bool nc_launcher_cd_read(CPUState *c) {
    static const CdReadLayout layout = {
        .entry = 0x80014408, .state = 0x80015b48,
        .set_sync = 0x80012120, .set_ready = 0x80012138,
        .vsync = 0x80013e28, .status = 0x80011f94,
        .control = 0x800123b4, .retry = 0x80014230,
    };
    return cd_read(c, &layout);
}

bool nc_launcher_cd_interrupt_callback(CPUState *c) {
    static const CdCallbackLayout layout = {
        .entry = 0x80013cf8, .getintr = 0x80012708, .ports = 0x80015adc,
        .callbacks = 0x80015818, .results = 0x80015b80,
    };
    return cd_interrupt_callback(c, &layout);
}
