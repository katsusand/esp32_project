#include "nvs_schema.h"

const nvs_namespace_descriptor_t NVS_NS_CYD_DISPLAY = {
    .name = "cyd_display",
    .scope = NVS_STORAGE_SCOPE_SYSTEM,
    .owner_component = "cyd_display",
    .reset_class = NVS_RESET_CLASS_SYSTEM,
};

const nvs_namespace_descriptor_t NVS_NS_APP_SHELL = {
    .name = "app_shell",
    .scope = NVS_STORAGE_SCOPE_SYSTEM,
    .owner_component = "app_shell",
    .reset_class = NVS_RESET_CLASS_SYSTEM,
};

const nvs_namespace_descriptor_t NVS_NS_TIME_SYNC = {
    .name = "time_sync",
    .scope = NVS_STORAGE_SCOPE_FEATURE,
    .owner_component = "time_sync",
    .reset_class = NVS_RESET_CLASS_FEATURE,
};

const nvs_namespace_descriptor_t NVS_NS_RADIO_MANAGER = {
    .name = "radio_manager",
    .scope = NVS_STORAGE_SCOPE_FEATURE,
    .owner_component = "radio_manager",
    .reset_class = NVS_RESET_CLASS_FEATURE,
};

const nvs_namespace_descriptor_t NVS_NS_ESP32_WIFI_STA = {
    .name = "esp32_wifi_sta",
    .scope = NVS_STORAGE_SCOPE_FEATURE,
    .owner_component = "wifi_profile_store",
    .reset_class = NVS_RESET_CLASS_FEATURE,
};

const nvs_namespace_descriptor_t NVS_NS_APP_SCHEDULER = {
    .name = "app_scheduler",
    .scope = NVS_STORAGE_SCOPE_FEATURE,
    .owner_component = "app_scheduler",
    .reset_class = NVS_RESET_CLASS_FEATURE,
};

const nvs_namespace_descriptor_t NVS_NS_SYS_DISPLAY = {
    .name = "sys_display",
    .scope = NVS_STORAGE_SCOPE_SYSTEM,
    .owner_component = "cyd_display",
    .reset_class = NVS_RESET_CLASS_SYSTEM,
};

const nvs_namespace_descriptor_t NVS_NS_SYS_INPUT = {
    .name = "sys_input",
    .scope = NVS_STORAGE_SCOPE_SYSTEM,
    .owner_component = "cyd_input",
    .reset_class = NVS_RESET_CLASS_SYSTEM,
};

const nvs_namespace_descriptor_t NVS_NS_SYS_SHELL = {
    .name = "sys_shell",
    .scope = NVS_STORAGE_SCOPE_SYSTEM,
    .owner_component = "app_shell",
    .reset_class = NVS_RESET_CLASS_SYSTEM,
};

const nvs_namespace_descriptor_t NVS_NS_SYS_SOUND = {
    .name = "sys_sound",
    .scope = NVS_STORAGE_SCOPE_SYSTEM,
    .owner_component = "cyd_speaker",
    .reset_class = NVS_RESET_CLASS_SYSTEM,
};

const nvs_namespace_descriptor_t NVS_NS_FEAT_TIME_SYNC = {
    .name = "feat_time_sync",
    .scope = NVS_STORAGE_SCOPE_FEATURE,
    .owner_component = "time_sync",
    .reset_class = NVS_RESET_CLASS_FEATURE,
};

const nvs_namespace_descriptor_t NVS_NS_FEAT_WIFI_STA = {
    .name = "feat_wifi_sta",
    .scope = NVS_STORAGE_SCOPE_FEATURE,
    .owner_component = "wifi_profile_store",
    .reset_class = NVS_RESET_CLASS_FEATURE,
};

const nvs_namespace_descriptor_t NVS_NS_FEAT_RADIO = {
    .name = "feat_radio",
    .scope = NVS_STORAGE_SCOPE_FEATURE,
    .owner_component = "radio_manager",
    .reset_class = NVS_RESET_CLASS_FEATURE,
};

const nvs_namespace_descriptor_t NVS_NS_FEAT_ESPNOW = {
    .name = "feat_espnow",
    .scope = NVS_STORAGE_SCOPE_FEATURE,
    .owner_component = "espnow",
    .reset_class = NVS_RESET_CLASS_FEATURE,
};

const nvs_namespace_descriptor_t NVS_NS_APP_CLOCK = {
    .name = "app_clock",
    .scope = NVS_STORAGE_SCOPE_APP,
    .owner_component = "clock_app",
    .reset_class = NVS_RESET_CLASS_APP,
};

const nvs_namespace_descriptor_t NVS_NS_APP_LOGGER = {
    .name = "app_logger",
    .scope = NVS_STORAGE_SCOPE_APP,
    .owner_component = "logger_app",
    .reset_class = NVS_RESET_CLASS_APP,
};

const nvs_key_descriptor_t NVS_KEY_CYD_DISPLAY_BRIGHTNESS = {
    .ns = &NVS_NS_CYD_DISPLAY,
    .key = "brightness",
    .value_type = NVS_VALUE_TYPE_U8,
    .versioned_payload = false,
};

const nvs_key_descriptor_t NVS_KEY_CYD_DISPLAY_CONFIG = {
    .ns = &NVS_NS_CYD_DISPLAY,
    .key = "config_v1",
    .value_type = NVS_VALUE_TYPE_BLOB,
    .versioned_payload = true,
};

const nvs_key_descriptor_t NVS_KEY_CYD_DISPLAY_TOUCH_CAL = {
    .ns = &NVS_NS_CYD_DISPLAY,
    .key = "touch_cal",
    .value_type = NVS_VALUE_TYPE_BLOB,
    .versioned_payload = true,
};

const nvs_key_descriptor_t NVS_KEY_APP_SHELL_IDLE_TIMEOUT = {
    .ns = &NVS_NS_APP_SHELL,
    .key = "idle_timeout",
    .value_type = NVS_VALUE_TYPE_U16,
    .versioned_payload = false,
};

const nvs_key_descriptor_t NVS_KEY_APP_SHELL_CONFIG = {
    .ns = &NVS_NS_APP_SHELL,
    .key = "config_v1",
    .value_type = NVS_VALUE_TYPE_BLOB,
    .versioned_payload = true,
};

const nvs_key_descriptor_t NVS_KEY_TIME_SYNC_INTERVAL_MINUTES = {
    .ns = &NVS_NS_TIME_SYNC,
    .key = "interval_min",
    .value_type = NVS_VALUE_TYPE_U16,
    .versioned_payload = false,
};

const nvs_key_descriptor_t NVS_KEY_TIME_SYNC_TIMEZONE = {
    .ns = &NVS_NS_TIME_SYNC,
    .key = "timezone",
    .value_type = NVS_VALUE_TYPE_STR,
    .versioned_payload = false,
};

const nvs_key_descriptor_t NVS_KEY_TIME_SYNC_CONFIG = {
    .ns = &NVS_NS_TIME_SYNC,
    .key = "config_v1",
    .value_type = NVS_VALUE_TYPE_BLOB,
    .versioned_payload = true,
};

const nvs_key_descriptor_t NVS_KEY_RADIO_MANAGER_IDLE_TIMEOUT = {
    .ns = &NVS_NS_RADIO_MANAGER,
    .key = "idle_timeout",
    .value_type = NVS_VALUE_TYPE_U16,
    .versioned_payload = false,
};

const nvs_key_descriptor_t NVS_KEY_RADIO_MANAGER_CONFIG = {
    .ns = &NVS_NS_RADIO_MANAGER,
    .key = "config_v1",
    .value_type = NVS_VALUE_TYPE_BLOB,
    .versioned_payload = true,
};

const nvs_key_descriptor_t NVS_KEY_WIFI_PROFILE_STORE_PROFILES = {
    .ns = &NVS_NS_ESP32_WIFI_STA,
    .key = "profiles_v1",
    .value_type = NVS_VALUE_TYPE_BLOB,
    .versioned_payload = true,
};

const nvs_key_descriptor_t NVS_KEY_WIFI_PROFILE_STORE_LEGACY_SSID = {
    .ns = &NVS_NS_ESP32_WIFI_STA,
    .key = "ssid",
    .value_type = NVS_VALUE_TYPE_STR,
    .versioned_payload = false,
};

const nvs_key_descriptor_t NVS_KEY_WIFI_PROFILE_STORE_LEGACY_PASSWORD = {
    .ns = &NVS_NS_ESP32_WIFI_STA,
    .key = "password",
    .value_type = NVS_VALUE_TYPE_STR,
    .versioned_payload = false,
};

const nvs_key_descriptor_t NVS_KEY_APP_SCHEDULER_CONFIG = {
    .ns = &NVS_NS_APP_SCHEDULER,
    .key = "config_v1",
    .value_type = NVS_VALUE_TYPE_BLOB,
    .versioned_payload = true,
};
