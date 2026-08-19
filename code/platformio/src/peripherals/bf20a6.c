#include "peripherals/bf20a6.h"

#define TAG "BF20A6"

#ifdef CAMERA_OBJECT

static struct bf20a6_state s_bf20a6 = {0};

/*
 * brief: Set BF20A6 reset pin logic level through GPBA expander.
 * input: high_level - true to release reset, false to hold reset.
 * output: ESP_OK on success; otherwise GPBA write error.
 */
static esp_err_t _bf20a6_set_reset_level(bool high_level)
{
    return gpba02b_pin_write(CAM_IO_RESET_PORT, CAM_IO_RESET_PIN, high_level);
}

/*
 * brief: Configure BF20A6 control pins (RESET/PWDN/LIGHT) as GPBA outputs.
 * input: none.
 * output: ESP_OK on success; otherwise GPBA mode configuration error.
 */
static esp_err_t _bf20a6_config_control_pins(void)
{
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(CAM_IO_RESET_PORT,
                                              CAM_IO_RESET_PIN,
                                              GPBA02B_PIN_MODE_OUTPUT),
                         TAG,
                         "set CAM RESET pin mode failed");
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(CAM_IO_PWDN_PORT,
                                              CAM_IO_PWDN_PIN,
                                              GPBA02B_PIN_MODE_OUTPUT),
                         TAG,
                         "set CAM PWDN pin mode failed");
    USER_RETURN_ON_ERROR(gpba02b_pin_set_mode(CAM_IO_LIGHT_PORT,
                                              CAM_IO_LIGHT_PIN,
                                              GPBA02B_PIN_MODE_OUTPUT),
                         TAG,
                         "set CAM LIGHT pin mode failed");
    return ESP_OK;
}

/*
 * brief: Configure DVP sync/data pins as GPIO inputs for camera interface path.
 * input: none.
 * output: ESP_OK on success; otherwise GPIO configuration error.
 */
static esp_err_t _bf20a6_config_dvp_input_pins(void)
{
    const gpio_num_t input_pins[] = {
        CAM_IO_HREF,
        CAM_IO_VSYNC,
        CAM_IO_PCLK,
        CAM_IO_D0,
        CAM_IO_D1,
        CAM_IO_D2,
        CAM_IO_D3,
        CAM_IO_D4,
        CAM_IO_D5,
        CAM_IO_D6,
        CAM_IO_D7,
    };
    gpio_config_t io_cfg = {0};
    uint64_t pin_mask = 0;
    size_t i;

    for (i = 0; i < (sizeof(input_pins) / sizeof(input_pins[0])); i++)
    {
        gpio_num_t pin = input_pins[i];
        if ((pin != GPIO_NUM_NC) && GPIO_IS_VALID_GPIO(pin))
        {
            pin_mask |= (1ULL << (uint32_t)pin);
        }
    }

    if (pin_mask == 0ULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    io_cfg.pin_bit_mask = pin_mask;
    io_cfg.mode = GPIO_MODE_INPUT;
    io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_cfg.intr_type = GPIO_INTR_DISABLE;

    return gpio_config(&io_cfg);
}

/*
 * brief: Create SCCB master bus and add BF20A6 SCCB device handle.
 * input: none.
 * output: ESP_OK on success; otherwise I2C bus/device creation error.
 */
static esp_err_t _bf20a6_init_sccb(void)
{
    i2c_master_bus_config_t bus_cfg = {0};
    i2c_device_config_t dev_cfg = {0};
    esp_err_t ret;

    if (s_bf20a6.sccb_ready)
    {
        return ESP_OK;
    }

    if ((CAM_IO_SCCB_SDA == GPIO_NUM_NC) || (CAM_IO_SCCB_SCL == GPIO_NUM_NC) ||
        !GPIO_IS_VALID_GPIO(CAM_IO_SCCB_SDA) || !GPIO_IS_VALID_GPIO(CAM_IO_SCCB_SCL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.i2c_port = (i2c_port_num_t)CAM_SCCB_I2C_PORT;
    bus_cfg.scl_io_num = CAM_IO_SCCB_SCL;
    bus_cfg.sda_io_num = CAM_IO_SCCB_SDA;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = 1;

    ret = i2c_new_master_bus(&bus_cfg, &s_bf20a6.sccb_bus);
    if (ret != ESP_OK)
    {
        return ret;
    }

    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = USER_BF20A6_SCCB_ADDR;
    dev_cfg.scl_speed_hz = CAM_SCCB_FREQ_HZ;

    ret = i2c_master_bus_add_device(s_bf20a6.sccb_bus, &dev_cfg, &s_bf20a6.sccb_dev);
    if (ret != ESP_OK)
    {
        (void)i2c_del_master_bus(s_bf20a6.sccb_bus);
        s_bf20a6.sccb_bus = NULL;
        return ret;
    }

    s_bf20a6.sccb_ready = true;
    return ESP_OK;
}

/*
 * brief: Return true when BF20A6 low-level initialization has completed.
 * input: none.
 * output: true when ready; otherwise false.
 */
bool bf20a6_is_ready(void)
{
    return s_bf20a6.ready;
}

/*
 * brief: Release BF20A6 private SCCB ownership before esp32-camera takes over.
 * input: none.
 * output: ESP_OK on success; otherwise propagated SCCB resource release error.
 */
esp_err_t bf20a6_prepare_preview_start(void)
{
    esp_err_t ret;

    ret = bf20a6_deinit_device();
    if ((ret == ESP_OK) || (ret == ESP_ERR_INVALID_STATE))
    {
        return ESP_OK;
    }

    return ret;
}

/*
 * brief: Write one SCCB register on BF20A6.
 * input: reg - register address; value - register value.
 * output: ESP_OK on success; otherwise I2C state/transfer error.
 */
esp_err_t bf20a6_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx_buf[2];

    if (!s_bf20a6.sccb_ready || (s_bf20a6.sccb_dev == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    tx_buf[0] = reg;
    tx_buf[1] = value;
    return i2c_master_transmit(s_bf20a6.sccb_dev, tx_buf, sizeof(tx_buf), CAM_SCCB_TIMEOUT_MS);
}

/*
 * brief: Read one SCCB register from BF20A6.
 * input: reg - register address; value - output register value pointer.
 * output: ESP_OK on success; otherwise invalid-arg/state or I2C transfer error.
 */
esp_err_t bf20a6_read_reg(uint8_t reg, uint8_t *value)
{
    if ((value == NULL) || !s_bf20a6.sccb_ready || (s_bf20a6.sccb_dev == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit_receive(s_bf20a6.sccb_dev,
                                       &reg,
                                       1,
                                       value,
                                       1,
                                       CAM_SCCB_TIMEOUT_MS);
}

/*
 * brief: Write a register table sequentially over SCCB.
 * input: regs - register/value table; reg_count - number of table entries.
 * output: ESP_OK on success; otherwise invalid-arg/state or transfer error.
 */
esp_err_t bf20a6_write_regs(const bf20a6_reg_val_t *regs, size_t reg_count)
{
    size_t i;

    if ((regs == NULL) && (reg_count > 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    for (i = 0; i < reg_count; i++)
    {
        USER_RETURN_ON_ERROR(bf20a6_write_reg(regs[i].reg, regs[i].value),
                             TAG,
                             "bf20a6_write_reg failed");
    }

    return ESP_OK;
}

/*
 * brief: Drive BF20A6 PWDN pin level.
 * input: power_down - true for power-down mode, false for normal operation.
 * output: ESP_OK on success; otherwise GPBA write error.
 */
esp_err_t bf20a6_set_pwdn(bool power_down)
{
    return gpba02b_pin_write(CAM_IO_PWDN_PORT, CAM_IO_PWDN_PIN, power_down);
}

/*
 * brief: Control camera light output pin level.
 * input: enable - true to enable light, false to disable light.
 * output: ESP_OK on success; otherwise GPBA write error.
 */
esp_err_t bf20a6_set_light(bool enable)
{
    return gpba02b_pin_write(CAM_IO_LIGHT_PORT, CAM_IO_LIGHT_PIN, enable);
}

/*
 * brief: Issue hardware reset/power-up sequence for BF20A6.
 * input: none.
 * output: ESP_OK on success; otherwise GPBA pin write error.
 */
esp_err_t bf20a6_hard_reset(void)
{
    USER_RETURN_ON_ERROR(bf20a6_set_pwdn(true), TAG, "set PWDN high failed");
    delay_ms(2U);

    USER_RETURN_ON_ERROR(bf20a6_set_pwdn(false), TAG, "set PWDN low failed");
    delay_ms(2U);

    USER_RETURN_ON_ERROR(_bf20a6_set_reset_level(false), TAG, "set RESET low failed");
    delay_ms(2U);

    USER_RETURN_ON_ERROR(_bf20a6_set_reset_level(true), TAG, "set RESET high failed");
    delay_ms(20U);

    return ESP_OK;
}

/*
 * brief: Issue software reset through SCCB_RESET bit.
 * input: none.
 * output: ESP_OK on success; otherwise SCCB transfer error.
 */
esp_err_t bf20a6_soft_reset(void)
{
    USER_RETURN_ON_ERROR(bf20a6_write_reg(BF20A6_REG_SCCB_RESET, BF20A6_SCCB_RESET_BIT),
                         TAG,
                         "write SCCB_RESET failed");
    delay_ms(10U);
    return ESP_OK;
}

/*
 * brief: Read BF20A6 product/version IDs.
 * input: id - output ID structure pointer.
 * output: ESP_OK on success; otherwise invalid-arg or SCCB transfer error.
 */
esp_err_t bf20a6_read_id(bf20a6_id_t *id)
{
    if (id == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    USER_RETURN_ON_ERROR(bf20a6_read_reg(BF20A6_REG_VER_BME, &id->ver), TAG, "read VER failed");
    USER_RETURN_ON_ERROR(bf20a6_read_reg(BF20A6_REG_PIDH_BME, &id->pidh), TAG, "read PIDH failed");
    USER_RETURN_ON_ERROR(bf20a6_read_reg(BF20A6_REG_PIDL_BME, &id->pidl), TAG, "read PIDL failed");
    return ESP_OK;
}

/*
 * brief: Verify BF20A6 PID value matches expected silicon ID.
 * input: none.
 * output: ESP_OK when ID matches; otherwise ESP_ERR_NOT_FOUND.
 */
esp_err_t bf20a6_verify_id(void)
{
    bf20a6_id_t id = {0};
    uint16_t pid;

    USER_RETURN_ON_ERROR(bf20a6_read_id(&id), TAG, "bf20a6_read_id failed");

    pid = ((uint16_t)id.pidh << 8U) | (uint16_t)id.pidl;
    if (pid != BF20A6_PID_VALUE)
    {
        ESP_LOGE(TAG,
                 "unexpected BF20A6 PID: pidh=0x%02X pidl=0x%02X ver=0x%02X",
                 id.pidh,
                 id.pidl,
                 id.ver);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG,
             "BF20A6 detected: pidh=0x%02X pidl=0x%02X ver=0x%02X",
             id.pidh,
             id.pidl,
             id.ver);
    return ESP_OK;
}

/*
 * brief: Initialize BF20A6 low-level hardware and verify SCCB communication.
 * input: none.
 * output: ESP_OK on success; otherwise propagated peripheral setup error.
 */
esp_err_t bf20a6_init_device(void)
{
    if (s_bf20a6.ready)
    {
        return ESP_OK;
    }

    USER_RETURN_ON_ERROR(_bf20a6_config_control_pins(), TAG, "_bf20a6_config_control_pins failed");
    USER_RETURN_ON_ERROR(_bf20a6_config_dvp_input_pins(), TAG, "_bf20a6_config_dvp_input_pins failed");
    USER_RETURN_ON_ERROR(bf20a6_set_light(false), TAG, "bf20a6_set_light failed");

#if CAM_XCLK_EXTERNAL_OSC
    ESP_LOGI(TAG,
             "use external XCLK oscillator: %u Hz (CAM_IO_XCLK=%d)",
             (unsigned)CAM_XCLK_EXTERNAL_HZ,
             (int)CAM_IO_XCLK);
#else
    ESP_LOGE(TAG, "internal XCLK output is not implemented in bf20a6 driver");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    USER_RETURN_ON_ERROR(_bf20a6_init_sccb(), TAG, "_bf20a6_init_sccb failed");
    USER_RETURN_ON_ERROR(bf20a6_hard_reset(), TAG, "bf20a6_hard_reset failed");
    USER_RETURN_ON_ERROR(bf20a6_soft_reset(), TAG, "bf20a6_soft_reset failed");
    USER_RETURN_ON_ERROR(bf20a6_verify_id(), TAG, "bf20a6_verify_id failed");

    s_bf20a6.ready = true;
    return ESP_OK;
}

/*
 * brief: Remove SCCB device/bus handles and reset software state.
 * input: none.
 * output: ESP_OK on success; otherwise I2C resource release error.
 */
esp_err_t bf20a6_deinit_device(void)
{
    esp_err_t ret;

    if (!s_bf20a6.sccb_ready)
    {
        s_bf20a6.ready = false;
        return ESP_OK;
    }

    ret = i2c_master_bus_rm_device(s_bf20a6.sccb_dev);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = i2c_del_master_bus(s_bf20a6.sccb_bus);
    if (ret != ESP_OK)
    {
        return ret;
    }

    s_bf20a6.sccb_dev = NULL;
    s_bf20a6.sccb_bus = NULL;
    s_bf20a6.sccb_ready = false;
    s_bf20a6.ready = false;
    return ESP_OK;
}

#else

bool bf20a6_is_ready(void)
{
    return false;
}

esp_err_t bf20a6_prepare_preview_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bf20a6_write_reg(uint8_t reg, uint8_t value)
{
    (void)reg;
    (void)value;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bf20a6_read_reg(uint8_t reg, uint8_t *value)
{
    (void)reg;
    (void)value;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bf20a6_write_regs(const bf20a6_reg_val_t *regs, size_t reg_count)
{
    (void)regs;
    (void)reg_count;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bf20a6_set_pwdn(bool power_down)
{
    (void)power_down;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bf20a6_set_light(bool enable)
{
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bf20a6_hard_reset(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bf20a6_soft_reset(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bf20a6_read_id(bf20a6_id_t *id)
{
    (void)id;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bf20a6_verify_id(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bf20a6_init_device(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bf20a6_deinit_device(void)
{
    return ESP_OK;
}

#endif
