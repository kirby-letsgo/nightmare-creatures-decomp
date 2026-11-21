#include "port/android_pad.h"

#include "port/input.h"

/* Normalized touch coordinates and rendered rectangles share a 640 x 360 layout.
 * The direction zones overlap at their corners so one finger can walk and turn. */
typedef struct TouchButton {
    SDL_FRect rect;
    u16 bit;
    const char *label;
} TouchButton;

static const TouchButton buttons[] = {
    {{42, 224, 56, 47}, PAD_UP, "UP"},
    {{42, 293, 56, 47}, PAD_DOWN, "DN"},
    {{6, 255, 54, 54}, PAD_LEFT, "<"},
    {{80, 255, 54, 54}, PAD_RIGHT, ">"},
    {{548, 207, 46, 41}, PAD_TRIANGLE, "TRI"},
    {{500, 252, 46, 41}, PAD_SQUARE, "SQ"},
    {{596, 252, 42, 41}, PAD_CIRCLE, "O"},
    {{548, 297, 46, 41}, PAD_CROSS, "X"},
    {{8, 20, 52, 36}, PAD_L2, "L2"},
    {{65, 20, 52, 36}, PAD_L1, "L1"},
    {{523, 20, 52, 36}, PAD_R1, "R1"},
    {{580, 20, 52, 36}, PAD_R2, "R2"},
    {{251, 313, 65, 32}, PAD_SELECT, "SELECT"},
    {{324, 313, 65, 32}, PAD_START, "START"},
};

u16 android_pad_read(void) {
    if (SDL_HasGamepad()) {
        return 0;
    }
    u16 pressed = 0;
    int count = 0;
    SDL_TouchID *devices = SDL_GetTouchDevices(&count);
    for (int d = 0; d < count; d++) {
        int n = 0;
        SDL_Finger **fingers = SDL_GetTouchFingers(devices[d], &n);
        for (int f = 0; f < n; f++) {
            SDL_FPoint point = {fingers[f]->x * 640, fingers[f]->y * 360};
            for (size_t i = 0; i < SDL_arraysize(buttons); i++) {
                if (SDL_PointInRectFloat(&point, &buttons[i].rect)) {
                    pressed |= buttons[i].bit;
                }
            }
        }
        SDL_free(fingers);
    }
    SDL_free(devices);
    return pressed;
}

void android_pad_draw(SDL_Renderer *renderer, u16 pressed) {
    int w, h;
    if (SDL_HasGamepad() || !SDL_GetRenderOutputSize(renderer, &w, &h) || w <= 0 || h <= 0) {
        return;
    }
    SDL_SetRenderScale(renderer, w / 640.0f, h / 360.0f);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (size_t i = 0; i < SDL_arraysize(buttons); i++) {
        const TouchButton *b = &buttons[i];
        SDL_SetRenderDrawColor(renderer, 40, 16, 24, (pressed & b->bit) ? 200 : 92);
        SDL_RenderFillRect(renderer, &b->rect);
        SDL_SetRenderDrawColor(renderer, 245, 234, 230, 170);
        SDL_RenderRect(renderer, &b->rect);
        SDL_RenderDebugText(renderer, b->rect.x + (b->rect.w - 8 * SDL_strlen(b->label)) / 2,
                            b->rect.y + (b->rect.h - 8) / 2, b->label);
    }
    SDL_SetRenderScale(renderer, 1, 1);
}
