#ifndef CYD_WIFI_SETUP_H
#define CYD_WIFI_SETUP_H

#include <stdbool.h>
#include "esp_err.h"
#include "app_shell.h"
#include "cyd_input.h"
#include "esp32_wifi_sta.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CYD_WIFI_SETUP_AUTOCONNECT_IDLE = 0,
    CYD_WIFI_SETUP_AUTOCONNECT_SEARCHING,
    CYD_WIFI_SETUP_AUTOCONNECT_TRYING,
} cyd_wifi_setup_autoconnect_phase_t;

typedef struct {
    cyd_wifi_setup_autoconnect_phase_t phase;
    char ssid[33];
} cyd_wifi_setup_autoconnect_progress_t;

typedef enum {
    CYD_WIFI_SETUP_PASSWORD_CONTINUE = 0,
    CYD_WIFI_SETUP_PASSWORD_CANCELLED,
    CYD_WIFI_SETUP_PASSWORD_CONNECTED,
} cyd_wifi_setup_password_result_t;

const app_shell_app_t *cyd_wifi_setup_get_app(void);
void cyd_wifi_setup_set_return_app(const app_shell_app_t *app);
esp_err_t cyd_wifi_setup_connect_configured(TickType_t wait_ticks,
                                            esp32_wifi_sta_failure_reason_t *failure_reason);
cyd_wifi_setup_autoconnect_progress_t cyd_wifi_setup_get_autoconnect_progress(void);
void cyd_wifi_setup_begin_scan_session(void);
esp_err_t cyd_wifi_setup_poll_scan_session(const cyd_input_event_t *event,
                                           bool *selected,
                                           bool *cancelled,
                                           esp32_wifi_sta_scan_record_t *selected_record);
void cyd_wifi_setup_begin_password_session(const esp32_wifi_sta_scan_record_t *record);
esp_err_t cyd_wifi_setup_poll_password_session(const cyd_input_event_t *event,
                                               cyd_wifi_setup_password_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
