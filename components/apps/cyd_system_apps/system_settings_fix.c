#include <stddef.h>
#include "system_settings_fix.h"
#include "system_settings_fix_internal.h"

/*
 * The fixes products asked for. Written only before app_shell_start() and read
 * only after it, on the shell task, so no lock is needed.
 */
typedef struct {
    system_settings_fix_mode_t mode;
    const char *reason;
} system_settings_fix_entry_t;

static system_settings_fix_entry_t s_fixes[SYSTEM_SETTINGS_ITEM_COUNT];

static const char *const k_item_names[SYSTEM_SETTINGS_ITEM_COUNT] = {
    [SYSTEM_SETTINGS_ITEM_BRIGHTNESS] = "brightness",
    [SYSTEM_SETTINGS_ITEM_THEME] = "theme",
    [SYSTEM_SETTINGS_ITEM_IDLE_RETURN] = "idle_return",
    [SYSTEM_SETTINGS_ITEM_TIMEZONE] = "timezone",
    [SYSTEM_SETTINGS_ITEM_SYNC_INTERVAL] = "sync_interval",
    [SYSTEM_SETTINGS_ITEM_WIFI_IDLE] = "wifi_idle",
    [SYSTEM_SETTINGS_ITEM_TOUCH_CALIBRATE] = "touch_calibrate",
    [SYSTEM_SETTINGS_ITEM_STORED_SSIDS] = "stored_ssids",
    [SYSTEM_SETTINGS_ITEM_WIFI_SETUP] = "wifi_setup",
    [SYSTEM_SETTINGS_ITEM_SYNC_NOW] = "sync_now",
    [SYSTEM_SETTINGS_ITEM_CLEAR_TOUCH_CALIB] = "clear_touch_calib",
    [SYSTEM_SETTINGS_ITEM_CLEAR_APP_DATA] = "clear_app_data",
    [SYSTEM_SETTINGS_ITEM_CLEAR_NVS] = "clear_nvs",
};

bool system_settings_item_has_value(system_settings_item_t item)
{
    return item < SYSTEM_SETTINGS_ITEM_TOUCH_CALIBRATE;
}

esp_err_t system_settings_fix_set(system_settings_item_t item, system_settings_fix_mode_t mode, const char *reason)
{
    if ((unsigned)item >= SYSTEM_SETTINGS_ITEM_COUNT || (unsigned)mode > SYSTEM_SETTINGS_FIX_HIDDEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mode == SYSTEM_SETTINGS_FIX_READONLY && !system_settings_item_has_value(item)) {
        /* A greyed-out button with nothing to read is just a dead button. */
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_fixes[item].mode = mode;
    s_fixes[item].reason = mode == SYSTEM_SETTINGS_FIX_NONE ? NULL : reason;
    return ESP_OK;
}

void system_settings_fix_reset(void)
{
    for (size_t i = 0; i < SYSTEM_SETTINGS_ITEM_COUNT; ++i) {
        s_fixes[i] = (system_settings_fix_entry_t){ 0 };
    }
}

system_settings_fix_mode_t system_settings_fix_mode(system_settings_item_t item)
{
    return (unsigned)item < SYSTEM_SETTINGS_ITEM_COUNT ? s_fixes[item].mode : SYSTEM_SETTINGS_FIX_NONE;
}

const char *system_settings_fix_reason(system_settings_item_t item)
{
    return (unsigned)item < SYSTEM_SETTINGS_ITEM_COUNT ? s_fixes[item].reason : NULL;
}

const char *system_settings_item_name(system_settings_item_t item)
{
    return (unsigned)item < SYSTEM_SETTINGS_ITEM_COUNT ? k_item_names[item] : "";
}

bool system_settings_items_all_hidden(const system_settings_item_t *items, size_t count)
{
    if (items == NULL || count == 0) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (system_settings_fix_mode(items[i]) != SYSTEM_SETTINGS_FIX_HIDDEN) {
            return false;
        }
    }
    return true;
}

bool system_settings_fixed_action(uint16_t action_id, const system_settings_action_item_t *map, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (map[i].action_id == action_id) {
            return system_settings_fix_mode(map[i].item) != SYSTEM_SETTINGS_FIX_NONE;
        }
    }
    return false;
}
