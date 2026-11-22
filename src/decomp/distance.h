#ifndef NC_DECOMP_DISTANCE_H
#define NC_DECOMP_DISTANCE_H

#include "port/recomp.h"

bool nc_actor_distance_terms(CPUState *c);
bool nc_distance_root(CPUState *c);
bool nc_actor_heading(CPUState *c);
bool nc_prepare_local_point_heading(CPUState *c);
bool nc_prepare_reference_distance(CPUState *c);
bool nc_store_reference_distance(CPUState *c);
bool nc_prepare_relative_heading(CPUState *c);
bool nc_finish_relative_heading(CPUState *c);
bool nc_prepare_record_projection(CPUState *c);
bool nc_store_projected_heading(CPUState *c);
bool nc_select_projected_heading(CPUState *c, bool *reproject);
bool nc_projected_heading_gap(CPUState *c);
bool nc_prepare_saved_bearing_projection(CPUState *c);
bool nc_finish_projected_point_update(CPUState *c);
bool nc_prepare_facing_offset(CPUState *c);
bool nc_begin_projected_point_update(CPUState *c, bool *use_point);
bool nc_prepare_rotated_point_offset(CPUState *c, bool negative_z);
bool nc_apply_rotated_point_offset(CPUState *c, bool subtract_x);
bool nc_begin_actor_point_placement(CPUState *c);
bool nc_finish_actor_point_placement(CPUState *c);
bool nc_place_actor_points(CPUState *c);
bool nc_prepare_point_heading(CPUState *c);
bool nc_return_point_heading(CPUState *c);
bool nc_transform_part_offset(CPUState *c);
bool nc_compose_part_matrix(CPUState *c);
bool nc_finish_part_translation(CPUState *c);
bool nc_prepare_heading_matrix(CPUState *c);
bool nc_prepare_matrix_planar_length(CPUState *c);
bool nc_finish_matrix_headings(CPUState *c);
bool nc_vector_heading(CPUState *c);
bool nc_heading_cosine(CPUState *c);
bool nc_heading_sine(CPUState *c);
bool nc_transform_part_position(CPUState *c, bool quarter_scale);
bool nc_prepare_heading_projection(CPUState *c);
bool nc_finish_heading_projection(CPUState *c);
bool nc_project_heading_offset(CPUState *c);
bool nc_init_actor_coordinates(CPUState *c);

#endif
