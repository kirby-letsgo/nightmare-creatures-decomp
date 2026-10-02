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
    /* audio, 0..100 */
    int volume_master;
    int volume_music;
    int volume_sfx;
    /* video */
    bool fullscreen;
    int render_scale; /* 1..8 */
    TextureFilter filter;
    bool show_fps;
} Settings;

extern Settings settings;

/* Loads settings (defaults for anything missing). `dir` ends with a path separator. */
void settings_load(const char *dir);
void settings_save(void);

#endif
