/* Runtime patch flags used by the recompiled code (config/patches.txt). */
#ifndef NC_PORT_PATCHES_H
#define NC_PORT_PATCHES_H

#include <stdbool.h>

extern bool nc_flag_no_adrenaline;

/* Sets the patch flags from the current settings. */
void patches_apply(void);

#endif
