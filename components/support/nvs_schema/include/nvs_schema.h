#pragma once

#include <stdbool.h>
#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NVS_STORAGE_SCOPE_SYSTEM = 0,
    NVS_STORAGE_SCOPE_FEATURE,
    NVS_STORAGE_SCOPE_APP,
} nvs_storage_scope_t;

typedef enum {
    NVS_VALUE_TYPE_U8 = 0,
    NVS_VALUE_TYPE_U16,
    NVS_VALUE_TYPE_STR,
    NVS_VALUE_TYPE_BLOB,
} nvs_value_type_t;

typedef enum {
    NVS_RESET_CLASS_SYSTEM = 0,
    NVS_RESET_CLASS_FEATURE,
    NVS_RESET_CLASS_APP,
    NVS_RESET_CLASS_FULL,
} nvs_reset_class_t;

typedef struct {
    const char *name;
    nvs_storage_scope_t scope;
    const char *owner_component;
    nvs_reset_class_t reset_class;
} nvs_namespace_descriptor_t;

typedef struct {
    const nvs_namespace_descriptor_t *ns;
    const char *key;
    nvs_value_type_t value_type;
    bool versioned_payload;
} nvs_key_descriptor_t;

/* Current namespaces used by the repository. */
extern const nvs_namespace_descriptor_t NVS_NS_CYD_DISPLAY;
extern const nvs_namespace_descriptor_t NVS_NS_APP_SHELL;
extern const nvs_namespace_descriptor_t NVS_NS_TIME_SYNC;
extern const nvs_namespace_descriptor_t NVS_NS_RADIO_MANAGER;
extern const nvs_namespace_descriptor_t NVS_NS_ESP32_WIFI_STA;
extern const nvs_namespace_descriptor_t NVS_NS_APP_SCHEDULER;

/* Reserved future namespaces to make ownership visible in code early. */
extern const nvs_namespace_descriptor_t NVS_NS_SYS_DISPLAY;
extern const nvs_namespace_descriptor_t NVS_NS_SYS_INPUT;
extern const nvs_namespace_descriptor_t NVS_NS_SYS_SHELL;
extern const nvs_namespace_descriptor_t NVS_NS_SYS_SOUND;
extern const nvs_namespace_descriptor_t NVS_NS_FEAT_TIME_SYNC;
extern const nvs_namespace_descriptor_t NVS_NS_FEAT_WIFI_STA;
extern const nvs_namespace_descriptor_t NVS_NS_FEAT_RADIO;
extern const nvs_namespace_descriptor_t NVS_NS_FEAT_ESPNOW;
extern const nvs_namespace_descriptor_t NVS_NS_APP_CLOCK;
extern const nvs_namespace_descriptor_t NVS_NS_APP_LOGGER;

extern const nvs_key_descriptor_t NVS_KEY_CYD_DISPLAY_BRIGHTNESS;
extern const nvs_key_descriptor_t NVS_KEY_CYD_DISPLAY_CONFIG;
extern const nvs_key_descriptor_t NVS_KEY_CYD_DISPLAY_TOUCH_CAL;
extern const nvs_key_descriptor_t NVS_KEY_APP_SHELL_IDLE_TIMEOUT;
extern const nvs_key_descriptor_t NVS_KEY_APP_SHELL_CONFIG;
extern const nvs_key_descriptor_t NVS_KEY_TIME_SYNC_INTERVAL_MINUTES;
extern const nvs_key_descriptor_t NVS_KEY_TIME_SYNC_TIMEZONE;
extern const nvs_key_descriptor_t NVS_KEY_TIME_SYNC_CONFIG;
extern const nvs_key_descriptor_t NVS_KEY_RADIO_MANAGER_IDLE_TIMEOUT;
extern const nvs_key_descriptor_t NVS_KEY_RADIO_MANAGER_CONFIG;
extern const nvs_key_descriptor_t NVS_KEY_WIFI_PROFILE_STORE_PROFILES;
extern const nvs_key_descriptor_t NVS_KEY_WIFI_PROFILE_STORE_LEGACY_SSID;
extern const nvs_key_descriptor_t NVS_KEY_WIFI_PROFILE_STORE_LEGACY_PASSWORD;
extern const nvs_key_descriptor_t NVS_KEY_APP_SCHEDULER_CONFIG;

static inline esp_err_t nvs_open_descriptor(const nvs_namespace_descriptor_t *descriptor,
                                            nvs_open_mode_t open_mode,
                                            nvs_handle_t *out_handle)
{
    return nvs_open(descriptor->name, open_mode, out_handle);
}

#ifdef __cplusplus
}
#endif
