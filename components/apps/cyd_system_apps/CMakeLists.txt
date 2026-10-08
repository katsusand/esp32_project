idf_component_register(SRCS "cyd_system_apps_common.c"
                            "system_settings_view.c"
                            "system_info_app.c"
                            "system_settings_app.c"
                            "system_touch_calibration_app.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_registry app_shell nvs_schema cyd_display cyd_input cyd_ui cyd_wifi_setup esp_app_format esp_hw_support esp_system heap nvs_flash nvs_health time_sync wifi_connection wifi_profile_store wifi_rssi_history radio_manager)
