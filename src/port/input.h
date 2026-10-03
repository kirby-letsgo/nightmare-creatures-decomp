/* Keyboard and gamepad input with user-configurable bindings (Settings > Button mapping). */
#ifndef NC_PORT_INPUT_H
#define NC_PORT_INPUT_H

#include "port/recomp.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

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

#define INPUT_BUTTON_COUNT 14

/* Gamepad binding values: an SDL_GamepadButton, one of these, or INPUT_UNBOUND. */
#define INPUT_UNBOUND (-1)
#define INPUT_PAD_LEFT_TRIGGER 100
#define INPUT_PAD_RIGHT_TRIGGER 101

typedef struct InputButton {
    const char *name; /* shown in the menu */
    const char *id;   /* settings.ini key suffix */
    u16 bit;
} InputButton;

extern const InputButton input_buttons[INPUT_BUTTON_COUNT];

int input_default_key(int button);
int input_default_pad(int button);

/* Display names and settings.ini names for bindings. */
const char *input_key_label(int scancode);
const char *input_pad_label(int binding);
const char *input_pad_id(int binding);
int input_pad_from_id(const char *id);

/* Keys the port uses for itself (menu, save states, fullscreen); not bindable. */
bool input_key_reserved(SDL_Scancode key);
/* Shortcut modifier: Cmd on macOS, Ctrl elsewhere. */
bool input_shortcut_modifier(SDL_Keymod mod);

/* Current pad state from the keyboard / all connected gamepads (left stick = d-pad). */
u16 input_read_keyboard(void);
u16 input_read_gamepads(void);

#endif
