#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "wifi_profile_store.h"

#define TAG "wifi_profile_store"
#define NVS_NAMESPACE "esp32_wifi_sta"
#define NVS_PROFILES_KEY "profiles_v1"
#define NVS_LEGACY_SSID_KEY "ssid"
#define NVS_LEGACY_PASSWORD_KEY "password"
#define WIFI_PROFILE_STORE_VERSION 1U

typedef struct {
    uint8_t authmode;
    uint8_t reserved[3];
    uint32_t last_success_seq;
    char ssid[33];
    char password[65];
} wifi_profile_store_entry_disk_t;

typedef struct {
    uint32_t version;
    uint32_t success_seq;
    wifi_profile_store_entry_disk_t entries[WIFI_PROFILE_STORE_MAX_ENTRIES];
} wifi_profile_store_blob_t;

static bool wifi_profile_store_entry_valid(const wifi_profile_store_entry_disk_t *entry)
{
    return entry != NULL && entry->ssid[0] != '\0';
}

static void wifi_profile_store_sort_entries(wifi_profile_store_entry_t *entries, size_t count)
{
    if (entries == NULL) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (entries[j].last_success_seq > entries[i].last_success_seq) {
                wifi_profile_store_entry_t tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
}

static esp_err_t wifi_profile_store_read_blob(wifi_profile_store_blob_t *blob)
{
    nvs_handle_t nvs_handle;
    size_t blob_size = sizeof(*blob);

    ESP_RETURN_ON_FALSE(blob != NULL, ESP_ERR_INVALID_ARG, TAG, "blob is null");
    memset(blob, 0, sizeof(*blob));

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(nvs_handle, NVS_PROFILES_KEY, blob, &blob_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    if (blob_size != sizeof(*blob) || blob->version != WIFI_PROFILE_STORE_VERSION) {
        ESP_LOGW(TAG, "invalid Wi-Fi profile blob");
        return ESP_ERR_INVALID_VERSION;
    }

    return ESP_OK;
}

static esp_err_t wifi_profile_store_load_legacy_entry(wifi_profile_store_entry_t *entry)
{
    nvs_handle_t nvs_handle;
    size_t ssid_len = sizeof(entry->ssid);
    size_t password_len = sizeof(entry->password);

    ESP_RETURN_ON_FALSE(entry != NULL, ESP_ERR_INVALID_ARG, TAG, "entry is null");
    memset(entry, 0, sizeof(*entry));

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(nvs_handle, NVS_LEGACY_SSID_KEY, entry->ssid, &ssid_len);
    if (err == ESP_OK) {
        esp_err_t password_err = nvs_get_str(nvs_handle, NVS_LEGACY_PASSWORD_KEY, entry->password, &password_len);
        if (password_err == ESP_ERR_NVS_NOT_FOUND) {
            entry->password[0] = '\0';
        } else if (password_err != ESP_OK) {
            err = password_err;
        }
    }
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        entry->authmode = WIFI_AUTH_OPEN;
        entry->last_success_seq = 1;
    }
    return err;
}

static esp_err_t wifi_profile_store_write_blob(const wifi_profile_store_blob_t *blob)
{
    nvs_handle_t nvs_handle;

    ESP_RETURN_ON_FALSE(blob != NULL, ESP_ERR_INVALID_ARG, TAG, "blob is null");
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle), TAG, "open NVS failed");

    esp_err_t err = nvs_set_blob(nvs_handle, NVS_PROFILES_KEY, blob, sizeof(*blob));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    return err;
}

static esp_err_t wifi_profile_store_prepare_blob_for_write(wifi_profile_store_blob_t *blob)
{
    ESP_RETURN_ON_FALSE(blob != NULL, ESP_ERR_INVALID_ARG, TAG, "blob is null");

    esp_err_t err = wifi_profile_store_read_blob(blob);
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH || err == ESP_ERR_INVALID_VERSION) {
        wifi_profile_store_entry_t legacy_entry = { 0 };
        memset(blob, 0, sizeof(*blob));
        blob->version = WIFI_PROFILE_STORE_VERSION;

        err = wifi_profile_store_load_legacy_entry(&legacy_entry);
        if (err == ESP_OK && legacy_entry.ssid[0] != '\0') {
            wifi_profile_store_entry_disk_t *dst = &blob->entries[0];
            dst->authmode = (uint8_t)legacy_entry.authmode;
            dst->last_success_seq = legacy_entry.last_success_seq;
            strlcpy(dst->ssid, legacy_entry.ssid, sizeof(dst->ssid));
            strlcpy(dst->password, legacy_entry.password, sizeof(dst->password));
            blob->success_seq = legacy_entry.last_success_seq;
            return ESP_OK;
        }
        if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_OK) {
            return ESP_OK;
        }
        return err;
    }

    return err;
}

esp_err_t wifi_profile_store_load_entries(wifi_profile_store_entry_t *entries,
                                          size_t entry_capacity,
                                          size_t *entry_count)
{
    wifi_profile_store_blob_t blob = { 0 };
    size_t count = 0;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(entry_count != NULL, ESP_ERR_INVALID_ARG, TAG, "entry_count is null");
    ESP_RETURN_ON_FALSE(entries != NULL || entry_capacity == 0, ESP_ERR_INVALID_ARG, TAG, "entries is null");
    *entry_count = 0;

    if (entries != NULL && entry_capacity > 0) {
        memset(entries, 0, entry_capacity * sizeof(entries[0]));
    }

    err = wifi_profile_store_read_blob(&blob);
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH || err == ESP_ERR_INVALID_VERSION) {
        wifi_profile_store_entry_t legacy_entry = { 0 };
        err = wifi_profile_store_load_legacy_entry(&legacy_entry);
        if (err == ESP_OK && legacy_entry.ssid[0] != '\0') {
            if (entries != NULL && entry_capacity > 0) {
                entries[0] = legacy_entry;
            }
            *entry_count = 1;
            return ESP_OK;
        }
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_OK;
        }
        return err == ESP_OK ? ESP_OK : err;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "read Wi-Fi profile blob failed");

    for (size_t i = 0; i < WIFI_PROFILE_STORE_MAX_ENTRIES; ++i) {
        const wifi_profile_store_entry_disk_t *src = &blob.entries[i];
        if (!wifi_profile_store_entry_valid(src)) {
            continue;
        }
        if (entries != NULL && count < entry_capacity) {
            wifi_profile_store_entry_t *dst = &entries[count];
            memset(dst, 0, sizeof(*dst));
            memcpy(dst->ssid, src->ssid, sizeof(dst->ssid));
            memcpy(dst->password, src->password, sizeof(dst->password));
            dst->authmode = (wifi_auth_mode_t)src->authmode;
            dst->last_success_seq = src->last_success_seq;
        }
        ++count;
    }

    if (entries != NULL && count > 1) {
        size_t sort_count = count < entry_capacity ? count : entry_capacity;
        wifi_profile_store_sort_entries(entries, sort_count);
    }
    *entry_count = count;
    return ESP_OK;
}

bool wifi_profile_store_has_profiles(void)
{
    size_t count = 0;
    return wifi_profile_store_load_entries(NULL, 0, &count) == ESP_OK && count > 0;
}

esp_err_t wifi_profile_store_record_success(const char *ssid,
                                            const char *password,
                                            wifi_auth_mode_t authmode)
{
    wifi_profile_store_blob_t blob = { 0 };
    esp_err_t err = wifi_profile_store_prepare_blob_for_write(&blob);
    size_t target_index = WIFI_PROFILE_STORE_MAX_ENTRIES;

    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "ssid is empty");
    ESP_RETURN_ON_FALSE(strlen(ssid) <= 32, ESP_ERR_INVALID_ARG, TAG, "ssid is too long");
    ESP_RETURN_ON_FALSE(password == NULL || strlen(password) <= 64, ESP_ERR_INVALID_ARG, TAG, "password is too long");

    ESP_RETURN_ON_ERROR(err, TAG, "prepare Wi-Fi profile blob failed");

    for (size_t i = 0; i < WIFI_PROFILE_STORE_MAX_ENTRIES; ++i) {
        if (strncmp(blob.entries[i].ssid, ssid, sizeof(blob.entries[i].ssid)) == 0) {
            target_index = i;
            break;
        }
        if (target_index == WIFI_PROFILE_STORE_MAX_ENTRIES &&
            !wifi_profile_store_entry_valid(&blob.entries[i])) {
            target_index = i;
        }
    }

    if (target_index == WIFI_PROFILE_STORE_MAX_ENTRIES) {
        uint32_t oldest_seq = UINT32_MAX;
        target_index = 0;
        for (size_t i = 0; i < WIFI_PROFILE_STORE_MAX_ENTRIES; ++i) {
            if (blob.entries[i].last_success_seq < oldest_seq) {
                oldest_seq = blob.entries[i].last_success_seq;
                target_index = i;
            }
        }
    }

    blob.version = WIFI_PROFILE_STORE_VERSION;
    blob.success_seq++;
    wifi_profile_store_entry_disk_t *dst = &blob.entries[target_index];
    memset(dst, 0, sizeof(*dst));
    dst->authmode = (uint8_t)authmode;
    dst->last_success_seq = blob.success_seq;
    strlcpy(dst->ssid, ssid, sizeof(dst->ssid));
    strlcpy(dst->password, password != NULL ? password : "", sizeof(dst->password));

    err = wifi_profile_store_write_blob(&blob);
    ESP_RETURN_ON_ERROR(err, TAG, "write Wi-Fi profile blob failed");
    ESP_LOGI(TAG, "saved Wi-Fi profile for SSID \"%s\"", ssid);
    return ESP_OK;
}

esp_err_t wifi_profile_store_set_priority(const char *ssid)
{
    wifi_profile_store_blob_t blob = { 0 };
    size_t target_index = WIFI_PROFILE_STORE_MAX_ENTRIES;

    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "ssid is empty");
    ESP_RETURN_ON_FALSE(strlen(ssid) <= 32, ESP_ERR_INVALID_ARG, TAG, "ssid is too long");
    ESP_RETURN_ON_ERROR(wifi_profile_store_prepare_blob_for_write(&blob), TAG, "prepare Wi-Fi profile blob failed");

    for (size_t i = 0; i < WIFI_PROFILE_STORE_MAX_ENTRIES; ++i) {
        if (strncmp(blob.entries[i].ssid, ssid, sizeof(blob.entries[i].ssid)) == 0) {
            target_index = i;
            break;
        }
    }

    ESP_RETURN_ON_FALSE(target_index != WIFI_PROFILE_STORE_MAX_ENTRIES, ESP_ERR_NOT_FOUND, TAG, "SSID not found");

    blob.version = WIFI_PROFILE_STORE_VERSION;
    blob.success_seq++;
    blob.entries[target_index].last_success_seq = blob.success_seq;

    esp_err_t err = wifi_profile_store_write_blob(&blob);
    ESP_RETURN_ON_ERROR(err, TAG, "write Wi-Fi profile blob failed");
    ESP_LOGI(TAG, "prioritized Wi-Fi profile for SSID \"%s\"", ssid);
    return ESP_OK;
}

esp_err_t wifi_profile_store_remove(const char *ssid)
{
    wifi_profile_store_blob_t blob = { 0 };
    size_t target_index = WIFI_PROFILE_STORE_MAX_ENTRIES;

    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "ssid is empty");
    ESP_RETURN_ON_FALSE(strlen(ssid) <= 32, ESP_ERR_INVALID_ARG, TAG, "ssid is too long");
    ESP_RETURN_ON_ERROR(wifi_profile_store_prepare_blob_for_write(&blob), TAG, "prepare Wi-Fi profile blob failed");

    for (size_t i = 0; i < WIFI_PROFILE_STORE_MAX_ENTRIES; ++i) {
        if (strncmp(blob.entries[i].ssid, ssid, sizeof(blob.entries[i].ssid)) == 0) {
            target_index = i;
            break;
        }
    }

    ESP_RETURN_ON_FALSE(target_index != WIFI_PROFILE_STORE_MAX_ENTRIES, ESP_ERR_NOT_FOUND, TAG, "SSID not found");

    memset(&blob.entries[target_index], 0, sizeof(blob.entries[target_index]));
    blob.version = WIFI_PROFILE_STORE_VERSION;

    esp_err_t err = wifi_profile_store_write_blob(&blob);
    ESP_RETURN_ON_ERROR(err, TAG, "write Wi-Fi profile blob failed");
    ESP_LOGI(TAG, "removed Wi-Fi profile for SSID \"%s\"", ssid);
    return ESP_OK;
}
