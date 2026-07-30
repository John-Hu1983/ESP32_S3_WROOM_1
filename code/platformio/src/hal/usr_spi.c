#include "usr_spi.h"

#include <string.h>

#include "soc/soc_caps.h"

static bool spi_host_is_valid(spi_host_device_t host)
{
    return ((int)host >= 0) && ((int)host < SOC_SPI_PERIPH_NUM);
}

static esp_err_t spi_init_device(usr_spi_s *spi)
{
    spi_device_interface_config_t dev_config = {0};
    esp_err_t ret = ESP_OK;

    ret = spi_bus_initialize(spi->host, &spi->bus, SPI_DMA_CH_AUTO);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
    {
        return ret;
    }

    dev_config.clock_speed_hz = spi->clock_speed_hz;
    dev_config.mode = 0;
    dev_config.spics_io_num = spi->cs_pin;
    dev_config.queue_size = 1;
    dev_config.flags = SPI_DEVICE_HALFDUPLEX;
    ret = spi_bus_add_device(spi->host, &dev_config, &spi->fd);

    return ret;
}

esp_err_t spi_create_device(usr_spi_s *spi,
                            spi_host_device_t host,
                            gpio_num_t miso,
                            gpio_num_t mosi,
                            gpio_num_t clk,
                            gpio_num_t cs_pin,
                            int speed_hz)
{
    esp_err_t ret;

    if (spi == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!spi_host_is_valid(host) || (speed_hz <= 0))
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(spi, 0, sizeof(*spi));

    spi->host = host;
    spi->bus.miso_io_num = miso;
    spi->bus.mosi_io_num = mosi;
    spi->bus.sclk_io_num = clk;
    spi->bus.quadwp_io_num = -1;
    spi->bus.quadhd_io_num = -1;
    spi->cs_pin = cs_pin;
    spi->clock_speed_hz = speed_hz;

    // if (cs_pin != GPIO_NUM_NC)
    // {
    //     gpio_config_t io_cfg = {0};
    //     io_cfg.intr_type = GPIO_INTR_DISABLE;
    //     io_cfg.mode = GPIO_MODE_OUTPUT;
    //     io_cfg.pin_bit_mask = (1ULL << (uint32_t)cs_pin);
    //     io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    //     io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;

    //     ret = gpio_config(&io_cfg);
    //     if (ret != ESP_OK)
    //     {
    //         return ret;
    //     }

    //     ret = gpio_set_level(cs_pin, 1);
    //     if (ret != ESP_OK)
    //     {
    //         return ret;
    //     }
    // }

    return spi_init_device(spi);
}

esp_err_t spi_write_nbyte(usr_spi_s *spi, const uint8_t *data, size_t len)
{
    spi_transaction_t t = {0};

    if ((spi == NULL) || (data == NULL) || (len == 0))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (spi->fd == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    t.length = len * 8;
    t.tx_buffer = data;
    return spi_device_polling_transmit(spi->fd, &t);
}

esp_err_t spi_read_nbyte(usr_spi_s *spi, uint8_t *data, size_t len)
{
    spi_transaction_t t = {0};

    if ((spi == NULL) || (data == NULL) || (len == 0))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (spi->fd == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    t.length = 0;
    t.rxlength = len * 8;
    t.rx_buffer = data;
    return spi_device_polling_transmit(spi->fd, &t);
}
