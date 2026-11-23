#ifndef NC_DECOMP_DISTANCE_H
#define NC_DECOMP_DISTANCE_H

#include "port/recomp.h"

bool nc_begin_actor_distance(CPUState *c);
bool nc_call_actor_distance_root(CPUState *c);
bool nc_finish_actor_distance(CPUState *c, bool missing_target);
bool nc_save_point_update_frame(CPUState *c);
bool nc_select_point_update_row(CPUState *c);
bool nc_call_record_point_bearing(CPUState *c);
bool nc_set_linked_heading_offset(CPUState *c);
bool nc_apply_linked_heading_offset(CPUState *c);
bool nc_follow_heading_link(CPUState *c, bool first);
bool nc_copy_linked_heading_offset(CPUState *c);
bool nc_set_unlinked_pose_heading(CPUState *c);
bool nc_apply_unlinked_pose_bearing(CPUState *c);
bool nc_copy_linked_bearing(CPUState *c);
bool nc_check_projection_bypass(CPUState *c);
bool nc_project_record_point(CPUState *c);
bool nc_check_record_projection_limit(CPUState *c);
bool nc_begin_placement_point_scan(CPUState *c);
bool nc_test_placement_point_index(CPUState *c);
bool nc_advance_placement_point_index(CPUState *c);
bool nc_finish_placement_point_scan(CPUState *c);
bool nc_check_placement_point(CPUState *c);
bool nc_call_placement_node_traversal(CPUState *c);
bool nc_call_placement_angle_update(CPUState *c);
bool nc_store_placement_angle_result(CPUState *c);
bool nc_call_placement_part_traversal(CPUState *c);
bool nc_place_record_pose(CPUState *c);
bool nc_write_textured_triangle(CPUState *c);
bool nc_return_placement_faces(CPUState *c, u32 packet_offset, u32 count_offset, u32 face_count);
bool nc_advance_placement_face(CPUState *c, bool textured, bool quad);
bool nc_set_untextured_packet_color(CPUState *c, bool quad, bool indexed);
bool nc_finish_untextured_packet(CPUState *c, bool quad);
bool nc_finish_textured_packet(CPUState *c, bool quad);
bool nc_link_placement_packet(CPUState *c, unsigned table_reg, u32 tag_mask, u32 address_mask,
                              u32 packet_size);
bool nc_write_textured_quad(CPUState *c);
bool nc_write_projected_triangle(CPUState *c);
bool nc_write_projected_quad(CPUState *c);
bool nc_begin_textured_faces(CPUState *c, bool quad);
bool nc_read_textured_face_count(CPUState *c, bool quad);
bool nc_init_textured_face_cursors(CPUState *c, bool quad);
bool nc_apply_textured_face_flags(CPUState *c, bool quad);
bool nc_begin_untextured_triangles(CPUState *c);
bool nc_begin_untextured_quads(CPUState *c);
bool nc_select_untextured_faces(CPUState *c, bool quad);
bool nc_measure_untextured_triangle(CPUState *c);
bool nc_measure_placement_quad(CPUState *c, bool textured);
bool nc_measure_placement_triangle(CPUState *c);
bool nc_check_placement_face(CPUState *c, unsigned first_reg, u32 count, u32 limit, bool *accept);
bool nc_begin_pose_traversal(CPUState *c, bool small_frame);
bool nc_placement_lighting_offset(CPUState *c);
bool nc_begin_placement_lighting(CPUState *c);
bool nc_install_placement_matrices(CPUState *c, u32 continuation);
bool nc_project_placement_depth(CPUState *c, u32 pose, u32 depth_offset, unsigned depth_reg);
bool nc_apply_placement_depth_cue(CPUState *c, u32 depth_offset, unsigned factor_reg);
bool nc_begin_projection_traversal(CPUState *c);
bool nc_disable_projection_effect(CPUState *c);
bool nc_call_projection_lighting(CPUState *c, u32 entry);
bool nc_apply_projection_intensity(CPUState *c);
bool nc_check_projection_effect(CPUState *c, bool *active);
bool nc_set_projection_pose_mask(CPUState *c);
bool nc_load_projection_pose_descriptor(CPUState *c);
bool nc_load_projection_pose_flags(CPUState *c);
bool nc_filter_projection_pose(CPUState *c, bool *project);
bool nc_select_projection_pose(CPUState *c, bool index_ready);
bool nc_set_projection_pose_cursor(CPUState *c);
bool nc_select_placement_pose(CPUState *c, bool index_ready, u32 output_offset);
bool nc_save_linked_placement_pose(CPUState *c);
bool nc_push_projection_pose(CPUState *c);
bool nc_advance_projection_pose(CPUState *c);
bool nc_advance_placement_pose(CPUState *c);
bool nc_select_projection_packet_mode(CPUState *c);
bool nc_call_projection_textured_faces(CPUState *c, bool quad);
bool nc_check_untextured_projection_mode(CPUState *c);
bool nc_call_projection_untextured_faces(CPUState *c, bool quad);
bool nc_return_projection_traversal(CPUState *c);
bool nc_return_linked_pose_traversal(CPUState *c);
bool nc_return_placement_projection(CPUState *c);
bool nc_begin_placement_vertex_projection(CPUState *c);
bool nc_prepare_placement_vertex_groups(CPUState *c);
bool nc_check_placement_vertex_remainder(CPUState *c);
bool nc_return_placement_vertices(CPUState *c);
bool nc_project_placement_vertex_triple(CPUState *c, unsigned depth_shift);
bool nc_select_indexed_face_poses(CPUState *c, bool textured);
bool nc_prepare_indexed_vertex(CPUState *c, u32 pose, u32 vertex_index);
bool nc_store_indexed_vertex_depth(CPUState *c, u32 depth_offset, u32 next_pose);
bool nc_measure_indexed_face(CPUState *c, u32 return_pc);
bool nc_set_indexed_face_flags(CPUState *c);
bool nc_begin_indexed_face_list(CPUState *c, bool textured);
bool nc_load_indexed_packet_limit(CPUState *c, bool textured);
bool nc_begin_indexed_faces(CPUState *c);
bool nc_return_indexed_faces(CPUState *c);
bool nc_advance_indexed_face(CPUState *c, bool textured);
bool nc_save_indexed_face_cursors(CPUState *c, bool textured);
bool nc_link_indexed_face_packet(CPUState *c, bool textured);
bool nc_write_indexed_face_packet(CPUState *c, bool textured);
bool nc_project_indexed_vertex(CPUState *c, u32 screen_offset);
bool nc_project_remaining_placement_vertex(CPUState *c, bool sixteenth_depth);
bool nc_call_indexed_pose_projection(CPUState *c);
bool nc_call_pose_vertex_projection(CPUState *c, u32 return_pc);
bool nc_check_placement_transform(CPUState *c);
bool nc_select_placement_projection_args(CPUState *c);
bool nc_begin_placement_angle_update(CPUState *c);
bool nc_set_type7_pose_angles(CPUState *c);
bool nc_set_type7_linked_heading(CPUState *c);
bool nc_set_other_placement_angles(CPUState *c);
bool nc_finish_placement_angle_step(CPUState *c);
bool nc_begin_record_point_heading(CPUState *c);
bool nc_return_record_point_heading(CPUState *c);
bool nc_expand_record_point_bearing(CPUState *c);
bool nc_lookup_point_row_record(CPUState *c);
bool nc_prepare_point_distance_root(CPUState *c);
bool nc_store_point_pose_heading(CPUState *c);
bool nc_prepare_point_coordinate_query(CPUState *c);
bool nc_advance_point_index(CPUState *c);
bool nc_finish_point_update(CPUState *c);
bool nc_restore_point_update_frame(CPUState *c);
bool nc_check_point_proximity(CPUState *c, bool from_query);
bool nc_step_point_x(CPUState *c);
bool nc_step_point_z(CPUState *c);
bool nc_turn_toward_point(CPUState *c);
bool nc_call_point_distance_bearing(CPUState *c);
bool nc_actor_distance_terms(CPUState *c);
bool nc_distance_root(CPUState *c);
bool nc_actor_heading(CPUState *c);
bool nc_prepare_local_point_heading(CPUState *c);
bool nc_prepare_reference_distance(CPUState *c);
bool nc_store_reference_distance(CPUState *c);
typedef enum {
    NC_PART_SEARCH_FALLBACK,
    NC_PART_SEARCH_NEXT_HEADING,
    NC_PART_SEARCH_NEXT_PART,
    NC_PART_SEARCH_DONE,
} NcPartSearchStep;
NcPartSearchStep nc_advance_part_heading_search(CPUState *c, bool matched);
bool nc_restore_part_search_frame(CPUState *c);
bool nc_begin_part_pose_heading(CPUState *c);
bool nc_check_part_heading_turn(CPUState *c);
bool nc_reset_part_pose_heading(CPUState *c);
typedef enum {
    NC_PART_FLAGS_FALLBACK,
    NC_PART_FLAGS_FIRST,
    NC_PART_FLAGS_LATER,
    NC_PART_FLAGS_CLEAR,
} NcPartFlagPath;
NcPartFlagPath nc_check_turn_parts(CPUState *c);
bool nc_finish_part_turn(CPUState *c);
bool nc_reset_part_turn_counter(CPUState *c);
bool nc_advance_part_pose_heading(CPUState *c, bool double_step, bool *search);
bool nc_begin_part_heading_search(CPUState *c);
bool nc_test_part_heading(CPUState *c);
bool nc_select_part_bearing_point(CPUState *c);
bool nc_prepare_part_point_bearing(CPUState *c);
bool nc_call_part_with_bearing(CPUState *c);
bool nc_select_bearing_point(CPUState *c);
bool nc_prepare_selected_point_bearing(CPUState *c);
bool nc_finish_selected_point_bearing(CPUState *c);
bool nc_prepare_reference_bearing(CPUState *c);
bool nc_call_reference_facing(CPUState *c);
bool nc_finish_reference_bearing(CPUState *c);
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
bool nc_call_spatial_attenuation(CPUState *c, bool actor);
typedef enum {
    NC_POINT_LEVEL_FALLBACK,
    NC_POINT_LEVEL_SET,
    NC_POINT_LEVEL_DISTANT
} NcPointLevelAction;
typedef enum {
    NC_ACTOR_LEVEL_FALLBACK,
    NC_ACTOR_LEVEL_CHECK_FLAGS,
    NC_ACTOR_LEVEL_DISTANT,
    NC_ACTOR_LEVEL_DISTANT_RECORD_READY,
    NC_ACTOR_LEVEL_SET,
    NC_ACTOR_LEVEL_CHANNEL_READY
} NcActorLevelPath;
NcActorLevelPath nc_check_actor_level_distance(CPUState *c);
NcActorLevelPath nc_check_actor_level_flags(CPUState *c);
bool nc_call_distant_actor(CPUState *c, bool load_record);
bool nc_set_actor_levels(CPUState *c, bool load_channel);
NcPointLevelAction nc_select_point_levels(CPUState *c, bool flagged);
bool nc_scale_spatial_level(CPUState *c, bool actor);
bool nc_spatial_attenuation(CPUState *c);
bool nc_prepare_spatial_actor_bearing(CPUState *c);
bool nc_begin_spatial_point(CPUState *c);
bool nc_begin_spatial_actor(CPUState *c);
bool nc_finish_spatial_frame(CPUState *c, bool actor, bool distant);
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
bool nc_step_record_angle(CPUState *c);
bool nc_begin_placement_defaults(CPUState *c);
bool nc_finish_placement_defaults(CPUState *c);
bool nc_sample_placement_axes(CPUState *c);
bool nc_store_placement_axes(CPUState *c);
bool nc_init_placement_pose(CPUState *c);
bool nc_begin_placement_matrices(CPUState *c);
bool nc_begin_placement_vectors(CPUState *c);
bool nc_finish_placement_matrices(CPUState *c);
bool nc_finish_placement_vectors(CPUState *c);
bool nc_prepare_placement_yaw(CPUState *c);
bool nc_prepare_placement_yaw_scale(CPUState *c);
bool nc_prepare_placement_heading_vector(CPUState *c);
bool nc_prepare_placement_vector(CPUState *c, u32 source_offset, u32 result_offset, u32 resume);
bool nc_prepare_placement_rotation(CPUState *c);
bool nc_prepare_placement_scale(CPUState *c);
bool nc_prepare_placement_position(CPUState *c);
bool nc_prepare_placement_translation(CPUState *c);
bool nc_prepare_placement_light_rotation(CPUState *c);
bool nc_prepare_placement_light_scale(CPUState *c);
bool nc_prepare_placement_call(CPUState *c, u32 argument, u32 resume);
bool nc_prepare_placement_transpose(CPUState *c);
bool nc_prepare_transposed_placement_scale(CPUState *c);
bool nc_begin_actor_placement_target(CPUState *c, bool *has_pose);
bool nc_finish_actor_placement_target(CPUState *c, bool has_pose);
bool nc_prepare_actor_placement_target(CPUState *c);
bool nc_latch_placement_tick(CPUState *c);
bool nc_begin_placement_update(CPUState *c);
typedef enum {
    NC_PLACEMENT_COUNTER_FALLBACK,
    NC_PLACEMENT_COUNTER_ZERO,
    NC_PLACEMENT_CHECK_TARGET,
    NC_PLACEMENT_UPDATE_MODE,
    NC_PLACEMENT_APPLY_PRESET,
} NcPlacementCounterPath;
bool nc_update_placement_clock(CPUState *c);
bool nc_call_placement_reset(CPUState *c);
bool nc_finish_placement_reset(CPUState *c);
bool nc_begin_placement_refresh(CPUState *c, bool *refresh);
bool nc_finish_placement_refresh(CPUState *c);
NcPlacementCounterPath nc_check_placement_counter(CPUState *c);
bool nc_apply_placement_preset(CPUState *c);
bool nc_begin_placement_mode(CPUState *c, bool *update_actor);
bool nc_select_placement_step(CPUState *c, bool *step);
bool nc_restore_placement_frame(CPUState *c);
bool nc_step_placement_record(CPUState *c);
bool nc_step_record_angle_pair(CPUState *c);
bool nc_prepare_paired_point_bearing(CPUState *c);
bool nc_place_paired_point_actor(CPUState *c, bool store_heading);
bool nc_call_placement_record_update(CPUState *c);
bool nc_finish_paired_point_placement(CPUState *c);
bool nc_call_actor_coordinate_init(CPUState *c);
bool nc_init_actor_coordinates(CPUState *c);

#endif
