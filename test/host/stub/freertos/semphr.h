#pragma once
/* A mutex for a test that runs on one thread: taking one that is already taken
   cannot wait for anybody, so it reports a timeout at once. */
#include <stdlib.h>
#include "FreeRTOS.h"
typedef struct {
    int taken;
} stub_mutex_t;
typedef stub_mutex_t *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (SemaphoreHandle_t)calloc(1, sizeof(stub_mutex_t));
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t ticks)
{
    (void)ticks;
    if (m->taken) {
        return pdFALSE;
    }
    m->taken = 1;
    return pdTRUE;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t m)
{
    if (!m->taken) {
        return pdFALSE;
    }
    m->taken = 0;
    return pdTRUE;
}
