#include "port/input.h"

#include "port/settings.h"

#include <string.h>

const InputButton input_buttons[INPUT_BUTTON_COUNT] = {
    {"Up", "up", PAD_UP},
    {"Down", "down", PAD_DOWN},
    {"Left", "left", PAD_LEFT},
    {"Right", "right", PAD_RIGHT},
    {"Cross", "cross", PAD_CROSS},
    {"Circle", "circle", PAD_CIRCLE},
    {"Square", "square", PAD_SQUARE},
    {"Triangle", "triangle", PAD_TRIANGLE},
    {"L1", "l1", PAD_L1},
    {"R1", "r1", PAD_R1},
    {"L2", "l2", PAD_L2},
    {"R2", "r2", PAD_R2},
    {"Start", "start", PAD_START},
    {"Select", "select", PAD_SELECT},
};

static const int default_keys[INPUT_BUTTON_COUNT] = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT,   SDL_SCANCODE_RIGHT,     SDL_SCANCODE_X,
    SDL_SCANCODE_C,  SDL_SCANCODE_Z,    SDL_SCANCODE_S,      SDL_SCANCODE_Q,         SDL_SCANCODE_W,
    SDL_SCANCODE_1,  SDL_SCANCODE_2,    SDL_SCANCODE_RETURN, SDL_SCANCODE_BACKSPACE,
};

static const int default_pads[INPUT_BUTTON_COUNT] = {
    SDL_GAMEPAD_BUTTON_DPAD_UP,       SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    SDL_GAMEPAD_BUTTON_DPAD_LEFT,     SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    SDL_GAMEPAD_BUTTON_SOUTH,         SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_WEST,          SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    INPUT_PAD_LEFT_TRIGGER,           INPUT_PAD_RIGHT_TRIGGER,
    SDL_GAMEPAD_BUTTON_START,         SDL_GAMEPAD_BUTTON_BACK,
};

int input_default_key(int button) {
    return default_keys[button];
}

int input_default_pad(int button) {
    return default_pads[button];
}

const char *input_key_label(int scancode) {
    if (scancode <= 0) {
        return "-";
    }
    const char *name = SDL_GetScancodeName((SDL_Scancode)scancode);
    return name != NULL && name[0] != '\0' ? name : "?";
}

/* Gamepad buttons by position (layouts differ: south is A on Xbox, Cross on PlayStation). */
const char *input_pad_label(int binding) {
    switch (binding) {
    case INPUT_UNBOUND:
        return "-";
    case INPUT_PAD_LEFT_TRIGGER:
        return "Left trigger";
    case INPUT_PAD_RIGHT_TRIGGER:
        return "Right trigger";
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return "South (A / Cross)";
    case SDL_GAMEPAD_BUTTON_EAST:
        return "East (B / Circle)";
    case SDL_GAMEPAD_BUTTON_WEST:
        return "West (X / Square)";
    case SDL_GAMEPAD_BUTTON_NORTH:
        return "North (Y / Triangle)";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return "Left shoulder";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return "Right shoulder";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return "Left stick click";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return "Right stick click";
    case SDL_GAMEPAD_BUTTON_START:
        return "Start / Menu";
    case SDL_GAMEPAD_BUTTON_BACK:
        return "Back / View";
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return "D-pad up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return "D-pad down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return "D-pad left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return "D-pad right";
    default: {
        const char *s = SDL_GetGamepadStringForButton((SDL_GamepadButton)binding);
        return s != NULL ? s : "?";
    }
    }
}

const char *input_pad_id(int binding) {
    switch (binding) {
    case INPUT_UNBOUND:
        return "none";
    case INPUT_PAD_LEFT_TRIGGER:
        return "lefttrigger";
    case INPUT_PAD_RIGHT_TRIGGER:
        return "righttrigger";
    default: {
        const char *s = SDL_GetGamepadStringForButton((SDL_GamepadButton)binding);
        return s != NULL ? s : "none";
    }
    }
}

int input_pad_from_id(const char *id) {
    if (strcmp(id, "lefttrigger") == 0) {
        return INPUT_PAD_LEFT_TRIGGER;
    }
    if (strcmp(id, "righttrigger") == 0) {
        return INPUT_PAD_RIGHT_TRIGGER;
    }
    SDL_GamepadButton b = SDL_GetGamepadButtonFromString(id);
    return b == SDL_GAMEPAD_BUTTON_INVALID ? INPUT_UNBOUND : (int)b;
}

bool input_key_reserved(SDL_Scancode key) {
    return key == SDL_SCANCODE_ESCAPE || key == SDL_SCANCODE_F5 || key == SDL_SCANCODE_F9 ||
           key == SDL_SCANCODE_F11;
}

bool input_shortcut_modifier(SDL_Keymod mod) {
#ifdef __APPLE__
    return (mod & SDL_KMOD_GUI) != 0;
#else
    return (mod & SDL_KMOD_CTRL) != 0;
#endif
}

u16 input_read_keyboard(void) {
    /* Keys pressed together with Cmd/Ctrl are shortcuts (Cmd+S, Cmd+R), not game input. */
    if (input_shortcut_modifier(SDL_GetModState())) {
        return 0;
    }
    int numkeys = 0;
    const bool *keys = SDL_GetKeyboardState(&numkeys);
    u16 buttons = 0;
    for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
        int k = settings.key_bind[i];
        if (k > 0 && k < numkeys && keys[k]) {
            buttons |= input_buttons[i].bit;
        }
    }
    return buttons;
}

u16 input_read_gamepads(void) {
    enum { STICK_DEADZONE = 12000, TRIGGER_THRESHOLD = 8000 };
    u16 buttons = 0;
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int g = 0; g < count; g++) {
        SDL_Gamepad *pad = SDL_GetGamepadFromID(ids[g]);
        if (pad == NULL) {
            continue;
        }
        for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
            int b = settings.pad_bind[i];
            bool down = false;
            if (b == INPUT_PAD_LEFT_TRIGGER) {
                down = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > TRIGGER_THRESHOLD;
            } else if (b == INPUT_PAD_RIGHT_TRIGGER) {
                down = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > TRIGGER_THRESHOLD;
            } else if (b >= 0) {
                down = SDL_GetGamepadButton(pad, (SDL_GamepadButton)b);
            }
            if (down) {
                buttons |= input_buttons[i].bit;
            }
        }
        /* The left stick always works as the d-pad. */
        Sint16 x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
        Sint16 y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        buttons |= x < -STICK_DEADZONE ? PAD_LEFT : (x > STICK_DEADZONE ? PAD_RIGHT : 0);
        buttons |= y < -STICK_DEADZONE ? PAD_UP : (y > STICK_DEADZONE ? PAD_DOWN : 0);
    }
    SDL_free(ids);
    return buttons;
}
