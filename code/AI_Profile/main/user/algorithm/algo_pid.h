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
    float err;            // Current control error: target - feedback.
    float p_term;         // Latest proportional contribution.
    float i_term;         // Latest integral contribution after integral clamp.
    float d_term;         // Latest derivative contribution.
    float out;            // Latest final output after output saturation.
} algo_pid_terms_s;

typedef struct {
    bool inited;          // True after successful init.
    bool first_cycle;     // True before first step; derivative term is suppressed.
    float target;         // Setpoint used to compute error.
    float prev_err;       // Previous-step error used by derivative term.
    float i_acc;          // Internal integral accumulator state.
    algo_pid_cfg_s cfg;   // Active PID configuration.
    algo_pid_terms_s terms;  // Cached terms from the most recent step.
} algo_pid_s;
// clang-format on

void algo_pid_cfg_set_default(algo_pid_cfg_s* cfg);

esp_err_t algo_pid_init(algo_pid_s* pid, const algo_pid_cfg_s* cfg);
esp_err_t algo_pid_deinit(algo_pid_s* pid);
esp_err_t algo_pid_reset(algo_pid_s* pid);

esp_err_t algo_pid_set_cfg(algo_pid_s* pid, const algo_pid_cfg_s* cfg);
esp_err_t algo_pid_get_cfg(const algo_pid_s* pid, algo_pid_cfg_s* cfg);

esp_err_t algo_pid_set_target(algo_pid_s* pid, float target);
esp_err_t algo_pid_get_target(const algo_pid_s* pid, float* target);

esp_err_t algo_pid_step(algo_pid_s* pid, float feedback, float* output);
esp_err_t algo_pid_step_with_target(algo_pid_s* pid,
                                    float target,
                                    float feedback,
                                    float* output);

esp_err_t algo_pid_get_terms(const algo_pid_s* pid, algo_pid_terms_s* terms);

#ifdef __cplusplus
}
#endif
