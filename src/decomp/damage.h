#ifndef NC_DECOMP_DAMAGE_H
#define NC_DECOMP_DAMAGE_H

#include "port/recomp.h"

/* Select table power and return damage in the game's 4096-point health scale. */
bool nc_select_base_damage(CPUState *c);

/* Compute the health value consumed by the store at US PSX2.EXE 8004EB78.
 * False leaves the guest state untouched for instruction-timed execution. */
bool nc_calculate_hit_health(CPUState *c, bool check_mode);

#endif
