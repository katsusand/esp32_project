#include <stdbool.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include "esp_log.h"
#include "app_launcher.h"
#include "app_launcher_view.h"
#include "app_registry.h"
#include "app_shell.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_ui.h"

#define TAG "app_launcher"
#define APP_LAUNCHER_INPUT_POLL_MS 50
#define APP_LAUNCHER_ROWS_PER_PAGE APP_LAUNCHER_VIEW_APPS_PER_PAGE

typedef struct {
    bool pending;
    uint16_t action_id;
} app_launcher_touch_tracker_t;

static cyd_display_screen_t s_launcher_screen;
static const app_shell_app_t *s_launcher_return_app;
static app_launcher_touch_tracker_t s_launcher_tracker;
static size_t s_launcher_page;

static bool app_launcher_touch_confirmed_action(const cyd_input_event_t *event,
                                                app_launcher_touch_tracker_t *tracker,
                                                uint16_t *action_id)
{
    if (event == NULL || tracker == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    switch (event->data.touch.action) {
    case CYD_INPUT_TOUCH_ACTION_PRESS:
        tracker->pending = cyd_display_screen_hit_test(&s_launcher_screen,
                                                       event->data.touch.x,
                                                       event->data.touch.y,
                                                       &tracker->action_id);
        return false;
    case CYD_INPUT_TOUCH_ACTION_RELEASE: {
        if (!tracker->pending) {
            return false;
        }
        tracker->pending = false;

        uint16_t released_action = 0;
        if (!cyd_display_screen_hit_test(&s_launcher_screen,
                                         event->data.touch.x,
                                         event->data.touch.y,
                                         &released_action) ||
            released_action != tracker->action_id) {
            return false;
        }
        if (action_id != NULL) {
            *action_id = released_action;
        }
        return true;
    }
    default:
        return false;
    }
}

static size_t app_launcher_page_count(void)
{
    size_t count = app_registry_count();
    if (count == 0) {
        return 1;
    }
    return (count + APP_LAUNCHER_ROWS_PER_PAGE - 1) / APP_LAUNCHER_ROWS_PER_PAGE;
}

static bool app_launcher_is_home(void)
{
    return app_shell_get_home_app() == app_launcher_get_app();
}

static esp_err_t app_launcher_show(void)
{
    size_t total = app_registry_count();
    size_t first = s_launcher_page * APP_LAUNCHER_ROWS_PER_PAGE;
    app_launcher_view_model_t model = {
        .total = total,
        .page_index = s_launcher_page,
        .page_count = app_launcher_page_count(),
        /* As home there is nothing above to go back to, so the control is
           omitted rather than drawn as a dead button. */
        .show_back = !app_launcher_is_home() && s_launcher_return_app != NULL,
    };

    for (size_t i = 0; i < APP_LAUNCHER_ROWS_PER_PAGE && first + i < total; ++i) {
        const app_registry_entry_t *entry = app_registry_at(first + i);
        model.titles[i] = entry != NULL ? entry->title : NULL;
        model.count = i + 1U;
    }
    app_launcher_view_build(&s_launcher_screen, &model);
    return cyd_ui_submit(&s_launcher_screen);
}

static esp_err_t app_launcher_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;

    if (from_app != NULL) {
        s_launcher_return_app = from_app;
    }
    s_launcher_tracker = (app_launcher_touch_tracker_t){ 0 };
    if (s_launcher_page >= app_launcher_page_count()) {
        s_launcher_page = 0;
    }
    return app_launcher_show();
}

static esp_err_t app_launcher_step(void *ctx)
{
    (void)ctx;

    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(APP_LAUNCHER_INPUT_POLL_MS)) != ESP_OK) {
        return ESP_OK;
    }

    uint16_t action_id = 0;
    if (!app_launcher_touch_confirmed_action(&event, &s_launcher_tracker, &action_id)) {
        return ESP_OK;
    }

    if (action_id == APP_LAUNCHER_ACTION_BACK) {
        ESP_RETURN_ON_ERROR(app_shell_return_to(s_launcher_return_app), TAG, "switch back failed");
        return ESP_OK;
    }

    /* Like the settings pages, navigation stops at both ends. */
    if (action_id == APP_LAUNCHER_ACTION_PREV_PAGE && s_launcher_page > 0) {
        s_launcher_page--;
        return app_launcher_show();
    }
    if (action_id == APP_LAUNCHER_ACTION_NEXT_PAGE && s_launcher_page + 1 < app_launcher_page_count()) {
        s_launcher_page++;
        return app_launcher_show();
    }

    if (action_id >= APP_LAUNCHER_ACTION_APP_BASE &&
        action_id < (APP_LAUNCHER_ACTION_APP_BASE + APP_LAUNCHER_ROWS_PER_PAGE)) {
        size_t index = (s_launcher_page * APP_LAUNCHER_ROWS_PER_PAGE) +
                       (size_t)(action_id - APP_LAUNCHER_ACTION_APP_BASE);
        const app_registry_entry_t *entry = app_registry_at(index);

        if (entry == NULL || entry->app == NULL) {
            return ESP_OK;
        }
        /* Selecting the launcher itself would leave the shell where it already
           is and re-run enter() for no reason. */
        if (entry->app == app_launcher_get_app()) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(app_shell_switch_to(entry->app), TAG, "switch to app failed");
    }

    return ESP_OK;
}

static const app_shell_app_t s_app_launcher_shell_app = {
    .id = "launcher",
    .ctx = NULL,
    .enter = app_launcher_enter,
    .step = app_launcher_step,
    .leave = NULL,
};

const app_shell_app_t *app_launcher_get_app(void)
{
    return &s_app_launcher_shell_app;
}
