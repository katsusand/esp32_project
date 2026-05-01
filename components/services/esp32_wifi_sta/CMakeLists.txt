idf_component_register(SRCS "esp32_wifi_sta.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi esp_netif esp_event nvs_flash wifi_profile_store)
