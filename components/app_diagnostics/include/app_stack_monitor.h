#pragma once

#define APP_STACK_MONITOR_OVERPROVISIONED_BYTES 3072U
#define APP_STACK_MONITOR_LOW_BYTES 1024U

#if defined(APP_DEV) && APP_DEV
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"

#define APP_STACK_MONITOR_CHECK(tag, task_name, interval_ms) do { \
        static TickType_t s_app_stack_monitor_last_tick; \
        TickType_t app_stack_monitor_now = xTaskGetTickCount(); \
        TickType_t app_stack_monitor_interval = pdMS_TO_TICKS(interval_ms); \
        if (app_stack_monitor_interval == 0 || \
            app_stack_monitor_now - s_app_stack_monitor_last_tick >= app_stack_monitor_interval) { \
            s_app_stack_monitor_last_tick = app_stack_monitor_now; \
            unsigned app_stack_monitor_watermark = (unsigned)uxTaskGetStackHighWaterMark(NULL); \
            if (app_stack_monitor_watermark > APP_STACK_MONITOR_OVERPROVISIONED_BYTES) { \
                ESP_LOGW(tag, "%s stack spare high: %u bytes (> %u)", \
                         task_name, \
                         app_stack_monitor_watermark, \
                         APP_STACK_MONITOR_OVERPROVISIONED_BYTES); \
            } else if (app_stack_monitor_watermark <= APP_STACK_MONITOR_LOW_BYTES) { \
                ESP_LOGW(tag, "%s stack spare low: %u bytes (<= %u)", \
                         task_name, \
                         app_stack_monitor_watermark, \
                         APP_STACK_MONITOR_LOW_BYTES); \
            } else { \
                ESP_LOGI(tag, "%s stack spare ok: %u bytes", task_name, app_stack_monitor_watermark); \
            } \
        } \
    } while (0)

#define APP_HEAP_MONITOR_CHECK(tag, interval_ms) do { \
        static TickType_t s_app_heap_monitor_last_tick; \
        TickType_t app_heap_monitor_now = xTaskGetTickCount(); \
        TickType_t app_heap_monitor_interval = pdMS_TO_TICKS(interval_ms); \
        if (app_heap_monitor_interval == 0 || \
            app_heap_monitor_now - s_app_heap_monitor_last_tick >= app_heap_monitor_interval) { \
            s_app_heap_monitor_last_tick = app_heap_monitor_now; \
            ESP_LOGI(tag, \
                     "heap: free=%u min=%u 8bit=%u min_8bit=%u largest_8bit=%u", \
                     (unsigned)esp_get_free_heap_size(), \
                     (unsigned)esp_get_minimum_free_heap_size(), \
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), \
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT), \
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)); \
        } \
    } while (0)
#else
#define APP_STACK_MONITOR_CHECK(tag, task_name, interval_ms) do { \
        (void)(tag); \
        (void)(task_name); \
        (void)(interval_ms); \
    } while (0)
#define APP_HEAP_MONITOR_CHECK(tag, interval_ms) do { \
        (void)(tag); \
        (void)(interval_ms); \
    } while (0)
#endif
