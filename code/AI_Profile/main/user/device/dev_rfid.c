#include "dev_rfid.h"

#include <esp_heap_caps.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DEV_RFID_DEFAULT_VIEW_SECTOR (3U)
#define DEV_RFID_CARD_MISS_CONFIRM_COUNT (3U)
#define DEV_RFID_ACCESS_ERR_SILENT_COUNT (2U)

static RFID_ctx_s s_RFID;
static RFID_uid_s s_last_uid_cache;
static bool s_last_uid_cache_valid;
static bool s_fm11_mode_active;

typedef struct {
    bool card_present;
    uint8_t reader_version;
    uint8_t last_uid_len;
    uint8_t card_miss_count;
    uint8_t access_err_count;
    uint8_t selected_sector;
    bool selected_sector_dirty;
    uint8_t last_uid[CPN_RFID_UID_MAX_LEN];
    bool dump_need_scroll_top;
    char status_text[DEV_RFID_STATUS_TEXT_LEN];
    char uid_text[DEV_RFID_UID_TEXT_LEN];
    char* dump_text;
} dev_rfid_view_state_s;

static dev_rfid_view_state_s s_rfid_view;
static char* s_rfid_view_dump_text;

/*
 * brief : _dev_rfid_alloc_buf.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static void* _dev_rfid_alloc_buf(size_t size)
{
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

/*
 * brief : _dev_rfid_bind_dump_text.
 * input : none.
 * output: return value from this function.
 * type  : private
 */
static bool _dev_rfid_bind_dump_text(void)
{
    if (s_rfid_view_dump_text == NULL) {
        s_rfid_view_dump_text = (char*)_dev_rfid_alloc_buf(DEV_RFID_DUMP_TEXT_LEN);
        if (s_rfid_view_dump_text == NULL) {
            return false;
        }
        memset(s_rfid_view_dump_text, 0, DEV_RFID_DUMP_TEXT_LEN);
    }

    s_rfid_view.dump_text = s_rfid_view_dump_text;
    return true;
}

/*
 * brief : _dev_rfid_set_status_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _dev_rfid_set_status_text(const char* fmt, ...)
{
    va_list args;

    if (fmt == NULL) {
        return;
    }

    va_start(args, fmt);
    (void)vsnprintf(s_rfid_view.status_text, sizeof(s_rfid_view.status_text), fmt, args);
    va_end(args);
}

/*
 * brief : _dev_rfid_set_uid_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _dev_rfid_set_uid_text(const char* fmt, ...)
{
    va_list args;

    if (fmt == NULL) {
        return;
    }

    va_start(args, fmt);
    (void)vsnprintf(s_rfid_view.uid_text, sizeof(s_rfid_view.uid_text), fmt, args);
    va_end(args);
}

/*
 * brief : _dev_rfid_clamp_sector.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static uint8_t _dev_rfid_clamp_sector(uint8_t sector)
{
    if (sector > DEV_RFID_VIEW_SECTOR_MAX) {
        return DEV_RFID_VIEW_SECTOR_MAX;
    }
    return sector;
}

/*
 * brief : _dev_rfid_describe_card_type.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _dev_rfid_describe_card_type(uint8_t sak, const char** out_type, uint16_t* out_capacity)
{
    if (out_type == NULL) {
        return;
    }

    *out_type = "Unknown";
    if (out_capacity != NULL) {
        *out_capacity = 0U;
    }

    switch (sak) {
    case 0x08U:
        *out_type = "Mifare-1K";
        if (out_capacity != NULL) {
            *out_capacity = 1024U;
        }
        break;
    case 0x18U:
        *out_type = "Mifare-4K";
        if (out_capacity != NULL) {
            *out_capacity = 4096U;
        }
        break;
    case 0x09U:
        *out_type = "Mifare-Mini";
        if (out_capacity != NULL) {
            *out_capacity = 320U;
        }
        break;
    case 0x00U:
        *out_type = "Mifare-Ultralight";
        if (out_capacity != NULL) {
            *out_capacity = 64U;
        }
        break;
    default:
        break;
    }
}

/*
 * brief : _dev_rfid_clear_dump_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _dev_rfid_clear_dump_text(void)
{
    uint8_t selected_sector = _dev_rfid_clamp_sector(s_rfid_view.selected_sector);
    if (!_dev_rfid_bind_dump_text()) {
        return;
    }

    (void)snprintf(
        s_rfid_view.dump_text,
        DEV_RFID_DUMP_TEXT_LEN,
        "Place card near antenna...\n"
        "UP click: sector +1, DOWN click: sector -1.\n"
        "Current sector: S%02u",
        (unsigned)selected_sector
    );
    s_rfid_view.dump_need_scroll_top = true;
}

/*
 * brief : _dev_rfid_append_dump_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _dev_rfid_append_dump_text(const char* fmt, ...)
{
    char line[192] = { 0 };
    va_list args;
    size_t used_len = 0U;

    if (fmt == NULL) {
        return;
    }
    if (!_dev_rfid_bind_dump_text()) {
        return;
    }

    va_start(args, fmt);
    (void)vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    used_len = strlen(s_rfid_view.dump_text);
    if (used_len < (DEV_RFID_DUMP_TEXT_LEN - 1U)) {
        strncat(s_rfid_view.dump_text, line, DEV_RFID_DUMP_TEXT_LEN - used_len - 1U);
    }
}

/*
 * brief : _dev_rfid_uid_equal.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static bool _dev_rfid_uid_equal(const RFID_uid_s* uid)
{
    if ((uid == NULL) || !uid->uid_valid) {
        return false;
    }

    if (s_rfid_view.last_uid_len != uid->uid_len) {
        return false;
    }

    return memcmp(s_rfid_view.last_uid, uid->uid, uid->uid_len) == 0;
}

/*
 * brief : _dev_rfid_cache_uid_for_view.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _dev_rfid_cache_uid_for_view(const RFID_uid_s* uid)
{
    if (uid == NULL) {
        return;
    }

    s_rfid_view.last_uid_len = uid->uid_len;
    if (s_rfid_view.last_uid_len > CPN_RFID_UID_MAX_LEN) {
        s_rfid_view.last_uid_len = CPN_RFID_UID_MAX_LEN;
    }

    memset(s_rfid_view.last_uid, 0, sizeof(s_rfid_view.last_uid));
    memcpy(s_rfid_view.last_uid, uid->uid, s_rfid_view.last_uid_len);
}

/*
 * brief : _dev_rfid_format_uid_text.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _dev_rfid_format_uid_text(const RFID_uid_s* uid)
{
    char uid_hex[48] = { 0 };
    size_t used_len = 0U;
    uint8_t i = 0U;
    int print_len = 0;
    const char* card_type = "Unknown";
    uint16_t capacity = 0U;

    if (uid == NULL) {
        return;
    }

    if (!uid->uid_valid) {
        _dev_rfid_set_uid_text(
            "UID: n/a\nType: Unknown\nATQA: %02X%02X  Anti-collision: %s",
            uid->atqa[0],
            uid->atqa[1],
            uid->anticollision_supported ? "on" : "off"
        );
        return;
    }

    for (i = 0U; i < uid->uid_len; i++) {
        print_len = snprintf(
            uid_hex + used_len,
            sizeof(uid_hex) - used_len,
            (i == 0U) ? "%02X" : " %02X",
            uid->uid[i]
        );
        if (print_len <= 0) {
            break;
        }
        if ((size_t)print_len >= (sizeof(uid_hex) - used_len)) {
            used_len = sizeof(uid_hex) - 1U;
            break;
        }
        used_len += (size_t)print_len;
    }

    _dev_rfid_describe_card_type(uid->sak, &card_type, &capacity);

    if (capacity > 0U) {
        _dev_rfid_set_uid_text(
            "UID: %s\nType: %s\nSAK: 0x%02X  Capacity: %u Byte",
            uid_hex,
            card_type,
            uid->sak,
            (unsigned)capacity
        );
    } else {
        _dev_rfid_set_uid_text(
            "UID: %s\nType: %s\nSAK: 0x%02X  Capacity: n/a",
            uid_hex,
            card_type,
            uid->sak
        );
    }
}

/*
 * brief : _dev_rfid_dump_selected_sector.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _dev_rfid_dump_selected_sector(const RFID_uid_s* uid, uint8_t sector)
{
    uint8_t selected_sector = DEV_RFID_VIEW_SECTOR_MIN;
    RFID_sector_data_s sector_data = { 0 };
    esp_err_t ret = ESP_FAIL;
    uint8_t first_block = 0U;
    uint8_t block_count = 0U;
    uint8_t last_block = 0U;
    uint8_t i = 0U;
    uint8_t block_index = 0U;
    uint8_t block_bytes = 0U;
    uint8_t j = 0U;

    if (uid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!_dev_rfid_bind_dump_text()) {
        return ESP_ERR_NO_MEM;
    }

    selected_sector = _dev_rfid_clamp_sector(sector);
    ret = RFID_read_sector(selected_sector, CPN_RFID_KEY_A, NULL, uid, &sector_data);

    s_rfid_view.dump_text[0] = '\0';
    s_rfid_view.dump_need_scroll_top = true;

    if (ret != ESP_OK) {
        _dev_rfid_append_dump_text("Sector: %02u\n", (unsigned)selected_sector);
        _dev_rfid_append_dump_text("Result: read/auth fail (%d)\n", (int)ret);
        _dev_rfid_append_dump_text("Hint: check key type or card quality.");
        return ret;
    }

    first_block = sector_data.first_block;
    block_count = sector_data.block_count;
    if (block_count == 0U) {
        block_count = 1U;
    }
    last_block = (uint8_t)(first_block + block_count - 1U);

    _dev_rfid_append_dump_text(
        "Sector: %02u   Blocks: %u-%u   Key: Key A\n",
        (unsigned)selected_sector,
        (unsigned)first_block,
        (unsigned)last_block
    );
    _dev_rfid_append_dump_text("----------------------------------------\n");

    for (i = 0U; i < block_count; i++) {
        block_index = (uint8_t)(sector_data.first_block + i);
        block_bytes = sector_data.block_bytes;
        if ((block_bytes == 0U) || (block_bytes > CPN_RFID_BLOCK_LEN)) {
            block_bytes = CPN_RFID_BLOCK_LEN;
        }

        _dev_rfid_append_dump_text("Block%03u: ", (unsigned)block_index);
        for (j = 0U; j < block_bytes; j++) {
            _dev_rfid_append_dump_text("%02X", sector_data.block_data[i][j]);
            if ((j + 1U) < block_bytes) {
                _dev_rfid_append_dump_text(" ");
            }
        }

        if ((block_count > 1U) && ((i + 1U) == block_count)) {
            _dev_rfid_append_dump_text("  [Trailer Block]");
        }
        _dev_rfid_append_dump_text("\n");
    }

    _dev_rfid_append_dump_text("Result: OK");
    if (block_count > 1U) {
        _dev_rfid_append_dump_text("\nWarn: last block is key trailer.");
    }
    return ESP_OK;
}

/*
 * brief : _RFID_cache_uid.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _RFID_cache_uid(const RFID_uid_s* uid)
{
    if ((uid == NULL) || (!uid->uid_valid) || (uid->uid_len < 4U)) {
        return;
    }

    s_last_uid_cache = *uid;
    s_last_uid_cache_valid = true;
}

/*
 * brief : _RFID_build_fm11_auth_key.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _RFID_build_fm11_auth_key(
    const uint8_t key_in[CPN_RFID_KEY_LEN],
    uint8_t key_out[CPN_RFID_KEY_LEN]
)
{
    if (key_out == NULL) {
        return;
    }

    if (key_in != NULL) {
        key_out[0] = key_in[0];
        key_out[1] = key_in[1];
        key_out[2] = key_in[2];
        key_out[3] = key_in[3];
    } else {
        key_out[0] = 0xFFU;
        key_out[1] = 0xFFU;
        key_out[2] = 0xFFU;
        key_out[3] = 0xFFU;
    }

    /* FM11RF005M auth key format is 4-byte key + 0x00 0x00. */
    key_out[4] = 0x00U;
    key_out[5] = 0x00U;
}

/*
 * brief : _RFID_map_key_type.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _RFID_map_key_type(RFID_key_type_e key_type, mfrc522_key_type_t* out_key_type)
{
    if (out_key_type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (key_type == CPN_RFID_KEY_A) {
        *out_key_type = MFRC522_KEY_A;
        return ESP_OK;
    }
    if (key_type == CPN_RFID_KEY_B) {
        *out_key_type = MFRC522_KEY_B;
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

/*
 * brief : _RFID_get_sector_layout.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _RFID_get_sector_layout(uint8_t sector, uint8_t* first_block, uint8_t* block_count)
{
    if ((first_block == NULL) || (block_count == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (sector < 32U) {
        *first_block = (uint8_t)(sector * 4U);
        *block_count = 4U;
        return ESP_OK;
    }

    if (sector < 40U) {
        *first_block = (uint8_t)(128U + ((sector - 32U) * 16U));
        *block_count = 16U;
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

/*
 * brief : _RFID_import_uid.
 * input : see parameters.
 * output: none.
 * type  : private
 */
static void _RFID_import_uid(const mfrc522_uid_t* in_uid, RFID_uid_s* out_uid)
{
    if ((in_uid == NULL) || (out_uid == NULL)) {
        return;
    }

    memset(out_uid, 0, sizeof(*out_uid));
    out_uid->atqa[0] = in_uid->atqa[0];
    out_uid->atqa[1] = in_uid->atqa[1];
    out_uid->sak = in_uid->sak;
    out_uid->uid_len = in_uid->size;
    if (out_uid->uid_len > CPN_RFID_UID_MAX_LEN) {
        out_uid->uid_len = CPN_RFID_UID_MAX_LEN;
    }
    memcpy(out_uid->uid, in_uid->uid, out_uid->uid_len);
    out_uid->uid_valid = (out_uid->uid_len > 0U);
    out_uid->anticollision_supported = true;
}

/*
 * brief : _RFID_export_uid.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _RFID_export_uid(const RFID_uid_s* in_uid, mfrc522_uid_t* out_uid)
{
    if ((in_uid == NULL) || (out_uid == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!in_uid->uid_valid || (in_uid->uid_len < 4U)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    memset(out_uid, 0, sizeof(*out_uid));
    out_uid->atqa[0] = in_uid->atqa[0];
    out_uid->atqa[1] = in_uid->atqa[1];
    out_uid->sak = in_uid->sak;
    out_uid->size = in_uid->uid_len;
    if (out_uid->size > MFRC522_UID_MAX_LEN) {
        out_uid->size = MFRC522_UID_MAX_LEN;
    }
    memcpy(out_uid->uid, in_uid->uid, out_uid->size);

    return ESP_OK;
}

/*
 * brief : _RFID_authenticate_fm11.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _RFID_authenticate_fm11(
    RFID_key_type_e key_type,
    const uint8_t key[CPN_RFID_KEY_LEN],
    const RFID_uid_s* uid
)
{
    mfrc522_key_type_t dev_key_type;
    esp_err_t ret = _RFID_map_key_type(key_type, &dev_key_type);
    if (ret != ESP_OK) {
        return ret;
    }

    mfrc522_uid_t dev_uid = { 0 };
    ret = _RFID_export_uid(uid, &dev_uid);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t auth_key[CPN_RFID_KEY_LEN] = { 0 };
    _RFID_build_fm11_auth_key(key, auth_key);

    /* FM11RF005M requires AUTH address to be 0. */
    return mfrc522_authenticate(dev_key_type, 0U, auth_key, &dev_uid);
}

/*
 * brief : _RFID_try_probe_fm11_uid.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _RFID_try_probe_fm11_uid(RFID_uid_s* out_uid, const uint8_t atqa[2])
{
    uint8_t block0[CPN_RFID_FM11_BLOCK_LEN] = { 0 };
    uint8_t block1[CPN_RFID_FM11_BLOCK_LEN] = { 0 };
    esp_err_t ret = ESP_FAIL;

    if ((out_uid == NULL) || (atqa == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = mfrc522_read_block4(0U, block0);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = mfrc522_read_block4(1U, block1);
    if (ret != ESP_OK) {
        return ret;
    }

    memset(out_uid, 0, sizeof(*out_uid));
    out_uid->atqa[0] = atqa[0];
    out_uid->atqa[1] = atqa[1];
    out_uid->uid_len = CPN_RFID_FM11_BLOCK_LEN;
    memcpy(out_uid->uid, block1, CPN_RFID_FM11_BLOCK_LEN);
    out_uid->uid_valid = true;
    out_uid->anticollision_supported = false;

    return ESP_OK;
}

/*
 * brief : _RFID_access_without_anticollision.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _RFID_access_without_anticollision(RFID_uid_s* out_uid)
{
    uint8_t atqa[2] = { 0 };
    esp_err_t ret = ESP_FAIL;
    mfrc522_uid_t known_uid = { 0 };
    esp_err_t export_ret = ESP_FAIL;
    esp_err_t select_ret = ESP_FAIL;
    esp_err_t fm11_ret = ESP_FAIL;

    if (out_uid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = mfrc522_request_type_a(atqa);
    if (ret == ESP_ERR_NOT_FOUND) {
        ret = mfrc522_wakeup_type_a(atqa);
    }
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_last_uid_cache_valid) {
        export_ret = _RFID_export_uid(&s_last_uid_cache, &known_uid);
        if (export_ret == ESP_OK) {
            select_ret = mfrc522_select_uid(&known_uid);
            if (select_ret == ESP_OK) {
                *out_uid = s_last_uid_cache;
                out_uid->atqa[0] = atqa[0];
                out_uid->atqa[1] = atqa[1];
                out_uid->anticollision_supported = false;
                s_fm11_mode_active = false;
                return ESP_OK;
            }
        }
    }

    fm11_ret = _RFID_try_probe_fm11_uid(out_uid, atqa);
    if (fm11_ret == ESP_OK) {
        s_fm11_mode_active = true;
        _RFID_cache_uid(out_uid);
        return ESP_OK;
    }

    memset(out_uid, 0, sizeof(*out_uid));
    out_uid->atqa[0] = atqa[0];
    out_uid->atqa[1] = atqa[1];
    out_uid->uid_valid = false;
    out_uid->anticollision_supported = false;
    s_fm11_mode_active = false;

    return ESP_OK;
}

/*
 * brief : _RFID_transform.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _RFID_transform(
    const uint8_t* src_data,
    size_t data_len,
    const uint8_t* crypt_key,
    size_t key_len,
    uint8_t* dst_data
)
{
    size_t i = 0U;
    uint8_t salt = 0U;

    if (((src_data == NULL) && (data_len > 0U)) || ((dst_data == NULL) && (data_len > 0U))
        || ((crypt_key == NULL) && (data_len > 0U)) || ((data_len > 0U) && (key_len == 0U))) {
        return ESP_ERR_INVALID_ARG;
    }

    for (i = 0U; i < data_len; i++) {
        salt = (uint8_t)(0x5AU ^ (uint8_t)(i * 17U));
        dst_data[i] = (uint8_t)(src_data[i] ^ crypt_key[i % key_len] ^ salt);
    }

    return ESP_OK;
}

/*
 * brief : RFID_init.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t RFID_init(void)
{
    esp_err_t ret = ESP_FAIL;
    uint8_t version = 0U;

    if (RFID_is_ready()) {
        return ESP_OK;
    }

    ret = mfrc522_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = mfrc522_get_version(&version);
    if (ret != ESP_OK) {
        (void)mfrc522_deinit();
        return ret;
    }

    if ((version == 0x00U) || (version == 0xFFU)) {
        (void)mfrc522_deinit();
        return ESP_FAIL;
    }

    s_RFID.initialized = true;
    s_RFID.reader_version = version;
    s_last_uid_cache_valid = false;
    s_fm11_mode_active = false;
    memset(&s_last_uid_cache, 0, sizeof(s_last_uid_cache));

    memset(&s_rfid_view, 0, sizeof(s_rfid_view));
    if (!_dev_rfid_bind_dump_text()) {
        (void)mfrc522_deinit();
        s_RFID.initialized = false;
        s_RFID.reader_version = 0U;
        return ESP_ERR_NO_MEM;
    }

    s_rfid_view.reader_version = version;
    s_rfid_view.selected_sector = DEV_RFID_DEFAULT_VIEW_SECTOR;
    s_rfid_view.selected_sector_dirty = true;
    _dev_rfid_set_status_text("Reader ready, version=0x%02X", version);
    _dev_rfid_set_uid_text("UID: --");
    _dev_rfid_clear_dump_text();

    return ESP_OK;
}

/*
 * brief : RFID_deinit.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t RFID_deinit(void)
{
    esp_err_t ret = mfrc522_deinit();
    s_RFID.initialized = false;
    s_RFID.reader_version = 0U;
    s_last_uid_cache_valid = false;
    s_fm11_mode_active = false;
    memset(&s_last_uid_cache, 0, sizeof(s_last_uid_cache));

    memset(&s_rfid_view, 0, sizeof(s_rfid_view));
    (void)_dev_rfid_bind_dump_text();
    s_rfid_view.selected_sector = DEV_RFID_DEFAULT_VIEW_SECTOR;
    s_rfid_view.selected_sector_dirty = true;
    _dev_rfid_set_status_text("Reader not ready");
    _dev_rfid_set_uid_text("UID: --");
    _dev_rfid_clear_dump_text();

    return ret;
}

/*
 * brief : RFID_is_ready.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
bool RFID_is_ready(void)
{
    return s_RFID.initialized && mfrc522_is_ready();
}

/*
 * brief : RFID_get_reader_version.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t RFID_get_reader_version(uint8_t* version)
{
    uint8_t current_version = 0U;
    esp_err_t ret = ESP_FAIL;

    if (version == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!RFID_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = mfrc522_get_version(&current_version);
    if (ret != ESP_OK) {
        return ret;
    }
    if ((current_version != 0x00U) && (current_version != 0xFFU)) {
        s_RFID.reader_version = current_version;
        s_rfid_view.reader_version = current_version;
    }

    *version = s_RFID.reader_version;
    return ESP_OK;
}

/*
 * brief : RFID_access.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t RFID_access(RFID_access_s* access_info)
{
    bool card_present = false;
    esp_err_t ret = ESP_FAIL;
    mfrc522_uid_t raw_uid = { 0 };
    esp_err_t fallback_ret = ESP_FAIL;

    if (access_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!RFID_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(access_info, 0, sizeof(*access_info));
    access_info->reader_version = s_RFID.reader_version;

    ret = mfrc522_poll_card(&card_present);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!card_present) {
        return ESP_OK;
    }

    access_info->card_present = true;

    ret = mfrc522_read_uid(&raw_uid);
    if (ret == ESP_OK) {
        s_fm11_mode_active = false;
        _RFID_import_uid(&raw_uid, &access_info->uid);
        _RFID_cache_uid(&access_info->uid);
        return ESP_OK;
    }

    if (ret == ESP_ERR_NOT_FOUND) {
        access_info->card_present = false;
        return ESP_OK;
    }

    if ((ret == ESP_ERR_TIMEOUT) || (ret == ESP_ERR_INVALID_RESPONSE) || (ret == ESP_FAIL)) {
        fallback_ret = _RFID_access_without_anticollision(&access_info->uid);
        if (fallback_ret == ESP_OK) {
            return ESP_OK;
        }
    }

    return ret;
}

/*
 * brief : _dev_rfid_refresh_snapshot.
 * input : see parameters.
 * output: return value from this function.
 * type  : private
 */
static esp_err_t _dev_rfid_refresh_snapshot(void)
{
    RFID_access_s access_info = { 0 };
    esp_err_t ret = RFID_access(&access_info);
    uint8_t selected_sector = _dev_rfid_clamp_sector(s_rfid_view.selected_sector);
    bool uid_changed = false;
    bool sector_changed = false;

    if (!_dev_rfid_bind_dump_text()) {
        return ESP_ERR_NO_MEM;
    }

    if (ret == ESP_ERR_INVALID_STATE) {
        s_rfid_view.card_present = false;
        s_rfid_view.last_uid_len = 0U;
        s_rfid_view.card_miss_count = 0U;
        s_rfid_view.access_err_count = 0U;
        s_rfid_view.selected_sector_dirty = true;
        memset(s_rfid_view.last_uid, 0, sizeof(s_rfid_view.last_uid));

        _dev_rfid_set_status_text("Reader state error, reinit...");
        _dev_rfid_set_uid_text("UID: --");
        _dev_rfid_clear_dump_text();
        return ret;
    }

    if (ret != ESP_OK) {
        if (s_rfid_view.access_err_count < 0xFFU) {
            s_rfid_view.access_err_count++;
        }

        if (s_rfid_view.card_present
            && (s_rfid_view.access_err_count <= DEV_RFID_ACCESS_ERR_SILENT_COUNT)) {
            return ret;
        }

        _dev_rfid_set_status_text("Card access transient err: %d", (int)ret);
        return ret;
    }

    s_rfid_view.access_err_count = 0U;
    s_rfid_view.reader_version = access_info.reader_version;

    if (!access_info.card_present) {
        if (s_rfid_view.card_present) {
            if (s_rfid_view.card_miss_count < 0xFFU) {
                s_rfid_view.card_miss_count++;
            }
            if (s_rfid_view.card_miss_count < DEV_RFID_CARD_MISS_CONFIRM_COUNT) {
                return ESP_OK;
            }

            s_rfid_view.card_present = false;
            s_rfid_view.last_uid_len = 0U;
            s_rfid_view.card_miss_count = 0U;
            memset(s_rfid_view.last_uid, 0, sizeof(s_rfid_view.last_uid));

            _dev_rfid_set_status_text("No card, waiting...");
            _dev_rfid_set_uid_text("UID: --");
            _dev_rfid_clear_dump_text();
        } else {
            s_rfid_view.card_miss_count = 0U;
            if (s_rfid_view.selected_sector_dirty) {
                _dev_rfid_set_status_text("No card, waiting...");
                _dev_rfid_clear_dump_text();
            }
        }

        s_rfid_view.selected_sector_dirty = false;
        return ESP_OK;
    }

    s_rfid_view.card_miss_count = 0U;

    if (!access_info.uid.uid_valid) {
        if (s_rfid_view.card_present && (s_rfid_view.last_uid_len == 0U)
            && !s_rfid_view.selected_sector_dirty) {
            return ESP_OK;
        }

        s_rfid_view.card_present = true;
        s_rfid_view.last_uid_len = 0U;
        memset(s_rfid_view.last_uid, 0, sizeof(s_rfid_view.last_uid));

        _dev_rfid_set_status_text("Card detected, anti-collision unsupported");
        _dev_rfid_format_uid_text(&access_info.uid);

        s_rfid_view.dump_text[0] = '\0';
        _dev_rfid_append_dump_text("Tag responds, but UID read is unavailable.\n");
        _dev_rfid_append_dump_text(
            "ATQA=%02X%02X\n", access_info.uid.atqa[0], access_info.uid.atqa[1]
        );
        _dev_rfid_append_dump_text(
            "Selected sector S%02u requires UID and was skipped.",
            (unsigned)selected_sector
        );
        s_rfid_view.dump_need_scroll_top = true;
        s_rfid_view.selected_sector_dirty = false;
        return ESP_OK;
    }

    uid_changed = !s_rfid_view.card_present || !_dev_rfid_uid_equal(&access_info.uid);
    sector_changed = s_rfid_view.selected_sector_dirty;
    if (!uid_changed && !sector_changed) {
        return ESP_OK;
    }

    s_rfid_view.card_present = true;
    _dev_rfid_cache_uid_for_view(&access_info.uid);
    _dev_rfid_format_uid_text(&access_info.uid);
    _dev_rfid_set_status_text("Card detected, reading S%02u...", (unsigned)selected_sector);

    ret = _dev_rfid_dump_selected_sector(&access_info.uid, selected_sector);
    s_rfid_view.selected_sector_dirty = false;
    if (ret == ESP_OK) {
        _dev_rfid_set_status_text(
            "Card read OK, S%02u, version=0x%02X",
            (unsigned)selected_sector,
            s_rfid_view.reader_version
        );
    } else {
        _dev_rfid_set_status_text("Card read fail, S%02u, err=%d", (unsigned)selected_sector, (int)ret);
    }

    return ret;
}

/*
 * brief : dev_rfid_get_snapshot.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t dev_rfid_get_snapshot(dev_rfid_snapshot_s* out_snapshot)
{
    esp_err_t ret = ESP_OK;

    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!_dev_rfid_bind_dump_text()) {
        memset(out_snapshot, 0, sizeof(*out_snapshot));
        out_snapshot->reader_ready = false;
        out_snapshot->card_present = false;
        out_snapshot->reader_version = s_RFID.reader_version;
        (void)snprintf(
            out_snapshot->status_text,
            sizeof(out_snapshot->status_text),
            "Reader not ready"
        );
        (void)snprintf(out_snapshot->uid_text, sizeof(out_snapshot->uid_text), "UID: --");
        (void)snprintf(
            out_snapshot->dump_text,
            sizeof(out_snapshot->dump_text),
            "RFID buffer alloc fail"
        );
        return ESP_ERR_NO_MEM;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));

    if (!RFID_is_ready()) {
        if (s_rfid_view.status_text[0] == '\0') {
            _dev_rfid_set_status_text("Reader not ready");
        }
        if (s_rfid_view.uid_text[0] == '\0') {
            _dev_rfid_set_uid_text("UID: --");
        }
        if (s_rfid_view.dump_text[0] == '\0') {
            _dev_rfid_clear_dump_text();
        }

        out_snapshot->reader_ready = false;
        out_snapshot->card_present = false;
        out_snapshot->reader_version = s_RFID.reader_version;
        out_snapshot->dump_need_scroll_top = s_rfid_view.dump_need_scroll_top;

        (void)snprintf(
            out_snapshot->status_text,
            sizeof(out_snapshot->status_text),
            "%s",
            s_rfid_view.status_text
        );
        (void)snprintf(
            out_snapshot->uid_text,
            sizeof(out_snapshot->uid_text),
            "%s",
            s_rfid_view.uid_text
        );
        (void)snprintf(
            out_snapshot->dump_text,
            sizeof(out_snapshot->dump_text),
            "%s",
            s_rfid_view.dump_text
        );

        s_rfid_view.dump_need_scroll_top = false;
        return ESP_ERR_INVALID_STATE;
    }

    ret = _dev_rfid_refresh_snapshot();

    out_snapshot->reader_ready = (ret != ESP_ERR_INVALID_STATE);
    out_snapshot->card_present = s_rfid_view.card_present;
    out_snapshot->reader_version = s_rfid_view.reader_version;
    out_snapshot->dump_need_scroll_top = s_rfid_view.dump_need_scroll_top;

    (void)snprintf(
        out_snapshot->status_text,
        sizeof(out_snapshot->status_text),
        "%s",
        s_rfid_view.status_text
    );
    (void)snprintf(
        out_snapshot->uid_text,
        sizeof(out_snapshot->uid_text),
        "%s",
        s_rfid_view.uid_text
    );
    (void)snprintf(
        out_snapshot->dump_text,
        sizeof(out_snapshot->dump_text),
        "%s",
        s_rfid_view.dump_text
    );

    s_rfid_view.dump_need_scroll_top = false;
    return ret;
}

/*
 * brief : dev_rfid_set_view_sector.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t dev_rfid_set_view_sector(uint8_t sector)
{
    if (sector > DEV_RFID_VIEW_SECTOR_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_rfid_view.selected_sector != sector) {
        s_rfid_view.selected_sector = sector;
        s_rfid_view.selected_sector_dirty = true;
    }

    return ESP_OK;
}

/*
 * brief : dev_rfid_get_view_sector.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
uint8_t dev_rfid_get_view_sector(void)
{
    return _dev_rfid_clamp_sector(s_rfid_view.selected_sector);
}

/*
 * brief : RFID_halt.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t RFID_halt(void)
{
    if (!RFID_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    return mfrc522_halt();
}

/*
 * brief : RFID_read_sector.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t RFID_read_sector(
    uint8_t sector,
    RFID_key_type_e key_type,
    const uint8_t key[CPN_RFID_KEY_LEN],
    const RFID_uid_s* uid,
    RFID_sector_data_s* out_sector
)
{
    uint8_t block_data4[CPN_RFID_FM11_BLOCK_LEN] = { 0 };
    esp_err_t ret = ESP_FAIL;
    mfrc522_key_type_t dev_key_type;
    mfrc522_uid_t dev_uid = { 0 };
    mfrc522_sector_data_t dev_sector = { 0 };
    uint8_t i = 0U;

    if ((uid == NULL) || (out_sector == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!RFID_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(out_sector, 0, sizeof(*out_sector));

    if (s_fm11_mode_active) {
        if (sector >= CPN_RFID_FM11_BLOCK_COUNT) {
            return ESP_ERR_INVALID_ARG;
        }

        out_sector->sector = sector;
        out_sector->first_block = sector;
        out_sector->block_count = 1U;
        out_sector->block_bytes = CPN_RFID_FM11_BLOCK_LEN;

        if (sector >= 8U) {
            ret = _RFID_authenticate_fm11(key_type, key, uid);
            if (ret != ESP_OK) {
                return ret;
            }

            ret = mfrc522_read_block4(sector, block_data4);
            mfrc522_stop_crypto();
            if (ret != ESP_OK) {
                return ret;
            }
        } else {
            ret = mfrc522_read_block4(sector, block_data4);
            if (ret != ESP_OK) {
                return ret;
            }
        }

        memcpy(out_sector->block_data[0], block_data4, CPN_RFID_FM11_BLOCK_LEN);
        return ESP_OK;
    }

    ret = _RFID_map_key_type(key_type, &dev_key_type);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _RFID_export_uid(uid, &dev_uid);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = mfrc522_read_sector(sector, dev_key_type, key, &dev_uid, &dev_sector);
    if (ret != ESP_OK) {
        return ret;
    }

    out_sector->sector = dev_sector.sector;
    out_sector->first_block = dev_sector.first_block;
    out_sector->block_count = dev_sector.block_count;
    out_sector->block_bytes = CPN_RFID_BLOCK_LEN;
    if (out_sector->block_count > CPN_RFID_SECTOR_MAX_BLOCKS) {
        out_sector->block_count = CPN_RFID_SECTOR_MAX_BLOCKS;
    }

    for (i = 0U; i < out_sector->block_count; i++) {
        memcpy(out_sector->block_data[i], dev_sector.block_data[i], CPN_RFID_BLOCK_LEN);
    }

    return ESP_OK;
}

/*
 * brief : RFID_write_block.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t RFID_write_block(
    uint8_t block_addr,
    RFID_key_type_e key_type,
    const uint8_t key[CPN_RFID_KEY_LEN],
    const RFID_uid_s* uid,
    const uint8_t in_block[CPN_RFID_BLOCK_LEN]
)
{
    esp_err_t ret = ESP_FAIL;
    mfrc522_key_type_t dev_key_type;
    mfrc522_uid_t dev_uid = { 0 };

    if ((uid == NULL) || (in_block == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!RFID_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_fm11_mode_active) {
        if (block_addr >= CPN_RFID_FM11_BLOCK_COUNT) {
            return ESP_ERR_INVALID_ARG;
        }
        if (block_addr < 2U) {
            return ESP_ERR_NOT_SUPPORTED;
        }

        ret = _RFID_authenticate_fm11(key_type, key, uid);
        if (ret != ESP_OK) {
            return ret;
        }

        ret = mfrc522_write_block4(block_addr, in_block);
        mfrc522_stop_crypto();
        return ret;
    }

    ret = _RFID_map_key_type(key_type, &dev_key_type);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _RFID_export_uid(uid, &dev_uid);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = mfrc522_authenticate(dev_key_type, block_addr, key, &dev_uid);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = mfrc522_write_block(block_addr, in_block);
    mfrc522_stop_crypto();
    return ret;
}

/*
 * brief : RFID_write_sector.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t RFID_write_sector(
    uint8_t sector,
    RFID_key_type_e key_type,
    const uint8_t key[CPN_RFID_KEY_LEN],
    const RFID_uid_s* uid,
    const RFID_sector_data_s* in_sector,
    bool write_trailer_block
)
{
    uint8_t first_block = 0U;
    uint8_t block_count = 0U;
    esp_err_t ret = ESP_FAIL;
    mfrc522_key_type_t dev_key_type;
    mfrc522_uid_t dev_uid = { 0 };
    uint8_t trailer_block = 0U;
    uint8_t write_count = 0U;
    uint8_t i = 0U;

    if ((uid == NULL) || (in_sector == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!RFID_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_fm11_mode_active) {
        if (sector >= CPN_RFID_FM11_BLOCK_COUNT) {
            return ESP_ERR_INVALID_ARG;
        }
        if ((in_sector->block_count == 0U) || (in_sector->block_bytes == 0U)) {
            return ESP_ERR_INVALID_ARG;
        }

        return RFID_write_block(sector, key_type, key, uid, in_sector->block_data[0]);
    }

    ret = _RFID_get_sector_layout(sector, &first_block, &block_count);
    if (ret != ESP_OK) {
        return ret;
    }
    if (in_sector->block_count < block_count) {
        return ESP_ERR_INVALID_SIZE;
    }

    ret = _RFID_map_key_type(key_type, &dev_key_type);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = _RFID_export_uid(uid, &dev_uid);
    if (ret != ESP_OK) {
        return ret;
    }

    trailer_block = (uint8_t)(first_block + block_count - 1U);
    ret = mfrc522_authenticate(dev_key_type, trailer_block, key, &dev_uid);
    if (ret != ESP_OK) {
        return ret;
    }

    write_count = block_count;
    if (!write_trailer_block && (write_count > 0U)) {
        write_count--;
    }

    for (i = 0U; i < write_count; i++) {
        ret = mfrc522_write_block((uint8_t)(first_block + i), in_sector->block_data[i]);
        if (ret != ESP_OK) {
            mfrc522_stop_crypto();
            return ret;
        }
    }

    mfrc522_stop_crypto();
    return ESP_OK;
}

/*
 * brief : RFID_encrypt_buffer.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t RFID_encrypt_buffer(
    const uint8_t* plain_data,
    size_t data_len,
    const uint8_t* crypt_key,
    size_t key_len,
    uint8_t* cipher_data
)
{
    return _RFID_transform(plain_data, data_len, crypt_key, key_len, cipher_data);
}

/*
 * brief : RFID_decrypt_buffer.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
esp_err_t RFID_decrypt_buffer(
    const uint8_t* cipher_data,
    size_t data_len,
    const uint8_t* crypt_key,
    size_t key_len,
    uint8_t* plain_data
)
{
    return _RFID_transform(cipher_data, data_len, crypt_key, key_len, plain_data);
}