#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "nvs.h"
#include "nvs_health.h"
#include "nvs_schema.h"
#include "settings_pin_store.h"

#define TAG "settings_pin"
#define SETTINGS_PIN_STORE_VERSION 1U

/* This component owns this data, so the namespace is declared here. */
NVS_SCHEMA_DECLARE_NS(NVS_NS, "sys_pin");
static const nvs_key_descriptor_t NVS_KEY_PIN = {
    .ns = NVS_NS,
    .key = "pin_v1",
};
static const nvs_key_descriptor_t NVS_KEY_FAILS = {
    .ns = NVS_NS,
    .key = "fails",
};

typedef struct {
    uint32_t version;
    uint32_t flags;
    uint32_t iterations;
    uint8_t salt[SETTINGS_PIN_SALT_LEN];
    uint8_t hash[SETTINGS_PIN_HASH_LEN];
} settings_pin_disk_t;

static void settings_pin_store_wipe(void *buf, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)buf;
    while (len-- > 0) {
        *p++ = 0;
    }
}

esp_err_t settings_pin_store_load(settings_pin_store_config_t *config, settings_pin_store_state_t *state)
{
    nvs_handle_t handle = 0;
    settings_pin_disk_t disk = { 0 };
    size_t size = sizeof(disk);

    ESP_RETURN_ON_FALSE(config != NULL && state != NULL, ESP_ERR_INVALID_ARG, TAG, "null argument");
    *state = SETTINGS_PIN_STORE_OFF;
    memset(config, 0, sizeof(*config));

    esp_err_t err = nvs_open_descriptor(NVS_KEY_PIN.ns, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "open NVS failed");

    err = nvs_get_blob(handle, NVS_KEY_PIN.key, &disk, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH || (err == ESP_OK && (size != sizeof(disk) ||
                                                                disk.version != SETTINGS_PIN_STORE_VERSION ||
                                                                disk.iterations == 0))) {
        /* Locked, not open: a PIN we cannot read must not read as "no PIN". */
        nvs_health_report_invalid(&NVS_KEY_PIN, ESP_ERR_INVALID_VERSION, "invalid settings PIN blob");
        *state = SETTINGS_PIN_STORE_CORRUPT;
        settings_pin_store_wipe(&disk, sizeof(disk));
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "read settings PIN failed");

    memcpy(config->record.salt, disk.salt, sizeof(disk.salt));
    memcpy(config->record.hash, disk.hash, sizeof(disk.hash));
    config->record.iterations = disk.iterations;
    config->flags = disk.flags;
    settings_pin_store_wipe(&disk, sizeof(disk));
    *state = SETTINGS_PIN_STORE_ON;
    return ESP_OK;
}

esp_err_t settings_pin_store_save(const settings_pin_store_config_t *config)
{
    nvs_handle_t handle = 0;
    settings_pin_disk_t disk = {
        .version = SETTINGS_PIN_STORE_VERSION,
    };

    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    disk.flags = config->flags;
    disk.iterations = config->record.iterations;
    memcpy(disk.salt, config->record.salt, sizeof(disk.salt));
    memcpy(disk.hash, config->record.hash, sizeof(disk.hash));

    esp_err_t err = nvs_open_descriptor(NVS_KEY_PIN.ns, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, NVS_KEY_PIN.key, &disk, sizeof(disk));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    settings_pin_store_wipe(&disk, sizeof(disk));
    return err;
}

esp_err_t settings_pin_store_erase(void)
{
    nvs_handle_t handle = 0;

    esp_err_t err = nvs_open_descriptor(NVS_KEY_PIN.ns, NVS_READWRITE, &handle);
    ESP_RETURN_ON_ERROR(err, TAG, "open NVS failed");
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t settings_pin_store_load_failures(uint32_t *failures)
{
    nvs_handle_t handle = 0;

    ESP_RETURN_ON_FALSE(failures != NULL, ESP_ERR_INVALID_ARG, TAG, "failures is null");
    *failures = 0;
    esp_err_t err = nvs_open_descriptor(NVS_KEY_FAILS.ns, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "open NVS failed");
    err = nvs_get_u32(handle, NVS_KEY_FAILS.key, failures);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *failures = 0;
        return ESP_OK;
    }
    return err;
}

esp_err_t settings_pin_store_save_failures(uint32_t failures)
{
    nvs_handle_t handle = 0;

    ESP_RETURN_ON_ERROR(nvs_open_descriptor(NVS_KEY_FAILS.ns, NVS_READWRITE, &handle), TAG, "open NVS failed");
    esp_err_t err = nvs_set_u32(handle, NVS_KEY_FAILS.key, failures);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

void settings_pin_store_new_record(settings_pin_record_t *record)
{
    if (record == NULL) {
        return;
    }
    memset(record, 0, sizeof(*record));
    /* Per device and per PIN: the same PIN on two units hashes differently. */
    esp_fill_random(record->salt, sizeof(record->salt));
    record->iterations = SETTINGS_PIN_STORE_ITERATIONS;
}

bool settings_pin_store_hash(const char *pin,
                             size_t pin_len,
                             const uint8_t *salt,
                             size_t salt_len,
                             uint32_t iterations,
                             uint8_t out[SETTINGS_PIN_HASH_LEN])
{
    if (pin == NULL || salt == NULL || out == NULL || iterations == 0) {
        return false;
    }
    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const unsigned char *)pin, pin_len, salt, salt_len,
                                            iterations, SETTINGS_PIN_HASH_LEN, out);
    if (ret != 0) {
        ESP_LOGE(TAG, "PIN hash failed: -0x%04x", (unsigned)-ret);
        return false;
    }
    return true;
}
