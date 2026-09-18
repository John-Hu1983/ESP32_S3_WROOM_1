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
 * brief : _algo_pid_cfg_valid.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _algo_pid_cfg_valid(const algo_pid_cfg_s* cfg) {
    if (cfg == NULL) {
        return false;
    }

    if (!_algo_pid_is_finite(cfg->kp) || !_algo_pid_is_finite(cfg->ki)
        || !_algo_pid_is_finite(cfg->kd) || !_algo_pid_is_finite(cfg->out_min)
        || !_algo_pid_is_finite(cfg->out_max) || !_algo_pid_is_finite(cfg->i_min)
        || !_algo_pid_is_finite(cfg->i_max) || !_algo_pid_is_finite(cfg->dt_s)
        || !_algo_pid_is_finite(cfg->err_deadband)) {
        return false;
    }

    if (cfg->dt_s <= ALGO_PID_DEFAULT_EPSILON) {
        return false;
    }

    if (cfg->out_min > cfg->out_max) {
        return false;
    }

    if (cfg->i_min > cfg->i_max) {
        return false;
    }

    if (cfg->err_deadband < 0.0f) {
        return false;
    }

    return true;
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
 * brief : _algo_pid_step_internal.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _algo_pid_step_internal(algo_pid_s* pid,
                                         float feedback,
                                         float* output) {
    float err = 0.0f;
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

    err = pid->target - feedback;
    if (!_algo_pid_is_finite(err)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (_algo_pid_abs_f32(err) <= pid->cfg.err_deadband) {
        err = 0.0f;
    }

    p_term = pid->cfg.kp * err;

    i_prev = pid->i_acc;
    i_delta = pid->cfg.ki * err * pid->cfg.dt_s;
    i_next = i_prev + i_delta;
    i_next = _algo_pid_clamp_f32(i_next, pid->cfg.i_min, pid->cfg.i_max);

    if (pid->first_cycle) {
        d_term = 0.0f;
    }
    else {
        d_term = pid->cfg.kd * (err - pid->prev_err) / pid->cfg.dt_s;
    }

    out_raw = p_term + i_next + d_term;
    out_sat = _algo_pid_clamp_f32(out_raw, pid->cfg.out_min, pid->cfg.out_max);

    if (!_algo_pid_is_effectively_equal(out_sat, out_raw)
        && (_algo_pid_abs_f32(pid->cfg.ki) > ALGO_PID_DEFAULT_EPSILON)) {
        if ((err > 0.0f) && (out_sat >= pid->cfg.out_max)
            && (i_delta > ALGO_PID_DEFAULT_EPSILON)) {
            anti_windup_rollback = true;
        }
        if ((err < 0.0f) && (out_sat <= pid->cfg.out_min)
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
    pid->prev_err = err;
    pid->first_cycle = false;

    pid->terms.err = err;
    pid->terms.p_term = p_term;
    pid->terms.i_term = i_next;
    pid->terms.d_term = d_term;
    pid->terms.out = out_sat;

    *output = out_sat;
    return ESP_OK;
}

/*
 * brief : algo_pid_cfg_set_default.
 * input : see parameters.
 * output: none.
 * type  : public
 */
void algo_pid_cfg_set_default(algo_pid_cfg_s* cfg) {
    if (cfg == NULL) {
        return;
    }

    cfg->kp = 1.0f;
    cfg->ki = 0.0f;
    cfg->kd = 0.0f;
    cfg->out_min = -100.0f;
    cfg->out_max = 100.0f;
    cfg->i_min = -100.0f;
    cfg->i_max = 100.0f;
    cfg->dt_s = ALGO_PID_DEFAULT_DT_S;
    cfg->err_deadband = 0.0f;
}

/*
 * brief : algo_pid_init.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t algo_pid_init(algo_pid_s* pid, const algo_pid_cfg_s* cfg) {
    if ((pid == NULL) || (cfg == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!_algo_pid_cfg_valid(cfg)) {
        return ESP_ERR_INVALID_ARG;
    }

    *pid = (algo_pid_s){ 0 };
    pid->inited = true;
    pid->first_cycle = true;
    pid->cfg = *cfg;

    return ESP_OK;
}

/*
 * brief : algo_pid_deinit.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t algo_pid_deinit(algo_pid_s* pid) {
    if (pid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *pid = (algo_pid_s){ 0 };
    return ESP_OK;
}

/*
 * brief : algo_pid_reset.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t algo_pid_reset(algo_pid_s* pid) {
    if (pid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!pid->inited) {
        return ESP_ERR_INVALID_STATE;
    }

    pid->first_cycle = true;
    pid->prev_err = 0.0f;
    pid->i_acc = 0.0f;
    pid->terms = (algo_pid_terms_s){ 0 };

    return ESP_OK;
}

/*
 * brief : algo_pid_set_cfg.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t algo_pid_set_cfg(algo_pid_s* pid, const algo_pid_cfg_s* cfg) {
    if ((pid == NULL) || (cfg == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!pid->inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!_algo_pid_cfg_valid(cfg)) {
        return ESP_ERR_INVALID_ARG;
    }

    pid->cfg = *cfg;
    pid->i_acc = _algo_pid_clamp_f32(pid->i_acc, cfg->i_min, cfg->i_max);

    return ESP_OK;
}

/*
 * brief : algo_pid_get_cfg.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t algo_pid_get_cfg(const algo_pid_s* pid, algo_pid_cfg_s* cfg) {
    if ((pid == NULL) || (cfg == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!pid->inited) {
        return ESP_ERR_INVALID_STATE;
    }

    *cfg = pid->cfg;
    return ESP_OK;
}

/*
 * brief : algo_pid_set_target.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t algo_pid_set_target(algo_pid_s* pid, float target) {
    if (pid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!pid->inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!_algo_pid_is_finite(target)) {
        return ESP_ERR_INVALID_ARG;
    }

    pid->target = target;
    return ESP_OK;
}

/*
 * brief : algo_pid_get_target.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t algo_pid_get_target(const algo_pid_s* pid, float* target) {
    if ((pid == NULL) || (target == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!pid->inited) {
        return ESP_ERR_INVALID_STATE;
    }

    *target = pid->target;
    return ESP_OK;
}

/*
 * brief : algo_pid_step.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t algo_pid_step(algo_pid_s* pid, float feedback, float* output) {
    return _algo_pid_step_internal(pid, feedback, output);
}

/*
 * brief : algo_pid_step_with_target.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t algo_pid_step_with_target(algo_pid_s* pid,
                                    float target,
                                    float feedback,
                                    float* output) {
    esp_err_t ret = ESP_OK;

    ret = algo_pid_set_target(pid, target);
    if (ret != ESP_OK) {
        return ret;
    }

    return _algo_pid_step_internal(pid, feedback, output);
}

/*
 * brief : algo_pid_get_terms.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t algo_pid_get_terms(const algo_pid_s* pid, algo_pid_terms_s* terms) {
    if ((pid == NULL) || (terms == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!pid->inited) {
        return ESP_ERR_INVALID_STATE;
    }

    *terms = pid->terms;
    return ESP_OK;
}
