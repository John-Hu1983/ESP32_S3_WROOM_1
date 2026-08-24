#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#include <stddef.h>
#include "user/inc/user_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GPBA02B_CMD_READ (0x00u)
#define GPBA02B_CMD_WRITE (0x80u)

#define GPBA02B_REG_BUFA 0x00u
#define GPBA02B_REG_BUFB 0x01u
#define GPBA02B_REG_BUFC 0x02u
#define GPBA02B_REG_NEW_FUNC_03 0x03u
#define GPBA02B_REG_DIRA 0x04u
#define GPBA02B_REG_DIRB 0x05u
#define GPBA02B_REG_DIRC 0x06u
#define GPBA02B_REG_NEW_FUNC_07 0x07u
#define GPBA02B_REG_ATTA 0x08u
#define GPBA02B_REG_ATTB 0x09u
#define GPBA02B_REG_ATTC 0x0Au
#define GPBA02B_REG_DATAA 0x0Cu
#define GPBA02B_REG_DATAB 0x0Du
#define GPBA02B_REG_DATAC 0x0Eu

#define GPBA02B_REG_PA_PWM_ENABLE 0x17u
#define GPBA02B_REG_PWMCK 0x1Bu
#define GPBA02B_REG_PA_DUTY0 0x1Cu
#define GPBA02B_REG_PA_DUTY1 0x1Du
#define GPBA02B_REG_PA_DUTY2 0x1Eu
#define GPBA02B_REG_PA_DUTY3 0x1Fu
#define GPBA02B_REG_PC_PWM_ENABLE 0x27u
#define GPBA02B_REG_PA_DUTY4 0x2Bu
#define GPBA02B_REG_PA_DUTY5 0x2Cu
#define GPBA02B_REG_PA_DUTY6 0x2Du
#define GPBA02B_REG_PA_DUTY7 0x2Eu
#define GPBA02B_REG_PC_DUTY0 0x2Fu
#define GPBA02B_REG_PC_DUTY1 0x33u
#define GPBA02B_REG_PC_DUTY2 0x37u
#define GPBA02B_REG_PC_DUTY3 0x3Bu
#define GPBA02B_REG_PC_DUTY4 0x3Cu
#define GPBA02B_REG_PC_DUTY5 0x3Du
#define GPBA02B_REG_PC_DUTY6 0x3Eu
#define GPBA02B_REG_PC_DUTY7 0x3Fu

#define GPBA02B_PWM_BASE_CLOCK_HZ (11000000u)
#define GPBA02B_PWM_DIV_SEL_MASK (0x07u)
#define GPBA02B_PWMCK_PA_DIV_SHIFT (4u)
#define GPBA02B_PWMCK_PA_DIV_MASK (0x70u)
#define GPBA02B_PWMCK_PC_DIV_MASK (0x07u)

typedef enum {
    GPBA02B_PORT_A = 0,
    GPBA02B_PORT_B,
    GPBA02B_PORT_C,
} gpba02b_port_t;

typedef enum {
    GPBA02B_PWM_FREQ_42969HZ_DIV1 = 0, /* 11000000 / (1 * 256) */
    GPBA02B_PWM_FREQ_21484HZ_DIV2,     /* 11000000 / (2 * 256) */
    GPBA02B_PWM_FREQ_10742HZ_DIV4,     /* 11000000 / (4 * 256) */
    GPBA02B_PWM_FREQ_2686HZ_DIV16,     /* 11000000 / (16 * 256) */
    GPBA02B_PWM_FREQ_1343HZ_DIV32,     /* 11000000 / (32 * 256) */
    GPBA02B_PWM_FREQ_671HZ_DIV64,      /* 11000000 / (64 * 256) */
    GPBA02B_PWM_FREQ_336HZ_DIV128,     /* 11000000 / (128 * 256) */
    GPBA02B_PWM_FREQ_168HZ_DIV256,     /* 11000000 / (256 * 256) */
} gpba02b_pwm_freq_t;

typedef enum {
    GPBA02B_IO_STYLE_INPUT_HIGH_Z = 0,
    GPBA02B_IO_STYLE_INPUT_PULL_LOW,
    GPBA02B_IO_STYLE_INPUT_PULL_HIGH,
    GPBA02B_IO_STYLE_OUTPUT_CMOS,
    GPBA02B_IO_STYLE_OUTPUT_CMOS_INVERTED,
    GPBA02B_IO_STYLE_OUTPUT_OPEN_DRAIN_NMOS,
    GPBA02B_IO_STYLE_OUTPUT_OPEN_DRAIN_PMOS,
} gpba02b_io_style_t;

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t sclk_io_num;
    gpio_num_t mosi_io_num;
    gpio_num_t miso_io_num;
    gpio_num_t cs_io_num;
    int clock_hz;
    int queue_size;
    uint8_t device_bit;
    bool initialize_bus;
} gpba02b_config_t;

typedef struct {
    spi_device_handle_t spi;
    spi_host_device_t spi_host;
    bool initialized;
    bool bus_initialized;
    uint8_t device_bit;
    bool new_function_enabled;
    uint8_t pa_pwm_enable_shadow;
    uint8_t pc_pwm_enable_shadow;
    uint8_t pwmck_shadow;
} gpba02b_ctx_t;

extern gpba02b_config_t g_gpba02b_config;

esp_err_t gpba02b_init_object(void);
esp_err_t gpba02b_deinit(void);

/* 1) Configure GPIO mode by (port, pin, style). */
esp_err_t gpba02b_set_io_mode(gpba02b_port_t port, uint8_t pin, gpba02b_io_style_t style);

/* 2) Read GPIO level by (port, pin). */
esp_err_t gpba02b_read_io_level(gpba02b_port_t port, uint8_t pin, uint8_t* level);

/* 3) Write GPIO level by (port, pin, level). */
esp_err_t gpba02b_write_io_level(gpba02b_port_t port, uint8_t pin, uint8_t level);

/* 4) Configure one pin into PWM mode. */
esp_err_t gpba02b_config_pwm_mode(gpba02b_port_t port, uint8_t pin);

/* 5) Set PWM frequency for one port (A or C) by selectable hardware-supported enum. */
esp_err_t gpba02b_set_pwm_frequency(gpba02b_port_t port, gpba02b_pwm_freq_t frequency);

/* 6) Set PWM duty percent for one pin (0..100). */
esp_err_t gpba02b_set_pwm_duty(gpba02b_port_t port, uint8_t pin, uint8_t duty_percent);

/*
 * 7) Read one raw GPBA02B register (0x00..0x3F).
 * Note: many new-function registers are write-only per datasheet.
 */
esp_err_t gpba02b_read_register(uint8_t reg, uint8_t* value);

/* 8) Update command device-id bit at runtime (0 or 1). */
esp_err_t gpba02b_set_device_id(uint8_t device_bit);

/* 9) Query current command device-id bit. */
uint8_t gpba02b_get_device_id(void);

#ifdef __cplusplus
}
#endif
