#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "user/device/dev_mfrc522.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define CPN_RFID_UID_MAX_LEN       (10U)
#define CPN_RFID_KEY_LEN           (6U)
#define CPN_RFID_BLOCK_LEN         (16U)
#define CPN_RFID_SECTOR_MAX_BLOCKS (16U)
#define CPN_RFID_FM11_BLOCK_LEN    (4U)
#define CPN_RFID_FM11_BLOCK_COUNT  (16U)

#define DEV_RFID_STATUS_TEXT_LEN (96U)
#define DEV_RFID_UID_TEXT_LEN    (96U)
#define DEV_RFID_DUMP_TEXT_LEN   (6144U)

#define DEV_RFID_VIEW_SECTOR_MIN (0U)
#define DEV_RFID_VIEW_SECTOR_MAX (15U)
// clang-format on

typedef struct {
    bool initialized;
    uint8_t reader_version;
} RFID_ctx_s;

typedef enum {
    CPN_RFID_KEY_A = 0x60U,
    CPN_RFID_KEY_B = 0x61U,
} RFID_key_type_e;

typedef struct {
    uint8_t atqa[2];
    uint8_t uid[CPN_RFID_UID_MAX_LEN];
    uint8_t uid_len;
    uint8_t sak;
    bool uid_valid;
    bool anticollision_supported;
} RFID_uid_s;

typedef struct {
    bool card_present;
    uint8_t reader_version;
    RFID_uid_s uid;
} RFID_access_s;

typedef struct {
    bool reader_ready;
    bool card_present;
    bool dump_need_scroll_top;
    uint8_t reader_version;
    char status_text[DEV_RFID_STATUS_TEXT_LEN];
    char uid_text[DEV_RFID_UID_TEXT_LEN];
    char dump_text[DEV_RFID_DUMP_TEXT_LEN];
} dev_rfid_snapshot_s;

typedef struct {
    uint8_t sector;
    uint8_t first_block;
    uint8_t block_count;
    uint8_t block_bytes;
    uint8_t block_data[CPN_RFID_SECTOR_MAX_BLOCKS][CPN_RFID_BLOCK_LEN];
} RFID_sector_data_s;

esp_err_t RFID_init(void);
esp_err_t RFID_deinit(void);
bool RFID_is_ready(void);
esp_err_t RFID_get_reader_version(uint8_t* version);

esp_err_t RFID_access(RFID_access_s* access_info);
esp_err_t RFID_halt(void);
esp_err_t dev_rfid_get_snapshot(dev_rfid_snapshot_s* out_snapshot);
esp_err_t dev_rfid_set_view_sector(uint8_t sector);
uint8_t dev_rfid_get_view_sector(void);

esp_err_t RFID_read_sector(
    uint8_t sector,
    RFID_key_type_e key_type,
    const uint8_t key[CPN_RFID_KEY_LEN],
    const RFID_uid_s* uid,
    RFID_sector_data_s* out_sector
);
esp_err_t RFID_write_block(
    uint8_t block_addr,
    RFID_key_type_e key_type,
    const uint8_t key[CPN_RFID_KEY_LEN],
    const RFID_uid_s* uid,
    const uint8_t in_block[CPN_RFID_BLOCK_LEN]
);
esp_err_t RFID_write_sector(
    uint8_t sector,
    RFID_key_type_e key_type,
    const uint8_t key[CPN_RFID_KEY_LEN],
    const RFID_uid_s* uid,
    const RFID_sector_data_s* in_sector,
    bool write_trailer_block
);

esp_err_t RFID_encrypt_buffer(
    const uint8_t* plain_data,
    size_t data_len,
    const uint8_t* crypt_key,
    size_t key_len,
    uint8_t* cipher_data
);
esp_err_t RFID_decrypt_buffer(
    const uint8_t* cipher_data,
    size_t data_len,
    const uint8_t* crypt_key,
    size_t key_len,
    uint8_t* plain_data
);

#ifdef __cplusplus
}
#endif
