#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include "esp_log.h"
#include "app_scheduler.h"
#include "app_shell.h"
#include "cyd_clock_alarm.h"
#include "cyd_clock_settings_app.h"
#include "cyd_clock_settings_view.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_ui.h"

#define TAG "cyd_clock_settings"
#define CYD_CLOCK_SETTINGS_INPUT_POLL_MS 50
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_SUN (CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_BASE + 0)
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_MON (CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_BASE + 1)
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_TUE (CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_BASE + 2)
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_WED (CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_BASE + 3)
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_THU (CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_BASE + 4)
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_FRI (CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_BASE + 5)
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_SAT (CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_BASE + 6)

_Static_assert(APP_SCHEDULER_MAX_ENTRIES <= CYD_CLOCK_SETTINGS_VIEW_SCHEDULES_MAX,
               "the scheduler page must show every schedule");

typedef enum {
    CYD_CLOCK_SETTINGS_PAGE_ALARM1 = 0,
    CYD_CLOCK_SETTINGS_PAGE_ALARM2,
    CYD_CLOCK_SETTINGS_PAGE_SCHEDULER,
    CYD_CLOCK_SETTINGS_PAGE_COUNT,
} cyd_clock_settings_page_t;

typedef struct {
    bool pending;
    bool long_pressed;
    uint16_t action_id;
} cyd_clock_settings_touch_tracker_t;

static cyd_display_screen_t s_clock_settings_screen;
static const app_shell_app_t *s_clock_settings_return_app;
static cyd_clock_settings_page_t s_clock_settings_page = CYD_CLOCK_SETTINGS_PAGE_ALARM1;
static cyd_clock_settings_touch_tracker_t s_clock_settings_touch_tracker;

static bool cyd_clock_settings_touch_confirmed_action(const cyd_input_event_t *event,
                                                      cyd_clock_settings_touch_tracker_t *tracker,
                                                      uint16_t *action_id)
{
    if (event == NULL || tracker == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    switch (event->data.touch.action) {
    case CYD_INPUT_TOUCH_ACTION_PRESS:
        tracker->pending = cyd_display_screen_hit_test(&s_clock_settings_screen,
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
                         cyd_display_screen_hit_test(&s_clock_settings_screen,
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

static bool cyd_clock_settings_is_stepper_action(uint16_t action_id)
{
    switch (action_id) {
    case CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_DOWN:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_UP:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_DOWN:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_UP:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_DOWN:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_UP:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_DOWN:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_UP:
        return true;
    default:
        return false;
    }
}

static bool cyd_clock_settings_touch_stepper_action(const cyd_input_event_t *event, uint16_t *action_id)
{
    uint16_t pressed_action_id = 0;

    if (event == NULL || action_id == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    if (event->data.touch.action != CYD_INPUT_TOUCH_ACTION_PRESS &&
        event->data.touch.action != CYD_INPUT_TOUCH_ACTION_REPEAT) {
        return false;
    }

    if (!cyd_display_screen_hit_test(&s_clock_settings_screen,
                                     event->data.touch.x,
                                     event->data.touch.y,
                                     &pressed_action_id)) {
        return false;
    }

    if (!cyd_clock_settings_is_stepper_action(pressed_action_id)) {
        return false;
    }

    *action_id = pressed_action_id;
    return true;
}

static uint8_t cyd_clock_settings_wrap_u8_down(uint8_t value, uint8_t min_value, uint8_t max_value)
{
    return (value <= min_value) ? max_value : (uint8_t)(value - 1U);
}

static uint8_t cyd_clock_settings_wrap_u8_up(uint8_t value, uint8_t min_value, uint8_t max_value)
{
    return (value >= max_value) ? min_value : (uint8_t)(value + 1U);
}

static cyd_clock_settings_view_sched_state_t cyd_clock_settings_view_state(app_scheduler_state_t state)
{
    switch (state) {
    case APP_SCHEDULER_STATE_DISABLED: return CYD_CLOCK_SETTINGS_VIEW_SCHED_DISABLED;
    case APP_SCHEDULER_STATE_WAITING: return CYD_CLOCK_SETTINGS_VIEW_SCHED_WAITING;
    case APP_SCHEDULER_STATE_ACTIVE: return CYD_CLOCK_SETTINGS_VIEW_SCHED_ACTIVE;
    case APP_SCHEDULER_STATE_STOPPED: return CYD_CLOCK_SETTINGS_VIEW_SCHED_STOPPED;
    default: return CYD_CLOCK_SETTINGS_VIEW_SCHED_UNKNOWN;
    }
}

/* Statuses for the scheduler page; the model points into these. */
static app_scheduler_status_t s_clock_settings_statuses[APP_SCHEDULER_MAX_ENTRIES];

static void cyd_clock_settings_fill_scheduler(cyd_clock_settings_view_model_t *m)
{
    size_t count = 0;
    esp_err_t err = app_scheduler_list(s_clock_settings_statuses, APP_SCHEDULER_MAX_ENTRIES, &count);

    m->schedule_capacity = APP_SCHEDULER_MAX_ENTRIES;
    if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        m->schedules_unavailable = true;
        return;
    }
    m->schedule_count = count;
    for (size_t i = 0; i < count && i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        const app_scheduler_status_t *st = &s_clock_settings_statuses[i];
        m->schedules[i] = (cyd_clock_settings_view_schedule_t){
            .slot = (unsigned)st->slot_id + 1U,
            .owner = st->config.owner,
            .tag = st->config.tag,
            .window = st->config.mode == APP_SCHEDULER_MODE_WINDOW,
            .latched = st->config.behavior == APP_SCHEDULER_BEHAVIOR_LATCHED,
            .app_scope = st->config.scope != APP_SCHEDULER_SCOPE_FEATURE,
            .state = cyd_clock_settings_view_state(st->state),
            .at_hour = st->config.at.hour,
            .at_minute = st->config.at.minute,
            .to_hour = st->config.to.hour,
            .to_minute = st->config.to.minute,
        };
    }
}

static esp_err_t cyd_clock_settings_show(void)
{
    static const cyd_clock_settings_view_page_t pages[] = {
        [CYD_CLOCK_SETTINGS_PAGE_ALARM1] = CYD_CLOCK_SETTINGS_VIEW_ALARM1,
        [CYD_CLOCK_SETTINGS_PAGE_ALARM2] = CYD_CLOCK_SETTINGS_VIEW_ALARM2,
        [CYD_CLOCK_SETTINGS_PAGE_SCHEDULER] = CYD_CLOCK_SETTINGS_VIEW_SCHEDULER,
    };
    cyd_clock_settings_view_model_t model = { .page = pages[s_clock_settings_page] };

    if (s_clock_settings_page == CYD_CLOCK_SETTINGS_PAGE_SCHEDULER) {
        cyd_clock_settings_fill_scheduler(&model);
    } else {
        cyd_clock_alarm_config_t alarm = { 0 };
        cyd_clock_alarm_id_t id = s_clock_settings_page == CYD_CLOCK_SETTINGS_PAGE_ALARM1 ? CYD_CLOCK_ALARM_1
                                                                                         : CYD_CLOCK_ALARM_2;
        ESP_RETURN_ON_ERROR(cyd_clock_alarm_get(id, &alarm), TAG, "load alarm config failed");
        model.hour = alarm.hour;
        model.minute = alarm.minute;
        model.weekday_mask = alarm.weekday_mask;
    }
    cyd_clock_settings_view_build(&s_clock_settings_screen, &model);
    return cyd_ui_submit(&s_clock_settings_screen);
}

static esp_err_t cyd_clock_settings_handle_alarm_action(uint16_t action_id, bool *handled)
{
    cyd_clock_alarm_config_t alarm1 = { 0 };
    cyd_clock_alarm_config_t alarm2 = { 0 };

    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    ESP_RETURN_ON_ERROR(cyd_clock_alarm_get(CYD_CLOCK_ALARM_1, &alarm1),
                        TAG,
                        "get alarm1 config failed");
    ESP_RETURN_ON_ERROR(cyd_clock_alarm_get(CYD_CLOCK_ALARM_2, &alarm2),
                        TAG,
                        "get alarm2 config failed");

    if (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_DOWN ||
        action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_UP) {
        alarm1.hour = (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_DOWN)
                          ? cyd_clock_settings_wrap_u8_down(alarm1.hour, 0, 23)
                          : cyd_clock_settings_wrap_u8_up(alarm1.hour, 0, 23);
        ESP_RETURN_ON_ERROR(cyd_clock_alarm_set(CYD_CLOCK_ALARM_1, &alarm1),
                            TAG,
                            "set alarm1 hour failed");
        ESP_RETURN_ON_ERROR(cyd_clock_settings_show(), TAG, "refresh clock settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_DOWN ||
        action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_UP) {
        alarm1.minute = (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_DOWN)
                            ? cyd_clock_settings_wrap_u8_down(alarm1.minute, 0, 59)
                            : cyd_clock_settings_wrap_u8_up(alarm1.minute, 0, 59);
        ESP_RETURN_ON_ERROR(cyd_clock_alarm_set(CYD_CLOCK_ALARM_1, &alarm1),
                            TAG,
                            "set alarm1 minute failed");
        ESP_RETURN_ON_ERROR(cyd_clock_settings_show(), TAG, "refresh clock settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_DOWN ||
        action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_UP) {
        alarm2.hour = (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_DOWN)
                          ? cyd_clock_settings_wrap_u8_down(alarm2.hour, 0, 23)
                          : cyd_clock_settings_wrap_u8_up(alarm2.hour, 0, 23);
        ESP_RETURN_ON_ERROR(cyd_clock_alarm_set(CYD_CLOCK_ALARM_2, &alarm2),
                            TAG,
                            "set alarm2 hour failed");
        ESP_RETURN_ON_ERROR(cyd_clock_settings_show(), TAG, "refresh clock settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_DOWN ||
        action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_UP) {
        alarm2.minute = (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_DOWN)
                            ? cyd_clock_settings_wrap_u8_down(alarm2.minute, 0, 59)
                            : cyd_clock_settings_wrap_u8_up(alarm2.minute, 0, 59);
        ESP_RETURN_ON_ERROR(cyd_clock_alarm_set(CYD_CLOCK_ALARM_2, &alarm2),
                            TAG,
                            "set alarm2 minute failed");
        ESP_RETURN_ON_ERROR(cyd_clock_settings_show(), TAG, "refresh clock settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (s_clock_settings_page == CYD_CLOCK_SETTINGS_PAGE_ALARM1) {
        uint8_t toggle_mask = 0;

        switch (action_id) {
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_SUN:
            toggle_mask = APP_SCHEDULER_WEEKDAY_SUNDAY;
            break;
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_MON:
            toggle_mask = APP_SCHEDULER_WEEKDAY_MONDAY;
            break;
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_TUE:
            toggle_mask = APP_SCHEDULER_WEEKDAY_TUESDAY;
            break;
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_WED:
            toggle_mask = APP_SCHEDULER_WEEKDAY_WEDNESDAY;
            break;
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_THU:
            toggle_mask = APP_SCHEDULER_WEEKDAY_THURSDAY;
            break;
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_FRI:
            toggle_mask = APP_SCHEDULER_WEEKDAY_FRIDAY;
            break;
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_SAT:
            toggle_mask = APP_SCHEDULER_WEEKDAY_SATURDAY;
            break;
        default:
            break;
        }

        if (toggle_mask != 0) {
            alarm1.weekday_mask ^= toggle_mask;
            ESP_RETURN_ON_ERROR(cyd_clock_alarm_set(CYD_CLOCK_ALARM_1, &alarm1),
                                TAG,
                                "set alarm1 weekday mask failed");
            ESP_RETURN_ON_ERROR(cyd_clock_settings_show(), TAG, "refresh clock settings failed");
            *handled = true;
            return ESP_OK;
        }
    }

    return ESP_OK;
}

static esp_err_t cyd_clock_settings_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;

    ESP_RETURN_ON_FALSE(from_app != NULL, ESP_ERR_INVALID_STATE, TAG, "clock settings return app not set");
    s_clock_settings_return_app = from_app;
    s_clock_settings_page = CYD_CLOCK_SETTINGS_PAGE_ALARM1;
    s_clock_settings_touch_tracker = (cyd_clock_settings_touch_tracker_t){ 0 };
    return cyd_clock_settings_show();
}

static esp_err_t cyd_clock_settings_step(void *ctx)
{
    (void)ctx;

    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_CLOCK_SETTINGS_INPUT_POLL_MS)) == ESP_OK) {
        uint16_t action_id = 0;
        bool handled = false;

        if (cyd_clock_settings_touch_stepper_action(&event, &action_id)) {
            return cyd_clock_settings_handle_alarm_action(action_id, &handled);
        }

        if (!cyd_clock_settings_touch_confirmed_action(&event, &s_clock_settings_touch_tracker, &action_id)) {
            return ESP_OK;
        }

        if (action_id == CYD_CLOCK_SETTINGS_ACTION_PREV_PAGE &&
            s_clock_settings_page > CYD_CLOCK_SETTINGS_PAGE_ALARM1) {
            --s_clock_settings_page;
            return cyd_clock_settings_show();
        }
        if (action_id == CYD_CLOCK_SETTINGS_ACTION_NEXT_PAGE &&
            (size_t)s_clock_settings_page + 1 < CYD_CLOCK_SETTINGS_PAGE_COUNT) {
            s_clock_settings_page = (cyd_clock_settings_page_t)((size_t)s_clock_settings_page + 1U);
            return cyd_clock_settings_show();
        }
        if (action_id == CYD_CLOCK_SETTINGS_ACTION_BACK) {
            ESP_RETURN_ON_ERROR(app_shell_return_to(s_clock_settings_return_app), TAG, "switch back failed");
            return ESP_OK;
        }

        ESP_RETURN_ON_ERROR(cyd_clock_settings_handle_alarm_action(action_id, &handled),
                            TAG,
                            "handle alarm action failed");
    }

    return ESP_OK;
}

static const app_shell_app_t s_cyd_clock_settings_shell_app = {
    .id = "clock_settings",
    .ctx = NULL,
    .enter = cyd_clock_settings_enter,
    .step = cyd_clock_settings_step,
    .leave = NULL,
};

const app_shell_app_t *cyd_clock_settings_app_get_app(void)
{
    return &s_cyd_clock_settings_shell_app;
}
