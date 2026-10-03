#include "port/disc.h"
#include "port/exe.h"
#include "port/hw/gpu.h"
#include "port/hw/hw.h"
#include "port/hw/spu.h"
#include "port/memcard.h"
#include "port/platform.h"
#include "port/runtime.h"
#include "port/savestate.h"
#include "port/settings.h"
#include "port/ui/menu.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Native PS1 resolution; the window starts at 3x. */
#define PSX_WIDTH 320
#define PSX_HEIGHT 240

#define DEFAULT_DISC "roms/Nightmare Creatures.chd"
#define BOOT_EXE "cdrom:\\SLUS_005.82;1"
#define BOOT_STACK 0x801FFF00u /* SYSTEM.CNF STACK */

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *screen;
static u32 screen_w, screen_h;
static u32 *screen_pixels;
static Uint64 last_frame_ns;
static SDL_AudioStream *audio;
static bool menu_requested;
static bool headless; /* NC_HEADLESS=1: no window, no audio device, unpaced (automated runs) */

/* --- input -------------------------------------------------------------------------------- */

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

/* All connected gamepads drive pad 1. The left stick acts as the d-pad. */
static u16 read_gamepads(void) {
    static const struct {
        SDL_GamepadButton button;
        u16 bit;
    } map[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, PAD_CROSS},      {SDL_GAMEPAD_BUTTON_EAST, PAD_CIRCLE},
        {SDL_GAMEPAD_BUTTON_WEST, PAD_SQUARE},      {SDL_GAMEPAD_BUTTON_NORTH, PAD_TRIANGLE},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PAD_L1}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_R1},
        {SDL_GAMEPAD_BUTTON_START, PAD_START},      {SDL_GAMEPAD_BUTTON_BACK, PAD_SELECT},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_UP},       {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_DOWN},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_LEFT},   {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_RIGHT},
    };
    enum { STICK_DEADZONE = 12000, TRIGGER_THRESHOLD = 8000 };
    u16 buttons = 0;
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int g = 0; g < count; g++) {
        SDL_Gamepad *pad = SDL_GetGamepadFromID(ids[g]);
        if (pad == NULL) {
            continue;
        }
        for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
            if (SDL_GetGamepadButton(pad, map[i].button)) {
                buttons |= map[i].bit;
            }
        }
        if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > TRIGGER_THRESHOLD) {
            buttons |= PAD_L2;
        }
        if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > TRIGGER_THRESHOLD) {
            buttons |= PAD_R2;
        }
        Sint16 x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
        Sint16 y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        buttons |= x < -STICK_DEADZONE ? PAD_LEFT : (x > STICK_DEADZONE ? PAD_RIGHT : 0);
        buttons |= y < -STICK_DEADZONE ? PAD_UP : (y > STICK_DEADZONE ? PAD_DOWN : 0);
    }
    SDL_free(ids);
    return buttons;
}

/* Testing aid: NC_INPUT=file plays a scripted input timeline. Each line is
 * "<first_frame> <last_frame> <buttons...>" with buttons from: up down left right cross circle
 * square triangle l1 r1 l2 r2 start select. Lines starting with '#' are comments. */
typedef struct ScriptStep {
    unsigned first, last;
    u16 buttons;
} ScriptStep;

static u16 input_script(void) {
    static ScriptStep steps[256];
    static int count = -1;
    static unsigned frame;
    if (count < 0) {
        count = 0;
        const char *path = SDL_getenv("NC_INPUT");
        FILE *f = path ? fopen(path, "r") : NULL;
        char line[256];
        while (f != NULL && fgets(line, sizeof line, f) != NULL && count < 256) {
            static const struct {
                const char *name;
                u16 bit;
            } names[] = {{"up", PAD_UP},         {"down", PAD_DOWN},
                         {"left", PAD_LEFT},     {"right", PAD_RIGHT},
                         {"cross", PAD_CROSS},   {"circle", PAD_CIRCLE},
                         {"square", PAD_SQUARE}, {"triangle", PAD_TRIANGLE},
                         {"l1", PAD_L1},         {"r1", PAD_R1},
                         {"l2", PAD_L2},         {"r2", PAD_R2},
                         {"start", PAD_START},   {"select", PAD_SELECT}};
            ScriptStep st = {0};
            char *tok = strtok(line, " \t\r\n");
            if (tok == NULL || tok[0] == '#') {
                continue;
            }
            st.first = (unsigned)atoi(tok);
            tok = strtok(NULL, " \t\r\n");
            st.last = tok ? (unsigned)atoi(tok) : st.first;
            while ((tok = strtok(NULL, " \t\r\n")) != NULL) {
                for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
                    if (strcmp(tok, names[i].name) == 0) {
                        st.buttons |= names[i].bit;
                    }
                }
            }
            steps[count++] = st;
        }
        if (f != NULL) {
            fclose(f);
        }
    }
    frame++;
    u16 buttons = 0;
    for (int i = 0; i < count; i++) {
        if (frame >= steps[i].first && frame <= steps[i].last) {
            buttons |= steps[i].buttons;
        }
    }
    return buttons;
}

/* Testing aid: NC_PRESS_START=N taps Start for a few frames every N frames (skips movies,
 * advances menus) so later parts of the game can be reached unattended. */
static u16 scripted_input(void) {
    static int every = -1;
    static unsigned frame;
    if (every < 0) {
        const char *env = SDL_getenv("NC_PRESS_START");
        every = env ? SDL_atoi(env) : 0;
    }
    if (every <= 0) {
        return 0;
    }
    return (++frame % (unsigned)every) < 4 ? PAD_START : 0;
}

/* Debugging aid: NC_RAMDUMP=N writes main RAM to build/ram/frame_XXXXX.bin every N frames
 * (for locating game variables by comparing snapshots). */
static void dump_ram(void) {
    static int every = -1;
    static unsigned frame;
    if (every < 0) {
        const char *env = SDL_getenv("NC_RAMDUMP");
        every = env ? SDL_atoi(env) : 0;
        if (every > 0) {
            SDL_CreateDirectory("build/ram");
        }
    }
    static int from = -1;
    if (from < 0) {
        const char *env = SDL_getenv("NC_RAMDUMP_FROM");
        from = env ? SDL_atoi(env) : 0;
    }
    if (every <= 0 || ++frame % (unsigned)every != 0 || frame < (unsigned)from) {
        return;
    }
    char path[64];
    SDL_snprintf(path, sizeof path, "build/ram/frame_%05u.bin", frame);
    SDL_SaveFile(path, nc_ram, RAM_SIZE);
}

/* --- video -------------------------------------------------------------------------------- */

/* Debugging aid: NC_SHOT_EVERY=N saves the display area to build/shots/ every N frames. */
static void save_debug_shot(const GpuDisplay *d) {
    static int every = -1;
    static unsigned frame;
    if (every < 0) {
        const char *env = SDL_getenv("NC_SHOT_EVERY");
        every = env ? SDL_atoi(env) : 0;
        if (every > 0) {
            SDL_CreateDirectory("build/shots");
        }
    }
    if (every <= 0 || ++frame % (unsigned)every != 0) {
        return;
    }
    int w = (int)(d->width * d->scale), h = (int)(d->height * d->scale);
    SDL_Surface *surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, screen_pixels, w * 4);
    if (surf != NULL) {
        char path[64];
        SDL_snprintf(path, sizeof path, "build/shots/frame_%05u.bmp", frame);
        SDL_SaveBMP(surf, path);
        SDL_DestroySurface(surf);
    }
}

/* Converts the PS1 display area into the screen texture. */
static void update_screen(void) {
    GpuDisplay d;
    gpu_display_info(&d);
    if (!d.enabled || d.width == 0 || d.height == 0) {
        screen_w = screen_h = 0;
        return;
    }
    u32 w = d.width * d.scale, h = d.height * d.scale;
    if (screen == NULL || w != screen_w || h != screen_h) {
        SDL_DestroyTexture(screen);
        free(screen_pixels);
        screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                                   (int)w, (int)h);
        screen_pixels = malloc((size_t)w * h * 4);
        screen_w = w;
        screen_h = h;
    }
    gpu_display_rgba(screen_pixels, &d);
    SDL_UpdateTexture(screen, NULL, screen_pixels, (int)(w * 4));
    save_debug_shot(&d);
}

/* Small overlay text in the top-left corner, on line `line`. SDL's debug font is 8x8 pixels:
 * scale it with the output so it stays readable (about 1/40 of the screen height), with a dark
 * backing for contrast. */
static void draw_overlay_text(const char *text, int line) {
    int ww, wh;
    SDL_GetRenderOutputSize(renderer, &ww, &wh);
    float scale = (float)wh / 320.0f;
    scale = scale < 1.0f ? 1.0f : scale;
    float y = 4.0f + (float)line * 20.0f;
    SDL_SetRenderScale(renderer, scale, scale);
    SDL_FRect bg = {4, y, 8.0f * (float)SDL_strlen(text) + 8, 16};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDebugText(renderer, 8, y + 4, text);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

static unsigned fps_shown;

/* Draws the last game frame letterboxed to 4:3, or 16:9 in widescreen mode when the frame
 * contains 3D (still pictures such as level intro cards, and movies, stay 4:3). Also used
 * behind the pause menu. */
static void draw_game(void) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (screen != NULL && screen_w > 0) {
        SDL_SetTextureScaleMode(screen, settings.filter == FILTER_BILINEAR ? SDL_SCALEMODE_LINEAR
                                                                           : SDL_SCALEMODE_NEAREST);
        int ww, wh;
        SDL_GetRenderOutputSize(renderer, &ww, &wh);
        GpuDisplay d;
        gpu_display_info(&d);
        bool wide = settings.widescreen && !d.rgb24 && gpu_frame_has_3d();
        static int last_wide = -1;
        if (wide != last_wide) {
            NC_LOG("display: %s (vblank %u)", wide ? "16:9" : "4:3", nc_frame_count);
            last_wide = wide;
        }
        float aw = wide ? 16.0f : 4.0f;
        float ah = wide ? 9.0f : 3.0f;
        float scale = (float)wh / ah < (float)ww / aw ? (float)wh / ah : (float)ww / aw;
        SDL_FRect dst = {((float)ww - scale * aw) / 2.0f, ((float)wh - scale * ah) / 2.0f,
                         scale * aw, scale * ah};
        SDL_RenderTexture(renderer, screen, NULL, &dst);
    }
    if (settings.show_fps) {
        char text[32];
        SDL_snprintf(text, sizeof text, "%u fps", fps_shown);
        draw_overlay_text(text, 0);
    }
    const char *state_msg = savestate_message();
    if (state_msg != NULL) {
        draw_overlay_text(state_msg, settings.show_fps ? 1 : 0);
    }
}

/* Counts the game's own frames (display buffer flips) per second. NC_FPS=1 also logs them. */
static void update_fps(void) {
    static int log_enabled = -1;
    static unsigned vblanks;
    static u32 last_flips;
    static Uint64 last_ns;
    if (log_enabled < 0) {
        log_enabled = SDL_getenv("NC_FPS") != NULL;
        last_ns = SDL_GetTicksNS();
    }
    if (++vblanks % 60 != 0) {
        return;
    }
    Uint64 now = SDL_GetTicksNS();
    u32 flips = gpu_flip_count();
    fps_shown = flips - last_flips;
    if (log_enabled) {
        NC_LOG("fps: game %u, vblank %.1f", fps_shown, 60.0e9 / (double)(now - last_ns));
    }
    last_flips = flips;
    last_ns = now;
}

/* --- audio -------------------------------------------------------------------------------- */

/* Debugging aid: NC_WAV=path records the audio output to a 16-bit stereo WAV file. */
static void record_wav(const s16 *samples, int bytes) {
    static SDL_IOStream *wav;
    static int state = -1;
    static Uint32 data_bytes;
    if (state < 0) {
        const char *path = SDL_getenv("NC_WAV");
        wav = path ? SDL_IOFromFile(path, "wb") : NULL;
        state = wav != NULL;
        if (wav != NULL) {
            Uint8 header[44] = {0};
            SDL_WriteIO(wav, header, sizeof header); /* filled in as data arrives */
        }
    }
    if (!state) {
        return;
    }
    SDL_WriteIO(wav, samples, (size_t)bytes);
    data_bytes += (Uint32)bytes;
    /* Rewrite the header each time so the file is valid even if the game is killed. */
    Sint64 end = SDL_TellIO(wav);
    SDL_SeekIO(wav, 0, SDL_IO_SEEK_SET);
    SDL_WriteIO(wav, "RIFF", 4);
    SDL_WriteU32LE(wav, 36 + data_bytes);
    SDL_WriteIO(wav, "WAVEfmt ", 8);
    SDL_WriteU32LE(wav, 16);
    SDL_WriteU16LE(wav, 1);
    SDL_WriteU16LE(wav, 2);
    SDL_WriteU32LE(wav, SPU_RATE);
    SDL_WriteU32LE(wav, SPU_RATE * 4);
    SDL_WriteU16LE(wav, 4);
    SDL_WriteU16LE(wav, 16);
    SDL_WriteIO(wav, "data", 4);
    SDL_WriteU32LE(wav, data_bytes);
    SDL_SeekIO(wav, end, SDL_IO_SEEK_SET);
}

/* Renders one frame of audio (44100 / 60 samples) and queues it for the device. */
static void output_audio(void) {
    enum { FRAME_SAMPLES = SPU_RATE / 60 };
    static s16 buf[FRAME_SAMPLES * 2];
    spu_render(buf, FRAME_SAMPLES);
    record_wav(buf, (int)sizeof buf);
    if (audio != NULL) {
        SDL_PutAudioStreamData(audio, buf, (int)sizeof buf);
    }
}

/* Paces emulation to real time. With audio, the sound card's clock is the master: we wait
 * while more than a few frames of audio are queued, so the queue never overflows (no dropped
 * audio) and never drifts against video. Without audio, absolute 60 Hz deadlines are used. */
static void pace(void) {
    const Uint64 frame_ns = 1000000000ull / 60;
    if (headless) {
        return;
    }
    if (audio != NULL) {
        enum { TARGET_BYTES = (SPU_RATE / 60) * 4 * 3 }; /* ~50 ms */
        /* Bounded wait: if the device stops consuming (output switched, Bluetooth asleep),
         * fall back to wall-clock pacing instead of freezing. */
        Uint64 start = SDL_GetTicksNS();
        while (SDL_GetAudioStreamQueued(audio) > TARGET_BYTES &&
               SDL_GetTicksNS() - start < 3 * frame_ns) {
            SDL_DelayPrecise(1000000);
        }
        if (SDL_GetAudioStreamQueued(audio) > TARGET_BYTES * 4) {
            SDL_ClearAudioStream(audio); /* device stalled: drop the backlog */
        }
        last_frame_ns = SDL_GetTicksNS();
        return;
    }
    last_frame_ns += frame_ns;
    Uint64 now = SDL_GetTicksNS();
    if (now < last_frame_ns) {
        SDL_DelayPrecise(last_frame_ns - now);
    } else if (now - last_frame_ns > 4 * frame_ns) {
        last_frame_ns = now;
    }
}

/* --- frame loop --------------------------------------------------------------------------- */

/* After a save state is loaded: re-apply the user's resolution (the state carries its own) and
 * drop audio queued from before the load. */
static void after_state_load(void) {
    gpu_set_scale(settings.render_scale);
    if (audio != NULL) {
        SDL_ClearAudioStream(audio);
    }
}

static void quit_game(void) {
    settings_save();
    exit(0);
}

/* Shows the pause menu over the frozen game until the player resumes. */
static void pause_menu(void) {
    if (audio != NULL) {
        SDL_PauseAudioStreamDevice(audio);
    }
    if (menu_run_pause(draw_game) == MENU_QUIT) {
        quit_game();
    }
    if (audio != NULL) {
        SDL_ClearAudioStream(audio);
        SDL_ResumeAudioStreamDevice(audio);
    }
    last_frame_ns = SDL_GetTicksNS();
}

/* Called once per emulated VBlank: input, window events, video, audio and pacing. */
static void on_frame(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            quit_game();
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.scancode == SDL_SCANCODE_ESCAPE && !event.key.repeat) {
                menu_requested = true;
            } else if (event.key.scancode == SDL_SCANCODE_F5 && !event.key.repeat) {
                savestate_request_save(0);
            } else if (event.key.scancode == SDL_SCANCODE_F9 && !event.key.repeat) {
                savestate_request_load(0);
            } else if (event.key.scancode == SDL_SCANCODE_F11 && !event.key.repeat) {
                settings.fullscreen = !settings.fullscreen;
                menu_apply_settings();
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            SDL_OpenGamepad(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
                menu_requested = true;
            }
            break;
        default:
            break;
        }
    }
    bios_set_pad(read_keyboard() | read_gamepads() | scripted_input() | input_script());
    dump_ram();
    update_screen();
    draw_game();
    SDL_RenderPresent(renderer);
    output_audio();
    update_fps();
    pace();

    if (menu_requested) {
        menu_requested = false;
        pause_menu();
    }
}

/* --- startup ------------------------------------------------------------------------------ */

int main(int argc, char **argv) {
    const char *disc_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--disc") == 0 && i + 1 < argc) {
            disc_arg = argv[++i];
        }
    }

    nc_log_init();
    headless = SDL_getenv("NC_HEADLESS") != NULL;
    if (headless) {
        /* Hidden window and no sound card: nothing to steal focus or to pace against. */
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    char *data_dir = SDL_GetPrefPath("NightmareCreatures", "nightmare-port");
    /* NC_DATA_DIR=<dir/> overrides the user data folder (settings, memory cards): lets test
     * runs use their own settings without touching the player's. */
    const char *dir = SDL_getenv("NC_DATA_DIR");
    dir = dir != NULL ? dir : (data_dir != NULL ? data_dir : "./");
    settings_load(dir);
    memcard_init(dir);
    savestate_init(dir);
    savestate_after_load = after_state_load;
    SDL_free(data_dir);

    /* A disc given on the command line wins; otherwise use the remembered one, falling back
     * to the repo's roms/ folder for development builds. */
    if (disc_arg != NULL) {
        SDL_snprintf(settings.disc_path, sizeof settings.disc_path, "%s", disc_arg);
    } else if (settings.disc_path[0] == '\0' && disc_check(DEFAULT_DISC) == DISC_OK) {
        SDL_snprintf(settings.disc_path, sizeof settings.disc_path, "%s", DEFAULT_DISC);
    }

    window = SDL_CreateWindow("Nightmare Creatures", PSX_WIDTH * 3, PSX_HEIGHT * 3,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                  (headless ? SDL_WINDOW_HIDDEN : 0));
    platform_keep_awake();
    renderer = window != NULL ? SDL_CreateRenderer(window, NULL) : NULL;
    if (renderer == NULL) {
        SDL_Log("Could not create the window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 0); /* pacing is done by on_frame */

    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, SPU_RATE};
    audio = headless
                ? NULL
                : SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio == NULL) {
        SDL_Log("No audio output: %s", SDL_GetError());
    }

    menu_init(window, renderer);
    menu_apply_settings();

    /* NC_SKIP_MENU=1 boots straight into the game (for scripted test runs). */
    bool skip_menu = (SDL_getenv("NC_SKIP_MENU") != NULL || headless) &&
                     disc_check(settings.disc_path) == DISC_OK;
    if (!skip_menu && menu_run_start() == MENU_QUIT) {
        quit_game();
    }
    settings_save();

    if (!disc_open(settings.disc_path)) {
        SDL_Log("Could not open the disc image at '%s'.", settings.disc_path);
        SDL_Quit();
        return 1;
    }
    if (audio != NULL) {
        SDL_ResumeAudioStreamDevice(audio);
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
    menu_shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
