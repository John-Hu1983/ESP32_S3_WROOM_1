#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp_global.h"
#include "user/device/dev_gpba02b.h"
#include "user/inc/user_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
/* Length and size limits. */
#define MFRC522_UID_MAX_LEN       (10U)
#define MFRC522_KEY_LEN           (6U)
#define MFRC522_BLOCK_LEN         (16U)
#define MFRC522_FM11_BLOCK_LEN    (4U)
#define MFRC522_FM11_BLOCK_COUNT  (16U)
#define MFRC522_SECTOR_MAX_BLOCKS (16U)
#define MFRC522_CMD_TIMEOUT_MS    (50U)
#define MFRC522_FIFO_MAX_LEN      (64U)
#define MFRC522_DEFAULT_KEY_BYTE  (0xFFU)

/* Interrupt flags. */
#define MFRC522_COM_IRQ_TIMER (0x01U)
#define MFRC522_COM_IRQ_IDLE  (0x10U)
#define MFRC522_COM_IRQ_RX    (0x20U)
#define MFRC522_DIV_IRQ_CRC   (0x04U)

/* Bit masks and protocol constants. */
#define MFRC522_BIT_START_SEND     (0x80U)
#define MFRC522_FIFO_FLUSH         (0x80U)
#define MFRC522_COLL_ERR_BIT       (0x20U)
#define MFRC522_STATUS2_CRYPTO1_ON (0x08U)
#define MFRC522_TX_ANTENNA_ON_MASK (0x03U)
#define MFRC522_PICC_ACK           (0x0AU)
#define MFRC522_SAK_CASCADE_BIT    (0x04U)

typedef enum {
    MFRC522_CMD_IDLE       = 0x00U,
    MFRC522_CMD_CALC_CRC   = 0x03U,
    MFRC522_CMD_TRANSCEIVE = 0x0CU,
    MFRC522_CMD_MF_AUTHENT = 0x0EU,
    MFRC522_CMD_SOFT_RESET = 0x0FU,
} mfrc522_cmd_e;

typedef enum {
    /* Core command/IRQ/FIFO/control registers. */
    MFRC522_REG_COMMAND      = 0x01U,
    MFRC522_REG_COM_IRQ      = 0x04U,
    MFRC522_REG_DIV_IRQ      = 0x05U,
    MFRC522_REG_ERROR        = 0x06U,
    MFRC522_REG_STATUS2      = 0x08U,
    MFRC522_REG_FIFO_DATA    = 0x09U,
    MFRC522_REG_FIFO_LEVEL   = 0x0AU,
    MFRC522_REG_CONTROL      = 0x0CU,
    MFRC522_REG_BIT_FRAMING  = 0x0DU,
    MFRC522_REG_COLL         = 0x0EU,

    /* Mode and TX path registers. */
    MFRC522_REG_MODE         = 0x11U,
    MFRC522_REG_TX_CONTROL   = 0x14U,
    MFRC522_REG_TX_ASK       = 0x15U,

    /* CRC/timer/RF and identification registers. */
    MFRC522_REG_CRC_RESULT_H = 0x21U,
    MFRC522_REG_CRC_RESULT_L = 0x22U,
    MFRC522_REG_RF_CFG       = 0x26U,
    MFRC522_REG_T_MODE       = 0x2AU,
    MFRC522_REG_T_PRESCALER  = 0x2BU,
    MFRC522_REG_T_RELOAD_H   = 0x2CU,
    MFRC522_REG_T_RELOAD_L   = 0x2DU,
    MFRC522_REG_VERSION      = 0x37U,
} mfrc522_reg_e;

typedef enum {
    /* ISO14443A request/wakeup. */
    MFRC522_PICC_CMD_REQA     = 0x26U,
    MFRC522_PICC_CMD_WUPA     = 0x52U,

    /* Anticollision/select cascade commands. */
    MFRC522_PICC_CMD_CT       = 0x88U,
    MFRC522_PICC_CMD_SEL_CL1  = 0x93U,
    MFRC522_PICC_CMD_SEL_CL2  = 0x95U,
    MFRC522_PICC_CMD_SEL_CL3  = 0x97U,

    /* MIFARE-style halt/read/write. */
    MFRC522_PICC_CMD_HLTA     = 0x50U,
    MFRC522_PICC_CMD_MF_READ  = 0x30U,
    MFRC522_PICC_CMD_MF_WRITE = 0xA0U,
} mfrc522_picc_cmd_e;

typedef enum {
    MFRC522_KEY_A             = 0x60U,
    MFRC522_KEY_B             = 0x61U,
} mfrc522_key_type_t;
// clang-format on

typedef struct {
    spi_device_handle_t spi;
    spi_host_device_t spi_host;
    bool initialized;
    bool bus_initialized_here;
} mfrc522_ctx_t;

typedef struct {
    uint8_t atqa[2];
    uint8_t uid[MFRC522_UID_MAX_LEN];
    uint8_t size;
    uint8_t sak;
} mfrc522_uid_t;

typedef struct {
    uint8_t sector;
    uint8_t first_block;
    uint8_t block_count;
    uint8_t block_data[MFRC522_SECTOR_MAX_BLOCKS][MFRC522_BLOCK_LEN];
} mfrc522_sector_data_t;

esp_err_t mfrc522_init(void);
esp_err_t mfrc522_deinit(void);
bool mfrc522_is_ready(void);
esp_err_t mfrc522_get_version(uint8_t* version);

esp_err_t mfrc522_poll_card(bool* present);
esp_err_t mfrc522_request_type_a(uint8_t atqa[2]);
esp_err_t mfrc522_wakeup_type_a(uint8_t atqa[2]);
esp_err_t mfrc522_read_uid(mfrc522_uid_t* uid);
esp_err_t mfrc522_select_uid(const mfrc522_uid_t* uid);

esp_err_t mfrc522_authenticate(
    mfrc522_key_type_t key_type,
    uint8_t block_addr,
    const uint8_t key[MFRC522_KEY_LEN],
    const mfrc522_uid_t* uid
);
void mfrc522_stop_crypto(void);
esp_err_t mfrc522_halt(void);

esp_err_t mfrc522_read_block(uint8_t block_addr, uint8_t out_block[MFRC522_BLOCK_LEN]);
esp_err_t mfrc522_write_block(uint8_t block_addr, const uint8_t in_block[MFRC522_BLOCK_LEN]);
esp_err_t mfrc522_read_block4(uint8_t block_addr, uint8_t out_block[MFRC522_FM11_BLOCK_LEN]);
esp_err_t mfrc522_write_block4(uint8_t block_addr, const uint8_t in_block[MFRC522_FM11_BLOCK_LEN]);
esp_err_t mfrc522_read_sector(
    uint8_t sector,
    mfrc522_key_type_t key_type,
    const uint8_t key[MFRC522_KEY_LEN],
    const mfrc522_uid_t* uid,
    mfrc522_sector_data_t* out_sector
);

#ifdef __cplusplus
}
#endif
