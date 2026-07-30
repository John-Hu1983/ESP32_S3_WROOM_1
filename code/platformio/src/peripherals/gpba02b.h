#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_log.h"
#include "esp_err.h"

#include "hal/usr_spi.h"

#define GPBA02B_REG_BUFA 0x00
#define GPBA02B_REG_BUFB 0x01
#define GPBA02B_REG_BUFC 0x02
#define GPBA02B_REG_FUNC_ENABLE_03 0x03
#define GPBA02B_REG_DIRA 0x04
#define GPBA02B_REG_DIRB 0x05
#define GPBA02B_REG_DIRC 0x06
#define GPBA02B_REG_FUNC_ENABLE_07 0x07
#define GPBA02B_REG_ATTA 0x08
#define GPBA02B_REG_ATTB 0x09
#define GPBA02B_REG_ATTC 0x0A
#define GPBA02B_REG_INT_FLAG_ENABLE 0x0B
#define GPBA02B_REG_DATAA 0x0C
#define GPBA02B_REG_DATAB 0x0D
#define GPBA02B_REG_DATAC 0x0E
#define GPBA02B_REG_CURRENT_SINK 0x13
#define GPBA02B_REG_PA_PWM_ENABLE 0x17
#define GPBA02B_REG_PWM_CLOCK 0x1B
#define GPBA02B_REG_INT_EDGE_CMOS 0x23
#define GPBA02B_REG_PA0_DUTY 0x1C
#define GPBA02B_REG_PC_PWM_ENABLE 0x27
#define GPBA02B_REG_PA4_DUTY 0x2B
#define GPBA02B_REG_PC0_DUTY 0x2F
#define GPBA02B_REG_PC1_DUTY 0x33
#define GPBA02B_REG_PC2_DUTY 0x37
#define GPBA02B_REG_PC3_DUTY 0x3B

#define GPBA02B_DEVICE_SELECT_BIT 0U
#define GPBA02B_REG_ADDR_MASK 0x3FU
#define GPBA02B_PORT_PIN_COUNT 8U
#define GPBA02B_INT_MASK 0x0FU

typedef enum
{
    GPBA02B_PORT_A = 0,
    GPBA02B_PORT_B = 1,
    GPBA02B_PORT_C = 2,
} gpba02b_port_t;

typedef struct
{
    uint8_t buf;
    uint8_t dir;
    uint8_t att;
    uint8_t data;
} gpba02b_gpio_s;

typedef enum
{
    GPBA02B_PIN_MODE_INPUT_FLOATING = 0,
    GPBA02B_PIN_MODE_INPUT_PULLDOWN,
    GPBA02B_PIN_MODE_INPUT_PULLUP,
    GPBA02B_PIN_MODE_OUTPUT,
    GPBA02B_PIN_MODE_OUTPUT_INVERTED,
    GPBA02B_PIN_MODE_OPEN_DRAIN_NMOS,
    GPBA02B_PIN_MODE_OPEN_DRAIN_PMOS,
} gpba02b_pin_mode_t;

typedef enum
{
    GPBA02B_PWM_DIV_1 = 0,
    GPBA02B_PWM_DIV_2,
    GPBA02B_PWM_DIV_4,
    GPBA02B_PWM_DIV_16,
    GPBA02B_PWM_DIV_32,
    GPBA02B_PWM_DIV_64,
    GPBA02B_PWM_DIV_128,
    GPBA02B_PWM_DIV_256,
} gpba02b_pwm_div_t;

typedef void (*gpba02b_int_callback_t)(uint8_t int_flags, void *user_ctx);

esp_err_t gpba02b_init_device(void);
esp_err_t gpba02b_reg_write(uint8_t reg_addr, uint8_t value);
esp_err_t gpba02b_reg_read(uint8_t reg_addr, uint8_t *value);
esp_err_t gpba02b_get_gpio_regs(gpba02b_port_t port, const gpba02b_gpio_s **gpio_regs);

esp_err_t gpba02b_pin_set_mode(gpba02b_port_t port, uint8_t pin, gpba02b_pin_mode_t mode);
esp_err_t gpba02b_port_write(gpba02b_port_t port, uint8_t value);
esp_err_t gpba02b_port_read(gpba02b_port_t port, uint8_t *value);
esp_err_t gpba02b_pin_write(gpba02b_port_t port, uint8_t pin, bool level);
esp_err_t gpba02b_pin_read(gpba02b_port_t port, uint8_t pin, bool *level);

esp_err_t gpba02b_pwm_enable_mask(gpba02b_port_t port, uint8_t channel_mask, bool enable);
esp_err_t gpba02b_pwm_set_duty(gpba02b_port_t port, uint8_t channel, uint8_t duty);
esp_err_t gpba02b_pwm_set_clock_div(gpba02b_port_t port, gpba02b_pwm_div_t div);

esp_err_t gpba02b_int_set_enable_mask(uint8_t enable_mask);
esp_err_t gpba02b_int_set_falling_edge_mask(uint8_t falling_edge_mask);
esp_err_t gpba02b_int_get_flags(uint8_t *int_flags);
esp_err_t gpba02b_int_clear_flags(uint8_t int_flags);
void gpba02b_int_set_callback(gpba02b_int_callback_t cb, void *user_ctx);
esp_err_t gpba02b_int_handle_external_event(void);
