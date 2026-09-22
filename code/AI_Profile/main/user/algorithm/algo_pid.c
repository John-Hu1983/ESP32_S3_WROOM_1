#include "algo_pid.h"

#include <math.h>

/*
 * brief : _algo_pid_is_finite.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _algo_pid_is_finite(float value) {
    return isfinite(value);
}

/*
 * brief : _algo_pid_clamp_f32.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static float _algo_pid_clamp_f32(float value, float min, float max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }

    return value;
}

/*
 * brief : _algo_pid_abs_f32.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static float _algo_pid_abs_f32(float value) {
    return fabsf(value);
}

/*
 * brief : _algo_pid_is_effectively_equal.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _algo_pid_is_effectively_equal(float lhs, float rhs) {
    return _algo_pid_abs_f32(lhs - rhs) <= ALGO_PID_DEFAULT_EPSILON;
}

/*
 * brief : algo_pid_step.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t algo_pid_step(algo_pid_s* pid, float feedback, float* output) {
    float p_term = 0.0f;
    float i_prev = 0.0f;
    float i_delta = 0.0f;
    float i_next = 0.0f;
    float d_term = 0.0f;
    float out_raw = 0.0f;
    float out_sat = 0.0f;
    bool anti_windup_rollback = false;

    if ((pid == NULL) || (output == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!pid->inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!_algo_pid_is_finite(feedback)) {
        return ESP_ERR_INVALID_ARG;
    }

    pid->feedback = feedback;
    pid->curr_err = pid->target - pid->feedback;
    if (!_algo_pid_is_finite(pid->curr_err)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (_algo_pid_abs_f32(pid->curr_err) <= pid->cfg.err_deadband) {
        pid->curr_err = 0.0f;
    }

    p_term = pid->cfg.kp * pid->curr_err;

    i_prev = pid->i_acc;
    i_delta = pid->cfg.ki * pid->curr_err * pid->cfg.dt_s;
    i_next = i_prev + i_delta;
    i_next = _algo_pid_clamp_f32(i_next, pid->cfg.i_min, pid->cfg.i_max);

    if (pid->first_cycle) {
        d_term = 0.0f;
    }
    else {
        d_term = pid->cfg.kd * (pid->curr_err - pid->prev_err) / pid->cfg.dt_s;
    }

    out_raw = p_term + i_next + d_term;
    out_sat = _algo_pid_clamp_f32(out_raw, pid->cfg.out_min, pid->cfg.out_max);

    if (!_algo_pid_is_effectively_equal(out_sat, out_raw)
        && (_algo_pid_abs_f32(pid->cfg.ki) > ALGO_PID_DEFAULT_EPSILON)) {
        if ((pid->curr_err > 0.0f) && (out_sat >= pid->cfg.out_max)
            && (i_delta > ALGO_PID_DEFAULT_EPSILON)) {
            anti_windup_rollback = true;
        }
        if ((pid->curr_err < 0.0f) && (out_sat <= pid->cfg.out_min)
            && (i_delta < -ALGO_PID_DEFAULT_EPSILON)) {
            anti_windup_rollback = true;
        }

        if (anti_windup_rollback) {
            i_next = i_prev;
            out_raw = p_term + i_next + d_term;
            out_sat = _algo_pid_clamp_f32(out_raw, pid->cfg.out_min, pid->cfg.out_max);
        }
    }

    if (!_algo_pid_is_finite(out_sat) || !_algo_pid_is_finite(i_next)
        || !_algo_pid_is_finite(p_term) || !_algo_pid_is_finite(d_term)) {
        return ESP_FAIL;
    }

    pid->i_acc = i_next;
    pid->prev_err = pid->curr_err;
    pid->first_cycle = false;

    *output = out_sat;
    return ESP_OK;
}

/*
 * brief : algo_pid_get_curr_err.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t algo_pid_get_curr_err(const algo_pid_s* pid, float* curr_err) {
    if ((pid == NULL) || (curr_err == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!pid->inited) {
        return ESP_ERR_INVALID_STATE;
    }

    *curr_err = pid->curr_err;
    return ESP_OK;
}
