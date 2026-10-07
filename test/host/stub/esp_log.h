#pragma once
#include <stdio.h>
extern int g_log_quiet;
#define ESP_LOGE(tag, fmt, ...) do { if(!g_log_quiet) fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGW(tag, fmt, ...) do { if(!g_log_quiet) fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGI(tag, fmt, ...) do { if(!g_log_quiet) fprintf(stderr, "I %s: " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGD(tag, fmt, ...) do { if(!g_log_quiet) fprintf(stderr, "D %s: " fmt "\n", tag, ##__VA_ARGS__); } while(0)

/* Defined by a test that cares which tags are turned off. */
typedef int esp_log_level_t;
#define ESP_LOG_NONE 0
#define ESP_LOG_INFO 3
void esp_log_level_set(const char *tag, esp_log_level_t level);
esp_log_level_t esp_log_level_get(const char *tag);
