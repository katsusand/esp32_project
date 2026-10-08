#ifndef CYD_WIFI_SETUP_VIEW_H
#define CYD_WIFI_SETUP_VIEW_H

/*
 * The Wi-Fi setup screens, built from a model only: the network list, the
 * "connecting" and "saved" notices and the failure dialog. The password entry
 * itself is cyd_text_input's keyboard screen.
 *
 * English contract: cyd_wifi_setup_view_build() calls no service and touches
 * no global state, so the simulator and the host tests can build every state.
 * Service values arrive as the view's own types; this header depends on
 * cyd_display only.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cyd_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* + index on the current page */
#define CYD_WIFI_SETUP_VIEW_ACTION_AP_BASE 0x0100
#define CYD_WIFI_SETUP_VIEW_ACTION_BACK 0x030a
#define CYD_WIFI_SETUP_VIEW_ACTION_RESCAN 0x030b
#define CYD_WIFI_SETUP_VIEW_ACTION_PREV 0x030c
#define CYD_WIFI_SETUP_VIEW_ACTION_NEXT 0x030d
#define CYD_WIFI_SETUP_VIEW_ACTION_OK 0x030e

/* Networks per page: 32px rows, so a finger can pick one on the resistive panel. */
#define CYD_WIFI_SETUP_VIEW_APS_PER_PAGE 5

typedef enum {
    CYD_WIFI_SETUP_VIEW_SCAN = 0,   /* the network list, or its searching / empty / error state */
    CYD_WIFI_SETUP_VIEW_CONNECTING,
    CYD_WIFI_SETUP_VIEW_SAVED,
    CYD_WIFI_SETUP_VIEW_FAILED,     /* with an OK button */
} cyd_wifi_setup_view_screen_t;

/* esp32_wifi_sta_failure_reason_t, as far as the operator can act on it. */
typedef enum {
    CYD_WIFI_SETUP_VIEW_FAILURE_OTHER = 0,
    CYD_WIFI_SETUP_VIEW_FAILURE_AUTH,
    CYD_WIFI_SETUP_VIEW_FAILURE_NOT_FOUND,
    CYD_WIFI_SETUP_VIEW_FAILURE_TIMEOUT,
} cyd_wifi_setup_view_failure_t;

typedef struct {
    const char *ssid;
    int8_t rssi;
} cyd_wifi_setup_view_ap_t;

typedef struct {
    cyd_wifi_setup_view_screen_t screen;

    /* SCAN */
    bool scanning;
    bool scan_failed;
    const char *scan_error; /* technical, English, e.g. "ESP_ERR_TIMEOUT" */
    cyd_wifi_setup_view_ap_t aps[CYD_WIFI_SETUP_VIEW_APS_PER_PAGE]; /* the current page only */
    size_t ap_count;   /* on this page */
    size_t page_index;
    size_t page_count;

    /* CONNECTING / SAVED / FAILED */
    const char *ssid;
    cyd_wifi_setup_view_failure_t failure;
    const char *error; /* technical, English; may be NULL */
} cyd_wifi_setup_view_model_t;

/* "強い" / "ふつう" / "弱い" */
const char *cyd_wifi_setup_view_signal_text(int8_t rssi);

/* Clears `screen` and builds the screen the model selects. */
void cyd_wifi_setup_view_build(cyd_display_screen_t *screen, const cyd_wifi_setup_view_model_t *model);

#ifdef __cplusplus
}
#endif

#endif
