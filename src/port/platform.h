/* Small platform-specific helpers. */
#ifndef NC_PORT_PLATFORM_H
#define NC_PORT_PLATFORM_H

/* Keeps the OS from throttling the game when its window is hidden or covered (macOS App Nap),
 * so emulation, music and scripted runs keep full speed. No-op elsewhere. */
void platform_keep_awake(void);

#endif
