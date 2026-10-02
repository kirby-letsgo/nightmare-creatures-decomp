/* Start screen and in-game menu (Dear ImGui over SDL3). */
#ifndef NC_PORT_UI_MENU_H
#define NC_PORT_UI_MENU_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MenuResult { MENU_START, MENU_RESUME, MENU_QUIT } MenuResult;

/* Draws the game's last frame behind the menu. */
typedef void (*MenuDrawBackground)(void);

void menu_init(SDL_Window *window, SDL_Renderer *renderer);
void menu_shutdown(void);

/* Blocking loops; both return when the player picks an action. */
MenuResult menu_run_start(void);
MenuResult menu_run_pause(MenuDrawBackground draw_background);

/* Applies settings that take effect immediately (volumes, fullscreen, filtering). */
void menu_apply_settings(void);

#ifdef __cplusplus
}
#endif

#endif
