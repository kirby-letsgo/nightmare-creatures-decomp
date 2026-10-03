/* Modern (camera-relative) controls.
 *
 * The game only has tank controls: left/right rotate the player, up/down walk. The hook below
 * runs inside the player-control routine (config/hooks.txt) right after it reads the pad, and in
 * modern mode replaces the direction bits: the stick direction, taken relative to the camera,
 * becomes a target heading; the player is turned towards it with left/right and walks forward
 * once roughly facing it. Menus read the pad elsewhere and keep normal d-pad behaviour.
 *
 * The camera follows behind the player, so a camera-relative target would chase itself while
 * turning. As in other tank-to-modern conversions, the camera angle is latched when a direction
 * is first pushed and kept until the stick returns to neutral. */
#include "port/controls.h"

#include "port/runtime.h"
#include "port/settings.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>

/* Game variables (PSX2.EXE), located by RAM diffing (see docs/analysis.md). */
#define ADDR_PLAYER_HEADING 0x800CB1CCu /* u16, 65536 = full turn, turning left increases it */
#define ADDR_CAMERA_YAW 0x800D763Au     /* u16, 4096 = full turn, eases towards the player */

/* Pad bits as seen by the player-control routine (PS1 layout, 1 = pressed). */
enum { BTN_UP = 0x10, BTN_RIGHT = 0x20, BTN_DOWN = 0x40, BTN_LEFT = 0x80 };
#define BTN_DIRECTIONS (BTN_UP | BTN_RIGHT | BTN_DOWN | BTN_LEFT)

#define DEADZONE 0.30f
#define SNAP_UNITS 1100          /* within one turn step: face the target exactly */
#define WALK_WHILE_TURNING 12743 /* ~70 degrees: start walking once roughly facing the target */

/* Direction the player is pushing, in screen space (x right, y down). Digital directions come
 * from the emulated pad (keyboard, d-pad, scripted input); a gamepad's left stick, when moved,
 * gives a precise analog direction instead. */
static void read_stick(float *x, float *y) {
    u16 pad = bios_get_pad();
    float sx = (float)((pad & BTN_RIGHT) != 0) - (float)((pad & BTN_LEFT) != 0);
    float sy = (float)((pad & BTN_DOWN) != 0) - (float)((pad & BTN_UP) != 0);

    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; i++) {
        SDL_Gamepad *gp = SDL_GetGamepadFromID(ids[i]);
        if (gp == NULL) {
            continue;
        }
        float ax = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
        float ay = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
        if (ax * ax + ay * ay > DEADZONE * DEADZONE) {
            sx = ax;
            sy = ay;
            break;
        }
    }
    SDL_free(ids);
    *x = sx;
    *y = sy;
}

static s32 wrap16(s32 v) {
    return (s32)(s16)(u16)v;
}

void nc_hook_player_input(CPUState *c) {
    static bool latched;
    static s32 latched_camera; /* 65536 units */

    if (settings.controls != CONTROLS_MODERN) {
        return;
    }
    float x, y;
    read_stick(&x, &y);
    u32 buttons = c->r[2] & ~(u32)BTN_DIRECTIONS;
    if (x * x + y * y < DEADZONE * DEADZONE) {
        latched = false;
        c->r[2] = buttons;
        return;
    }
    if (!latched) {
        latched_camera = (s32)MEM_R16(ADDR_CAMERA_YAW) * 16;
        latched = true;
    }

    /* Screen up = straight ahead (camera yaw); screen left = +90 degrees (turning left). */
    float angle = atan2f(-x, -y);
    s32 target = latched_camera + (s32)lroundf(angle * (65536.0f / (2.0f * (float)M_PI)));
    s32 heading = (s32)MEM_R16(ADDR_PLAYER_HEADING);
    s32 diff = wrap16(target - heading);

    if (abs(diff) <= SNAP_UNITS) {
        MEM_W16(ADDR_PLAYER_HEADING, (u16)target);
        buttons |= BTN_UP;
    } else {
        buttons |= diff > 0 ? BTN_LEFT : BTN_RIGHT;
        if (abs(diff) <= WALK_WHILE_TURNING) {
            buttons |= BTN_UP;
        }
    }
    c->r[2] = buttons;
}
