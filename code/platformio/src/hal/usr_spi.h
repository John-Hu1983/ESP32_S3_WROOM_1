#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/spi_master.h"

#include "esp_err.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "soc/soc_caps.h"

#include "user_config.h"

typedef struct
{
    spi_device_handle_t fd;
    spi_host_device_t host;
    spi_bus_config_t bus;
    gpio_num_t cs_pin;
    int clock_speed_hz;
} usr_spi_s;

typedef struct
{
    SemaphoreHandle_t mutex;
    StaticSemaphore_t storage;
} spi_host_mutex_s;

/* Initialize mutex for one SPI host shared by multiple peripherals. */
esp_err_t spi_bus_mutex_init(spi_host_device_t host);
/* Acquire SPI host mutex before performing one or more bus transactions. */
esp_err_t spi_bus_acquire(spi_host_device_t host, TickType_t timeout_ticks);
/* Release SPI host mutex after finishing bus transactions. */
esp_err_t spi_bus_release(spi_host_device_t host);
/* Return true when SPI host mutex is currently not owned by any task. */
bool spi_bus_is_idle(spi_host_device_t host);

/* Create one SPI device with bus pins, CS pin, and target clock speed. */
esp_err_t spi_create_device(usr_spi_s *spi,
                            spi_host_device_t host,
                            gpio_num_t miso,
                            gpio_num_t mosi,
                            gpio_num_t clk,
                            gpio_num_t cs_pin,
                            int speed_hz);
/* Send a raw byte buffer over SPI using blocking transfer. */
esp_err_t spi_write_nbyte(usr_spi_s *spi, const uint8_t *data, size_t len);
/* Send TX bytes then receive RX bytes as one logical operation, keeping CS asserted between phases. */
esp_err_t spi_write_read_nbyte(usr_spi_s *spi,
                               const uint8_t *tx_data,
                               size_t tx_len,
                               uint8_t *rx_data,
                               size_t rx_len);
/* Send a large byte buffer over SPI using queued DMA chunks. */
esp_err_t spi_write_nbyte_dma_queue(usr_spi_s *spi,
                                    const uint8_t *data,
                                    size_t len,
                                    size_t chunk_len,
                                    size_t queue_depth);
/* Read a raw byte buffer over SPI using blocking transfer. */
esp_err_t spi_read_nbyte(usr_spi_s *spi, uint8_t *data, size_t len);

