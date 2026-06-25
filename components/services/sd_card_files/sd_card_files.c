#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "sd_card_files.h"
#include "sd_card_storage.h"

static const char *TAG = "sd_card_files";
static const size_t SD_CARD_FILES_PATH_MAX = 128;

typedef enum {
    SD_CARD_FILE_EXPECT_NEW = 0,
    SD_CARD_FILE_EXPECT_EXISTING,
    SD_CARD_FILE_ALLOW_OVERWRITE,
} sd_card_file_open_policy_t;

static esp_err_t sd_card_files_build_path(const char *relative_path, char *out_path, size_t out_size)
{
    const char *mount_point = sd_card_storage_get_mount_point();
    const char *path = relative_path;

    ESP_RETURN_ON_FALSE(relative_path != NULL, ESP_ERR_INVALID_ARG, TAG, "relative path is null");
    ESP_RETURN_ON_FALSE(out_path != NULL, ESP_ERR_INVALID_ARG, TAG, "output path buffer is null");
    ESP_RETURN_ON_FALSE(out_size > 0, ESP_ERR_INVALID_ARG, TAG, "output path buffer is empty");
    ESP_RETURN_ON_FALSE(sd_card_storage_is_mounted(), ESP_ERR_INVALID_STATE, TAG, "sd card is not mounted");
    ESP_RETURN_ON_FALSE(mount_point != NULL, ESP_ERR_INVALID_STATE, TAG, "sd card mount point unavailable");

    while (*path == '/') {
        ++path;
    }

    ESP_RETURN_ON_FALSE(*path != '\0', ESP_ERR_INVALID_ARG, TAG, "relative path is empty");

    int written = snprintf(out_path, out_size, "%s/%s", mount_point, path);
    ESP_RETURN_ON_FALSE(written > 0 && (size_t)written < out_size,
                        ESP_ERR_INVALID_SIZE,
                        TAG,
                        "sd path is too long");
    return ESP_OK;
}

static bool sd_card_files_exists(const char *path)
{
    struct stat st;
    return path != NULL && stat(path, &st) == 0;
}

static esp_err_t sd_card_files_validate_open_policy(const char *path, sd_card_file_open_policy_t policy)
{
    bool exists = sd_card_files_exists(path);

    switch (policy) {
        case SD_CARD_FILE_EXPECT_NEW:
            ESP_RETURN_ON_FALSE(!exists, ESP_ERR_INVALID_STATE, TAG, "file already exists: %s", path);
            return ESP_OK;
        case SD_CARD_FILE_EXPECT_EXISTING:
            ESP_RETURN_ON_FALSE(exists, ESP_ERR_NOT_FOUND, TAG, "file not found: %s", path);
            return ESP_OK;
        case SD_CARD_FILE_ALLOW_OVERWRITE:
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t sd_card_files_write(const char *relative_path,
                                     const void *data,
                                     size_t size,
                                     const char *mode,
                                     sd_card_file_open_policy_t policy)
{
    char path[SD_CARD_FILES_PATH_MAX];
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data is null");
    ESP_RETURN_ON_FALSE(mode != NULL, ESP_ERR_INVALID_ARG, TAG, "mode is null");
    ESP_RETURN_ON_ERROR(sd_card_files_build_path(relative_path, path, sizeof(path)),
                        TAG,
                        "build path failed");
    ESP_RETURN_ON_ERROR(sd_card_files_validate_open_policy(path, policy),
                        TAG,
                        "open policy check failed");

    FILE *fp = fopen(path, mode);
    ESP_RETURN_ON_FALSE(fp != NULL, ESP_FAIL, TAG, "open failed: %s", path);

    if (size > 0) {
        size_t written = fwrite(data, 1, size, fp);
        if (written != size) {
            fclose(fp);
            return ESP_FAIL;
        }
    }

    if (fflush(fp) != 0) {
        fclose(fp);
        return ESP_FAIL;
    }
    if (fclose(fp) != 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t sd_card_files_write_new_text(const char *relative_path, const char *text)
{
    ESP_RETURN_ON_FALSE(text != NULL, ESP_ERR_INVALID_ARG, TAG, "text is null");
    return sd_card_files_write(relative_path, text, strlen(text), "wb", SD_CARD_FILE_EXPECT_NEW);
}

esp_err_t sd_card_files_append_existing_text(const char *relative_path, const char *text)
{
    ESP_RETURN_ON_FALSE(text != NULL, ESP_ERR_INVALID_ARG, TAG, "text is null");
    return sd_card_files_write(relative_path, text, strlen(text), "ab", SD_CARD_FILE_EXPECT_EXISTING);
}

esp_err_t sd_card_files_overwrite_text(const char *relative_path, const char *text)
{
    ESP_RETURN_ON_FALSE(text != NULL, ESP_ERR_INVALID_ARG, TAG, "text is null");
    return sd_card_files_write(relative_path, text, strlen(text), "wb", SD_CARD_FILE_ALLOW_OVERWRITE);
}

esp_err_t sd_card_files_write_new_binary(const char *relative_path, const void *data, size_t size)
{
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data is null");
    return sd_card_files_write(relative_path, data, size, "wb", SD_CARD_FILE_EXPECT_NEW);
}

esp_err_t sd_card_files_append_existing_binary(const char *relative_path, const void *data, size_t size)
{
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data is null");
    return sd_card_files_write(relative_path, data, size, "ab", SD_CARD_FILE_EXPECT_EXISTING);
}

esp_err_t sd_card_files_overwrite_binary(const char *relative_path, const void *data, size_t size)
{
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data is null");
    return sd_card_files_write(relative_path, data, size, "wb", SD_CARD_FILE_ALLOW_OVERWRITE);
}
