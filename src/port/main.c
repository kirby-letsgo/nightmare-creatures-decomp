#include "port/disc.h"
#include "port/exe.h"
#include "port/hw/gpu.h"
#include "port/hw/hw.h"
#include "port/hw/spu.h"
#include "port/memcard.h"
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
static SDL_Renderer *renderer;
static SDL_Texture *screen;
static u32 screen_w, screen_h;
static u32 *screen_pixels;
static Uint64 last_frame_ns;
static SDL_AudioStream *audio;

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
    SDL_Surface *surf =
        SDL_CreateSurfaceFrom((int)d->width, (int)d->height, SDL_PIXELFORMAT_ABGR8888,
                              screen_pixels, (int)(d->width * 4));
    if (surf != NULL) {
        char path[64];
        SDL_snprintf(path, sizeof path, "build/shots/frame_%05u.bmp", frame);
        SDL_SaveBMP(surf, path);
        SDL_DestroySurface(surf);
    }
}

/* Shows the PS1 display area, letterboxed to 4:3. */
static void present(void) {
    GpuDisplay d;
    gpu_display_info(&d);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (d.enabled && d.width > 0 && d.height > 0) {
        if (screen == NULL || d.width != screen_w || d.height != screen_h) {
            SDL_DestroyTexture(screen);
            free(screen_pixels);
            screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                       SDL_TEXTUREACCESS_STREAMING, (int)d.width, (int)d.height);
            SDL_SetTextureScaleMode(screen, SDL_SCALEMODE_NEAREST);
            screen_pixels = malloc((size_t)d.width * d.height * 4);
            screen_w = d.width;
            screen_h = d.height;
        }
        gpu_display_rgba(screen_pixels, &d);
        SDL_UpdateTexture(screen, NULL, screen_pixels, (int)(d.width * 4));
        save_debug_shot(&d);

        int ww, wh;
        SDL_GetRenderOutputSize(renderer, &ww, &wh);
        float scale = (float)wh / 3.0f < (float)ww / 4.0f ? (float)wh / 3.0f : (float)ww / 4.0f;
        SDL_FRect dst = {((float)ww - scale * 4.0f) / 2.0f, ((float)wh - scale * 3.0f) / 2.0f,
                         scale * 4.0f, scale * 3.0f};
        SDL_RenderTexture(renderer, screen, NULL, &dst);
    }
    SDL_RenderPresent(renderer);
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

/* Debugging aid: NC_FPS=1 logs the game's own frame rate (buffer flips) once per second. */
static void report_fps(void) {
    static int enabled = -1;
    static unsigned vblanks;
    static u32 last_flips;
    static Uint64 last_ns;
    if (enabled < 0) {
        enabled = SDL_getenv("NC_FPS") != NULL;
        last_ns = SDL_GetTicksNS();
    }
    if (!enabled || ++vblanks % 60 != 0) {
        return;
    }
    Uint64 now = SDL_GetTicksNS();
    u32 flips = gpu_flip_count();
    NC_LOG("fps: game %u, vblank %.1f", flips - last_flips, 60.0e9 / (double)(now - last_ns));
    last_flips = flips;
    last_ns = now;
}

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
    if (audio != NULL) {
        enum { TARGET_BYTES = (SPU_RATE / 60) * 4 * 3 }; /* ~50 ms */
        while (SDL_GetAudioStreamQueued(audio) > TARGET_BYTES) {
            SDL_DelayPrecise(1000000);
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

/* Called once per emulated VBlank: input, window events, video, and 60 Hz pacing. */
static void on_frame(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            exit(0);
        }
    }
    bios_set_pad(read_keyboard() | scripted_input());
    present();
    output_audio();
    report_fps();
    pace();
}

int main(int argc, char **argv) {
    const char *disc_path = DEFAULT_DISC;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--disc") == 0 && i + 1 < argc) {
            disc_path = argv[++i];
        }
    }

    nc_log_init();
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

    renderer = SDL_CreateRenderer(window, NULL);
    if (renderer == NULL) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 0); /* pacing is done by on_frame */

    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, SPU_RATE};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio != NULL) {
        SDL_ResumeAudioStreamDevice(audio);
    } else {
        SDL_Log("No audio output: %s", SDL_GetError());
    }

    if (!disc_open(disc_path)) {
        SDL_Log("Could not open the disc image at '%s'. Pass --disc <path to .chd>.", disc_path);
        SDL_Quit();
        return 1;
    }

    bios_init();
    char *data_dir = SDL_GetPrefPath("NightmareCreatures", "nightmare-port");
    memcard_init(data_dir != NULL ? data_dir : "./");
    SDL_free(data_dir);
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
