#ifndef NC_DECOMP_DISTANCE_H
#define NC_DECOMP_DISTANCE_H

#include "port/recomp.h"

bool nc_actor_distance_terms(CPUState *c);
bool nc_distance_root(CPUState *c);
bool nc_actor_heading(CPUState *c);
bool nc_prepare_local_point_heading(CPUState *c);
bool nc_prepare_reference_distance(CPUState *c);
bool nc_store_reference_distance(CPUState *c);
bool nc_prepare_point_heading(CPUState *c);
bool nc_return_point_heading(CPUState *c);
bool nc_vector_heading(CPUState *c);
bool nc_heading_cosine(CPUState *c);
bool nc_heading_sine(CPUState *c);
bool nc_transform_part_position(CPUState *c, bool quarter_scale);
bool nc_prepare_heading_projection(CPUState *c);
bool nc_finish_heading_projection(CPUState *c);
bool nc_project_heading_offset(CPUState *c);
bool nc_init_actor_coordinates(CPUState *c);

#endif
