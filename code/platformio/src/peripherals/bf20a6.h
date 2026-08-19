#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "bsp/delay.h"
#include "peripherals/gpba02b.h"
#include "user_config.h"

#define USER_BF20A6_SCCB_ADDR (0x6EU)

#define BF20A6_REG_VER_BME (0xFBU)
#define BF20A6_REG_PIDH_BME (0xFCU)
#define BF20A6_REG_PIDL_BME (0xFDU)
#define BF20A6_REG_SCCB_RESET (0xF2U)
#define BF20A6_SCCB_RESET_BIT (0x01U)

#define BF20A6_PID_VALUE (0x20A6U)

typedef struct
{
    uint8_t reg;
    uint8_t value;
} bf20a6_reg_val_t;

typedef struct
{
    uint8_t ver;
    uint8_t pidh;
    uint8_t pidl;
} bf20a6_id_t;

struct bf20a6_state
{
    i2c_master_bus_handle_t sccb_bus;
    i2c_master_dev_handle_t sccb_dev;
    bool sccb_ready;
    bool ready;
};

/* Initialize BF20A6 low-level hardware path: SCCB, power/reset pins, and sensor probe. */
esp_err_t bf20a6_init_device(void);
/* Release SCCB resources and reset runtime state. */
esp_err_t bf20a6_deinit_device(void);
/* Return true when BF20A6 low-level driver has completed initialization. */
bool bf20a6_is_ready(void);

/* Control BF20A6 PWDN pin: true powers down sensor, false powers up sensor. */
esp_err_t bf20a6_set_pwdn(bool power_down);
/* Control camera fill light pin. */
esp_err_t bf20a6_set_light(bool enable);
/* Toggle BF20A6 hardware reset pin with startup-safe timing. */
esp_err_t bf20a6_hard_reset(void);
/* Issue BF20A6 software reset through SCCB_RESET register. */
esp_err_t bf20a6_soft_reset(void);

/* Write one BF20A6 register over SCCB. */
esp_err_t bf20a6_write_reg(uint8_t reg, uint8_t value);
/* Read one BF20A6 register over SCCB. */
esp_err_t bf20a6_read_reg(uint8_t reg, uint8_t *value);
/* Write a continuous register/value table over SCCB. */
esp_err_t bf20a6_write_regs(const bf20a6_reg_val_t *regs, size_t reg_count);

/* Read BF20A6 chip ID fields. */
esp_err_t bf20a6_read_id(bf20a6_id_t *id);
/* Verify that probed BF20A6 PID matches BF20A6_PID_VALUE. */
esp_err_t bf20a6_verify_id(void);
/* Release BF20A6 private SCCB ownership before esp_camera_init path starts. */
esp_err_t bf20a6_prepare_preview_start(void);
