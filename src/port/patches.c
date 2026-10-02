#include "port/patches.h"

#include "port/settings.h"

bool nc_flag_no_adrenaline;

void patches_apply(void) {
    nc_flag_no_adrenaline = !settings.adrenaline;
}
