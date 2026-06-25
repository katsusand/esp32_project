#ifndef XPT2046_SOFTSPI_H
#define XPT2046_SOFTSPI_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t xpt2046_softspi_init(void);
esp_err_t xpt2046_softspi_get_raw(int16_t *x, int16_t *y, bool *touched);

#ifdef __cplusplus
}
#endif

#endif
