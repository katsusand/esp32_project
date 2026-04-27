#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app_shell.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_ui.h"
#include "esp32_wifi_sta.h"
#include "cyd_wifi_setup.h"
#include "time_sync.h"
#include "wifi_manager.h"
#include "wifi_profile_store.h"

#define WIFI_SCAN_STATUS_LINE_COUNT 10
#define WIFI_SCAN_RECORD_CAPACITY CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE
#define WIFI_PASSWORD_MAX_LEN   64
#define WIFI_SCREEN_FIRST_LINE_ROW 4
#define WIFI_SCREEN_LINE_HEIGHT_ROWS 2
#define WIFI_KEY_ROW_0          9
#define WIFI_KEY_ROW_1          12
#define WIFI_KEY_ROW_2          15
#define WIFI_KEY_CONTROL_ROW    20
#define WIFI_KEY_BOTTOM_ROW     25
#define WIFI_KEY_HEIGHT_ROWS    2
#define WIFI_CONTROL_HEIGHT_ROWS 3
#define WIFI_BOTTOM_HEIGHT_ROWS 4
#define WIFI_KEY_WIDTH_COLS     4
#define WIFI_BACK_COL           0
#define WIFI_BACK_ROW           0
#define WIFI_BACK_WIDTH_COLS    6
#define WIFI_BACK_HEIGHT_ROWS   3
#define WIFI_TITLE_COL          8
#define WIFI_TITLE_ROW          0
#define WIFI_TITLE_WIDTH_COLS   32
#define WIFI_TITLE_HEIGHT_ROWS  2
#define WIFI_ACTION_SCAN_BASE   0x0100
#define WIFI_ACTION_KEY_BASE    0x0200
#define WIFI_ACTION_LOWER       0x0301
#define WIFI_ACTION_UPPER       0x0302
#define WIFI_ACTION_SYMBOL      0x0303
#define WIFI_ACTION_DELETE      0x0304
#define WIFI_ACTION_SPACE       0x0305
#define WIFI_ACTION_CANCEL      0x0306
#define WIFI_ACTION_SAVE        0x0307
#define WIFI_ACTION_TOGGLE_PASSWORD 0x0308
#define WIFI_ACTION_TOGGLE_CASE 0x0309
#define WIFI_ACTION_SCAN_BACK   0x030a
#define WIFI_ACTION_SCAN_REFRESH 0x030b
#define WIFI_ACTION_SCAN_PREV   0x030c
#define WIFI_ACTION_SCAN_NEXT   0x030d

#ifndef CONFIG_ESP32_WIFI_STA_SSID
#define CONFIG_ESP32_WIFI_STA_SSID ""
#endif
#ifndef CONFIG_ESP32_WIFI_STA_MAX_RETRY
#define CONFIG_ESP32_WIFI_STA_MAX_RETRY 5
#endif
#ifndef CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS
#define CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS 15000
#endif
#ifndef CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER
#define CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER ""
#endif

#define TAG "cyd_wifi_setup"

static cyd_display_screen_t s_wifi_setup_screen;
static portMUX_TYPE s_autoconnect_progress_lock = portMUX_INITIALIZER_UNLOCKED;
static cyd_wifi_setup_autoconnect_progress_t s_autoconnect_progress = {
    .phase = CYD_WIFI_SETUP_AUTOCONNECT_IDLE,
};

static void wifi_set_autoconnect_progress(cyd_wifi_setup_autoconnect_phase_t phase, const char *ssid)
{
    portENTER_CRITICAL(&s_autoconnect_progress_lock);
    s_autoconnect_progress.phase = phase;
    if (ssid != NULL) {
        snprintf(s_autoconnect_progress.ssid, sizeof(s_autoconnect_progress.ssid), "%.32s", ssid);
    } else {
        s_autoconnect_progress.ssid[0] = '\0';
    }
    portEXIT_CRITICAL(&s_autoconnect_progress_lock);
}

typedef enum {
    WIFI_KEYBOARD_LOWER = 0,
    WIFI_KEYBOARD_UPPER,
    WIFI_KEYBOARD_SYMBOL,
    WIFI_KEYBOARD_SYMBOL_EXTRA,
} wifi_keyboard_page_t;

typedef enum {
    WIFI_PASSWORD_ACTION_NONE = 0,
    WIFI_PASSWORD_ACTION_CANCEL,
    WIFI_PASSWORD_ACTION_SAVE,
    WIFI_PASSWORD_ACTION_DELETE,
    WIFI_PASSWORD_ACTION_SPACE,
    WIFI_PASSWORD_ACTION_LOWER,
    WIFI_PASSWORD_ACTION_UPPER,
    WIFI_PASSWORD_ACTION_SYMBOL,
    WIFI_PASSWORD_ACTION_TOGGLE_SHOW,
    WIFI_PASSWORD_ACTION_TOGGLE_CASE,
    WIFI_PASSWORD_ACTION_CHAR,
} wifi_password_action_t;

typedef struct {
    wifi_password_action_t action;
    char ch;
} wifi_password_key_t;

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
    wifi_profile_store_entry_t profile;
    esp32_wifi_sta_scan_record_t scan_record;
} wifi_saved_candidate_t;

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
    char password[WIFI_PASSWORD_MAX_LEN + 1];
    wifi_keyboard_page_t page;
    bool show_password;
    wifi_touch_action_tracker_t touch_tracker;
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
                                      enabled ? CYD_UI_COLOR_WHITE : CYD_UI_COLOR_DARKGREY,
                                      enabled ? CYD_UI_COLOR_BLUE : CYD_UI_COLOR_DIMGREY,
                                      enabled ? CYD_UI_COLOR_CYAN : CYD_UI_COLOR_DARKGREY,
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

static const char *wifi_keyboard_row(wifi_keyboard_page_t page, size_t row)
{
    static const char *lower_rows[] = { "qwertyuiop", "asdfghjkl", "zxcvbnm.-_" };
    static const char *upper_rows[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM.-_" };
    static const char *symbol_rows[] = { "1234567890", "~!@#$%^&*", "-_=+,.;:/?" };
    static const char *symbol_extra_rows[] = { "1234567890", "()[]{}<>|", "'\"`/" };

    if (row >= 3) {
        return "";
    }
    if (page == WIFI_KEYBOARD_UPPER) {
        return upper_rows[row];
    }
    if (page == WIFI_KEYBOARD_SYMBOL) {
        return symbol_rows[row];
    }
    if (page == WIFI_KEYBOARD_SYMBOL_EXTRA) {
        return symbol_extra_rows[row];
    }
    return lower_rows[row];
}

static uint8_t wifi_keyboard_row_col_offset(const char *keys, size_t row)
{
    (void)keys;
    return row == 1 ? 2 : 0;
}

static void wifi_mask_password(char *dst, size_t dst_size, const char *password)
{
    size_t password_len = password != NULL ? strlen(password) : 0;
    size_t visible_len = password_len < (dst_size - 1) ? password_len : (dst_size - 1);

    for (size_t i = 0; i < visible_len; ++i) {
        dst[i] = '*';
    }
    dst[visible_len] = '\0';
}

static esp_err_t wifi_show_password_screen(const char *ssid,
                                           const char *password,
                                           wifi_keyboard_page_t page,
                                           bool show_password)
{
    cyd_display_screen_t *screen = &s_wifi_setup_screen;
    char ssid_line[CYD_DISPLAY_TEXT_MAX_LEN + 1];
    char password_line[CYD_DISPLAY_TEXT_MAX_LEN + 1];
    char show_button_line[CYD_DISPLAY_TEXT_MAX_LEN + 1];
    char masked[WIFI_PASSWORD_MAX_LEN + 1];
    const bool symbol_page = page == WIFI_KEYBOARD_SYMBOL || page == WIFI_KEYBOARD_SYMBOL_EXTRA;
    const char *case_button_text = symbol_page ? "abc" : (page == WIFI_KEYBOARD_UPPER ? "abc" : "ABC");
    const char *symbol_button_text = page == WIFI_KEYBOARD_SYMBOL ? "()[]" : "123";
    const uint8_t row_starts[] = { WIFI_KEY_ROW_0, WIFI_KEY_ROW_1, WIFI_KEY_ROW_2 };

    snprintf(ssid_line, sizeof(ssid_line), "SSID: %.33s", ssid != NULL ? ssid : "");
    if (show_password) {
        snprintf(password_line, sizeof(password_line), "PASS: %.34s", password != NULL ? password : "");
    } else {
        wifi_mask_password(masked, sizeof(masked), password);
        snprintf(password_line, sizeof(password_line), "PASS: %.34s", masked);
    }
    snprintf(show_button_line, sizeof(show_button_line), "[%c] SHOW PASSWORD", show_password ? 'x' : ' ');
    cyd_ui_screen_clear(screen);

    cyd_ui_add_text(screen, "Wi-Fi Password", 0, 1, CYD_DISPLAY_GRID_COLS, 2, CYD_DISPLAY_ALIGN_CENTER, 2, CYD_UI_COLOR_YELLOW);
    cyd_ui_add_text(screen, ssid_line, 1, 4, CYD_DISPLAY_GRID_COLS - 2, 1, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    cyd_ui_add_text(screen, password_line, 1, 6, CYD_DISPLAY_GRID_COLS - 2, 1, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_CYAN);
    cyd_ui_add_button(screen,
                    show_button_line,
                    1,
                    7,
                    24,
                    2,
                    show_password ? CYD_UI_COLOR_BLUE : CYD_UI_COLOR_DARKGREY,
                    CYD_UI_COLOR_LIGHTGREY,
                    WIFI_ACTION_TOGGLE_PASSWORD);

    for (size_t row = 0; row < 3; ++row) {
        const char *keys = wifi_keyboard_row(page, row);
        uint8_t col_offset = wifi_keyboard_row_col_offset(keys, row);
        for (size_t i = 0; i < strlen(keys); ++i) {
            char key_text[2] = { keys[i], '\0' };
            cyd_ui_add_button(screen,
                            key_text,
                            (uint8_t)(col_offset + i * WIFI_KEY_WIDTH_COLS),
                            row_starts[row],
                            WIFI_KEY_WIDTH_COLS,
                            WIFI_KEY_HEIGHT_ROWS,
                            CYD_UI_COLOR_DARKGREY,
                            CYD_UI_COLOR_LIGHTGREY,
                            (uint16_t)(WIFI_ACTION_KEY_BASE + (uint8_t)keys[i]));
        }
    }

    cyd_ui_add_button(screen, case_button_text, 0, WIFI_KEY_CONTROL_ROW, 8, WIFI_CONTROL_HEIGHT_ROWS, CYD_UI_COLOR_BLUE, CYD_UI_COLOR_CYAN, WIFI_ACTION_TOGGLE_CASE);
    cyd_ui_add_button(screen, symbol_button_text, 8, WIFI_KEY_CONTROL_ROW, 8, WIFI_CONTROL_HEIGHT_ROWS, CYD_UI_COLOR_BLUE, CYD_UI_COLOR_CYAN, WIFI_ACTION_SYMBOL);
    cyd_ui_add_button(screen, "DEL", 16, WIFI_KEY_CONTROL_ROW, 8, WIFI_CONTROL_HEIGHT_ROWS, CYD_UI_COLOR_RED, CYD_UI_COLOR_LIGHTGREY, WIFI_ACTION_DELETE);
    cyd_ui_add_button(screen, "SPACE", 24, WIFI_KEY_CONTROL_ROW, 16, WIFI_CONTROL_HEIGHT_ROWS, CYD_UI_COLOR_DARKGREY, CYD_UI_COLOR_LIGHTGREY, WIFI_ACTION_SPACE);
    cyd_ui_add_button(screen, "CANCEL", 1, WIFI_KEY_BOTTOM_ROW, 18, WIFI_BOTTOM_HEIGHT_ROWS, CYD_UI_COLOR_DARKGREY, CYD_UI_COLOR_LIGHTGREY, WIFI_ACTION_CANCEL);
    cyd_ui_add_button(screen, "SAVE", 21, WIFI_KEY_BOTTOM_ROW, 18, WIFI_BOTTOM_HEIGHT_ROWS, CYD_UI_COLOR_GREEN, CYD_UI_COLOR_LIGHTGREY, WIFI_ACTION_SAVE);

    return cyd_ui_submit(screen);
}

static wifi_password_key_t wifi_password_key_from_action(uint16_t action_id, wifi_keyboard_page_t page)
{
    wifi_password_key_t key = { .action = WIFI_PASSWORD_ACTION_NONE, .ch = '\0' };
    (void)page;

    switch (action_id) {
        case WIFI_ACTION_CANCEL:
            key.action = WIFI_PASSWORD_ACTION_CANCEL;
            break;
        case WIFI_ACTION_SAVE:
            key.action = WIFI_PASSWORD_ACTION_SAVE;
            break;
        case WIFI_ACTION_DELETE:
            key.action = WIFI_PASSWORD_ACTION_DELETE;
            break;
        case WIFI_ACTION_SPACE:
            key.action = WIFI_PASSWORD_ACTION_SPACE;
            break;
        case WIFI_ACTION_LOWER:
            key.action = WIFI_PASSWORD_ACTION_LOWER;
            break;
        case WIFI_ACTION_UPPER:
            key.action = WIFI_PASSWORD_ACTION_UPPER;
            break;
        case WIFI_ACTION_SYMBOL:
            key.action = WIFI_PASSWORD_ACTION_SYMBOL;
            break;
        case WIFI_ACTION_TOGGLE_PASSWORD:
            key.action = WIFI_PASSWORD_ACTION_TOGGLE_SHOW;
            break;
        case WIFI_ACTION_TOGGLE_CASE:
            key.action = WIFI_PASSWORD_ACTION_TOGGLE_CASE;
            break;
        default:
            if (action_id >= WIFI_ACTION_KEY_BASE && action_id <= WIFI_ACTION_KEY_BASE + 0x7f) {
                key.action = WIFI_PASSWORD_ACTION_CHAR;
                key.ch = (char)(action_id - WIFI_ACTION_KEY_BASE);
            }
            break;
    }
    return key;
}

static esp_err_t wifi_test_connect_and_save(const char *ssid, const char *password)
{
    esp32_wifi_sta_config_t config = {
        .ssid = ssid,
        .password = password,
        .max_retry = CONFIG_ESP32_WIFI_STA_MAX_RETRY,
        .authmode_threshold = WIFI_AUTH_OPEN,
        .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        .sae_h2e_identifier = CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER,
    };

    ESP_RETURN_ON_ERROR(cyd_display_show_text("Wi-Fi", "Connecting..."), TAG, "show connecting failed");
    ESP_RETURN_ON_ERROR(esp32_wifi_sta_init_with_config(&config), TAG, "Wi-Fi config failed");
    ESP_RETURN_ON_ERROR(esp32_wifi_sta_start(), TAG, "Wi-Fi start failed");
    ESP_RETURN_ON_ERROR(esp32_wifi_sta_wait_connected(pdMS_TO_TICKS(CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS)),
                        TAG,
                        "Wi-Fi connect test failed");
    ESP_RETURN_ON_ERROR(esp32_wifi_sta_save_credentials(ssid, password), TAG, "Wi-Fi credential save failed");
    return cyd_display_show_text("Wi-Fi", "Saved");
}

static int wifi_compare_candidates(const wifi_saved_candidate_t *lhs, const wifi_saved_candidate_t *rhs)
{
    if (lhs->profile.last_success_seq != rhs->profile.last_success_seq) {
        return lhs->profile.last_success_seq > rhs->profile.last_success_seq ? -1 : 1;
    }
    if (lhs->scan_record.rssi != rhs->scan_record.rssi) {
        return lhs->scan_record.rssi > rhs->scan_record.rssi ? -1 : 1;
    }
    return strncmp(lhs->profile.ssid, rhs->profile.ssid, sizeof(lhs->profile.ssid));
}

static void wifi_sort_candidates(wifi_saved_candidate_t *candidates, size_t count)
{
    if (candidates == NULL) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (wifi_compare_candidates(&candidates[i], &candidates[j]) > 0) {
                wifi_saved_candidate_t tmp = candidates[i];
                candidates[i] = candidates[j];
                candidates[j] = tmp;
            }
        }
    }
}

static size_t wifi_collect_saved_candidates(wifi_saved_candidate_t *candidates,
                                            size_t candidate_capacity,
                                            esp32_wifi_sta_failure_reason_t *failure_reason)
{
    wifi_profile_store_entry_t profiles[WIFI_PROFILE_STORE_MAX_ENTRIES] = { 0 };
    esp32_wifi_sta_scan_record_t scan_records[CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE] = { 0 };
    size_t profile_count = 0;
    size_t visible_ap_count = 0;
    size_t candidate_count = 0;

    if (failure_reason != NULL) {
        *failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    }

    if (wifi_profile_store_load_entries(profiles, WIFI_PROFILE_STORE_MAX_ENTRIES, &profile_count) != ESP_OK ||
        profile_count == 0) {
        if (failure_reason != NULL) {
            *failure_reason = ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE;
        }
        return 0;
    }

    wifi_set_autoconnect_progress(CYD_WIFI_SETUP_AUTOCONNECT_SEARCHING, NULL);

    if (esp32_wifi_sta_enter_scan_mode() != ESP_OK ||
        esp32_wifi_sta_get_scan_records(scan_records,
                                        CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE,
                                        &visible_ap_count) != ESP_OK) {
        if (failure_reason != NULL) {
            *failure_reason = ESP32_WIFI_STA_FAILURE_TIMEOUT;
        }
        return 0;
    }

    for (size_t i = 0; i < profile_count && candidate_count < candidate_capacity; ++i) {
        for (size_t j = 0; j < visible_ap_count; ++j) {
            if (strncmp(profiles[i].ssid, scan_records[j].ssid, sizeof(profiles[i].ssid)) != 0) {
                continue;
            }
            candidates[candidate_count].profile = profiles[i];
            candidates[candidate_count].scan_record = scan_records[j];
            ++candidate_count;
            break;
        }
    }

    wifi_sort_candidates(candidates, candidate_count);
    if (candidate_count == 0 && failure_reason != NULL) {
        *failure_reason = ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE;
    }
    return candidate_count;
}

static esp_err_t wifi_connect_saved_candidate(const wifi_saved_candidate_t *candidate,
                                              TickType_t wait_ticks,
                                              esp32_wifi_sta_failure_reason_t *failure_reason)
{
    esp32_wifi_sta_config_t config = {
        .ssid = candidate->profile.ssid,
        .password = candidate->profile.password,
        .max_retry = CONFIG_ESP32_WIFI_STA_MAX_RETRY,
        .authmode_threshold = WIFI_AUTH_OPEN,
        .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        .sae_h2e_identifier = CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER,
    };

    wifi_set_autoconnect_progress(CYD_WIFI_SETUP_AUTOCONNECT_TRYING, candidate->profile.ssid);

    esp_err_t err = esp32_wifi_sta_init_with_config(&config);
    if (err == ESP_OK) {
        err = esp32_wifi_sta_start();
    }
    if (err == ESP_OK && wait_ticks > 0) {
        err = esp32_wifi_sta_wait_connected(wait_ticks);
    }
    if (err == ESP_OK) {
        err = wifi_profile_store_record_success(candidate->profile.ssid,
                                                candidate->profile.password,
                                                candidate->scan_record.authmode);
    }
    if (err != ESP_OK && failure_reason != NULL) {
        *failure_reason = esp32_wifi_sta_get_last_failure_reason();
    }
    return err;
}

static void wifi_wait_ok_dialog(const char *title, const char *message)
{
    const char *lines[] = { message };
    const char *buttons[] = { "OK" };
    wifi_touch_mode_button_tracker_t touch_tracker = { 0 };

    ESP_ERROR_CHECK(cyd_display_show_mode_screen(title, lines, 1, buttons, 1, 0));
    while (true) {
        cyd_input_event_t event = { 0 };
        if (cyd_input_read_event(&event, portMAX_DELAY) != ESP_OK) {
            continue;
        }

        size_t button_index = 0;
        if (wifi_touch_event_confirmed_mode_button(&event, &touch_tracker, &button_index)) {
            return;
        }
    }
}

esp_err_t cyd_wifi_setup_connect_configured(TickType_t wait_ticks,
                                            esp32_wifi_sta_failure_reason_t *failure_reason)
{
    wifi_saved_candidate_t candidates[WIFI_PROFILE_STORE_MAX_ENTRIES] = { 0 };
    esp32_wifi_sta_failure_reason_t local_reason = ESP32_WIFI_STA_FAILURE_NONE;
    size_t candidate_count = wifi_collect_saved_candidates(candidates,
                                                           WIFI_PROFILE_STORE_MAX_ENTRIES,
                                                           &local_reason);

    if (failure_reason != NULL) {
        *failure_reason = local_reason;
    }

    if (candidate_count == 0) {
        wifi_set_autoconnect_progress(CYD_WIFI_SETUP_AUTOCONNECT_IDLE, NULL);
        return local_reason == ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    for (size_t i = 0; i < candidate_count; ++i) {
        esp_err_t err = wifi_connect_saved_candidate(&candidates[i], wait_ticks, &local_reason);
        if (failure_reason != NULL) {
            *failure_reason = local_reason;
        }
        if (err == ESP_OK) {
            wifi_set_autoconnect_progress(CYD_WIFI_SETUP_AUTOCONNECT_IDLE, NULL);
            return ESP_OK;
        }
    }

    wifi_set_autoconnect_progress(CYD_WIFI_SETUP_AUTOCONNECT_IDLE, NULL);
    return ESP_FAIL;
}

cyd_wifi_setup_autoconnect_progress_t cyd_wifi_setup_get_autoconnect_progress(void)
{
    cyd_wifi_setup_autoconnect_progress_t progress = { 0 };

    portENTER_CRITICAL(&s_autoconnect_progress_lock);
    progress = s_autoconnect_progress;
    portEXIT_CRITICAL(&s_autoconnect_progress_lock);
    return progress;
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
    memset(&s_password_session, 0, sizeof(s_password_session));
    s_password_session.initialized = true;
    s_password_session.page = WIFI_KEYBOARD_LOWER;
    if (record != NULL) {
        s_password_session.record = *record;
    }
    (void)wifi_show_password_screen(s_password_session.record.ssid,
                                    s_password_session.password,
                                    s_password_session.page,
                                    s_password_session.show_password);
}

esp_err_t cyd_wifi_setup_poll_password_session(const cyd_input_event_t *event,
                                               cyd_wifi_setup_password_result_t *result)
{
    ESP_RETURN_ON_FALSE(result != NULL, ESP_ERR_INVALID_ARG, TAG, "result required");
    *result = CYD_WIFI_SETUP_PASSWORD_CONTINUE;
    ESP_RETURN_ON_FALSE(s_password_session.initialized, ESP_ERR_INVALID_STATE, TAG, "password session not initialized");

    if (event == NULL) {
        return ESP_OK;
    }

    uint16_t action_id = 0;
    if (!wifi_touch_event_confirmed_action(event, &s_password_session.touch_tracker, &action_id)) {
        return ESP_OK;
    }

    wifi_password_key_t key = wifi_password_key_from_action(action_id, s_password_session.page);
    size_t password_len = strlen(s_password_session.password);

    switch (key.action) {
    case WIFI_PASSWORD_ACTION_CANCEL:
        *result = CYD_WIFI_SETUP_PASSWORD_CANCELLED;
        return ESP_OK;
    case WIFI_PASSWORD_ACTION_SAVE: {
        esp_err_t err = wifi_test_connect_and_save(s_password_session.record.ssid, s_password_session.password);
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
    case WIFI_PASSWORD_ACTION_DELETE:
        if (password_len > 0) {
            s_password_session.password[password_len - 1] = '\0';
        }
        break;
    case WIFI_PASSWORD_ACTION_SPACE:
        if (password_len < WIFI_PASSWORD_MAX_LEN) {
            s_password_session.password[password_len] = ' ';
            s_password_session.password[password_len + 1] = '\0';
        }
        break;
    case WIFI_PASSWORD_ACTION_LOWER:
        s_password_session.page = WIFI_KEYBOARD_LOWER;
        break;
    case WIFI_PASSWORD_ACTION_UPPER:
        s_password_session.page = WIFI_KEYBOARD_UPPER;
        break;
    case WIFI_PASSWORD_ACTION_SYMBOL:
        s_password_session.page = s_password_session.page == WIFI_KEYBOARD_SYMBOL ? WIFI_KEYBOARD_SYMBOL_EXTRA : WIFI_KEYBOARD_SYMBOL;
        break;
    case WIFI_PASSWORD_ACTION_TOGGLE_SHOW:
        s_password_session.show_password = !s_password_session.show_password;
        break;
    case WIFI_PASSWORD_ACTION_TOGGLE_CASE:
        if (s_password_session.page == WIFI_KEYBOARD_SYMBOL || s_password_session.page == WIFI_KEYBOARD_SYMBOL_EXTRA) {
            s_password_session.page = WIFI_KEYBOARD_LOWER;
        } else {
            s_password_session.page = s_password_session.page == WIFI_KEYBOARD_UPPER ? WIFI_KEYBOARD_LOWER : WIFI_KEYBOARD_UPPER;
        }
        break;
    case WIFI_PASSWORD_ACTION_CHAR:
        if (password_len < WIFI_PASSWORD_MAX_LEN) {
            s_password_session.password[password_len] = key.ch;
            s_password_session.password[password_len + 1] = '\0';
        }
        break;
    case WIFI_PASSWORD_ACTION_NONE:
    default:
        break;
    }

    ESP_RETURN_ON_ERROR(wifi_show_password_screen(s_password_session.record.ssid,
                                                  s_password_session.password,
                                                  s_password_session.page,
                                                  s_password_session.show_password),
                        TAG,
                        "show password failed");
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
        ESP_RETURN_ON_ERROR(wifi_manager_begin_setup(), TAG, "begin setup failed");
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

    wifi_manager_state_t state = WIFI_MANAGER_STATE_STOPPED;
    if (wifi_manager_get_state(&state) == ESP_OK && state == WIFI_MANAGER_STATE_CONNECTED) {
        s_wifi_setup_app_state.active = false;
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(wifi_manager_complete_setup(false), TAG, "complete setup failed");
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

        if (cyd_input_read_event(&event, portMAX_DELAY) != ESP_OK) {
            return ESP_OK;
        }

        ESP_RETURN_ON_ERROR(cyd_wifi_setup_poll_password_session(&event, &result),
                            TAG,
                            "password session failed");
        if (result == CYD_WIFI_SETUP_PASSWORD_CONNECTED) {
            ESP_RETURN_ON_ERROR(wifi_manager_complete_setup(true), TAG, "complete setup failed");
            s_wifi_setup_app_state.active = false;
            time_sync_request_soon();
            return cyd_wifi_setup_switch_back();
        }
        if (result == CYD_WIFI_SETUP_PASSWORD_CANCELLED) {
            s_wifi_setup_app_state.mode = CYD_WIFI_SETUP_APP_MODE_SCAN;
            cyd_wifi_setup_begin_scan_session();
        }
    }

    return ESP_OK;
}

static const app_shell_app_t s_cyd_wifi_setup_app = {
    .id = "wifi_setup",
    .ctx = NULL,
    .enter = cyd_wifi_setup_app_enter,
    .step = cyd_wifi_setup_app_step,
    .leave = cyd_wifi_setup_app_leave,
};

const app_shell_app_t *cyd_wifi_setup_get_app(void)
{
    return &s_cyd_wifi_setup_app;
}
