#include "c_key_core.h"

#include <math.h>
#include <string.h>

#define C_KEY_PI 3.14159265358979323846f
#define C_KEY_GEOMETRY_EPSILON 1.0e-5f

static float square(float value)
{
    return value * value;
}

static float normalize_angle(float angle_deg)
{
    while (angle_deg > 180.0f) {
        angle_deg -= 360.0f;
    }
    while (angle_deg < -180.0f) {
        angle_deg += 360.0f;
    }
    return angle_deg;
}

static void sort_values(float *values, size_t count)
{
    for (size_t i = 1; i < count; ++i) {
        const float selected = values[i];
        size_t j = i;
        while (j > 0 && values[j - 1] > selected) {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = selected;
    }
}

bool c_key_locate_two_anchors_front(
    const c_key_anchor_measurement_t anchors[C_KEY_ANCHOR_COUNT],
    c_key_point_t door_center,
    float front_angle_offset_deg,
    c_key_point_t *position,
    float *residual_rms_m)
{
    if (anchors == NULL || position == NULL ||
        !isfinite(door_center.x_m) || !isfinite(door_center.y_m) ||
        !isfinite(front_angle_offset_deg)) {
        return false;
    }

    for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
        if (!anchors[i].valid || !isfinite(anchors[i].distance_m) || anchors[i].distance_m <= 0.0f ||
            !isfinite(anchors[i].position.x_m) || !isfinite(anchors[i].position.y_m)) {
            return false;
        }
    }

    const c_key_anchor_measurement_t *a = &anchors[0];
    const c_key_anchor_measurement_t *b = &anchors[1];
    const float baseline_x = b->position.x_m - a->position.x_m;
    const float baseline_y = b->position.y_m - a->position.y_m;
    const float baseline_m = hypotf(baseline_x, baseline_y);
    if (!isfinite(baseline_m) || baseline_m < C_KEY_GEOMETRY_EPSILON) {
        return false;
    }

    const float unit_x = baseline_x / baseline_m;
    const float unit_y = baseline_y / baseline_m;
    const float along_m =
        (square(a->distance_m) - square(b->distance_m) +
         square(baseline_m)) /
        (2.0f * baseline_m);
    float height_squared_m2 = square(a->distance_m) - square(along_m);
    if (!isfinite(height_squared_m2) ||
        height_squared_m2 < -C_KEY_GEOMETRY_EPSILON) {
        return false;
    }
    height_squared_m2 = fmaxf(0.0f, height_squared_m2);

    const float base_x = a->position.x_m + along_m * unit_x;
    const float base_y = a->position.y_m + along_m * unit_y;
    const float height_m = sqrtf(height_squared_m2);
    const float perpendicular_x = -unit_y;
    const float perpendicular_y = unit_x;
    const c_key_point_t candidate_a = {
        .x_m = base_x + height_m * perpendicular_x,
        .y_m = base_y + height_m * perpendicular_y,
    };
    const c_key_point_t candidate_b = {
        .x_m = base_x - height_m * perpendicular_x,
        .y_m = base_y - height_m * perpendicular_y,
    };

    const float front_rad = front_angle_offset_deg * C_KEY_PI / 180.0f;
    const float front_x = sinf(front_rad);
    const float front_y = cosf(front_rad);
    const float score_a = (candidate_a.x_m - door_center.x_m) * front_x +
                          (candidate_a.y_m - door_center.y_m) * front_y;
    const float score_b = (candidate_b.x_m - door_center.x_m) * front_x +
                          (candidate_b.y_m - door_center.y_m) * front_y;
    const float selected_score = fmaxf(score_a, score_b);
    if (!isfinite(selected_score) || selected_score <= 0.0f) {
        return false;
    }

    *position = score_a >= score_b ? candidate_a : candidate_b;

    float residual_sum = 0.0f;
    for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
        const float dx = position->x_m - anchors[i].position.x_m;
        const float dy = position->y_m - anchors[i].position.y_m;
        const float predicted = sqrtf(square(dx) + square(dy));
        residual_sum += square(predicted - anchors[i].distance_m);
    }

    if (residual_rms_m != NULL) {
        *residual_rms_m = sqrtf(residual_sum / (float)C_KEY_ANCHOR_COUNT);
    }
    return true;
}

bool c_key_pose_from_xy(c_key_point_t position,
                        c_key_point_t door_center,
                        float door_radius_m,
                        float front_angle_offset_deg,
                        float residual_rms_m,
                        c_key_pose_t *pose)
{
    if (pose == NULL || !isfinite(position.x_m) || !isfinite(position.y_m) ||
        !isfinite(door_center.x_m) || !isfinite(door_center.y_m) ||
        !isfinite(door_radius_m) || door_radius_m < 0.0f ||
        !isfinite(front_angle_offset_deg)) {
        return false;
    }

    const float dx = position.x_m - door_center.x_m;
    const float dy = position.y_m - door_center.y_m;
    const float center_distance = sqrtf(square(dx) + square(dy));

    pose->position = position;
    pose->center_distance_m = center_distance;
    pose->boundary_distance_m = fmaxf(0.0f, center_distance - door_radius_m);
    pose->angle_deg = normalize_angle(atan2f(dx, dy) * 180.0f / C_KEY_PI - front_angle_offset_deg);
    pose->residual_rms_m = residual_rms_m;
    return isfinite(pose->angle_deg);
}

bool c_key_measurements_ready(const c_key_anchor_measurement_t anchors[C_KEY_ANCHOR_COUNT],
                              uint32_t now_ms,
                              uint32_t max_age_ms,
                              uint32_t max_skew_ms)
{
    if (anchors == NULL) {
        return false;
    }

    uint32_t minimum_age = UINT32_MAX;
    uint32_t maximum_age = 0;
    for (size_t i = 0; i < C_KEY_ANCHOR_COUNT; ++i) {
        if (!anchors[i].valid || !isfinite(anchors[i].distance_m) || anchors[i].distance_m <= 0.0f) {
            return false;
        }

        const uint32_t age = now_ms - anchors[i].timestamp_ms;
        if (age > max_age_ms) {
            return false;
        }
        if (age < minimum_age) {
            minimum_age = age;
        }
        if (age > maximum_age) {
            maximum_age = age;
        }
    }

    return maximum_age - minimum_age <= max_skew_ms;
}

void c_key_distance_filter_init(c_key_distance_filter_t *filter, float alpha)
{
    if (filter == NULL) {
        return;
    }

    memset(filter, 0, sizeof(*filter));
    filter->alpha = isfinite(alpha) ? fminf(1.0f, fmaxf(0.01f, alpha)) : 0.3f;
}

bool c_key_distance_filter_push(c_key_distance_filter_t *filter,
                                float raw_distance_m,
                                float minimum_m,
                                float maximum_m,
                                float *filtered_distance_m)
{
    if (filter == NULL || filtered_distance_m == NULL || !isfinite(raw_distance_m) ||
        raw_distance_m < minimum_m || raw_distance_m > maximum_m || minimum_m >= maximum_m) {
        return false;
    }

    filter->samples[filter->next] = raw_distance_m;
    filter->next = (filter->next + 1U) % C_KEY_FILTER_WINDOW;
    if (filter->count < C_KEY_FILTER_WINDOW) {
        ++filter->count;
    }

    float ordered[C_KEY_FILTER_WINDOW];
    memcpy(ordered, filter->samples, filter->count * sizeof(ordered[0]));
    sort_values(ordered, filter->count);

    float median;
    if ((filter->count & 1U) != 0U) {
        median = ordered[filter->count / 2U];
    } else {
        median = 0.5f * (ordered[filter->count / 2U - 1U] + ordered[filter->count / 2U]);
    }

    if (!filter->ema_initialized) {
        filter->ema = median;
        filter->ema_initialized = true;
    } else {
        filter->ema += filter->alpha * (median - filter->ema);
    }

    *filtered_distance_m = filter->ema;
    return true;
}

void c_key_angle_filter_init(c_key_angle_filter_t *filter,
                             float minimum_cutoff_hz,
                             float beta,
                             float derivative_cutoff_hz,
                             float hampel_sigma,
                             float hampel_min_threshold_deg,
                             uint8_t maximum_consecutive_rejections)
{
    if (filter == NULL) {
        return;
    }

    memset(filter, 0, sizeof(*filter));
    filter->minimum_cutoff_hz =
        isfinite(minimum_cutoff_hz) ? fmaxf(0.01f, minimum_cutoff_hz) : 0.8f;
    filter->beta = isfinite(beta) ? fmaxf(0.0f, beta) : 0.03f;
    filter->derivative_cutoff_hz =
        isfinite(derivative_cutoff_hz) ? fmaxf(0.01f, derivative_cutoff_hz) : 1.0f;
    filter->hampel_sigma =
        isfinite(hampel_sigma) ? fmaxf(1.0f, hampel_sigma) : 3.0f;
    filter->hampel_min_threshold_deg =
        isfinite(hampel_min_threshold_deg)
            ? fmaxf(1.0f, hampel_min_threshold_deg)
            : 12.0f;
    filter->maximum_consecutive_rejections =
        maximum_consecutive_rejections > 0U ? maximum_consecutive_rejections : 3U;
}

void c_key_angle_filter_reset(c_key_angle_filter_t *filter)
{
    if (filter == NULL) {
        return;
    }

    filter->count = 0U;
    filter->next = 0U;
    filter->filtered_deg = 0.0f;
    filter->previous_raw_deg = 0.0f;
    filter->filtered_derivative_deg_s = 0.0f;
    filter->last_timestamp_ms = 0U;
    filter->consecutive_rejections = 0U;
    filter->last_sample_rejected = false;
    filter->initialized = false;
}

static float one_euro_alpha(float delta_time_s, float cutoff_hz)
{
    const float ratio = 2.0f * C_KEY_PI * cutoff_hz * delta_time_s;
    return ratio / (ratio + 1.0f);
}

static bool angle_is_hampel_outlier(const c_key_angle_filter_t *filter,
                                    float raw_angle_deg)
{
    if (filter->count < C_KEY_ANGLE_HAMPEL_WINDOW) {
        return false;
    }

    float unwrapped[C_KEY_ANGLE_HAMPEL_WINDOW];
    for (size_t i = 0; i < filter->count; ++i) {
        unwrapped[i] = filter->filtered_deg +
                       normalize_angle(filter->samples[i] - filter->filtered_deg);
    }
    sort_values(unwrapped, filter->count);
    const float median = unwrapped[filter->count / 2U];

    float deviations[C_KEY_ANGLE_HAMPEL_WINDOW];
    for (size_t i = 0; i < filter->count; ++i) {
        deviations[i] = fabsf(unwrapped[i] - median);
    }
    sort_values(deviations, filter->count);
    const float mad = deviations[filter->count / 2U];
    const float threshold =
        fmaxf(filter->hampel_min_threshold_deg,
              filter->hampel_sigma * 1.4826f * mad);
    return fabsf(normalize_angle(raw_angle_deg - median)) > threshold;
}

bool c_key_angle_filter_push(c_key_angle_filter_t *filter,
                             float raw_angle_deg,
                             uint32_t timestamp_ms,
                             float *filtered_angle_deg)
{
    if (filter == NULL || filtered_angle_deg == NULL || !isfinite(raw_angle_deg)) {
        return false;
    }

    raw_angle_deg = normalize_angle(raw_angle_deg);
    if (!filter->initialized) {
        filter->samples[0] = raw_angle_deg;
        filter->count = 1U;
        filter->next = 1U;
        filter->filtered_deg = raw_angle_deg;
        filter->previous_raw_deg = raw_angle_deg;
        filter->last_timestamp_ms = timestamp_ms;
        filter->initialized = true;
        *filtered_angle_deg = raw_angle_deg;
        return true;
    }

    filter->last_sample_rejected = false;
    const bool hampel_outlier =
        angle_is_hampel_outlier(filter, raw_angle_deg);
    if (hampel_outlier) {
        if (filter->consecutive_rejections <
            filter->maximum_consecutive_rejections) {
            ++filter->consecutive_rejections;
            ++filter->rejected_samples;
            filter->last_sample_rejected = true;
            *filtered_angle_deg = filter->filtered_deg;
            return true;
        }

        for (size_t i = 0; i < C_KEY_ANGLE_HAMPEL_WINDOW; ++i) {
            filter->samples[i] = raw_angle_deg;
        }
        filter->count = C_KEY_ANGLE_HAMPEL_WINDOW;
        filter->next = 0U;
    }
    filter->consecutive_rejections = 0U;

    filter->samples[filter->next] = raw_angle_deg;
    filter->next = (filter->next + 1U) % C_KEY_ANGLE_HAMPEL_WINDOW;
    if (filter->count < C_KEY_ANGLE_HAMPEL_WINDOW) {
        ++filter->count;
    }

    uint32_t elapsed_ms = timestamp_ms - filter->last_timestamp_ms;
    if (elapsed_ms == 0U) {
        elapsed_ms = 1U;
    } else if (elapsed_ms > 250U) {
        elapsed_ms = 250U;
    }
    const float delta_time_s = (float)elapsed_ms * 0.001f;
    const float raw_derivative =
        normalize_angle(raw_angle_deg - filter->previous_raw_deg) /
        delta_time_s;
    const float derivative_alpha =
        one_euro_alpha(delta_time_s, filter->derivative_cutoff_hz);
    filter->filtered_derivative_deg_s +=
        derivative_alpha *
        (raw_derivative - filter->filtered_derivative_deg_s);

    const float cutoff_hz =
        filter->minimum_cutoff_hz +
        filter->beta * fabsf(filter->filtered_derivative_deg_s);
    const float angle_alpha = one_euro_alpha(delta_time_s, cutoff_hz);
    const float error_deg =
        normalize_angle(raw_angle_deg - filter->filtered_deg);
    filter->filtered_deg =
        normalize_angle(filter->filtered_deg + angle_alpha * error_deg);
    filter->previous_raw_deg = raw_angle_deg;
    filter->last_timestamp_ms = timestamp_ms;
    *filtered_angle_deg = filter->filtered_deg;
    return true;
}

c_key_thresholds_t c_key_default_thresholds(void)
{
    return (c_key_thresholds_t){
        .unlock_enter_m = 0.95f,
        .unlock_exit_m = 1.05f,
        .welcome_enter_m = 1.95f,
        .welcome_exit_m = 2.05f,
        .angle_enter_abs_deg = 43.0f,
        .angle_exit_abs_deg = 47.0f,
        .transition_confirm_frames = 4U,
    };
}

void c_key_state_machine_init(c_key_state_machine_t *machine,
                              c_key_thresholds_t thresholds)
{
    if (machine == NULL) {
        return;
    }

    memset(machine, 0, sizeof(*machine));
    machine->state = C_KEY_STATE_NO_KEY;
    machine->candidate_state = C_KEY_STATE_NO_KEY;
    machine->thresholds = thresholds;
}

static bool valid_thresholds(const c_key_thresholds_t *thresholds)
{
    return thresholds->unlock_enter_m < thresholds->unlock_exit_m &&
           thresholds->unlock_exit_m < thresholds->welcome_enter_m &&
           thresholds->welcome_enter_m < thresholds->welcome_exit_m &&
           thresholds->angle_enter_abs_deg < thresholds->angle_exit_abs_deg &&
           thresholds->transition_confirm_frames > 0U;
}

static void reset_candidate(c_key_state_machine_t *machine)
{
    machine->candidate_state = machine->state;
    machine->candidate_angle_inside = machine->angle_inside;
    machine->candidate_count = 0U;
}

static void confirm_valid_transition(c_key_state_machine_t *machine,
                                     c_key_state_t target_state,
                                     bool target_angle_inside)
{
    if (target_state == machine->state &&
        target_angle_inside == machine->angle_inside) {
        reset_candidate(machine);
        return;
    }

    if (machine->candidate_count > 0U &&
        machine->candidate_state == target_state &&
        machine->candidate_angle_inside == target_angle_inside) {
        ++machine->candidate_count;
    } else {
        machine->candidate_state = target_state;
        machine->candidate_angle_inside = target_angle_inside;
        machine->candidate_count = 1U;
    }

    if (machine->candidate_count >= machine->thresholds.transition_confirm_frames) {
        machine->state = target_state;
        machine->angle_inside = target_angle_inside;
        reset_candidate(machine);
    }
}

static c_key_state_t distance_state(const c_key_state_machine_t *machine, float distance_m)
{
    const c_key_thresholds_t *t = &machine->thresholds;

    if (machine->state == C_KEY_STATE_UNLOCKED) {
        if (distance_m < t->unlock_exit_m) {
            return C_KEY_STATE_UNLOCKED;
        }
        return distance_m < t->welcome_exit_m ? C_KEY_STATE_WELCOME : C_KEY_STATE_SENSING;
    }

    if (machine->state == C_KEY_STATE_WELCOME) {
        if (distance_m <= t->unlock_enter_m) {
            return C_KEY_STATE_UNLOCKED;
        }
        return distance_m >= t->welcome_exit_m ? C_KEY_STATE_SENSING : C_KEY_STATE_WELCOME;
    }

    if (distance_m <= t->unlock_enter_m) {
        return C_KEY_STATE_UNLOCKED;
    }
    if (distance_m <= t->welcome_enter_m) {
        return C_KEY_STATE_WELCOME;
    }
    return C_KEY_STATE_SENSING;
}

uint32_t c_key_state_machine_update(c_key_state_machine_t *machine,
                                    const c_key_state_input_t *input)
{
    if (machine == NULL || input == NULL || !valid_thresholds(&machine->thresholds)) {
        return C_KEY_EVENT_NONE;
    }

    const c_key_state_t previous_state = machine->state;
    const bool previous_welcome = machine->welcome_output;
    const bool previous_unlocked = machine->unlocked_output;

    if (!input->signal_present) {
        machine->state = C_KEY_STATE_NO_KEY;
        machine->angle_inside = false;
        reset_candidate(machine);
    } else if (!input->measurement_valid || !isfinite(input->boundary_distance_m) ||
               input->boundary_distance_m < 0.0f || !isfinite(input->angle_deg)) {
        machine->state = C_KEY_STATE_FAULT;
        machine->angle_inside = false;
        reset_candidate(machine);
    } else if (input->tag_id > 15U || input->accepted_id > 15U || input->tag_id != input->accepted_id) {
        machine->state = C_KEY_STATE_INVALID_ID;
        machine->angle_inside = false;
        reset_candidate(machine);
    } else {
        const float absolute_angle = fabsf(input->angle_deg);
        bool target_angle_inside = machine->angle_inside;
        if (target_angle_inside) {
            if (absolute_angle >= machine->thresholds.angle_exit_abs_deg) {
                target_angle_inside = false;
            }
        } else if (absolute_angle <= machine->thresholds.angle_enter_abs_deg) {
            target_angle_inside = true;
        }

        const c_key_state_t target_state =
            target_angle_inside
                ? distance_state(machine, input->boundary_distance_m)
                : C_KEY_STATE_OUT_OF_ANGLE;
        confirm_valid_transition(machine, target_state, target_angle_inside);
    }

    machine->welcome_output = machine->state == C_KEY_STATE_WELCOME ||
                              machine->state == C_KEY_STATE_UNLOCKED;
    machine->unlocked_output = machine->state == C_KEY_STATE_UNLOCKED;

    uint32_t events = C_KEY_EVENT_NONE;
    if (machine->state != previous_state) {
        events |= C_KEY_EVENT_STATE_CHANGED;
    }
    if (!previous_welcome && machine->welcome_output) {
        events |= C_KEY_EVENT_WELCOME_ON;
    }
    if (previous_welcome && !machine->welcome_output) {
        events |= C_KEY_EVENT_WELCOME_OFF;
    }
    if (!previous_unlocked && machine->unlocked_output) {
        events |= C_KEY_EVENT_UNLOCK;
    }
    if (previous_unlocked && !machine->unlocked_output) {
        events |= C_KEY_EVENT_LOCK;
    }
    return events;
}

const char *c_key_state_name(c_key_state_t state)
{
    switch (state) {
    case C_KEY_STATE_NO_KEY:
        return "NO_KEY";
    case C_KEY_STATE_INVALID_ID:
        return "INVALID_ID";
    case C_KEY_STATE_OUT_OF_ANGLE:
        return "OUT_OF_ANGLE";
    case C_KEY_STATE_SENSING:
        return "SENSING";
    case C_KEY_STATE_WELCOME:
        return "WELCOME";
    case C_KEY_STATE_UNLOCKED:
        return "UNLOCKED";
    case C_KEY_STATE_FAULT:
        return "FAULT";
    default:
        return "UNKNOWN";
    }
}
