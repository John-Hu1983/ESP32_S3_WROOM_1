#pragma once

#include "driver/spi_master.h"

#include "user_config.h"
typedef struct
{
    spi_device_handle_t fd;
    spi_host_device_t host;
    spi_bus_config_t bus;
    gpio_num_t cs_pin;
    int clock_speed_hz;
} usr_spi_s;

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
/* Send a large byte buffer over SPI using queued DMA chunks. */
esp_err_t spi_write_nbyte_dma_queue(usr_spi_s *spi,
                                    const uint8_t *data,
                                    size_t len,
                                    size_t chunk_len,
                                    size_t queue_depth);
/* Read a raw byte buffer over SPI using blocking transfer. */
esp_err_t spi_read_nbyte(usr_spi_s *spi, uint8_t *data, size_t len);

