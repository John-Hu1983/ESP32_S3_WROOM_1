#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t gpba02b_port_t;

enum {
    GPBA02B_PORT_A = 0,
    GPBA02B_PORT_B = 1,
    GPBA02B_PORT_C = 2,
};

typedef enum {
    GPBA02B_IO_REG_BUFFER = 0,
    GPBA02B_IO_REG_DIRECTION = 1,
    GPBA02B_IO_REG_ATTRIBUTE = 2,
} gpba02b_io_reg_t;

typedef enum {
    GPBA02B_IO_INPUT_FLOATING = 0,
    GPBA02B_IO_INPUT_PULL_LOW = 1,
    GPBA02B_IO_INPUT_PULL_HIGH = 2,
} gpba02b_io_input_mode_t;

typedef enum {
    GPBA02B_IO_OUTPUT_PUSH_PULL = 0,
    GPBA02B_IO_OUTPUT_OPEN_DRAIN_NMOS = 1,
    GPBA02B_IO_OUTPUT_OPEN_DRAIN_PMOS = 2,
} gpba02b_io_output_mode_t;

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t miso_io;
    gpio_num_t mosi_io;
    gpio_num_t sclk_io;
    gpio_num_t cs_io;
    int clock_hz;
    uint8_t device_id;
    int queue_size;
} gpba02b_config_t;

typedef struct {
    gpio_num_t gpio_num;
    gpio_int_type_t intr_type;
    bool pull_up;
    bool pull_down;
} gpba02b_host_irq_gpio_config_t;

typedef void (*gpba02b_host_irq_callback_t)(void* user_ctx);

typedef struct gpba02b gpba02b_t;

gpba02b_t* gpba02b_instance(void);

void gpba02b_get_default_config(gpba02b_config_t* config);

esp_err_t gpba02b_init(gpba02b_t* dev, const gpba02b_config_t* config);
void gpba02b_deinit(gpba02b_t* dev);

esp_err_t gpba02b_io_write(gpba02b_t* dev, gpba02b_port_t port, gpba02b_io_reg_t reg,
                           uint8_t value);
esp_err_t gpba02b_io_read(gpba02b_t* dev, gpba02b_port_t port, gpba02b_io_reg_t reg,
                          uint8_t* value);
esp_err_t gpba02b_write_io(gpba02b_t* dev, gpba02b_port_t port, uint8_t pin, bool level);
esp_err_t gpba02b_read_io(gpba02b_t* dev, gpba02b_port_t port, uint8_t pin, bool* level);
esp_err_t gpba02b_config_io_input_mode(gpba02b_t* dev, gpba02b_port_t port, uint8_t pin,
                                       gpba02b_io_input_mode_t mode);
esp_err_t gpba02b_config_io_output_mode(gpba02b_t* dev, gpba02b_port_t port, uint8_t pin,
                                        gpba02b_io_output_mode_t mode, bool level);

esp_err_t gpba02b_config_io_input(gpba02b_t* dev, gpba02b_port_t port, uint8_t pin,
                                  bool pull_up);
esp_err_t gpba02b_config_io_output(gpba02b_t* dev, gpba02b_port_t port, uint8_t pin,
                                   bool open_collector, bool level);

esp_err_t gpba02b_read_port_input(gpba02b_t* dev, gpba02b_port_t port, uint8_t* value);

esp_err_t gpba02b_enable_new_functions(gpba02b_t* dev);
esp_err_t gpba02b_set_software_reset_disabled(gpba02b_t* dev, bool disabled);

esp_err_t gpba02b_pwm_set_clock_div(gpba02b_t* dev, uint8_t pa_div, uint8_t pc_div);
esp_err_t gpba02b_pwm_enable_channels(gpba02b_t* dev, gpba02b_port_t port, uint8_t channel_mask);
esp_err_t gpba02b_pwm_set_channel_duty(gpba02b_t* dev, gpba02b_port_t port, uint8_t channel,
                                       uint8_t duty);
esp_err_t gpba02b_current_sink_set(gpba02b_t* dev, gpba02b_port_t port, bool enable,
                                   uint8_t current_level);
esp_err_t gpba02b_pwm_with_current_sink_setup(gpba02b_t* dev, gpba02b_port_t port,
                                              uint8_t channel_mask, uint8_t current_level);

esp_err_t gpba02b_interrupt_configure(gpba02b_t* dev, uint8_t enable_mask,
                                      uint8_t falling_edge_mask);
esp_err_t gpba02b_interrupt_read(gpba02b_t* dev, uint8_t* flags, uint8_t* enable_mask);
esp_err_t gpba02b_interrupt_clear(gpba02b_t* dev, uint8_t flags_mask);

esp_err_t gpba02b_host_irq_install(gpba02b_t* dev,
                                   const gpba02b_host_irq_gpio_config_t* config,
                                   gpba02b_host_irq_callback_t callback, void* user_ctx);
esp_err_t gpba02b_host_irq_uninstall(gpba02b_t* dev);

#ifdef __cplusplus
}
#endif
