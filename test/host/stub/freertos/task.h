#pragma once
#include "FreeRTOS.h"
/* Defined by the test. */
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreate(void (*task)(void *),
                       const char *name,
                       uint32_t stack_size,
                       void *arg,
                       unsigned priority,
                       TaskHandle_t *out_handle);
