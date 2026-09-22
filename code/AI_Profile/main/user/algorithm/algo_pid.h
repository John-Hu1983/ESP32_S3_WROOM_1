#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define ALGO_PID_DEFAULT_DT_S                (0.01f)
#define ALGO_PID_DEFAULT_EPSILON             (1e-6f)

typedef struct {
    float kp;             // Proportional gain: p_term = kp * err.
    float ki;             // Integral gain: i_term accumulates ki * err * dt.
    float kd;             // Derivative gain: d_term = kd * (err - prev_err) / dt.
    float out_min;        // Minimum saturated controller output.
    float out_max;        // Maximum saturated controller output.
    float i_min;          // Lower clamp for integral accumulator i_acc.
    float i_max;          // Upper clamp for integral accumulator i_acc.
    float dt_s;           // Control step period in seconds for each step call.
    float err_deadband;   // Absolute error deadband; inside this range err is treated as 0.
} algo_pid_cfg_s;

typedef struct {
    bool inited;          // True after successful init.
    bool first_cycle;     // True before first step; derivative term is suppressed.
    float target;         // Setpoint used to compute error.
    float prev_err;       // Previous-step error used by derivative term.
    float i_acc;          // Internal integral accumulator state.
    float curr_err;       // Cached current control error from the most recent step.
    algo_pid_cfg_s cfg;   // Active PID configuration.
} algo_pid_s;
// clang-format on

esp_err_t algo_pid_step(algo_pid_s* pid, float feedback, float* output);
esp_err_t algo_pid_get_curr_err(const algo_pid_s* pid, float* curr_err);

#ifdef __cplusplus
}
#endif
