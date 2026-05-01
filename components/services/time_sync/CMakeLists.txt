idf_component_register(SRCS "time_sync.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_diagnostics esp_netif esp_hw_support nvs_flash wifi_manager)
