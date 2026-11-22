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
bool nc_call_projected_point_heading(CPUState *c);
bool nc_store_projected_heading(CPUState *c);
bool nc_select_projected_heading(CPUState *c, bool *reproject);
bool nc_projected_heading_gap(CPUState *c);
bool nc_prepare_saved_bearing_projection(CPUState *c);
bool nc_finish_projected_point_update(CPUState *c, bool store_heading);
bool nc_prepare_facing_offset(CPUState *c);
bool nc_begin_projected_point_update(CPUState *c, bool *use_point);
bool nc_prepare_rotated_point_offset(CPUState *c, bool negative_z);
bool nc_apply_rotated_point_offset(CPUState *c, bool subtract_x);
bool nc_begin_actor_point_placement(CPUState *c);
bool nc_finish_actor_point_placement(CPUState *c);
bool nc_place_actor_points(CPUState *c);
bool nc_prepare_point_heading(CPUState *c);
bool nc_return_point_heading(CPUState *c);
bool nc_init_part_transform(CPUState *c);
bool nc_begin_part_chain(CPUState *c);
bool nc_append_part_parent(CPUState *c);
bool nc_prepare_part_chain_rotation(CPUState *c);
typedef enum { NC_NODE_FALLBACK, NC_NODE_NEXT, NC_NODE_POP, NC_NODE_RETURN } NcNodeContinuation;
NcNodeContinuation nc_advance_node_transform(CPUState *c, bool part_rotation);

bool nc_start_part_traversal(CPUState *c);
bool nc_restore_part_branch(CPUState *c);
bool nc_start_node_traversal(CPUState *c);
bool nc_restore_node_branch(CPUState *c);
bool nc_prepare_part_composition(CPUState *c);
bool nc_apply_part_matrix(CPUState *c);
bool nc_apply_node_matrix(CPUState *c);
bool nc_prepare_part_translation(CPUState *c);
bool nc_select_part_rotation(CPUState *c);
bool nc_select_node_transform(CPUState *c);
bool nc_prepare_node_rotation(CPUState *c);
typedef enum {
    NC_HEADING_FALLBACK,
    NC_HEADING_SKIP,
    NC_HEADING_FROM_PART,
    NC_HEADING_FROM_SHARED
} NcHeadingSource;
bool nc_build_traversal_matrix(CPUState *c);
bool nc_sample_traversal_angles(CPUState *c, bool shared);
NcHeadingSource nc_select_part_heading_source(CPUState *c);
bool nc_update_child_position(CPUState *c);
bool nc_filter_scaled_part(CPUState *c);
bool nc_begin_scaled_part_walk(CPUState *c);
NcNodeContinuation nc_advance_scaled_part_walk(CPUState *c);
bool nc_resume_scaled_part_walk(CPUState *c);
bool nc_start_scaled_part_position(CPUState *c);
bool nc_transform_part_offset(CPUState *c);
bool nc_compose_traversal_rotation(CPUState *c);
bool nc_compose_part_matrix(CPUState *c);
bool nc_finish_part_translation(CPUState *c);
bool nc_prepare_heading_matrix(CPUState *c);
bool nc_prepare_matrix_planar_length(CPUState *c);
bool nc_finish_matrix_headings(CPUState *c);
bool nc_vector_heading(CPUState *c);
typedef enum { NC_ROTATION_PART, NC_ROTATION_TRANSPOSED, NC_ROTATION_PACKED } NcRotationInput;

bool nc_finish_part_rotation(CPUState *c, NcRotationInput input);
bool nc_sample_part_rotation(CPUState *c, NcRotationInput input);
bool nc_store_target_vertical_velocity(CPUState *c);
bool nc_prepare_spatial_actor_bearing(CPUState *c);
bool nc_prepare_spatial_point_bearing(CPUState *c);
bool nc_prepare_spatial_distance(CPUState *c, bool actor);
bool nc_prepare_target_bearing(CPUState *c);
bool nc_prepare_target_distance(CPUState *c);
bool nc_prepare_planar_velocity(CPUState *c);
bool nc_finish_planar_velocity(CPUState *c);
bool nc_heading_cosine(CPUState *c);
bool nc_heading_sine(CPUState *c);
bool nc_transform_part_position(CPUState *c, bool quarter_scale);
bool nc_prepare_heading_projection(CPUState *c);
bool nc_finish_heading_projection(CPUState *c);
bool nc_project_heading_offset(CPUState *c);
bool nc_init_actor_coordinates(CPUState *c);

#endif
