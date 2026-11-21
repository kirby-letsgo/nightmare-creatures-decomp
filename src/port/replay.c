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
static unsigned hits_dealt, hits_taken;

void nc_hook_replay_damage(CPUState *c) {
    if (!level_frame)
        return;
    u32 player = MEM_R32(ADDR_PLAYER_ACTOR);
    u32 recipient = c->r[17], attacker = c->r[19];
    if ((s16)c->r[2] >= (s16)MEM_R16(recipient + ACTOR_HEALTH_OFFSET))
        return;
    if (recipient == player && attacker && attacker != player)
        hits_taken++;
    if (attacker == player && recipient != player)
        hits_dealt++;
}

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

/* A movie stores consecutive VBlanks with the same digital pad mask. Split long
 * holds into bounded runs so recording uses constant memory and regularly flushes. */
static FILE *reader, *writer;
static unsigned run_length, run_buttons;
static const char movie_header[] = "NC-PAD 1";

static void write_run(void) {
    if (run_length &&
        (fprintf(writer, "%u %04x\n", run_length, run_buttons) < 0 || fflush(writer) != 0)) {
        NC_FATAL("cannot write input recording");
    }
    run_length = 0;
}

static void finish_recording(void) {
    write_run();
    if (fputs("end\n", writer) == EOF || fclose(writer) != 0) {
        NC_FATAL("cannot finish input recording");
    }
}

bool replay_movie_active(void) {
    return reader || writer;
}

u16 replay_pad(u16 buttons) {
    if (writer) {
        if (buttons != run_buttons || run_length == 120)
            write_run();
        run_buttons = buttons;
        run_length++;
    } else if (reader) {
        if (!run_length) {
            char line[64], extra;
            if (!fgets(line, sizeof line, reader))
                NC_FATAL("input movie ended before its end marker");
            if (!strcmp(line, "end\n"))
                exit(EXIT_SUCCESS);
            if (sscanf(line, "%3u %4x %c", &run_length, &run_buttons, &extra) != 2 || !run_length ||
                run_length > 120)
                NC_FATAL("invalid input movie run: %s", line);
        }
        run_length--;
        buttons = (u16)run_buttons;
    }
    return buttons;
}

bool replay_init(const char *record, const char *play) {
    if (record && play) {
        NC_LOG("Choose either --record-input or --play-input");
        return false;
    }
    if (play) {
        char header[32];
        reader = fopen(play, "r");
        if (!reader || !fgets(header, sizeof header, reader) ||
            strncmp(header, movie_header, strlen(movie_header)) ||
            strcmp(header + strlen(movie_header), "\n")) {
            NC_LOG("Cannot read NC-PAD movie: %s", play);
            return false;
        }
        if (getenv("NC_INPUT") || getenv("NC_LEVEL_INPUT")) {
            NC_LOG("--play-input cannot be combined with another input script");
            return false;
        }
    }
    if (record) {
        writer = fopen(record, "wx");
        if (!writer || fprintf(writer, "%s\n", movie_header) < 0) {
            NC_LOG("Cannot create new input recording: %s", record);
            return false;
        }
        atexit(finish_recording);
    }
    read_script(&boot, getenv("NC_INPUT"));
    const char *path = getenv("NC_LEVEL_INPUT");
    if (path) {
        read_script(&level, path);
        if (!level.end) {
            NC_FATAL("missing or empty level replay %s", path);
        }
    }
    return true;
}

bool replay_level_enabled(void) {
    return level.end != 0 || reader;
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
    NC_LOG("replay %u %u %d %d %d %d %u %u", level_frame, (unsigned)MEM_R16(ADDR_PLAYER_HEADING),
           (s32)MEM_R32(ADDR_PLAYER_X), (s32)MEM_R32(ADDR_PLAYER_Z), (s16)MEM_R16(ADDR_ADRENALINE),
           (s16)MEM_R16(MEM_R32(ADDR_PLAYER_ACTOR) + ACTOR_HEALTH_OFFSET), hits_dealt, hits_taken);
    if (level.end && level_frame > level.end) {
        exit(EXIT_SUCCESS);
    }
}
