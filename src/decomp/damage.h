#ifndef NC_DECOMP_DAMAGE_H
#define NC_DECOMP_DAMAGE_H

#include "port/recomp.h"

typedef enum NcHitOutcome {
    NC_HIT_TIMED,
    NC_HIT_SURVIVED,
    NC_HIT_PLAYER_DEATH,
    NC_HIT_OTHER_DEATH,
} NcHitOutcome;

typedef enum NcReactionRoute {
    NC_REACTION_TIMED,
    NC_REACTION_SELECT_ACTION,
    NC_REACTION_STATE_13,
    NC_REACTION_APPLY_FLAGS,
} NcReactionRoute;

NcReactionRoute nc_route_hit_reaction(CPUState *c);
bool nc_enter_hit_state_13(CPUState *c, bool clear_hits);
NcHitOutcome nc_check_lethal_hit(CPUState *c);
bool nc_clear_hit_reaction_flags(CPUState *c);
bool nc_reset_hit_animation(CPUState *c);
bool nc_prepare_hit_reaction(CPUState *c, bool *skip);
bool nc_apply_lethal_reaction(CPUState *c, bool player);
bool nc_set_actor_state(CPUState *c);
bool nc_enter_death_state(CPUState *c);

/* Select table power and return damage in the game's 4096-point health scale. */
bool nc_select_base_damage(CPUState *c);

/* Compute the health value consumed by the store at US PSX2.EXE 8004EB78.
 * False leaves the guest state untouched for instruction-timed execution. */
bool nc_calculate_hit_health(CPUState *c, bool check_mode);

#endif
