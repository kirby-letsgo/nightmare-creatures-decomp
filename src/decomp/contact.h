#ifndef NC_DECOMP_CONTACT_H
#define NC_DECOMP_CONTACT_H

#include "port/recomp.h"

typedef enum NcContactResult {
    NC_CONTACT_TIMED, /* run the original block so an interrupt can occur inside it */
    NC_CONTACT_REJECT,
    NC_CONTACT_PASS,
} NcContactResult;

NcContactResult nc_attack_bounds(CPUState *c);
bool nc_prepare_attack_box(CPUState *c, bool outgoing);
NcContactResult nc_target_accepts_contact(CPUState *c);

#endif
