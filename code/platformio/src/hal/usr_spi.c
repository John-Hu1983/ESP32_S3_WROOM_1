#include "usr_spi.h"

#define USR_SPI_DEVICE_QUEUE_SIZE 6U

/*
 * brief: Check whether the given SPI host id is within valid SoC range.
 * input: host - SPI host id to validate.
 * output: true when host is valid; otherwise false.
 */
static bool spi_host_is_valid(spi_host_device_t host)
{
    return ((int)host >= 0) && ((int)host < SOC_SPI_PERIPH_NUM);
}

/*
 * brief: Initialize SPI bus and register one SPI device handle.
 * input: spi - pointer to initialized usr_spi_s configuration.
 * output: ESP_OK on success; otherwise ESP-IDF error code.
 */
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
    dev_config.queue_size = USR_SPI_DEVICE_QUEUE_SIZE;
    dev_config.flags = SPI_DEVICE_HALFDUPLEX;
    ret = spi_bus_add_device(spi->host, &dev_config, &spi->fd);

    return ret;
}

/*
 * brief: Release DMA chunk buffer pool used by queued write path.
 * input: dma_buffers - array of DMA buffer pointers; count - number of slots.
 * output: none.
 */
static void spi_free_dma_pool(uint8_t **dma_buffers, size_t count)
{
    size_t i;

    if (dma_buffers == NULL)
    {
        return;
    }

    for (i = 0; i < count; i++)
    {
        if (dma_buffers[i] != NULL)
        {
            heap_caps_free(dma_buffers[i]);
        }
    }
}

/*
 * brief: Create and initialize one SPI device from user pin and speed config.
 * input: spi/host/miso/mosi/clk/cs_pin/speed_hz - device and bus parameters.
 * output: ESP_OK when device is ready; otherwise ESP-IDF error code.
 */
esp_err_t spi_create_device(usr_spi_s *spi,
                            spi_host_device_t host,
                            gpio_num_t miso,
                            gpio_num_t mosi,
                            gpio_num_t clk,
                            gpio_num_t cs_pin,
                            int speed_hz)
{
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

     return spi_init_device(spi);
}

/*
 * brief: Write N bytes to SPI using blocking polling transmit mode.
 * input: spi - device handle wrapper; data - tx buffer; len - byte count.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG/STATE or driver error.
 */
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

/*
 * brief: Stream large SPI write using queued DMA transactions and bounce buffers.
 * input: spi - device wrapper; data - source buffer (supports PSRAM); len - bytes;
 *        chunk_len - bytes per queued transaction; queue_depth - in-flight count.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG/STATE/NO_MEM or driver error.
 */
esp_err_t spi_write_nbyte_dma_queue(usr_spi_s *spi,
                                    const uint8_t *data,
                                    size_t len,
                                    size_t chunk_len,
                                    size_t queue_depth)
{
    uint8_t **dma_buffers = NULL;
    spi_transaction_t *transactions = NULL;
    size_t depth;
    size_t submit_idx = 0;
    size_t pending = 0;
    size_t offset = 0;
    size_t i;
    esp_err_t ret = ESP_OK;

    if ((spi == NULL) || (data == NULL) || (len == 0) || (chunk_len == 0) || (queue_depth == 0))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (spi->fd == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    depth = queue_depth;
    if (depth > USR_SPI_DEVICE_QUEUE_SIZE)
    {
        depth = USR_SPI_DEVICE_QUEUE_SIZE;
    }

    dma_buffers = (uint8_t **)heap_caps_calloc(depth, sizeof(uint8_t *),
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    transactions = (spi_transaction_t *)heap_caps_calloc(depth, sizeof(spi_transaction_t),
                                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if ((dma_buffers == NULL) || (transactions == NULL))
    {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    for (i = 0; i < depth; i++)
    {
        dma_buffers[i] = (uint8_t *)heap_caps_malloc(chunk_len, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (dma_buffers[i] == NULL)
        {
            ret = ESP_ERR_NO_MEM;
            goto cleanup;
        }
    }

    while ((offset < len) || (pending > 0U))
    {
        while ((offset < len) && (pending < depth))
        {
            size_t tx_len = len - offset;

            if (tx_len > chunk_len)
            {
                tx_len = chunk_len;
            }

            memcpy(dma_buffers[submit_idx], data + offset, tx_len);
            memset(&transactions[submit_idx], 0, sizeof(spi_transaction_t));
            transactions[submit_idx].length = tx_len * 8U;
            transactions[submit_idx].tx_buffer = dma_buffers[submit_idx];

            ret = spi_device_queue_trans(spi->fd, &transactions[submit_idx], portMAX_DELAY);
            if (ret != ESP_OK)
            {
                goto cleanup;
            }

            offset += tx_len;
            pending++;
            submit_idx++;
            if (submit_idx >= depth)
            {
                submit_idx = 0;
            }
        }

        if (pending > 0U)
        {
            spi_transaction_t *done = NULL;

            ret = spi_device_get_trans_result(spi->fd, &done, portMAX_DELAY);
            if (ret != ESP_OK)
            {
                goto cleanup;
            }

            pending--;
        }
    }

cleanup:
    if ((ret != ESP_OK) && (spi->fd != NULL))
    {
        spi_transaction_t *done = NULL;
        while (spi_device_get_trans_result(spi->fd, &done, 0) == ESP_OK)
        {
        }
    }

    spi_free_dma_pool(dma_buffers, depth);
    if (transactions != NULL)
    {
        heap_caps_free(transactions);
    }
    if (dma_buffers != NULL)
    {
        heap_caps_free(dma_buffers);
    }

    return ret;
}

/*
 * brief: Read N bytes from SPI using blocking polling transmit mode.
 * input: spi - device handle wrapper; data - rx buffer; len - byte count.
 * output: ESP_OK on success; otherwise ESP_ERR_INVALID_ARG/STATE or driver error.
 */
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
