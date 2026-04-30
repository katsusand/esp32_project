idf_component_register(SRCS "cyd_system_apps.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_shell cyd_alarm cyd_display cyd_input cyd_speaker cyd_ui cyd_wifi_setup esp_app_format esp_hw_support esp_system heap time_sync wifi_manager wifi_profile_store)
