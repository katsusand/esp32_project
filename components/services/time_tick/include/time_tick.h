#ifndef TIME_TICK_H
#define TIME_TICK_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    time_t epoch_sec;
    struct tm local_time;
    bool valid;
    bool jumped;
    int32_t delta_sec;
} time_tick_event_t;

esp_err_t time_tick_start(void);
esp_err_t time_tick_subscribe(QueueHandle_t *queue_out);
esp_err_t time_tick_unsubscribe(QueueHandle_t queue);
bool time_tick_get_latest(time_tick_event_t *event);

#ifdef __cplusplus
}
#endif

#endif
