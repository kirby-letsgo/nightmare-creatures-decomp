#include "port/disc.h"
#include "port/exe.h"
#include "port/hw/hw.h"
#include "port/runtime.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdlib.h>
#include <string.h>

/* Native PS1 resolution; the window starts at 3x. */
#define PSX_WIDTH 320
#define PSX_HEIGHT 240

#define DEFAULT_DISC "roms/Nightmare Creatures.chd"
#define BOOT_EXE "cdrom:\\SLUS_005.82;1"
#define BOOT_STACK 0x801FFF00u /* SYSTEM.CNF STACK */

static SDL_Window *window;
static Uint64 last_frame_ns;

/* PS1 digital pad bits (1 = pressed). */
enum {
    PAD_SELECT = 1 << 0,
    PAD_START = 1 << 3,
    PAD_UP = 1 << 4,
    PAD_RIGHT = 1 << 5,
    PAD_DOWN = 1 << 6,
    PAD_LEFT = 1 << 7,
    PAD_L2 = 1 << 8,
    PAD_R2 = 1 << 9,
    PAD_L1 = 1 << 10,
    PAD_R1 = 1 << 11,
    PAD_TRIANGLE = 1 << 12,
    PAD_CIRCLE = 1 << 13,
    PAD_CROSS = 1 << 14,
    PAD_SQUARE = 1 << 15,
};

static u16 read_keyboard(void) {
    static const struct {
        SDL_Scancode key;
        u16 bit;
    } map[] = {
        {SDL_SCANCODE_UP, PAD_UP},        {SDL_SCANCODE_DOWN, PAD_DOWN},
        {SDL_SCANCODE_LEFT, PAD_LEFT},    {SDL_SCANCODE_RIGHT, PAD_RIGHT},
        {SDL_SCANCODE_RETURN, PAD_START}, {SDL_SCANCODE_BACKSPACE, PAD_SELECT},
        {SDL_SCANCODE_X, PAD_CROSS},      {SDL_SCANCODE_C, PAD_CIRCLE},
        {SDL_SCANCODE_Z, PAD_SQUARE},     {SDL_SCANCODE_S, PAD_TRIANGLE},
        {SDL_SCANCODE_Q, PAD_L1},         {SDL_SCANCODE_W, PAD_R1},
        {SDL_SCANCODE_1, PAD_L2},         {SDL_SCANCODE_2, PAD_R2},
    };
    const bool *keys = SDL_GetKeyboardState(NULL);
    u16 buttons = 0;
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (keys[map[i].key]) {
            buttons |= map[i].bit;
        }
    }
    return buttons;
}

/* Called once per emulated VBlank: input, window events, and 60 Hz pacing. */
static void on_frame(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            exit(0);
        }
    }
    bios_set_pad(read_keyboard());

    const Uint64 frame_ns = 1000000000ull / 60;
    Uint64 now = SDL_GetTicksNS();
    if (now - last_frame_ns < frame_ns) {
        SDL_DelayNS(frame_ns - (now - last_frame_ns));
    }
    last_frame_ns = SDL_GetTicksNS();
}

int main(int argc, char **argv) {
    const char *disc_path = DEFAULT_DISC;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--disc") == 0 && i + 1 < argc) {
            disc_path = argv[++i];
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("Nightmare Creatures", PSX_WIDTH * 3, PSX_HEIGHT * 3,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == NULL) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    if (!disc_open(disc_path)) {
        SDL_Log("Could not open the disc image at '%s'. Pass --disc <path to .chd>.", disc_path);
        SDL_Quit();
        return 1;
    }

    bios_init();
    nc_frame_hook = on_frame;
    last_frame_ns = SDL_GetTicksNS();

    /* Boot like the BIOS does: load the executable named in SYSTEM.CNF and run it with
     * interrupts enabled. */
    ExecInfo boot;
    if (!exe_load(BOOT_EXE, &boot)) {
        SDL_Log("Could not load %s from the disc", BOOT_EXE);
        return 1;
    }
    boot.s_addr = BOOT_STACK;
    boot.s_size = 0;
    nc_cpu.cop0[12] = 0x40000401u; /* COP2 usable, interrupts enabled */
    exe_exec(&nc_cpu, &boot, 0, 0);

    NC_LOG("boot executable returned");
    disc_close();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
