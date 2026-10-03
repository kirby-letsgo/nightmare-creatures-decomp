#include "port/settings.h"

#include "port/input.h"
#include "port/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Settings settings;
static char path[1100];

static void set_defaults(void) {
    memset(&settings, 0, sizeof settings);
    settings.adrenaline = true;
    settings.controls = CONTROLS_TANK;
    settings.mouse_camera = true;
    settings.mouse_sensitivity = 50;
    for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
        settings.key_bind[i] = input_default_key(i);
        settings.pad_bind[i] = input_default_pad(i);
    }
    for (int i = 0; i < HOTKEY_COUNT; i++) {
        input_default_hotkey(i, &settings.hotkey_key[i], &settings.hotkey_mods[i],
                             &settings.hotkey_pad[i]);
    }
    settings.volume_master = 100;
    settings.volume_music = 100;
    settings.volume_sfx = 100;
    settings.render_scale = 1;
    settings.texture_scale = 1;
    settings.filter = FILTER_NEAREST;
}

static bool parse_bool(const char *v) {
    return strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0;
}

static int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void settings_load(const char *dir) {
    set_defaults();
    snprintf(path, sizeof path, "%ssettings.ini", dir);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    char line[1200];
    while (fgets(line, sizeof line, f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (line[0] == '#' || line[0] == ';' || eq == NULL) {
            continue;
        }
        *eq = '\0';
        const char *key = line, *val = eq + 1;
        if (strcmp(key, "disc_path") == 0) {
            snprintf(settings.disc_path, sizeof settings.disc_path, "%s", val);
        } else if (strcmp(key, "adrenaline") == 0) {
            settings.adrenaline = parse_bool(val);
        } else if (strcmp(key, "controls") == 0) {
            settings.controls = strcmp(val, "modern") == 0 ? CONTROLS_MODERN : CONTROLS_TANK;
        } else if (strcmp(key, "mouse_camera") == 0) {
            settings.mouse_camera = parse_bool(val);
        } else if (strcmp(key, "mouse_sensitivity") == 0) {
            settings.mouse_sensitivity = clamp_int(atoi(val), 1, 100);
        } else if (strncmp(key, "hotkey_", 7) == 0) {
            for (int i = 0; i < HOTKEY_COUNT; i++) {
                size_t n = strlen(input_hotkeys[i].id);
                if (strncmp(key + 7, input_hotkeys[i].id, n) != 0) {
                    continue;
                }
                const char *field = key + 7 + n;
                if (strcmp(field, "_key") == 0) {
                    settings.hotkey_key[i] =
                        strcmp(val, "none") == 0 ? 0 : (int)SDL_GetScancodeFromName(val);
                } else if (strcmp(field, "_mods") == 0) {
                    settings.hotkey_mods[i] = (strstr(val, "shortcut") ? HOTMOD_SHORTCUT : 0) |
                                              (strstr(val, "shift") ? HOTMOD_SHIFT : 0) |
                                              (strstr(val, "alt") ? HOTMOD_ALT : 0);
                } else if (strcmp(field, "_pad") == 0) {
                    settings.hotkey_pad[i] = input_pad_from_id(val);
                }
            }
        } else if (strncmp(key, "key_", 4) == 0 || strncmp(key, "pad_", 4) == 0) {
            for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
                if (strcmp(key + 4, input_buttons[i].id) != 0) {
                    continue;
                }
                if (key[0] == 'k') {
                    SDL_Scancode sc = SDL_GetScancodeFromName(val);
                    settings.key_bind[i] = strcmp(val, "none") == 0 ? 0 : (int)sc;
                } else {
                    settings.pad_bind[i] = input_pad_from_id(val);
                }
            }
        } else if (strcmp(key, "volume_master") == 0) {
            settings.volume_master = clamp_int(atoi(val), 0, 100);
        } else if (strcmp(key, "volume_music") == 0) {
            settings.volume_music = clamp_int(atoi(val), 0, 100);
        } else if (strcmp(key, "volume_sfx") == 0) {
            settings.volume_sfx = clamp_int(atoi(val), 0, 100);
        } else if (strcmp(key, "fullscreen") == 0) {
            settings.fullscreen = parse_bool(val);
        } else if (strcmp(key, "texture_scale") == 0) {
            settings.texture_scale = clamp_int(atoi(val), 1, 4);
        } else if (strcmp(key, "render_scale") == 0) {
            settings.render_scale = clamp_int(atoi(val), 1, 8);
        } else if (strcmp(key, "texture_filter") == 0) {
            settings.filter = strcmp(val, "bilinear") == 0 ? FILTER_BILINEAR : FILTER_NEAREST;
        } else if (strcmp(key, "widescreen") == 0) {
            settings.widescreen = parse_bool(val);
        } else if (strcmp(key, "show_fps") == 0) {
            settings.show_fps = parse_bool(val);
        }
    }
    fclose(f);
}

void settings_save(void) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        NC_LOG("settings: cannot write %s", path);
        return;
    }
    fprintf(f, "# Nightmare Creatures PC port settings\n");
    fprintf(f, "disc_path=%s\n", settings.disc_path);
    fprintf(f, "adrenaline=%s\n", settings.adrenaline ? "on" : "off");
    fprintf(f, "controls=%s\n", settings.controls == CONTROLS_MODERN ? "modern" : "tank");
    fprintf(f, "mouse_camera=%s\n", settings.mouse_camera ? "on" : "off");
    fprintf(f, "mouse_sensitivity=%d\n", settings.mouse_sensitivity);
    for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
        int k = settings.key_bind[i];
        fprintf(f, "key_%s=%s\n", input_buttons[i].id,
                k > 0 ? SDL_GetScancodeName((SDL_Scancode)k) : "none");
        fprintf(f, "pad_%s=%s\n", input_buttons[i].id, input_pad_id(settings.pad_bind[i]));
    }
    for (int i = 0; i < HOTKEY_COUNT; i++) {
        int k = settings.hotkey_key[i], m = settings.hotkey_mods[i];
        fprintf(f, "hotkey_%s_key=%s\n", input_hotkeys[i].id,
                k > 0 ? SDL_GetScancodeName((SDL_Scancode)k) : "none");
        fprintf(f, "hotkey_%s_mods=%s%s%s\n", input_hotkeys[i].id,
                (m & HOTMOD_SHORTCUT) ? "shortcut " : "", (m & HOTMOD_SHIFT) ? "shift " : "",
                (m & HOTMOD_ALT) ? "alt" : "");
        fprintf(f, "hotkey_%s_pad=%s\n", input_hotkeys[i].id, input_pad_id(settings.hotkey_pad[i]));
    }
    fprintf(f, "volume_master=%d\n", settings.volume_master);
    fprintf(f, "volume_music=%d\n", settings.volume_music);
    fprintf(f, "volume_sfx=%d\n", settings.volume_sfx);
    fprintf(f, "fullscreen=%s\n", settings.fullscreen ? "on" : "off");
    fprintf(f, "render_scale=%d\n", settings.render_scale);
    fprintf(f, "texture_scale=%d\n", settings.texture_scale);
    fprintf(f, "texture_filter=%s\n", settings.filter == FILTER_BILINEAR ? "bilinear" : "nearest");
    fprintf(f, "show_fps=%s\n", settings.show_fps ? "on" : "off");
    fprintf(f, "widescreen=%s\n", settings.widescreen ? "on" : "off");
    fclose(f);
}
