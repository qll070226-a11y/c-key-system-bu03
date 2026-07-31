#ifndef C_KEY_CORE_H
#define C_KEY_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define C_KEY_ANCHOR_COUNT 2U
#define C_KEY_FILTER_WINDOW 5U

typedef struct {
    float x_m;
    float y_m;
} c_key_point_t;

typedef struct {
    c_key_point_t position;
    float distance_m;
    uint32_t timestamp_ms;
    uint16_t sequence;
    bool valid;
} c_key_anchor_measurement_t;

typedef struct {
    c_key_point_t position;
    float center_distance_m;
    float boundary_distance_m;
    float angle_deg;
    float residual_rms_m;
} c_key_pose_t;

typedef struct {
    float samples[C_KEY_FILTER_WINDOW];
    size_t count;
    size_t next;
    float ema;
    float alpha;
    bool ema_initialized;
} c_key_distance_filter_t;

typedef struct {
    float samples[C_KEY_FILTER_WINDOW];
    size_t count;
    size_t next;
    float filtered_deg;
    float stationary_alpha;
    float moving_alpha;
    float motion_threshold_deg;
    bool initialized;
} c_key_angle_filter_t;

typedef enum {
    C_KEY_STATE_NO_KEY = 0,
    C_KEY_STATE_INVALID_ID,
    C_KEY_STATE_OUT_OF_ANGLE,
    C_KEY_STATE_SENSING,
    C_KEY_STATE_WELCOME,
    C_KEY_STATE_UNLOCKED,
    C_KEY_STATE_FAULT,
} c_key_state_t;

typedef enum {
    C_KEY_EVENT_NONE = 0,
    C_KEY_EVENT_STATE_CHANGED = 1U << 0,
    C_KEY_EVENT_WELCOME_ON = 1U << 1,
    C_KEY_EVENT_WELCOME_OFF = 1U << 2,
    C_KEY_EVENT_UNLOCK = 1U << 3,
    C_KEY_EVENT_LOCK = 1U << 4,
} c_key_event_t;

typedef struct {
    float unlock_enter_m;
    float unlock_exit_m;
    float welcome_enter_m;
    float welcome_exit_m;
    float angle_enter_abs_deg;
    float angle_exit_abs_deg;
} c_key_thresholds_t;

typedef struct {
    bool signal_present;
    bool measurement_valid;
    uint8_t tag_id;
    uint8_t accepted_id;
    float boundary_distance_m;
    float angle_deg;
} c_key_state_input_t;

typedef struct {
    c_key_state_t state;
    c_key_thresholds_t thresholds;
    bool angle_inside;
    bool welcome_output;
    bool unlocked_output;
} c_key_state_machine_t;

bool c_key_locate_two_anchors_front(
    const c_key_anchor_measurement_t anchors[C_KEY_ANCHOR_COUNT],
    c_key_point_t door_center,
    float front_angle_offset_deg,
    c_key_point_t *position,
    float *residual_rms_m);

bool c_key_pose_from_xy(c_key_point_t position,
                        c_key_point_t door_center,
                        float door_radius_m,
                        float front_angle_offset_deg,
                        float residual_rms_m,
                        c_key_pose_t *pose);

bool c_key_measurements_ready(const c_key_anchor_measurement_t anchors[C_KEY_ANCHOR_COUNT],
                              uint32_t now_ms,
                              uint32_t max_age_ms,
                              uint32_t max_skew_ms);

void c_key_distance_filter_init(c_key_distance_filter_t *filter, float alpha);

bool c_key_distance_filter_push(c_key_distance_filter_t *filter,
                                float raw_distance_m,
                                float minimum_m,
                                float maximum_m,
                                float *filtered_distance_m);

void c_key_angle_filter_init(c_key_angle_filter_t *filter,
                             float stationary_alpha,
                             float moving_alpha,
                             float motion_threshold_deg);

void c_key_angle_filter_reset(c_key_angle_filter_t *filter);

bool c_key_angle_filter_push(c_key_angle_filter_t *filter,
                             float raw_angle_deg,
                             float *filtered_angle_deg);

c_key_thresholds_t c_key_default_thresholds(void);

void c_key_state_machine_init(c_key_state_machine_t *machine,
                              c_key_thresholds_t thresholds);

uint32_t c_key_state_machine_update(c_key_state_machine_t *machine,
                                    const c_key_state_input_t *input);

const char *c_key_state_name(c_key_state_t state);

#ifdef __cplusplus
}
#endif

#endif
