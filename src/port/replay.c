#include "port/replay.h"

#include "port/game_vars.h"
#include "port/input.h"
#include "port/runtime.h"

#include <stdlib.h>
#include <string.h>

typedef struct ScriptStep {
    unsigned first, last;
    u16 buttons;
} ScriptStep;

typedef struct InputScript {
    ScriptStep steps[256];
    unsigned count, end;
} InputScript;

static InputScript boot, level;
static unsigned level_frame;

static void read_script(InputScript *script, const char *path) {
    if (!path) {
        return;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *first = strtok(line, " \t\r\n");
        if (!first || first[0] == '#') {
            continue;
        }
        if (script->count == sizeof script->steps / sizeof script->steps[0]) {
            break;
        }
        ScriptStep *step = &script->steps[script->count++];
        step->first = (unsigned)atoi(first);
        char *last = strtok(NULL, " \t\r\n");
        step->last = last ? (unsigned)atoi(last) : step->first;
        if (step->last > script->end) {
            script->end = step->last;
        }
        char *button;
        while ((button = strtok(NULL, " \t\r\n")) != NULL) {
            for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
                if (strcmp(button, input_buttons[i].id) == 0) {
                    step->buttons |= input_buttons[i].bit;
                }
            }
        }
    }
    fclose(f);
}

void replay_init(void) {
    read_script(&boot, getenv("NC_INPUT"));
    const char *path = getenv("NC_LEVEL_INPUT");
    if (path) {
        read_script(&level, path);
        if (!level.end) {
            NC_FATAL("missing or empty level replay %s", path);
        }
    }
}

bool replay_level_enabled(void) {
    return level.end != 0;
}

u16 replay_input(void) {
    static unsigned vblank; /* Input scripts advance even across a save-state restore. */
    vblank++;
    const InputScript *script = level_frame ? &level : &boot;
    unsigned frame = level_frame ? level_frame : vblank;
    u16 buttons = 0;
    for (unsigned i = 0; i < script->count; i++) {
        const ScriptStep *step = &script->steps[i];
        if (frame >= step->first && frame <= step->last) {
            buttons |= step->buttons;
        }
    }
    return buttons;
}

void replay_level_frame(void) {
    if (!replay_level_enabled()) {
        return;
    }
    /* Record the state before applying this frame's input on the next VBlank. */
    level_frame++;
    NC_LOG("replay %u %u %d %d %d", level_frame, (unsigned)MEM_R16(ADDR_PLAYER_HEADING),
           (s32)MEM_R32(ADDR_PLAYER_X), (s32)MEM_R32(ADDR_PLAYER_Z), (s16)MEM_R16(ADDR_ADRENALINE));
    if (level_frame > level.end) {
        exit(EXIT_SUCCESS);
    }
}
