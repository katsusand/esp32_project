/*
 * Keeps the selected colour theme in NVS. Firmware only: the simulator and
 * the host tests build cyd_ui.c without this file.
 */
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_schema.h"
#include "cyd_ui.h"

#define TAG "cyd_ui"

/* System scope ("sys_"): the theme is a device setting, so Clear App Data keeps it. */
NVS_SCHEMA_DECLARE_NS(NVS_NS, "sys_ui");
static const nvs_key_descriptor_t NVS_KEY_CYD_UI_THEME = {
    .ns = NVS_NS,
    .key = "theme",
};

/* What NVS holds, so an unchanged setting costs no flash write. -1: unknown. */
static int s_cyd_ui_saved_theme = -1;

esp_err_t cyd_ui_theme_load(void)
{
    nvs_handle_t handle;
    uint8_t value = 0;

    esp_err_t err = nvs_open_descriptor(NVS_KEY_CYD_UI_THEME.ns, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        cyd_ui_set_theme(CYD_UI_THEME_ID_DEFAULT);
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "open theme NVS failed");
    err = nvs_get_u8(handle, NVS_KEY_CYD_UI_THEME.key, &value);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        cyd_ui_set_theme(CYD_UI_THEME_ID_DEFAULT);
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "read theme failed");

    /*
     * Not an NVS health problem: a theme added by newer firmware is unknown to
     * older firmware, and erasing all of NVS over a colour would be absurd.
     */
    if (value >= CYD_UI_THEME_ID_COUNT) {
        ESP_LOGW(TAG, "stored theme %u is unknown; using the default", (unsigned)value);
        cyd_ui_set_theme(CYD_UI_THEME_ID_DEFAULT);
        return ESP_OK;
    }
    cyd_ui_set_theme((cyd_ui_theme_id_t)value);
    s_cyd_ui_saved_theme = value;
    return ESP_OK;
}

esp_err_t cyd_ui_theme_save(void)
{
    const int current = (int)cyd_ui_theme_id();
    nvs_handle_t handle;

    if (current == s_cyd_ui_saved_theme) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(nvs_open_descriptor(NVS_KEY_CYD_UI_THEME.ns, NVS_READWRITE, &handle),
                        TAG, "open theme NVS failed");
    esp_err_t err = nvs_set_u8(handle, NVS_KEY_CYD_UI_THEME.key, (uint8_t)current);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_RETURN_ON_ERROR(err, TAG, "save theme failed");
    s_cyd_ui_saved_theme = current;
    return ESP_OK;
}
