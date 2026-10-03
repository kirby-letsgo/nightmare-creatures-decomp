/* User settings, persisted as settings.ini in the user data folder. */
#ifndef NC_PORT_SETTINGS_H
#define NC_PORT_SETTINGS_H

#include <stdbool.h>

typedef enum ControlScheme { CONTROLS_TANK, CONTROLS_MODERN } ControlScheme;
typedef enum TextureFilter { FILTER_NEAREST, FILTER_BILINEAR } TextureFilter;

typedef struct Settings {
    char disc_path[1024];
    /* gameplay */
    bool adrenaline;
    ControlScheme controls;
    bool mouse_camera;     /* mouse / right stick rotates the camera */
    int mouse_sensitivity; /* 1..100 */
    int key_bind[14];      /* SDL_Scancode per input_buttons[] entry, 0 = unbound */
    int pad_bind[14];      /* gamepad binding per input_buttons[] entry (see input.h) */
    int hotkey_key[2];     /* per input_hotkeys[] entry: scancode, 0 = unbound */
    int hotkey_mods[2];    /* HOTMOD_* bits */
    int hotkey_pad[2];     /* gamepad button or INPUT_UNBOUND */
    /* audio, 0..100 */
    int volume_master;
    int volume_music;
    int volume_sfx;
    /* video */
    bool fullscreen;
    int render_scale; /* 1..8 */
    TextureFilter filter;
    bool show_fps;
    bool widescreen; /* 16:9: 3D projection squeezed by 3/4, shown stretched */
} Settings;

extern Settings settings;

/* Loads settings (defaults for anything missing). `dir` ends with a path separator. */
void settings_load(const char *dir);
void settings_save(void);

#endif
