#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "bsp/delay.h"
#include "peripherals/gpba02b.h"
#include "user_config.h"

#define USER_OV2640_SCCB_ADDR (0x30U)

#define OV2640_REG_BANK_SEL (0xFFU)
#define OV2640_BANK_DSP (0x00U)
#define OV2640_BANK_SENSOR (0x01U)

#define OV2640_REG_COM7 (0x12U)
#define OV2640_COM7_SRST (0x80U)

#define OV2640_REG_PID (0x0AU)
#define OV2640_REG_VER (0x0BU)
#define OV2640_REG_MIDH (0x1CU)
#define OV2640_REG_MIDL (0x1DU)

#define OV2640_PID_VALUE (0x26U)

typedef struct
{
    uint8_t reg;
    uint8_t value;
} ov2640_reg_val_t;

typedef struct
{
    uint8_t midh;
    uint8_t midl;
    uint8_t pid;
    uint8_t ver;
} ov2640_id_t;

struct ov2640_state
{
    i2c_master_bus_handle_t sccb_bus;
    i2c_master_dev_handle_t sccb_dev;
    bool sccb_ready;
    bool ready;
};

/* Initialize OV2640 low-level hardware path: SCCB, power/reset pins, and sensor probe. */
esp_err_t ov2640_init_device(void);
/* Release SCCB resources and reset runtime state. */
esp_err_t ov2640_deinit_device(void);
/* Return true when OV2640 low-level driver has completed initialization. */
bool ov2640_is_ready(void);

/* Control OV2640 PWDN pin: true powers down sensor, false powers up sensor. */
esp_err_t ov2640_set_pwdn(bool power_down);
/* Control camera fill light pin. */
esp_err_t ov2640_set_light(bool enable);
/* Toggle OV2640 hardware reset pin with startup-safe timing. */
esp_err_t ov2640_hard_reset(void);
/* Issue OV2640 software reset through COM7. */
esp_err_t ov2640_soft_reset(void);

/* Select OV2640 register bank: OV2640_BANK_DSP or OV2640_BANK_SENSOR. */
esp_err_t ov2640_select_bank(uint8_t bank);
/* Write one OV2640 register over SCCB. */
esp_err_t ov2640_write_reg(uint8_t reg, uint8_t value);
/* Read one OV2640 register over SCCB. */
esp_err_t ov2640_read_reg(uint8_t reg, uint8_t *value);
/* Write a continuous register/value table over SCCB. */
esp_err_t ov2640_write_regs(const ov2640_reg_val_t *regs, size_t reg_count);

/* Read OV2640 chip ID fields from sensor bank. */
esp_err_t ov2640_read_id(ov2640_id_t *id);
/* Verify that probed OV2640 PID matches OV2640_PID_VALUE. */
esp_err_t ov2640_verify_id(void);
/* Release OV2640 private SCCB ownership before esp_camera_init path starts. */
esp_err_t ov2640_prepare_preview_start(void);
