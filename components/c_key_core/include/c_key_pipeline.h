#ifndef C_KEY_PIPELINE_H
#define C_KEY_PIPELINE_H

#include "c_key_core.h"

typedef struct {
    c_key_point_t anchor_positions[C_KEY_ANCHOR_COUNT];
    float distance_scale_factors[C_KEY_ANCHOR_COUNT];
    float distance_offsets_m[C_KEY_ANCHOR_COUNT];
    c_key_point_t door_center;
    float door_radius_m;
    float front_angle_offset_deg;
    float filter_alpha;
    float angle_filter_stationary_alpha;
    float angle_filter_moving_alpha;
    float angle_filter_motion_threshold_deg;
    float minimum_distance_m;
    float maximum_distance_m;
    float maximum_residual_m;
    uint32_t maximum_age_ms;
    uint32_t maximum_skew_ms;
    uint8_t accepted_id;
    c_key_thresholds_t thresholds;
} c_key_pipeline_config_t;

typedef struct {
    bool signal_present;
    uint8_t tag_id;
    uint32_t now_ms;
    c_key_anchor_measurement_t anchors[C_KEY_ANCHOR_COUNT];
} c_key_pipeline_input_t;

typedef struct {
    bool signal_present;
    bool measurement_valid;
    uint8_t tag_id;
    uint32_t now_ms;
    uint16_t sequence;
    float distance_m;
    float angle_deg;
} c_key_pdoa_input_t;

typedef struct {
    bool measurement_ready;
    bool pose_valid;
    uint8_t tag_id;
    float corrected_distances_m[C_KEY_ANCHOR_COUNT];
    float filtered_distances_m[C_KEY_ANCHOR_COUNT];
    c_key_pose_t pose;
    c_key_state_t state;
    bool welcome_output;
    bool unlocked_output;
    uint32_t events;
} c_key_pipeline_output_t;

typedef struct {
    c_key_pipeline_config_t config;
    c_key_distance_filter_t filters[C_KEY_ANCHOR_COUNT];
    float filtered_distances_m[C_KEY_ANCHOR_COUNT];
    uint32_t last_timestamps_ms[C_KEY_ANCHOR_COUNT];
    uint16_t last_sequences[C_KEY_ANCHOR_COUNT];
    bool sample_seen[C_KEY_ANCHOR_COUNT];
    bool filter_valid[C_KEY_ANCHOR_COUNT];
    c_key_angle_filter_t angle_filter;
    c_key_state_machine_t state_machine;
} c_key_pipeline_t;

bool c_key_pipeline_init(c_key_pipeline_t *pipeline,
                         const c_key_pipeline_config_t *config);

bool c_key_pipeline_set_accepted_id(c_key_pipeline_t *pipeline, uint8_t accepted_id);

bool c_key_pipeline_process(c_key_pipeline_t *pipeline,
                            const c_key_pipeline_input_t *input,
                            c_key_pipeline_output_t *output);

bool c_key_pipeline_process_pdoa(c_key_pipeline_t *pipeline,
                                 const c_key_pdoa_input_t *input,
                                 c_key_pipeline_output_t *output);

#endif
