/* PSX.EXE, the front end. It initializes the system, checks memory card port 1 and shows
 * CARDWARN.TIM when no save block would be available, then shuts down and asks the launcher
 * for the next module. The memory card, display, heap and file helpers are shared with other
 * executables through their headers. */
#include "decomp/psx_front.h"
#include "decomp/card_events.h"
#include "decomp/cd_load_file.h"
#include "decomp/display_buffers.h"
#include "decomp/heap.h"
#include "decomp/sdk_geom.h"
#include "decomp/sdk_start.h"
#include "decomp/shared_record.h"

enum {
    GP = 0x80027be0,
    SETTINGS_SLOT = 0x30, /* gp offset of the record shared with the launcher */

    /* Front-end routines. */
    CARD_OPEN_EVENTS = 0x80018de4,
    CARD_CLOSE_EVENTS = 0x80018f64,
    CARD_PROBE = 0x80019234,
    CARD_FILE_EXISTS = 0x800193e4,
    CARD_FREE_BLOCKS = 0x8001943c,
    FRONT_END = 0x8001952c,
    MAIN = 0x800196cc,
    READ_SECTORS = 0x800196f4,
    LOAD_FILE = 0x800197b0,
    SHOW_TIM_SCREEN = 0x8001986c,
    SKIP_INPUT = 0x80019938,
    INIT_PAD = 0x800199a8,
    STOP_PAD = 0x800199f8,
    SETUP_DISPLAY = 0x80019b40,
    CLEAR_DISPLAY = 0x80019c4c,
    LOAD_SCREEN_TIM = 0x80019d4c,
    INIT_HEAP = 0x80019edc,
    ALLOCATE = 0x8001a108,
    FREE = 0x8001a268,
    COUNT_VBLANK = 0x8001a3b0,
    INIT_GEOM = 0x800219b0,
    EMPTY_HOOK = 0x80025cd8,

    /* Library routines. */
    MEMCPY = 0x8001a508,
    MEMSET = 0x8001a53c,
    STRCAT = 0x8001a568,
    PRINTF = 0x8001a640,
    SPRINTF = 0x8001ae6c,
    OPEN = 0x8001b7c4,
    READ = 0x8001b7d4,
    CLOSE = 0x8001b7f4,
    FIRST_FILE = 0x8001b804,
    NEXT_FILE = 0x8001b814,
    VSYNC = 0x8001b9bc,
    RESET_CALLBACK = 0x8001bc00,
    VSYNC_CALLBACK = 0x8001bc90,
    STOP_CALLBACK = 0x8001bcf4,
    RESET_GRAPH = 0x8001d398,
    SET_GRAPH_DEBUG = 0x8001d608,
    SET_DISP_MASK = 0x8001d79c,
    DRAW_SYNC = 0x8001d838,
    LOAD_IMAGE = 0x8001dafc,
    PUT_DISP_ENV = 0x8001e084,
    OPEN_TIM = 0x8002020c,
    READ_TIM = 0x8002021c,
    REVERB_OFF = 0x80021b08,
    SPU_QUIT = 0x8002287c,
    CD_INIT = 0x800228f8,
    CD_SEARCH_FILE = 0x80024844,

    /* Data. */
    CARD_PATH = 0x80018000,     /* "bu%02x:%s" */
    CARD_PATTERN = 0x8001800c,  /* "bu%02x:*" */
    SAVE_NAME = 0x80018018,     /* the game's save file */
    WARNING_TIM = 0x80018028,   /* CARDWARN.TIM */
    MODULE_SUFFIX = 0x80018038, /* appended to the module name built on the stack */
    BUFFERS = 0x8002a25c,
    HEAP_START = 0x80027bd8,
    HEAP_SIZE = 0x80027bdc,
};

static const CardEventLayout psx_card = {
    .events = {0x84, 0x88, 0x8c, 0x90, 0x94, 0x98, 0x9c, 0xa0},
    .open_events = CARD_OPEN_EVENTS,
    .close_events = CARD_CLOSE_EVENTS,
    .test_all = 0x8001905c,
    .test_sw = 0x800190d4,
    .test_hw = 0x8001911c,
    .wait_sw = 0x80019164,
    .wait_hw = 0x800191cc,
    .probe = CARD_PROBE,
    .formatted = 0x80019354,
    .file_exists = CARD_FILE_EXISTS,
    .enter_critical = 0x8001b7a4,
    .exit_critical = 0x8001b7b4,
    .open_event = 0x8001b744,
    .close_event = 0x8001b754,
    .test_event = 0x8001b764,
    .enable_event = 0x8001b774,
    .disable_event = 0x8001b784,
    .init_card = 0x80025964,
    .start_card = 0x800259b8,
    .stop_card = 0x800259f0,
    .bu_init = 0x8001b734,
    .card_info = 0x800258dc,
    .card_clear = 0x8002591c,
    .card_load = 0x800258ec,
    .card_read = 0x800258fc,
    .new_card = 0x8002590c,
    .memset = MEMSET,
    .sprintf = SPRINTF,
    .open = OPEN,
    .close = CLOSE,
    .path_format = CARD_PATH,
};

static const DisplayLayout psx_display = {
    .buffer = BUFFERS,
    .settings_slot = SETTINGS_SLOT,
    .init_buffers = 0x80019a18,
    .setup = SETUP_DISPLAY,
    .apply_offset = 0x80019c04,
    .clear = CLEAR_DISPLAY,
    .set_geom_offset = 0x80021a38,
    .set_geom_screen = 0x80021a58,
    .set_def_draw_env = 0x8001c96c,
    .set_def_disp_env = 0x8001ca3c,
    .draw_sync = DRAW_SYNC,
    .clear_otag_r = 0x8001dd40,
    .clear_image = 0x8001d9cc,
};

static const HeapLayout psx_heap = {
    .count = 0x10,
    .used = 0x14,
    .available = 0x18,
    .head = 0x80,
    .init = INIT_HEAP,
    .resize = 0x80019f24,
    .allocate = ALLOCATE,
    .release = FREE,
    .find = 0x8001a338,
    .size = 0x8001a374,
    .fill = MEMSET,
    .copy = MEMCPY,
    .print = PRINTF,
    .out_of_memory = 0x800180f4,
};

/* --- startup and the front-end flow -------------------------------------------------------- */

bool nc_psx_start(CPUState *c) {
    static const StartLayout layout = {
        .memory_size = 0x80027bbc,
        .stack_size = 0x80027bb8,
        .heap_start = 0x80053cbc,
        .heap_size = 0x80027bc4,
        .heap_base = 0x80027bc0,
        .gp = GP,
        .main = MAIN,
    };
    return sdk_start(c, &layout);
}

bool nc_psx_main(CPUState *c) {
    if (!card_enter_frame(c, EMPTY_HOOK, MAIN + 0x10))
        return false;
    guest_call(c, FRONT_END, MAIN + 0x18);
    guest_leave_ra(c, 0x18, 0x10);
    return true;
}

/* Count the save blocks still free on the card in port 1, counting the game's own save as free,
 * and show the warning screen when there are none. */
static void check_card(CPUState *c) {
    enum { BLOCKS = 16 };
    R(4) = 0;
    guest_call(c, CARD_PROBE, FRONT_END + 0xb8);
    set_reg(c, 3, R(2) & 0xff);
    set_reg(c, 2, 1);
    bool ready = R(3) == R(2);
    R(2) = 4;
    if (!guest_branch(c, ready)) {
        const bool other = R(3) != R(2); /* not a newly inserted card either */
        R(2) = R(BLOCKS) & 0xff;
        ready = !guest_branch(c, other);
    }
    if (ready) {
        R(4) = 0;
        guest_call(c, CARD_FREE_BLOCKS, FRONT_END + 0xd8);
        set_reg(c, BLOCKS, R(2));
        set_address(c, 5, SAVE_NAME);
        R(4) = 0;
        guest_call(c, CARD_FILE_EXISTS, FRONT_END + 0xec);
        set_reg(c, 2, R(2) & 0xff);
        const bool missing = R(2) == 0;
        R(2) = R(BLOCKS) & 0xff;
        if (!guest_branch(c, missing)) {
            set_reg(c, BLOCKS, R(BLOCKS) + 1);
            set_reg(c, 2, R(BLOCKS) & 0xff);
        }
    }
    if (!guest_branch(c, R(2) != 0)) {
        R(4) = 0;
        guest_call(c, SET_DISP_MASK, FRONT_END + 0x110);
        set_reg(c, 2, 1);
        store_word(c, R(29) + 0x10, R(2)); /* skippable */
        set_address(c, 4, WARNING_TIM);
        set_reg(c, 5, 750); /* frames */
        set_reg(c, 6, 640);
        R(7) = 256;
        guest_call(c, SHOW_TIM_SCREEN, FRONT_END + 0x130);
    }
}

bool nc_psx_front_end(CPUState *c) {
    if (c->budget <= 5)
        return false;
    R(29) -= 0x68;
    MEM_W32(R(29) + 0x60, R(16));
    MEM_W32(R(29) + 0x64, R(31));
    R(31) = FRONT_END + 0x14;
    R(16) = 15; /* free save blocks on an empty card */
    c->budget -= 5;
    nc_call(c, RESET_CALLBACK);
    R(4) = 0;
    guest_call(c, RESET_GRAPH, FRONT_END + 0x1c);
    R(4) = 0;
    guest_call(c, SET_GRAPH_DEBUG, FRONT_END + 0x24);
    guest_call(c, CD_INIT, FRONT_END + 0x2c);
    guest_call(c, INIT_GEOM, FRONT_END + 0x34);
    guest_call(c, CARD_OPEN_EVENTS, FRONT_END + 0x3c);
    guest_call(c, INIT_PAD, FRONT_END + 0x44);

    /* The heap runs from the end of the front end to the reserved stack. */
    set_reg(c, 5, 0x801f0000);
    load_word_hi(c, 4, HEAP_START);
    load_word_hi(c, 2, HEAP_SIZE);
    set_reg(c, 5, R(5) | 0xef00);
    set_reg(c, 4, R(4) + R(2));
    R(5) -= R(4);
    guest_call(c, INIT_HEAP, FRONT_END + 0x68);

    /* Interlaced 640 x 256 with a projection distance of 250 and a black background. */
    load_word(c, 5, R(28) + SETTINGS_SLOT);
    set_reg(c, 4, 0);
    load_word(c, 2, R(5));
    set_reg(c, 3, 0x20000000);
    set_reg(c, 2, R(2) | R(3));
    MEM_W32(R(5), R(2));
    guest_call(c, SET_DISP_MASK, FRONT_END + 0x84);
    set_reg(c, 4, 640);
    set_reg(c, 5, 256);
    set_reg(c, 6, 0);
    set_reg(c, 7, 250);
    set_address(c, 2, BUFFERS);
    for (unsigned i = 0; i < 3; i++)
        store_word(c, R(29) + 0x10 + 4 * i, 0);
    MEM_W32(R(29) + 0x1c, R(2));
    guest_call(c, SETUP_DISPLAY, FRONT_END + 0xb0);

    check_card(c);

    /* Build a string from the shared name prefix (unused), then shut everything down. */
    load_word(c, 2, R(28));
    load_signed_half(c, 3, R(28) + 4);
    load_signed_byte(c, 4, R(28) + 6);
    store_word(c, R(29) + 0x20, R(2));
    store_half(c, R(29) + 0x24, R(3));
    store_byte(c, R(29) + 0x26, R(4));
    set_address(c, 5, MODULE_SUFFIX);
    R(4) = R(29) + 0x20;
    guest_call(c, STRCAT, FRONT_END + 0x158);
    guest_call(c, STOP_PAD, FRONT_END + 0x160);
    guest_call(c, CARD_CLOSE_EVENTS, FRONT_END + 0x168);
    R(4) = 3;
    guest_call(c, RESET_GRAPH, FRONT_END + 0x170);
    guest_call(c, RESET_CALLBACK, FRONT_END + 0x178);
    guest_call(c, STOP_CALLBACK, FRONT_END + 0x180);
    load_word(c, 3, R(28) + SETTINGS_SLOT);
    set_reg(c, 2, 1);
    store_word(c, R(3) + 0x24, R(2)); /* next module */
    guest_leave(c, 0x68, 0x64, 16, 16, 0x60);
    return true;
}

/* show_tim_screen(name, frames, width, height, skippable): load a full-screen TIM, display it
 * for `frames` VBlanks (or until Cross or Start when skippable) and release it. */
bool nc_psx_show_tim_screen(CPUState *c) {
    enum { FRAMES = 16, IMAGE = 17, SKIPPABLE = 18, DONE = 19 };
    if (c->budget <= 15)
        return false;
    R(29) -= 0x70;
    MEM_W32(R(29) + 0x68, R(31));
    for (unsigned reg = DONE; reg >= FRAMES; reg--)
        MEM_W32(R(29) + 0x58 + 4 * (reg - FRAMES), R(reg));
    R(SKIPPABLE) = MEM_R32(R(29) + 0x80);
    R(FRAMES) = R(5);
    MEM_W16(R(29) + 0x10, 0); /* unused RECT */
    MEM_W16(R(29) + 0x12, 0);
    MEM_W16(R(29) + 0x14, (u16)R(6));
    R(31) = SHOW_TIM_SCREEN + 0x34;
    MEM_W16(R(29) + 0x16, (u16)R(7));
    c->budget -= 15;
    nc_call(c, LOAD_FILE);
    set_reg(c, IMAGE, R(2));
    if (!guest_branch(c, R(IMAGE) == 0)) {
        R(4) = R(IMAGE);
        guest_call(c, LOAD_SCREEN_TIM, SHOW_TIM_SCREEN + 0x48);
        set_address(c, 4, BUFFERS + DB_SECOND + DB_DISPENV);
        R(FRAMES)--;
        guest_call(c, PUT_DISP_ENV, SHOW_TIM_SCREEN + 0x58);
        R(4) = 1;
        guest_call(c, SET_DISP_MASK, SHOW_TIM_SCREEN + 0x60);
        set_reg(c, 2, UINT32_MAX);
        const bool none = R(FRAMES) == R(2);
        R(2) = R(SKIPPABLE) << 16;
        if (!guest_branch(c, none)) {
            set_reg(c, SKIPPABLE, (u32)((s32)R(2) >> 16));
            set_reg(c, DONE, UINT32_MAX);
            bool again;
            do {
                if (!guest_branch(c, R(SKIPPABLE) == 0)) {
                    guest_call(c, SKIP_INPUT, SHOW_TIM_SCREEN + 0x84);
                    set_reg(c, 2, R(2) & 0xff);
                    if (guest_branch(c, R(2) != 0))
                        break;
                }
                R(4) = 0;
                guest_call(c, VSYNC, SHOW_TIM_SCREEN + 0x98);
                set_reg(c, FRAMES, R(FRAMES) - 1);
                again = guest_branch(c, R(FRAMES) != R(DONE));
            } while (again);
        }
        R(4) = R(IMAGE);
        guest_call(c, FREE, SHOW_TIM_SCREEN + 0xac);
    }
    guest_leave(c, 0x70, 0x68, 16, 19, 0x58);
    return true;
}

/* Clear the frame buffer and upload the pixels of the TIM at a0. */
bool nc_psx_load_screen_tim(CPUState *c) {
    if (c->budget <= 5)
        return false;
    R(29) -= 0x30;
    MEM_W32(R(29) + 0x28, R(16));
    MEM_W32(R(29) + 0x2c, R(31));
    R(31) = LOAD_SCREEN_TIM + 0x14;
    R(16) = R(4);
    c->budget -= 5;
    nc_call(c, CLEAR_DISPLAY);
    R(4) = R(16);
    guest_call(c, OPEN_TIM, LOAD_SCREEN_TIM + 0x1c);
    R(4) = R(29) + 0x10;
    guest_call(c, READ_TIM, LOAD_SCREEN_TIM + 0x24);
    load_word(c, 4, R(29) + 0x1c); /* pixel rectangle */
    load_word(c, 5, R(29) + 0x20); /* pixel data */
    guest_call(c, LOAD_IMAGE, LOAD_SCREEN_TIM + 0x34);
    R(4) = 0;
    guest_call(c, DRAW_SYNC, LOAD_SCREEN_TIM + 0x3c);
    guest_leave(c, 0x30, 0x2c, 16, 16, 0x28);
    return true;
}

/* PSX.EXE 80019D9C: reinitialize graphics, pads and sound, and count VBlanks. */
bool nc_psx_restart_display(CPUState *c) {
    const u32 entry = 0x80019d9c;
    if (!card_enter_frame(c, RESET_CALLBACK, entry + 0x10))
        return false;
    R(4) = 3;
    guest_call(c, RESET_GRAPH, entry + 0x18);
    guest_call(c, INIT_GEOM, entry + 0x20);
    guest_call(c, INIT_PAD, entry + 0x28);
    guest_call(c, REVERB_OFF, entry + 0x30);
    set_address(c, 4, COUNT_VBLANK);
    guest_call(c, VSYNC_CALLBACK, entry + 0x40);
    guest_leave_ra(c, 0x18, 0x10);
    return true;
}

/* PSX.EXE 80019DEC: the reverse of restart_display, with the screen cleared. */
bool nc_psx_shutdown_display(CPUState *c) {
    const u32 entry = 0x80019dec;
    if (!card_enter_frame(c, CLEAR_DISPLAY, entry + 0x10))
        return false;
    R(4) = 0;
    guest_call(c, VSYNC_CALLBACK, entry + 0x18);
    guest_call(c, SPU_QUIT, entry + 0x20);
    guest_call(c, STOP_PAD, entry + 0x28);
    R(4) = 3;
    guest_call(c, RESET_GRAPH, entry + 0x30);
    guest_call(c, RESET_CALLBACK, entry + 0x38);
    guest_call(c, STOP_CALLBACK, entry + 0x40);
    guest_leave_ra(c, 0x18, 0x10);
    return true;
}

bool nc_psx_load_file(CPUState *c) {
    static const FileLoadLayout layout = {
        .entry = LOAD_FILE,
        .prefix = 8,
        .suffix = 0x80027bec,
        .strcat = STRCAT,
        .search_file = CD_SEARCH_FILE,
        .allocate = ALLOCATE,
        .read_sectors = READ_SECTORS,
    };
    return cd_load_file(c, &layout);
}

/* The VSync callback: two frame counters at gp+0x28 and gp+0x20. */
bool nc_psx_count_vblank(CPUState *c) {
    if (c->budget <= 12)
        return false;
    R(2) = MEM_R32(R(28) + 0x28) + 1;
    R(3) = MEM_R32(R(28) + 0x20) + 1;
    MEM_W32(R(28) + 0x28, R(2));
    MEM_W32(R(28) + 0x20, R(3));
    c->budget -= 12;
    return true;
}

/* frame_timer(restart, resume, keep): restart remembers the frames elapsed since the last mark
 * and sets the mark to now; resume moves the mark forward by that amount; otherwise the mark is
 * now. Unless keep is set, gp+0x1C receives the mark times 20. */
bool nc_psx_frame_timer(CPUState *c) {
    if (c->budget <= 2)
        return false;
    c->budget -= 2;
    if (R(4)) {
        load_word(c, 3, R(28) + 0x28);
        load_word(c, 2, R(28) + 0x24);
        guest_nop(c);
        set_reg(c, 2, R(3) - R(2));
        store_word(c, R(28) + 0x78, R(2));
        store_word(c, R(28) + 0x24, R(3));
        guest_branch(c, true);
    } else {
        if (!guest_branch(c, R(5) == 0)) {
            load_word(c, 2, R(28) + 0x24);
            load_word(c, 3, R(28) + 0x78);
            R(2) += R(3);
            guest_branch(c, true);
        } else {
            load_word(c, 2, R(28) + 0x28);
        }
        guest_nop(c);
        store_word(c, R(28) + 0x24, R(2));
    }
    if (!guest_branch(c, R(6) != 0)) {
        load_word(c, 3, R(28) + 0x24);
        guest_nop(c);
        set_reg(c, 2, R(3) << 2);
        set_reg(c, 2, R(2) + R(3));
        set_reg(c, 2, R(2) << 2);
        store_word(c, R(28) + 0x1c, R(2));
    }
    guest_return(c);
    return true;
}

bool nc_psx_init_shared_record(CPUState *c) {
    return init_shared_record(c, SETTINGS_SLOT);
}

/* The memory card driver's patch: a jump to the code that follows it. */
bool nc_psx_card_patch_jump(CPUState *c) {
    if (c->budget <= 4)
        return false;
    R(2) = 0x80025a64;
    c->budget -= 4;
    nc_jump(c, R(2));
    return true;
}

/* With the controller and memory card interrupt enabled (base v1, normally I/O), wait for
 * JOY_STAT bit 7 to clear and continue at the first entry of the driver's table. */
bool nc_psx_card_wait_ack(CPUState *c) {
    if (c->budget <= 2)
        return false;
    load_word(c, 2, R(3) + 0x1074);
    guest_nop(c);
    set_reg(c, 2, R(2) & 0x80);
    if (guest_branch(c, R(2) == 0)) {
        guest_return(c);
        return true;
    }
    do {
        load_word(c, 2, R(3) + 0x1044);
        guest_nop(c);
        set_reg(c, 2, R(2) & 0x80);
    } while (guest_branch(c, R(2) != 0));
    load_word_hi(c, 2, 0x80027c38);
    guest_nop(c);
    guest_branch(c, true);
    nc_jump(c, R(2));
    return true;
}

/* --- memory card --------------------------------------------------------------------------- */

bool nc_psx_card_open_events(CPUState *c) {
    return card_open_events(c, &psx_card);
}

bool nc_psx_card_close_events(CPUState *c) {
    return card_close_events(c, &psx_card);
}

bool nc_psx_card_test_events(CPUState *c) {
    return card_test_events(c, &psx_card, psx_card.test_all, 0, CARD_EVENT_COUNT);
}

bool nc_psx_card_test_sw_events(CPUState *c) {
    return card_test_events(c, &psx_card, psx_card.test_sw, CARD_SW_EVENTS, 4);
}

bool nc_psx_card_test_hw_events(CPUState *c) {
    return card_test_events(c, &psx_card, psx_card.test_hw, CARD_HW_EVENTS, 4);
}

bool nc_psx_card_wait_sw_event(CPUState *c) {
    return card_wait_event(c, &psx_card, psx_card.wait_sw, CARD_SW_EVENTS);
}

bool nc_psx_card_wait_hw_event(CPUState *c) {
    return card_wait_event(c, &psx_card, psx_card.wait_hw, CARD_HW_EVENTS);
}

bool nc_psx_card_probe(CPUState *c) {
    return card_probe(c, &psx_card);
}

bool nc_psx_card_formatted(CPUState *c) {
    return card_formatted(c, &psx_card);
}

bool nc_psx_card_file_exists(CPUState *c) {
    return card_file_exists(c, &psx_card);
}

/* free_blocks(port): 15 minus the blocks used by every file on the card, or 0 when a file
 * cannot be read. */
bool nc_psx_card_free_blocks(CPUState *c) {
    enum { HANDLE = 16, ENTRY = 17, FREE_BLOCKS = 18, PORT = 19 };
    const u32 entry = CARD_FREE_BLOCKS;
    if (c->budget <= 13)
        return false;
    R(29) -= 0x150;
    MEM_W32(R(29) + 0x144, R(PORT));
    R(PORT) = R(4);
    MEM_W32(R(29) + 0x140, R(FREE_BLOCKS));
    R(FREE_BLOCKS) = 15;
    R(4) = R(29) + 0x10;
    R(5) = CARD_PATTERN;
    R(6) = R(PORT);
    MEM_W32(R(29) + 0x148, R(31));
    MEM_W32(R(29) + 0x13c, R(ENTRY));
    R(31) = entry + 0x34;
    MEM_W32(R(29) + 0x138, R(HANDLE));
    c->budget -= 13;
    nc_call(c, SPRINTF);
    set_reg(c, 4, R(29) + 0x10);
    set_reg(c, HANDLE, R(29) + 0x90); /* struct DIRENTRY */
    R(5) = R(HANDLE);
    guest_call(c, FIRST_FILE, entry + 0x44);
    bool more = R(2) != 0;
    R(2) = R(FREE_BLOCKS) & 0xff;
    if (guest_branch(c, more)) {
        set_reg(c, ENTRY, R(HANDLE));
        do {
            set_reg(c, 4, R(29) + 0x10);
            set_address(c, 5, CARD_PATH);
            set_reg(c, 6, R(PORT));
            R(7) = R(ENTRY);
            guest_call(c, SPRINTF, entry + 0x68);
            set_reg(c, 4, R(29) + 0x10);
            R(5) = 1; /* O_RDONLY */
            guest_call(c, OPEN, entry + 0x74);
            set_reg(c, HANDLE, R(2));
            set_reg(c, 2, UINT32_MAX);
            const bool opened = R(HANDLE) != R(2);
            R(4) = R(HANDLE);
            if (!guest_branch(c, opened)) {
                R(2) = 0;
                guest_branch(c, true);
                break;
            }
            /* The save header's fourth byte is its size in blocks. */
            set_reg(c, 5, R(29) + 0xb8);
            R(6) = 0x80;
            guest_call(c, READ, entry + 0x98);
            set_reg(c, 3, 0x80);
            const bool complete = R(2) == R(3);
            R(4) = R(HANDLE);
            if (!guest_branch(c, complete)) {
                R(4) = R(HANDLE);
                guest_call(c, CLOSE, entry + 0xac);
                R(2) = 0;
                guest_branch(c, true);
                break;
            }
            load_byte(c, 2, R(29) + 0xbb);
            R(FREE_BLOCKS) -= R(2);
            guest_call(c, CLOSE, entry + 0xc0);
            R(4) = R(ENTRY);
            guest_call(c, NEXT_FILE, entry + 0xc8);
            more = R(2) != 0;
            R(2) = R(FREE_BLOCKS) & 0xff;
        } while (guest_branch(c, more));
    }
    guest_leave(c, 0x150, 0x148, 16, 19, 0x138);
    return true;
}

/* --- display ------------------------------------------------------------------------------- */

bool nc_psx_init_display_buffers(CPUState *c) {
    return display_init_buffers(c, &psx_display);
}

bool nc_psx_setup_display(CPUState *c) {
    return display_setup(c, &psx_display);
}

bool nc_psx_apply_screen_offset(CPUState *c) {
    return display_apply_offset(c, &psx_display);
}

bool nc_psx_clear_display(CPUState *c) {
    return display_clear(c, &psx_display);
}

bool nc_psx_init_geom(CPUState *c) {
    static const GeomLayout layout = {
        .entry = INIT_GEOM,
        .saved_ra = 0x80027000,
        .patch_gte = 0x80021a68,
    };
    return sdk_init_geom(c, &layout);
}

bool nc_psx_set_geom_screen(CPUState *c) {
    return sdk_set_geom_screen(c);
}

/* --- record heap --------------------------------------------------------------------------- */

bool nc_psx_init_heap(CPUState *c) {
    return heap_init(c, &psx_heap);
}

bool nc_psx_resize(CPUState *c) {
    return heap_resize(c, &psx_heap);
}

bool nc_psx_allocate(CPUState *c) {
    return heap_allocate(c, &psx_heap);
}

bool nc_psx_free(CPUState *c) {
    return heap_release(c, &psx_heap);
}

bool nc_psx_find_allocation(CPUState *c) {
    return heap_find(c, &psx_heap);
}

bool nc_psx_allocation_size(CPUState *c) {
    return heap_size(c, &psx_heap);
}

bool nc_psx_heap_warning(CPUState *c, NcHeapWarning warning) {
    static const struct {
        u32 entry, message;
    } sites[] = {
        [NC_WARN_CALLOC] = {0x80019e3c, 0x80018048},
        [NC_WARN_MALLOC] = {0x80019e64, 0x80018074},
        [NC_WARN_REALLOC] = {0x80019e8c, 0x800180a0},
        [NC_WARN_FREE] = {0x80019eb4, 0x800180cc},
    };
    return heap_warning(c, sites[warning].entry, sites[warning].message, PRINTF);
}
