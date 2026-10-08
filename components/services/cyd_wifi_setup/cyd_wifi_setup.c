#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app_shell.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_text_input.h"
#include "cyd_ui.h"
#include "esp32_wifi_sta.h"
#include "cyd_wifi_setup.h"
#include "cyd_wifi_setup_view.h"
#include "time_sync.h"
#include "wifi_connection.h"

#define WIFI_SCAN_STATUS_LINE_COUNT CYD_WIFI_SETUP_VIEW_APS_PER_PAGE
#define WIFI_SCAN_RECORD_CAPACITY CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE
#define WIFI_ACTION_SCAN_BASE   CYD_WIFI_SETUP_VIEW_ACTION_AP_BASE
#define WIFI_ACTION_SCAN_BACK   CYD_WIFI_SETUP_VIEW_ACTION_BACK
#define WIFI_ACTION_SCAN_REFRESH CYD_WIFI_SETUP_VIEW_ACTION_RESCAN
#define WIFI_ACTION_SCAN_PREV   CYD_WIFI_SETUP_VIEW_ACTION_PREV
#define WIFI_ACTION_SCAN_NEXT   CYD_WIFI_SETUP_VIEW_ACTION_NEXT
#define WIFI_IDLE_POLL_MS       250

#ifndef CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS
#define CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS 15000
#endif

#define TAG "cyd_wifi_setup"

static cyd_display_screen_t s_wifi_setup_screen;

typedef struct {
    bool pending;
    bool long_pressed;
    uint16_t action_id;
} wifi_touch_action_tracker_t;

typedef struct {
    bool initialized;
    uint32_t scan_round;
    size_t page_index;
    size_t record_count;
    esp32_wifi_sta_scan_record_t records[WIFI_SCAN_RECORD_CAPACITY];
    size_t visible_count;
    wifi_touch_action_tracker_t touch_tracker;
} wifi_scan_session_t;

typedef struct {
    bool initialized;
    esp32_wifi_sta_scan_record_t record;
} wifi_password_session_t;

static wifi_scan_session_t s_scan_session = { 0 };
static wifi_password_session_t s_password_session = { 0 };
static const app_shell_app_t *s_wifi_setup_return_app;

typedef enum {
    CYD_WIFI_SETUP_APP_MODE_SCAN = 0,
    CYD_WIFI_SETUP_APP_MODE_PASSWORD,
} cyd_wifi_setup_app_mode_t;

typedef struct {
    bool active;
    cyd_wifi_setup_app_mode_t mode;
    esp32_wifi_sta_scan_record_t selected_record;
} cyd_wifi_setup_app_state_t;

static cyd_wifi_setup_app_state_t s_wifi_setup_app_state = { 0 };

static bool wifi_touch_event_confirmed_action(const cyd_input_event_t *event,
                                              wifi_touch_action_tracker_t *tracker,
                                              uint16_t *action_id)
{
    if (event == NULL || tracker == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    switch (event->data.touch.action) {
        case CYD_INPUT_TOUCH_ACTION_PRESS:
            tracker->pending = cyd_display_screen_hit_test(&s_wifi_setup_screen,
                                                           event->data.touch.x,
                                                           event->data.touch.y,
                                                           &tracker->action_id);
            tracker->long_pressed = false;
            return false;
        case CYD_INPUT_TOUCH_ACTION_LONG_PRESS:
        case CYD_INPUT_TOUCH_ACTION_REPEAT:
            if (tracker->pending) {
                tracker->long_pressed = true;
            }
            return false;
        case CYD_INPUT_TOUCH_ACTION_RELEASE: {
            uint16_t release_action_id = 0;
            bool confirmed = tracker->pending &&
                             !tracker->long_pressed &&
                             cyd_display_screen_hit_test(&s_wifi_setup_screen,
                                                         event->data.touch.x,
                                                         event->data.touch.y,
                                                         &release_action_id) &&
                             release_action_id == tracker->action_id;
            if (confirmed && action_id != NULL) {
                *action_id = release_action_id;
            }
            tracker->pending = false;
            tracker->long_pressed = false;
            tracker->action_id = 0;
            return confirmed;
        }
        default:
            return false;
    }
}

static size_t wifi_scan_session_page_count(const wifi_scan_session_t *session)
{
    if (session == NULL || session->record_count == 0) {
        return 1;
    }
    return (session->record_count + WIFI_SCAN_STATUS_LINE_COUNT - 1) / WIFI_SCAN_STATUS_LINE_COUNT;
}

/* `title` names the refresh in the log only; the view owns the wording. */
static esp_err_t wifi_show_scan_screen(const char *title,
                                       esp_err_t scan_ret,
                                       wifi_scan_session_t *session,
                                       bool scanning)
{
    cyd_wifi_setup_view_model_t model = {
        .screen = CYD_WIFI_SETUP_VIEW_SCAN,
        .scanning = scanning,
        .scan_failed = scan_ret != ESP_OK,
        .scan_error = scan_ret != ESP_OK ? esp_err_to_name(scan_ret) : NULL,
    };
    size_t page_count = 1;
    size_t first_record = 0;

    ESP_RETURN_ON_FALSE(title != NULL, ESP_ERR_INVALID_ARG, TAG, "title required");
    ESP_RETURN_ON_FALSE(session != NULL, ESP_ERR_INVALID_ARG, TAG, "scan session required");
    session->visible_count = 0;
    page_count = wifi_scan_session_page_count(session);
    if (session->page_index >= page_count) {
        session->page_index = page_count - 1;
    }
    first_record = session->page_index * WIFI_SCAN_STATUS_LINE_COUNT;

    if (scan_ret == ESP_OK && !scanning && session->record_count > 0) {
        size_t remaining_count = session->record_count - first_record;
        size_t count = remaining_count < WIFI_SCAN_STATUS_LINE_COUNT ? remaining_count : WIFI_SCAN_STATUS_LINE_COUNT;
        for (size_t i = 0; i < count; ++i) {
            model.aps[i] = (cyd_wifi_setup_view_ap_t){
                .ssid = session->records[first_record + i].ssid,
                .rssi = session->records[first_record + i].rssi,
            };
        }
        model.ap_count = count;
        session->visible_count = count;
    }
    model.page_index = session->page_index;
    model.page_count = page_count;
    cyd_wifi_setup_view_build(&s_wifi_setup_screen, &model);

    ESP_LOGI(TAG,
             "%s refresh %" PRIu32 ": %u APs page=%u/%u",
             title,
             session->scan_round,
             (unsigned)session->record_count,
             (unsigned)(session->page_index + 1),
             (unsigned)page_count);
    return cyd_ui_submit(&s_wifi_setup_screen);
}

static esp_err_t wifi_refresh_scan_session(wifi_scan_session_t *session)
{
    ESP_RETURN_ON_FALSE(session != NULL, ESP_ERR_INVALID_ARG, TAG, "scan session required");

    session->visible_count = 0;
    session->record_count = 0;
    session->page_index = 0;
    ESP_RETURN_ON_ERROR(wifi_show_scan_screen("Scanning...",
                                              ESP_OK,
                                              session,
                                              true),
                        TAG,
                        "show searching failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    /* The manager task runs the scan; this UI never operates the STA. */
    esp_err_t scan_ret = wifi_connection_setup_scan(session->records,
                                                    WIFI_SCAN_RECORD_CAPACITY,
                                                    &session->record_count);
    if (scan_ret != ESP_OK) {
        session->record_count = 0;
    }
    ESP_RETURN_ON_ERROR(wifi_show_scan_screen("Wi-Fi SSID list",
                                              scan_ret,
                                              session,
                                              false),
                        TAG,
                        "show scan list failed");
    session->scan_round++;
    return ESP_OK;
}

static void wifi_discard_pending_input(wifi_scan_session_t *session)
{
    if (session != NULL) {
        session->touch_tracker = (wifi_touch_action_tracker_t){ 0 };
    }

    (void)cyd_input_discard_pending_events();
}

static esp_err_t wifi_show_notice(cyd_wifi_setup_view_screen_t screen, const char *ssid)
{
    const cyd_wifi_setup_view_model_t model = {
        .screen = screen,
        .ssid = ssid,
    };
    cyd_wifi_setup_view_build(&s_wifi_setup_screen, &model);
    return cyd_ui_submit(&s_wifi_setup_screen);
}

static esp_err_t wifi_test_connect_and_save(const char *ssid,
                                            const char *password,
                                            wifi_auth_mode_t authmode,
                                            esp32_wifi_sta_failure_reason_t *failure_reason)
{
    ESP_RETURN_ON_ERROR(wifi_show_notice(CYD_WIFI_SETUP_VIEW_CONNECTING, ssid),
                        TAG,
                        "show connecting failed");
    ESP_RETURN_ON_ERROR(wifi_connection_connect_and_save(
                            ssid,
                            password,
                            authmode,
                            pdMS_TO_TICKS(CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS),
                            failure_reason),
                        TAG,
                        "Wi-Fi connect test failed");
    return wifi_show_notice(CYD_WIFI_SETUP_VIEW_SAVED, ssid);
}

static cyd_wifi_setup_view_failure_t wifi_view_failure(esp32_wifi_sta_failure_reason_t reason)
{
    switch (reason) {
    case ESP32_WIFI_STA_FAILURE_AUTH: return CYD_WIFI_SETUP_VIEW_FAILURE_AUTH;
    case ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE: return CYD_WIFI_SETUP_VIEW_FAILURE_NOT_FOUND;
    case ESP32_WIFI_STA_FAILURE_TIMEOUT: return CYD_WIFI_SETUP_VIEW_FAILURE_TIMEOUT;
    default: return CYD_WIFI_SETUP_VIEW_FAILURE_OTHER;
    }
}

static void wifi_wait_failed_dialog(const char *ssid, esp_err_t err, esp32_wifi_sta_failure_reason_t reason)
{
    const cyd_wifi_setup_view_model_t model = {
        .screen = CYD_WIFI_SETUP_VIEW_FAILED,
        .ssid = ssid,
        .failure = wifi_view_failure(reason),
        .error = esp_err_to_name(err),
    };
    wifi_touch_action_tracker_t touch_tracker = { 0 };

    /* Built into s_wifi_setup_screen, so taps are tested against the dialog.
       The scan screen is rebuilt there when the scan session restarts. */
    cyd_wifi_setup_view_build(&s_wifi_setup_screen, &model);
    esp_err_t submit_err = cyd_ui_submit(&s_wifi_setup_screen);
    if (submit_err != ESP_OK) {
        /* Not ESP_ERROR_CHECK: a failed draw must not reboot the device. With no
           OK button on screen there is nothing to wait for, so skip the dialog. */
        ESP_LOGW(TAG, "show failure dialog failed: %s", esp_err_to_name(submit_err));
        return;
    }
    while (true) {
        cyd_input_event_t event = { 0 };
        if (cyd_input_read_event(&event, pdMS_TO_TICKS(WIFI_IDLE_POLL_MS)) != ESP_OK) {
            if (app_shell_request_home_if_idle()) {
                return;
            }
            continue;
        }

        uint16_t action_id = 0;
        if (wifi_touch_event_confirmed_action(&event, &touch_tracker, &action_id) &&
            action_id == CYD_WIFI_SETUP_VIEW_ACTION_OK) {
            return;
        }
    }
}

void cyd_wifi_setup_begin_scan_session(void)
{
    memset(&s_scan_session, 0, sizeof(s_scan_session));
    s_scan_session.initialized = true;
    (void)wifi_refresh_scan_session(&s_scan_session);
}

esp_err_t cyd_wifi_setup_poll_scan_session(const cyd_input_event_t *event,
                                           bool *selected,
                                           bool *cancelled,
                                           esp32_wifi_sta_scan_record_t *selected_record)
{
    ESP_RETURN_ON_FALSE(selected != NULL, ESP_ERR_INVALID_ARG, TAG, "selected required");
    *selected = false;
    if (cancelled != NULL) {
        *cancelled = false;
    }

    if (!s_scan_session.initialized) {
        cyd_wifi_setup_begin_scan_session();
    }

    if (event == NULL) {
        return ESP_OK;
    }

    uint16_t action_id = 0;
    if (!wifi_touch_event_confirmed_action(event, &s_scan_session.touch_tracker, &action_id)) {
        return ESP_OK;
    }

    if (action_id == WIFI_ACTION_SCAN_BACK) {
        if (cancelled != NULL) {
            *cancelled = true;
        }
        return ESP_OK;
    }

    if (action_id == WIFI_ACTION_SCAN_REFRESH) {
        ESP_RETURN_ON_ERROR(wifi_refresh_scan_session(&s_scan_session), TAG, "manual scan failed");
        wifi_discard_pending_input(&s_scan_session);
        return ESP_OK;
    }

    if (action_id == WIFI_ACTION_SCAN_PREV) {
        if (s_scan_session.page_index > 0) {
            s_scan_session.page_index--;
            ESP_RETURN_ON_ERROR(wifi_show_scan_screen("Wi-Fi SSID list", ESP_OK, &s_scan_session, false),
                                TAG,
                                "show previous scan page failed");
        }
        return ESP_OK;
    }

    if (action_id == WIFI_ACTION_SCAN_NEXT) {
        size_t page_count = wifi_scan_session_page_count(&s_scan_session);
        if (s_scan_session.page_index + 1 < page_count) {
            s_scan_session.page_index++;
            ESP_RETURN_ON_ERROR(wifi_show_scan_screen("Wi-Fi SSID list", ESP_OK, &s_scan_session, false),
                                TAG,
                                "show next scan page failed");
        }
        return ESP_OK;
    }

    if (action_id >= WIFI_ACTION_SCAN_BASE &&
        action_id < WIFI_ACTION_SCAN_BASE + s_scan_session.visible_count) {
        size_t record_index = s_scan_session.page_index * WIFI_SCAN_STATUS_LINE_COUNT +
                              (action_id - WIFI_ACTION_SCAN_BASE);
        if (selected_record != NULL) {
            *selected_record = s_scan_session.records[record_index];
        }
        *selected = true;
    }

    return ESP_OK;
}

void cyd_wifi_setup_begin_password_session(const esp32_wifi_sta_scan_record_t *record)
{
    cyd_text_input_config_t config = {
        .title = "Wi-Fi のパスワード",
        .context_label = "SSID",
        .context_value = record != NULL ? record->ssid : "",
        .input_label = "パスワード",
        .initial_text = "",
        .max_len = 64,
        .obscure_input = true,
        .mode = CYD_TEXT_INPUT_MODE_PASSWORD,
    };

    memset(&s_password_session, 0, sizeof(s_password_session));
    s_password_session.initialized = true;
    if (record != NULL) {
        s_password_session.record = *record;
    }
    esp_err_t err = cyd_text_input_begin_session(&config);
    if (err != ESP_OK) {
        s_password_session.initialized = false;
        ESP_LOGE(TAG, "start password input failed: %s", esp_err_to_name(err));
    }
}

esp_err_t cyd_wifi_setup_poll_password_session(const cyd_input_event_t *event,
                                               cyd_wifi_setup_password_result_t *result)
{
    char password[65] = { 0 };
    cyd_text_input_result_t input_result = CYD_TEXT_INPUT_RESULT_CONTINUE;

    ESP_RETURN_ON_FALSE(result != NULL, ESP_ERR_INVALID_ARG, TAG, "result required");
    *result = CYD_WIFI_SETUP_PASSWORD_CONTINUE;
    ESP_RETURN_ON_FALSE(s_password_session.initialized, ESP_ERR_INVALID_STATE, TAG, "password session not initialized");

    ESP_RETURN_ON_ERROR(cyd_text_input_poll_session(event, &input_result, password, sizeof(password)),
                        TAG,
                        "password input failed");
    if (input_result == CYD_TEXT_INPUT_RESULT_CONTINUE) {
        return ESP_OK;
    }
    s_password_session.initialized = false;
    if (input_result == CYD_TEXT_INPUT_RESULT_CANCELLED) {
        *result = CYD_WIFI_SETUP_PASSWORD_CANCELLED;
        return ESP_OK;
    }
    if (input_result == CYD_TEXT_INPUT_RESULT_SAVED) {
        esp32_wifi_sta_failure_reason_t reason = ESP32_WIFI_STA_FAILURE_NONE;
        esp_err_t err = wifi_test_connect_and_save(s_password_session.record.ssid,
                                                   password,
                                                   s_password_session.record.authmode,
                                                   &reason);
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(800));
            *result = CYD_WIFI_SETUP_PASSWORD_CONNECTED;
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Wi-Fi SAVE failed: %s", esp_err_to_name(err));
        wifi_wait_failed_dialog(s_password_session.record.ssid, err, reason);
        *result = CYD_WIFI_SETUP_PASSWORD_CANCELLED;
        return ESP_OK;
    }
    return ESP_OK;
}

void cyd_wifi_setup_set_return_app(const app_shell_app_t *app)
{
    s_wifi_setup_return_app = app;
}

static esp_err_t cyd_wifi_setup_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;

    if (from_app != NULL) {
        s_wifi_setup_return_app = from_app;
    }

    if (!s_wifi_setup_app_state.active) {
        ESP_RETURN_ON_ERROR(wifi_connection_begin_setup(), TAG, "begin setup failed");
        s_wifi_setup_app_state.active = true;
    }

    s_wifi_setup_app_state.mode = CYD_WIFI_SETUP_APP_MODE_SCAN;
    memset(&s_wifi_setup_app_state.selected_record, 0, sizeof(s_wifi_setup_app_state.selected_record));
    cyd_wifi_setup_begin_scan_session();
    return ESP_OK;
}

static esp_err_t cyd_wifi_setup_app_leave(void *ctx)
{
    (void)ctx;

    if (!s_wifi_setup_app_state.active) {
        return ESP_OK;
    }

    wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;
    if (wifi_connection_get_state(&state) == ESP_OK && state == WIFI_CONNECTION_STATE_CONNECTED) {
        s_wifi_setup_app_state.active = false;
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(wifi_connection_complete_setup(false), TAG, "complete setup failed");
    s_wifi_setup_app_state.active = false;
    return ESP_OK;
}

static esp_err_t cyd_wifi_setup_switch_back(void)
{
    return app_shell_return_to(s_wifi_setup_return_app);
}

static esp_err_t cyd_wifi_setup_app_step(void *ctx)
{
    (void)ctx;

    if (s_wifi_setup_app_state.mode == CYD_WIFI_SETUP_APP_MODE_SCAN) {
        cyd_input_event_t event = { 0 };
        bool selected = false;
        bool cancelled = false;
        esp32_wifi_sta_scan_record_t selected_record = { 0 };

        if (cyd_input_read_event(&event, pdMS_TO_TICKS(50)) != ESP_OK) {
            return cyd_wifi_setup_poll_scan_session(NULL, &selected, &cancelled, NULL);
        }

        ESP_RETURN_ON_ERROR(cyd_wifi_setup_poll_scan_session(&event, &selected, &cancelled, &selected_record),
                            TAG,
                            "scan session failed");
        if (cancelled) {
            return cyd_wifi_setup_switch_back();
        }
        if (selected) {
            s_wifi_setup_app_state.selected_record = selected_record;
            cyd_wifi_setup_begin_password_session(&s_wifi_setup_app_state.selected_record);
            s_wifi_setup_app_state.mode = CYD_WIFI_SETUP_APP_MODE_PASSWORD;
        }
        return ESP_OK;
    }

    if (s_wifi_setup_app_state.mode == CYD_WIFI_SETUP_APP_MODE_PASSWORD) {
        cyd_input_event_t event = { 0 };
        cyd_wifi_setup_password_result_t result = CYD_WIFI_SETUP_PASSWORD_CONTINUE;

        if (cyd_input_read_event(&event, pdMS_TO_TICKS(WIFI_IDLE_POLL_MS)) != ESP_OK) {
            return cyd_wifi_setup_poll_password_session(NULL, &result);
        }

        ESP_RETURN_ON_ERROR(cyd_wifi_setup_poll_password_session(&event, &result),
                            TAG,
                            "password session failed");
        if (result == CYD_WIFI_SETUP_PASSWORD_CONNECTED) {
            ESP_RETURN_ON_ERROR(wifi_connection_complete_setup(true), TAG, "complete setup failed");
            s_wifi_setup_app_state.active = false;
            time_sync_request_soon_and_release_wifi();
            return cyd_wifi_setup_switch_back();
        }
        if (result == CYD_WIFI_SETUP_PASSWORD_CANCELLED) {
            s_wifi_setup_app_state.mode = CYD_WIFI_SETUP_APP_MODE_SCAN;
            cyd_wifi_setup_begin_scan_session();
        }
    }

    return ESP_OK;
}

static bool cyd_wifi_setup_idle_return_suppressed(void *ctx)
{
    (void)ctx;
    return s_wifi_setup_app_state.active;
}

static const app_shell_app_t s_cyd_wifi_setup_app = {
    .id = "wifi_setup",
    .ctx = NULL,
    .enter = cyd_wifi_setup_app_enter,
    .step = cyd_wifi_setup_app_step,
    .leave = cyd_wifi_setup_app_leave,
    .idle_return_suppressed = cyd_wifi_setup_idle_return_suppressed,
};

const app_shell_app_t *cyd_wifi_setup_get_app(void)
{
    return &s_cyd_wifi_setup_app;
}
