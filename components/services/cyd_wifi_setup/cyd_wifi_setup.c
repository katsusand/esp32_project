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
#include "time_sync.h"
#include "wifi_connection.h"
#include "wifi_connection.h"

#define WIFI_SCAN_STATUS_LINE_COUNT 10
#define WIFI_SCAN_RECORD_CAPACITY CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE
#define WIFI_SCREEN_FIRST_LINE_ROW 4
#define WIFI_SCREEN_LINE_HEIGHT_ROWS 2
#define WIFI_BACK_COL           0
#define WIFI_BACK_ROW           0
#define WIFI_BACK_WIDTH_COLS    6
#define WIFI_BACK_HEIGHT_ROWS   3
#define WIFI_TITLE_COL          8
#define WIFI_TITLE_ROW          0
#define WIFI_TITLE_WIDTH_COLS   32
#define WIFI_TITLE_HEIGHT_ROWS  2
#define WIFI_ACTION_SCAN_BASE   0x0100
#define WIFI_ACTION_SCAN_BACK   0x030a
#define WIFI_ACTION_SCAN_REFRESH 0x030b
#define WIFI_ACTION_SCAN_PREV   0x030c
#define WIFI_ACTION_SCAN_NEXT   0x030d
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
    bool pending;
    bool long_pressed;
    size_t button_index;
} wifi_touch_mode_button_tracker_t;

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
            tracker->pending = cyd_display_hit_test_action(event->data.touch.x,
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
                             cyd_display_hit_test_action(event->data.touch.x,
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

static bool wifi_touch_event_confirmed_mode_button(const cyd_input_event_t *event,
                                                   wifi_touch_mode_button_tracker_t *tracker,
                                                   size_t *button_index)
{
    if (event == NULL || tracker == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    switch (event->data.touch.action) {
        case CYD_INPUT_TOUCH_ACTION_PRESS:
            tracker->pending = cyd_display_hit_test_mode_button(event->data.touch.x,
                                                                event->data.touch.y,
                                                                &tracker->button_index);
            tracker->long_pressed = false;
            return false;
        case CYD_INPUT_TOUCH_ACTION_LONG_PRESS:
        case CYD_INPUT_TOUCH_ACTION_REPEAT:
            if (tracker->pending) {
                tracker->long_pressed = true;
            }
            return false;
        case CYD_INPUT_TOUCH_ACTION_RELEASE: {
            size_t release_button_index = 0;
            bool confirmed = tracker->pending &&
                             !tracker->long_pressed &&
                             cyd_display_hit_test_mode_button(event->data.touch.x,
                                                              event->data.touch.y,
                                                              &release_button_index) &&
                             release_button_index == tracker->button_index;
            if (confirmed && button_index != NULL) {
                *button_index = release_button_index;
            }
            tracker->pending = false;
            tracker->long_pressed = false;
            tracker->button_index = 0;
            return confirmed;
        }
        default:
            return false;
    }
}

static void wifi_format_scan_line(char *line, size_t line_size, const esp32_wifi_sta_scan_record_t *record)
{
    snprintf(line,
             line_size,
             "%-24s %4d ch%02u",
             record->ssid,
             record->rssi,
             (unsigned)record->channel);
}

static size_t wifi_scan_session_page_count(const wifi_scan_session_t *session)
{
    if (session == NULL || session->record_count == 0) {
        return 1;
    }
    return (session->record_count + WIFI_SCAN_STATUS_LINE_COUNT - 1) / WIFI_SCAN_STATUS_LINE_COUNT;
}

static void wifi_add_scan_control_button(cyd_display_screen_t *screen,
                                         const char *text,
                                         uint8_t col,
                                         uint16_t action_id,
                                         bool enabled)
{
    cyd_ui_add_button_with_fg_enabled(screen,
                                      text,
                                      col,
                                      26,
                                      12,
                                      3,
                                      CYD_UI_COLOR_WHITE,
                                      CYD_UI_COLOR_BLUE,
                                      CYD_UI_COLOR_CYAN,
                                      action_id,
                                      enabled);
}

static esp_err_t wifi_show_scan_screen(const char *title,
                                       esp_err_t scan_ret,
                                       wifi_scan_session_t *session,
                                       bool scanning)
{
    cyd_display_screen_t *screen = &s_wifi_setup_screen;
    char line_storage[WIFI_SCAN_STATUS_LINE_COUNT][CYD_DISPLAY_TEXT_MAX_LEN + 1];
    char title_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
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

    cyd_ui_screen_clear(screen);

    snprintf(title_line,
             sizeof(title_line),
             "%s %u/%u",
             title,
             (unsigned)(session->page_index + 1),
             (unsigned)page_count);
    cyd_ui_add_text(screen,
                    title_line,
                    WIFI_TITLE_COL,
                    WIFI_TITLE_ROW,
                    WIFI_TITLE_WIDTH_COLS,
                    WIFI_TITLE_HEIGHT_ROWS,
                    CYD_DISPLAY_ALIGN_RIGHT,
                    2,
                    CYD_UI_COLOR_YELLOW);

    if (scan_ret != ESP_OK) {
        snprintf(line_storage[0], sizeof(line_storage[0]), "scan error: %s", esp_err_to_name(scan_ret));
        cyd_ui_add_text(screen, line_storage[0], 1, 14, CYD_DISPLAY_GRID_COLS - 2, 1, CYD_DISPLAY_ALIGN_CENTER, 1, CYD_UI_COLOR_WHITE);
    } else {
        if (scanning) {
            cyd_ui_add_text(screen, "searching...", 1, 14, CYD_DISPLAY_GRID_COLS - 2, 1, CYD_DISPLAY_ALIGN_CENTER, 1, CYD_UI_COLOR_WHITE);
        } else if (session->record_count == 0) {
            cyd_ui_add_text(screen, "no AP found", 1, 14, CYD_DISPLAY_GRID_COLS - 2, 1, CYD_DISPLAY_ALIGN_CENTER, 1, CYD_UI_COLOR_WHITE);
        } else {
            size_t remaining_count = session->record_count - first_record;
            size_t count = remaining_count < WIFI_SCAN_STATUS_LINE_COUNT ? remaining_count : WIFI_SCAN_STATUS_LINE_COUNT;
            for (size_t i = 0; i < count; ++i) {
                wifi_format_scan_line(line_storage[i], sizeof(line_storage[i]), &session->records[first_record + i]);
                cyd_ui_add_button(screen,
                                line_storage[i],
                                1,
                                (uint8_t)(WIFI_SCREEN_FIRST_LINE_ROW + i * WIFI_SCREEN_LINE_HEIGHT_ROWS),
                                CYD_DISPLAY_GRID_COLS - 2,
                                WIFI_SCREEN_LINE_HEIGHT_ROWS,
                                CYD_UI_COLOR_DARKGREY,
                                CYD_UI_COLOR_LIGHTGREY,
                                (uint16_t)(WIFI_ACTION_SCAN_BASE + i));
            }
            session->visible_count = count;
        }
    }

    cyd_ui_add_button(screen,
                      "<<",
                      WIFI_BACK_COL,
                      WIFI_BACK_ROW,
                      WIFI_BACK_WIDTH_COLS,
                      WIFI_BACK_HEIGHT_ROWS,
                      CYD_UI_COLOR_BLUE,
                      CYD_UI_COLOR_CYAN,
                      WIFI_ACTION_SCAN_BACK);
    wifi_add_scan_control_button(screen, "SCAN", 1, WIFI_ACTION_SCAN_REFRESH, !scanning);
    wifi_add_scan_control_button(screen, "<", 14, WIFI_ACTION_SCAN_PREV, !scanning && session->page_index > 0);
    wifi_add_scan_control_button(screen, ">", 27, WIFI_ACTION_SCAN_NEXT, !scanning && session->page_index + 1 < page_count);

    ESP_LOGI(TAG,
             "%s refresh %" PRIu32 ": %u APs page=%u/%u",
             title,
             session->scan_round,
             (unsigned)session->record_count,
             (unsigned)(session->page_index + 1),
             (unsigned)page_count);
    return cyd_ui_submit(screen);
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

    esp_err_t scan_ret = esp32_wifi_sta_enter_scan_mode();
    if (scan_ret == ESP_OK) {
        esp_err_t records_ret = esp32_wifi_sta_get_scan_records(session->records,
                                                                WIFI_SCAN_RECORD_CAPACITY,
                                                                &session->record_count);
        if (records_ret != ESP_OK) {
            scan_ret = records_ret;
            session->record_count = 0;
        }
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

static esp_err_t wifi_test_connect_and_save(const char *ssid,
                                            const char *password,
                                            wifi_auth_mode_t authmode)
{
    ESP_RETURN_ON_ERROR(cyd_display_show_text("Wi-Fi", "Connecting..."), TAG, "show connecting failed");
    ESP_RETURN_ON_ERROR(wifi_connection_connect_and_save(
                            ssid,
                            password,
                            authmode,
                            pdMS_TO_TICKS(CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS),
                            NULL),
                        TAG,
                        "Wi-Fi connect test failed");
    return cyd_display_show_text("Wi-Fi", "Saved");
}

static void wifi_wait_ok_dialog(const char *title, const char *message)
{
    const char *lines[] = { message };
    const char *buttons[] = { "OK" };
    wifi_touch_mode_button_tracker_t touch_tracker = { 0 };

    ESP_ERROR_CHECK(cyd_display_show_mode_screen(title, lines, 1, buttons, 1, 0));
    while (true) {
        cyd_input_event_t event = { 0 };
        if (cyd_input_read_event(&event, pdMS_TO_TICKS(WIFI_IDLE_POLL_MS)) != ESP_OK) {
            if (app_shell_request_home_if_idle()) {
                return;
            }
            continue;
        }

        size_t button_index = 0;
        if (wifi_touch_event_confirmed_mode_button(&event, &touch_tracker, &button_index)) {
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
        .title = "Wi-Fi Password",
        .context_label = "SSID:",
        .context_value = record != NULL ? record->ssid : "",
        .input_label = "PASS:",
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
        esp_err_t err = wifi_test_connect_and_save(s_password_session.record.ssid,
                                                   password,
                                                   s_password_session.record.authmode);
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(800));
            *result = CYD_WIFI_SETUP_PASSWORD_CONNECTED;
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Wi-Fi SAVE failed: %s", esp_err_to_name(err));
        wifi_wait_ok_dialog("Wi-Fi Failed", esp_err_to_name(err));
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
    ESP_RETURN_ON_FALSE(s_wifi_setup_return_app != NULL, ESP_ERR_INVALID_STATE, TAG, "return app not set");
    return app_shell_switch_to(s_wifi_setup_return_app);
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
