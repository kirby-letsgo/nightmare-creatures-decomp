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
#define ADDR_PLAYER_X 0x800CB170u       /* s32, world units << 8 */
#define ADDR_PLAYER_Z 0x800CB178u
#define ADDR_CAMERA_X 0x800CB160u /* s32, world units */
#define ADDR_CAMERA_Z 0x800CB168u
#define CAMERA_RECORD_YAW 0xAu /* offset of the yaw in the camera record (func_80049838) */

/* Pad bits as seen by the player-control routine (PS1 layout, 1 = pressed). */
enum { BTN_UP = 0x10, BTN_RIGHT = 0x20, BTN_DOWN = 0x40, BTN_LEFT = 0x80 };
#define BTN_DIRECTIONS (BTN_UP | BTN_RIGHT | BTN_DOWN | BTN_LEFT)

#define DEADZONE 0.30f
#define NC_PI 3.14159265358979323846f
#ifndef CAMERA_ORBIT_SIGN
#define CAMERA_ORBIT_SIGN -1
#endif
#define SNAP_UNITS 1100          /* within one turn step: face the target exactly */
#define WALK_WHILE_TURNING 12743 /* ~70 degrees: start walking once roughly facing the target */
#define BACKPEDAL_FROM 24576     /* 135 degrees: a target this far behind means walk backwards */

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

/* --- free-look camera ------------------------------------------------------------------- */

#define FREELOOK_RETURN_DELAY_MS 1000
#define FREELOOK_RETURN_RATE 0.06f /* fraction of the offset removed per game frame */
#define STICK_TURN_RATE 60.0f      /* 4096-units per game frame at full right-stick deflection */

static float camera_offset; /* 4096 units, added to the game's camera yaw */
extern unsigned nc_frame_count;
static Uint64 last_look_ns;

static s32 camera_offset_units(void) {
    return (s32)lroundf(camera_offset);
}

/* Mouse and right-stick look. Called once per game frame from the player-control hook. */
static void update_freelook(void) {
    float delta = 0.0f;
    float mx = 0.0f;
    SDL_Window *win = SDL_GetKeyboardFocus();
    if (settings.mouse_camera && win != NULL) {
        if (!SDL_GetWindowRelativeMouseMode(win)) {
            SDL_SetWindowRelativeMouseMode(win, true);
        }
        SDL_GetRelativeMouseState(&mx, NULL);
        delta -= mx * (float)settings.mouse_sensitivity * 0.05f;
    }
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; i++) {
        SDL_Gamepad *gp = SDL_GetGamepadFromID(ids[i]);
        float rx = gp ? SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f : 0.0f;
        if (rx * rx > DEADZONE * DEADZONE) {
            delta -= rx * STICK_TURN_RATE;
        }
    }
    SDL_free(ids);

    /* Testing aid: NC_CAM_TEST=<frame> holds a 90-degree offset from that VBlank on. */
    static int test_frame = -1;
    if (test_frame < 0) {
        const char *env = getenv("NC_CAM_TEST");
        test_frame = env ? atoi(env) : 0;
    }
    if (test_frame > 0 && nc_frame_count >= (unsigned)test_frame) {
        camera_offset = 1024.0f;
        last_look_ns = SDL_GetTicksNS();
        return;
    }

    Uint64 now = SDL_GetTicksNS();
    if (delta != 0.0f) {
        camera_offset += delta;
        last_look_ns = now;
    } else if (now - last_look_ns > (Uint64)FREELOOK_RETURN_DELAY_MS * 1000000u) {
        camera_offset -= camera_offset * FREELOOK_RETURN_RATE;
        if (fabsf(camera_offset) < 1.0f) {
            camera_offset = 0.0f;
        }
    }
    /* Keep within half a turn either way so the return goes the short way round. */
    camera_offset = remainderf(camera_offset, 4096.0f);
}

/* View build entry: orbit the camera around the player by the free-look offset. */
static s32 saved_cam_x, saved_cam_z;
static u16 saved_yaw;
static bool view_patched;

static u32 patched_record;

void nc_hook_camera_view_begin(CPUState *c) {
    s32 off = camera_offset_units();
    if (off == 0 || view_patched) {
        return;
    }
    view_patched = true;
    u32 record = c->r[4];
    patched_record = record;
    saved_cam_x = (s32)MEM_R32(ADDR_CAMERA_X);
    saved_cam_z = (s32)MEM_R32(ADDR_CAMERA_Z);
    saved_yaw = MEM_R16(record + CAMERA_RECORD_YAW);

    float px = (float)(s32)MEM_R32(ADDR_PLAYER_X) / 256.0f;
    float pz = (float)(s32)MEM_R32(ADDR_PLAYER_Z) / 256.0f;
    float dx = (float)saved_cam_x - px, dz = (float)saved_cam_z - pz;
    float a = (float)off * (2.0f * NC_PI / 4096.0f) * (float)CAMERA_ORBIT_SIGN;
    float ca = cosf(a), sa = sinf(a);
    MEM_W32(ADDR_CAMERA_X, (u32)(s32)lroundf(px + dx * ca - dz * sa));
    MEM_W32(ADDR_CAMERA_Z, (u32)(s32)lroundf(pz + dx * sa + dz * ca));
    MEM_W16(record + CAMERA_RECORD_YAW, (u16)(saved_yaw + off));
}

void nc_hook_camera_update_begin(CPUState *c) {
    (void)c;
    controls_unpatch_camera();
}

void controls_unpatch_camera(void) {
    if (!view_patched) {
        return;
    }
    MEM_W32(ADDR_CAMERA_X, (u32)saved_cam_x);
    MEM_W32(ADDR_CAMERA_Z, (u32)saved_cam_z);
    MEM_W16(patched_record + CAMERA_RECORD_YAW, saved_yaw);
    view_patched = false;
}

static s32 wrap16(s32 v) {
    return (s32)(s16)(u16)v;
}

void nc_hook_player_input(CPUState *c) {
    static bool latched;
    static s32 latched_camera; /* 65536 units */

    update_freelook();
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
        latched_camera = ((s32)MEM_R16(ADDR_CAMERA_YAW) + camera_offset_units()) * 16;
        latched = true;
    }

    /* Screen up = straight ahead (camera yaw); screen left = +90 degrees (turning left). */
    float angle = atan2f(-x, -y);
    s32 target = latched_camera + (s32)lroundf(angle * (65536.0f / (2.0f * NC_PI)));
    s32 heading = (s32)MEM_R16(ADDR_PLAYER_HEADING);
    s32 diff = wrap16(target - heading);

    if (abs(diff) >= BACKPEDAL_FROM) {
        /* Target roughly behind the player: walk backwards (the game's own move) instead of
         * turning all the way round, steering so the back faces the target. */
        s32 back_diff = wrap16(target + 32768 - heading);
        buttons |= BTN_DOWN;
        if (abs(back_diff) > SNAP_UNITS) {
            buttons |= back_diff > 0 ? BTN_LEFT : BTN_RIGHT;
        }
    } else if (abs(diff) <= SNAP_UNITS) {
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

void controls_reset(void) {
    controls_unpatch_camera();
    camera_offset = 0.0f;
}
