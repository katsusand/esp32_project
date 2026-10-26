#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "app_registry.h"
#include "app_shell.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_system_apps.h"
#include "cyd_system_apps_internal.h"
#include "cyd_ui.h"
#include "cyd_wifi_setup.h"
#include "settings_pin.h"
#include "settings_pin_internal.h"
#include "settings_pin_logic.h"
#include "settings_pin_store.h"
#include "settings_pin_view.h"

#define TAG "settings_pin"

/*
 * Who is guarded, and what "inside settings" means.
 *
 * Guarded apps show the PIN screen first. The area is the guarded apps plus
 * the screens they open that hold nothing worth guarding on their own (touch
 * calibration, the PIN screen itself, the recovery provider). Leaving the area
 * closes the PIN again; moving inside it does not. Every app switch passes the
 * enter guard, which is how leaving is noticed.
 *
 * English contract: while the PIN is off the guard lets everything through and
 * changes nothing, so a device without a PIN behaves exactly as before.
 */

typedef enum {
    SETTINGS_PIN_FLOW_NONE = 0,
    SETTINGS_PIN_FLOW_UNLOCK,  /* in front of `target`; continues there with `origin` */
    SETTINGS_PIN_FLOW_NEW_PIN, /* opened from the settings page; goes back there */
} settings_pin_flow_t;

static portMUX_TYPE s_pin_lock = portMUX_INITIALIZER_UNLOCKED;

/* Stored state, loaded once. */
static bool s_pin_loaded;
static settings_pin_store_state_t s_pin_state = SETTINGS_PIN_STORE_OFF;
static settings_pin_store_config_t s_pin_config;
static uint32_t s_pin_failures;
static int64_t s_pin_wait_until_us;

/* The session: true from a correct PIN until the user leaves the area. */
static bool s_pin_unlocked;

/* The PIN screen's current job. */
static settings_pin_flow_t s_pin_flow;
static const app_shell_app_t *s_pin_target;
static const app_shell_app_t *s_pin_origin;
static bool s_pin_recovered; /* guarded by s_pin_lock: the provider may call from its own task */

/* The PIN screen itself. */
static settings_pin_view_screen_t s_pin_screen_kind;
static settings_pin_view_notice_t s_pin_notice;
static settings_pin_entry_t s_pin_entry;
static settings_pin_setup_t s_pin_setup;
static uint32_t s_pin_shown_wait;
static cyd_system_apps_touch_tracker_t s_pin_tracker;
static const cyd_ui_screen_owner_t s_pin_screen_owner = { .name = "settings_pin" };
static settings_pin_view_model_t s_pin_model;

static const settings_pin_recovery_provider_t *s_pin_provider;
static void *s_pin_provider_ctx;

/* Apps that asked for the PIN themselves (settings_pin_guard_app()). */
typedef struct {
    const app_shell_app_t *app;
    const char *title;
} settings_pin_extra_t;
static settings_pin_extra_t s_pin_extra[SETTINGS_PIN_GUARDED_APPS_MAX];

static const app_shell_app_t s_settings_pin_app;

/* ---- stored state ---------------------------------------------------------- */

static void settings_pin_ensure_loaded(void)
{
    if (s_pin_loaded) {
        return;
    }
    s_pin_loaded = true;

    esp_err_t err = settings_pin_store_load(&s_pin_config, &s_pin_state);
    if (err != ESP_OK) {
        /* Locked, not open: an unreadable PIN must not read as "no PIN". */
        ESP_LOGE(TAG, "reading the settings PIN failed: %s; treating it as locked", esp_err_to_name(err));
        s_pin_state = SETTINGS_PIN_STORE_CORRUPT;
    }
    if (s_pin_state != SETTINGS_PIN_STORE_ON) {
        return;
    }

    err = settings_pin_store_load_failures(&s_pin_failures);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reading the failure count failed: %s", esp_err_to_name(err));
    }
    /* The clock is not trustworthy across a restart, so a wait that was
       running starts again in full. Restarting never shortens it. */
    uint32_t wait = settings_pin_wait_seconds(s_pin_failures);
    if (wait > 0) {
        s_pin_wait_until_us = esp_timer_get_time() + (int64_t)wait * 1000000LL;
        ESP_LOGW(TAG, "%u wrong PINs in a row before this boot; waiting %u s", (unsigned)s_pin_failures,
                 (unsigned)wait);
    }
}

static uint32_t settings_pin_wait_remaining(void)
{
    int64_t left_us = s_pin_wait_until_us - esp_timer_get_time();

    if (left_us <= 0) {
        return 0;
    }
    return (uint32_t)((left_us + 999999LL) / 1000000LL);
}

static void settings_pin_save_failures(void)
{
    esp_err_t err = settings_pin_store_save_failures(s_pin_failures);
    if (err != ESP_OK) {
        /* The count still holds for this session; only a restart could reset it. */
        ESP_LOGW(TAG, "saving the failure count failed: %s", esp_err_to_name(err));
    }
}

/* ---- who is guarded -------------------------------------------------------- */

static bool settings_pin_is_app_settings_screen(const app_shell_app_t *app)
{
    for (size_t i = 0; i < app_registry_count(); ++i) {
        const app_registry_entry_t *entry = app_registry_at(i);
        if (entry != NULL && entry->settings_app == app) {
            return true;
        }
    }
    return false;
}

static const settings_pin_extra_t *settings_pin_find_extra(const app_shell_app_t *app)
{
    for (size_t i = 0; i < SETTINGS_PIN_GUARDED_APPS_MAX; ++i) {
        if (app != NULL && s_pin_extra[i].app == app) {
            return &s_pin_extra[i];
        }
    }
    return NULL;
}

static bool settings_pin_is_guarded(const app_shell_app_t *app)
{
    if (app == NULL) {
        return false;
    }
    return app == system_settings_app_get_app() ||
           app == cyd_wifi_setup_get_app() ||
           settings_pin_is_app_settings_screen(app) ||
           settings_pin_find_extra(app) != NULL;
}

static const app_shell_app_t *settings_pin_provider_app(void)
{
    portENTER_CRITICAL(&s_pin_lock);
    const settings_pin_recovery_provider_t *provider = s_pin_provider;
    portEXIT_CRITICAL(&s_pin_lock);
    return provider != NULL ? provider->app : NULL;
}

static bool settings_pin_in_area(const app_shell_app_t *app)
{
    return settings_pin_is_guarded(app) ||
           app == system_touch_calibration_app_get_app() ||
           app == &s_settings_pin_app ||
           (app != NULL && app == settings_pin_provider_app());
}

/* The same rule as app_shell's idle return, read without its "not home"
   condition: settings can be the home app after a boot into it. */
static bool settings_pin_idle_elapsed_by(uint32_t extra_ms)
{
    uint16_t seconds = app_shell_get_idle_return_timeout_seconds();
    TickType_t last = cyd_input_get_last_activity_tick();

    if (seconds == 0 || last == 0) {
        return false;
    }
    return (xTaskGetTickCount() - last) >= pdMS_TO_TICKS((uint32_t)seconds * 1000U + extra_ms);
}

static bool settings_pin_idle_elapsed(void)
{
    return settings_pin_idle_elapsed_by(0);
}

/* ---- the session and the flow ---------------------------------------------- */

static void settings_pin_clear_entry(void)
{
    settings_pin_entry_clear(&s_pin_entry);
    settings_pin_setup_reset(&s_pin_setup);
}

static void settings_pin_set_recovered(bool recovered)
{
    portENTER_CRITICAL(&s_pin_lock);
    s_pin_recovered = recovered;
    portEXIT_CRITICAL(&s_pin_lock);
}

static bool settings_pin_get_recovered(void)
{
    portENTER_CRITICAL(&s_pin_lock);
    bool recovered = s_pin_recovered;
    portEXIT_CRITICAL(&s_pin_lock);
    return recovered;
}

static void settings_pin_end_flow(void)
{
    s_pin_flow = SETTINGS_PIN_FLOW_NONE;
    s_pin_target = NULL;
    s_pin_origin = NULL;
    settings_pin_set_recovered(false);
    settings_pin_clear_entry();
}

static void settings_pin_lock_session(void)
{
    if (s_pin_unlocked) {
        ESP_LOGI(TAG, "settings PIN closed");
    }
    s_pin_unlocked = false;
}

static void settings_pin_start_unlock(const app_shell_app_t *target, const app_shell_app_t *origin)
{
    settings_pin_lock_session();
    settings_pin_end_flow();
    s_pin_flow = SETTINGS_PIN_FLOW_UNLOCK;
    s_pin_target = target;
    s_pin_origin = origin;
    s_pin_screen_kind = SETTINGS_PIN_VIEW_ENTER;
    s_pin_notice = SETTINGS_PIN_VIEW_NOTICE_NONE;
}

static const app_shell_app_t *settings_pin_guard(void *ctx, const app_shell_app_t *next_app,
                                                 const app_shell_app_t *from_app)
{
    (void)ctx;
    settings_pin_ensure_loaded();

    if (s_pin_state == SETTINGS_PIN_STORE_OFF) {
        return next_app;
    }
    if (!settings_pin_in_area(next_app)) {
        settings_pin_lock_session();
        settings_pin_end_flow();
        return next_app;
    }
    if (!settings_pin_is_guarded(next_app)) {
        return next_app;
    }
    if (s_pin_state == SETTINGS_PIN_STORE_CORRUPT) {
        /* The PIN cannot be checked. The only way on is the one that also
           removes it: Initialize NVS, with no way back into the pages. */
        system_settings_open_clear_nvs_confirm();
        return system_settings_app_get_app();
    }
    if (next_app == system_settings_app_get_app() && system_settings_forced_initialize_armed()) {
        /* Stored data this firmware cannot read: the boot opens Initialize NVS
           with no way out but erasing everything, the PIN included. */
        return next_app;
    }
    if (s_pin_unlocked && !settings_pin_idle_elapsed()) {
        return next_app;
    }
    settings_pin_start_unlock(next_app, from_app);
    return &s_settings_pin_app;
}

/* ---- the PIN screen -------------------------------------------------------- */

static bool settings_pin_recovery_available(void)
{
    portENTER_CRITICAL(&s_pin_lock);
    const settings_pin_recovery_provider_t *provider = s_pin_provider;
    void *ctx = s_pin_provider_ctx;
    portEXIT_CRITICAL(&s_pin_lock);

    return provider != NULL && provider->app != NULL &&
           (provider->available == NULL || provider->available(ctx));
}

static const char *settings_pin_title(void)
{
    if (s_pin_flow == SETTINGS_PIN_FLOW_NEW_PIN) {
        return "PIN の設定";
    }
    const settings_pin_extra_t *extra = settings_pin_find_extra(s_pin_target);
    if (extra != NULL && extra->title != NULL) {
        return extra->title;
    }
    return "設定";
}

static esp_err_t settings_pin_show(void)
{
    settings_pin_view_model_t *m = &s_pin_model;

    *m = (settings_pin_view_model_t){
        .screen = s_pin_screen_kind,
        .notice = s_pin_notice,
        .title = settings_pin_title(),
        .entered = s_pin_entry.len,
    };
    if (s_pin_screen_kind == SETTINGS_PIN_VIEW_ENTER) {
        m->wait_seconds = settings_pin_wait_remaining();
        if (s_pin_flow == SETTINGS_PIN_FLOW_UNLOCK && settings_pin_recovery_available()) {
            m->recovery_label = s_pin_provider->label;
        }
    }
    s_pin_shown_wait = m->wait_seconds;
    settings_pin_view_build(cyd_ui_screen_borrow(&s_pin_screen_owner), m);
    return cyd_ui_submit(cyd_ui_screen_of(&s_pin_screen_owner));
}

static void settings_pin_show_message(const char *title, const char *detail)
{
    s_pin_model = (settings_pin_view_model_t){
        .screen = SETTINGS_PIN_VIEW_MESSAGE,
        .title = settings_pin_title(),
        .message_title = title,
        .message_detail = detail,
    };
    settings_pin_view_build(cyd_ui_screen_borrow(&s_pin_screen_owner), &s_pin_model);
    esp_err_t err = cyd_ui_submit(cyd_ui_screen_of(&s_pin_screen_owner));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "showing a message failed: %s", esp_err_to_name(err));
    }
}

/* Stores `entry` as the new PIN, keeping the other settings of the old one. */
static esp_err_t settings_pin_store_new_pin(const settings_pin_entry_t *entry)
{
    settings_pin_store_config_t config = { 0 };
    int64_t started = esp_timer_get_time();

    settings_pin_store_new_record(&config.record);
    config.flags = 0U; /* bit 0 was "system info too"; that choice now belongs to the app */
    ESP_RETURN_ON_FALSE(settings_pin_record_fill(&config.record, entry, settings_pin_store_hash), ESP_FAIL, TAG,
                        "hashing the new PIN failed");
    ESP_LOGI(TAG, "PIN hash took %d ms", (int)((esp_timer_get_time() - started) / 1000));
    ESP_RETURN_ON_ERROR(settings_pin_store_save(&config), TAG, "saving the new PIN failed");

    s_pin_config = config;
    s_pin_state = SETTINGS_PIN_STORE_ON;
    s_pin_failures = 0;
    s_pin_wait_until_us = 0;
    settings_pin_save_failures();
    memset(&config, 0, sizeof(config));
    return ESP_OK;
}

/* A correct PIN, or a new one after recovery: on to where the user was going. */
static esp_err_t settings_pin_continue_to_target(void)
{
    const app_shell_app_t *target = s_pin_target;
    const app_shell_app_t *origin = s_pin_origin;

    s_pin_unlocked = true;
    settings_pin_end_flow();
    ESP_LOGI(TAG, "settings PIN accepted");
    ESP_RETURN_ON_FALSE(target != NULL, ESP_ERR_INVALID_STATE, TAG, "no app to continue to");
    return app_shell_switch_to_from(target, origin);
}

static esp_err_t settings_pin_submit_enter(void)
{
    if (settings_pin_wait_remaining() > 0 || !settings_pin_entry_complete(&s_pin_entry)) {
        return ESP_OK;
    }

    /* Counted before checking, so cutting the power after a wrong guess does
       not give the guess back. */
    ++s_pin_failures;
    settings_pin_save_failures();

    bool ok = settings_pin_record_matches(&s_pin_config.record, &s_pin_entry, settings_pin_store_hash);
    settings_pin_entry_clear(&s_pin_entry);
    if (ok) {
        s_pin_failures = 0;
        s_pin_wait_until_us = 0;
        settings_pin_save_failures();
        return settings_pin_continue_to_target();
    }

    uint32_t wait = settings_pin_wait_seconds(s_pin_failures);
    if (wait > 0) {
        s_pin_wait_until_us = esp_timer_get_time() + (int64_t)wait * 1000000LL;
    }
    ESP_LOGW(TAG, "wrong settings PIN (%u in a row, wait %u s)", (unsigned)s_pin_failures, (unsigned)wait);
    s_pin_notice = SETTINGS_PIN_VIEW_NOTICE_WRONG;
    return settings_pin_show();
}

static esp_err_t settings_pin_submit_new(void)
{
    settings_pin_setup_result_t result = settings_pin_setup_submit(&s_pin_setup, &s_pin_entry);

    settings_pin_entry_clear(&s_pin_entry);
    switch (result) {
    case SETTINGS_PIN_SETUP_NEED_SECOND:
        s_pin_screen_kind = SETTINGS_PIN_VIEW_NEW_SECOND;
        s_pin_notice = SETTINGS_PIN_VIEW_NOTICE_NONE;
        return settings_pin_show();
    case SETTINGS_PIN_SETUP_MISMATCH:
        s_pin_screen_kind = SETTINGS_PIN_VIEW_NEW_FIRST;
        s_pin_notice = SETTINGS_PIN_VIEW_NOTICE_MISMATCH;
        return settings_pin_show();
    case SETTINGS_PIN_SETUP_MATCH: {
        esp_err_t err = settings_pin_store_new_pin(&s_pin_setup.first);
        settings_pin_setup_reset(&s_pin_setup);
        if (err != ESP_OK) {
            s_pin_screen_kind = SETTINGS_PIN_VIEW_NEW_FIRST;
            s_pin_notice = SETTINGS_PIN_VIEW_NOTICE_SAVE_FAILED;
            return settings_pin_show();
        }
        ESP_LOGI(TAG, "new settings PIN stored");
        if (s_pin_flow == SETTINGS_PIN_FLOW_UNLOCK) {
            return settings_pin_continue_to_target();
        }
        s_pin_unlocked = true;
        const app_shell_app_t *back = s_pin_target;
        settings_pin_end_flow();
        return app_shell_switch_to(back != NULL ? back : system_settings_app_get_app());
    }
    default:
        return ESP_OK;
    }
}

/*
 * Cancel on the PIN screen.
 *
 * From the settings page it just goes back there. After a recovery it goes
 * back to asking for the old PIN: dropping the recovery is the safe answer.
 * In front of an app, it goes where the user came from; when that would be a
 * guarded app again (a boot into settings, where settings is also the home
 * app), there is nowhere to go, so the device restarts into its normal home.
 */
static esp_err_t settings_pin_cancel(void)
{
    if (s_pin_flow == SETTINGS_PIN_FLOW_NEW_PIN) {
        const app_shell_app_t *back = s_pin_target;
        settings_pin_end_flow();
        return app_shell_switch_to(back != NULL ? back : system_settings_app_get_app());
    }
    if (s_pin_screen_kind != SETTINGS_PIN_VIEW_ENTER) {
        settings_pin_set_recovered(false);
        settings_pin_clear_entry();
        s_pin_screen_kind = SETTINGS_PIN_VIEW_ENTER;
        s_pin_notice = SETTINGS_PIN_VIEW_NOTICE_NONE;
        return settings_pin_show();
    }

    const app_shell_app_t *dest = s_pin_origin != NULL ? s_pin_origin : app_shell_get_home_app();
    if (s_pin_target == system_settings_app_get_app()) {
        /* A view another app asked settings to open must not wait for the next visit. */
        system_settings_discard_direct_view();
    }
    settings_pin_end_flow();
    if (dest == NULL || settings_pin_is_guarded(dest)) {
        ESP_LOGI(TAG, "PIN screen cancelled with nowhere to go; restarting");
        (void)cyd_input_discard_pending_events();
        settings_pin_show_message("再起動します", "設定には入りません");
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
        return ESP_OK;
    }
    return app_shell_switch_to(dest);
}

static esp_err_t settings_pin_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;
    s_pin_tracker = (cyd_system_apps_touch_tracker_t){ 0 };
    settings_pin_entry_clear(&s_pin_entry);

    if (s_pin_flow == SETTINGS_PIN_FLOW_NONE) {
        /* Not opened by the guard or the settings page: nothing to do here. */
        return app_shell_return_to(from_app);
    }
    if (from_app != NULL && from_app == settings_pin_provider_app() && s_pin_flow == SETTINGS_PIN_FLOW_UNLOCK) {
        settings_pin_setup_reset(&s_pin_setup);
        s_pin_notice = SETTINGS_PIN_VIEW_NOTICE_NONE;
        if (settings_pin_get_recovered()) {
            ESP_LOGI(TAG, "recovery accepted; asking for a new PIN");
            s_pin_screen_kind = SETTINGS_PIN_VIEW_NEW_FIRST;
        } else {
            s_pin_screen_kind = SETTINGS_PIN_VIEW_ENTER;
        }
    }
    return settings_pin_show();
}

static esp_err_t settings_pin_handle_action(uint16_t action_id)
{
    if (action_id == SETTINGS_PIN_VIEW_ACTION_CANCEL) {
        return settings_pin_cancel();
    }
    if (action_id == SETTINGS_PIN_VIEW_ACTION_RECOVERY) {
        if (s_pin_flow == SETTINGS_PIN_FLOW_UNLOCK && s_pin_screen_kind == SETTINGS_PIN_VIEW_ENTER &&
            settings_pin_recovery_available()) {
            settings_pin_set_recovered(false);
            return app_shell_switch_to(settings_pin_provider_app());
        }
        return ESP_OK;
    }
    if (s_pin_screen_kind == SETTINGS_PIN_VIEW_ENTER && settings_pin_wait_remaining() > 0) {
        return ESP_OK;
    }
    if (action_id >= SETTINGS_PIN_VIEW_ACTION_DIGIT_BASE && action_id <= SETTINGS_PIN_VIEW_ACTION_DIGIT_BASE + 9U) {
        if (settings_pin_entry_push(&s_pin_entry, (char)('0' + (action_id - SETTINGS_PIN_VIEW_ACTION_DIGIT_BASE)))) {
            return settings_pin_show();
        }
        return ESP_OK;
    }
    if (action_id == SETTINGS_PIN_VIEW_ACTION_DELETE) {
        if (settings_pin_entry_pop(&s_pin_entry)) {
            return settings_pin_show();
        }
        return ESP_OK;
    }
    if (action_id == SETTINGS_PIN_VIEW_ACTION_OK) {
        return s_pin_screen_kind == SETTINGS_PIN_VIEW_ENTER ? settings_pin_submit_enter() : settings_pin_submit_new();
    }
    return ESP_OK;
}

static esp_err_t settings_pin_app_step(void *ctx)
{
    (void)ctx;

    /* The countdown, once a second, and the keys coming back when it ends. */
    if (s_pin_screen_kind == SETTINGS_PIN_VIEW_ENTER && settings_pin_wait_remaining() != s_pin_shown_wait) {
        ESP_RETURN_ON_ERROR(settings_pin_show(), TAG, "show PIN screen failed");
    }

    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_SYSTEM_APPS_INPUT_POLL_MS)) != ESP_OK) {
        return ESP_OK;
    }
    uint16_t action_id = 0;
    if (!cyd_system_apps_touch_confirmed_action(cyd_ui_screen_of(&s_pin_screen_owner), &event, &s_pin_tracker, &action_id)) {
        return ESP_OK;
    }
    return settings_pin_handle_action(action_id);
}

static esp_err_t settings_pin_app_leave(void *ctx)
{
    (void)ctx;
    settings_pin_clear_entry();
    return ESP_OK;
}

/*
 * No idle return while the home app is itself guarded (a boot into settings):
 * it would only bounce back to this screen, twenty times a second.
 */
static bool settings_pin_app_idle_return_suppressed(void *ctx)
{
    (void)ctx;
    return settings_pin_is_guarded(app_shell_get_home_app());
}

static const app_shell_app_t s_settings_pin_app = {
    .id = "settings_pin",
    .ctx = NULL,
    .enter = settings_pin_app_enter,
    .step = settings_pin_app_step,
    .leave = settings_pin_app_leave,
    .idle_return_suppressed = settings_pin_app_idle_return_suppressed,
};

/* ---- public API ------------------------------------------------------------ */

esp_err_t settings_pin_init(void)
{
    settings_pin_ensure_loaded();
    app_shell_set_enter_guard(settings_pin_guard, NULL);
    ESP_LOGI(TAG, "settings PIN %s",
             s_pin_state == SETTINGS_PIN_STORE_ON        ? "on"
             : s_pin_state == SETTINGS_PIN_STORE_CORRUPT ? "unreadable (locked)"
                                                         : "off");
    return ESP_OK;
}

bool settings_pin_is_enabled(void)
{
    settings_pin_ensure_loaded();
    return s_pin_state != SETTINGS_PIN_STORE_OFF;
}

const app_shell_app_t *settings_pin_app_get_app(void)
{
    return &s_settings_pin_app;
}

esp_err_t settings_pin_set_recovery_provider(const settings_pin_recovery_provider_t *provider, void *ctx)
{
    ESP_RETURN_ON_FALSE(provider == NULL || (provider->app != NULL && provider->label != NULL), ESP_ERR_INVALID_ARG,
                        TAG, "a recovery provider needs a label and an app");
    portENTER_CRITICAL(&s_pin_lock);
    s_pin_provider = provider;
    s_pin_provider_ctx = ctx;
    portEXIT_CRITICAL(&s_pin_lock);
    return ESP_OK;
}

esp_err_t settings_pin_guard_app(const app_shell_app_t *app, const char *title, bool guarded)
{
    ESP_RETURN_ON_FALSE(app != NULL, ESP_ERR_INVALID_ARG, TAG, "app is null");
    ESP_RETURN_ON_FALSE(app != &s_settings_pin_app, ESP_ERR_INVALID_ARG, TAG, "the PIN screen cannot guard itself");

    settings_pin_extra_t *free_slot = NULL;
    for (size_t i = 0; i < SETTINGS_PIN_GUARDED_APPS_MAX; ++i) {
        if (s_pin_extra[i].app == app) {
            if (!guarded) {
                s_pin_extra[i] = (settings_pin_extra_t){ 0 };
            } else {
                s_pin_extra[i].title = title;
            }
            return ESP_OK;
        }
        if (free_slot == NULL && s_pin_extra[i].app == NULL) {
            free_slot = &s_pin_extra[i];
        }
    }
    if (!guarded) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(free_slot != NULL, ESP_ERR_NO_MEM, TAG, "too many apps behind the PIN");
    *free_slot = (settings_pin_extra_t){ .app = app, .title = title };
    ESP_LOGI(TAG, "%s asks for the PIN when the PIN is on", app->id);
    return ESP_OK;
}

void settings_pin_recovery_succeeded(void)
{
    if (s_pin_flow != SETTINGS_PIN_FLOW_UNLOCK || s_pin_state != SETTINGS_PIN_STORE_ON) {
        ESP_LOGW(TAG, "recovery reported while no PIN is being asked for; ignored");
        return;
    }
    settings_pin_set_recovered(true);
    (void)app_shell_switch_to(&s_settings_pin_app);
}

/* ---- for the settings app -------------------------------------------------- */

esp_err_t settings_pin_begin_new_pin(void)
{
    settings_pin_ensure_loaded();
    settings_pin_end_flow();
    s_pin_flow = SETTINGS_PIN_FLOW_NEW_PIN;
    s_pin_target = system_settings_app_get_app();
    s_pin_screen_kind = SETTINGS_PIN_VIEW_NEW_FIRST;
    s_pin_notice = SETTINGS_PIN_VIEW_NOTICE_NONE;
    return app_shell_switch_to(&s_settings_pin_app);
}

esp_err_t settings_pin_turn_off(void)
{
    ESP_RETURN_ON_ERROR(settings_pin_store_erase(), TAG, "removing the PIN failed");
    memset(&s_pin_config, 0, sizeof(s_pin_config));
    s_pin_state = SETTINGS_PIN_STORE_OFF;
    s_pin_failures = 0;
    s_pin_wait_until_us = 0;
    s_pin_unlocked = false;
    settings_pin_end_flow();
    ESP_LOGI(TAG, "settings PIN turned off");
    return ESP_OK;
}

bool settings_pin_has_recovery(void)
{
    return settings_pin_provider_app() != NULL;
}

/* How long the shell's own idle return gets to take a non-home app home
   first; only if it is held back (home return not allowed) does the PIN close
   in place. Without it the PIN screen would flash up on the way home. */
#define SETTINGS_PIN_RELOCK_GRACE_MS 1000U

bool settings_pin_relock_if_idle(const app_shell_app_t *self)
{
    uint32_t grace_ms = app_shell_get_home_app() == self ? 0U : SETTINGS_PIN_RELOCK_GRACE_MS;

    if (s_pin_state != SETTINGS_PIN_STORE_ON || !s_pin_unlocked || !settings_pin_idle_elapsed_by(grace_ms)) {
        return false;
    }
    /* No origin: settings keeps the return app it already has. */
    settings_pin_start_unlock(self, NULL);
    return app_shell_switch_to(&s_settings_pin_app) == ESP_OK;
}

bool settings_pin_divert_unguarded(const app_shell_app_t *self, const app_shell_app_t *from_app)
{
    if (app_shell_has_enter_guard()) {
        return false;
    }
    ESP_LOGE(TAG, "settings_pin_init() was not called before app_shell_start(); installing the guard now");
    app_shell_set_enter_guard(settings_pin_guard, NULL);

    const app_shell_app_t *chosen = settings_pin_guard(NULL, self, from_app);
    if (chosen == self) {
        return false;
    }
    (void)app_shell_switch_to(chosen);
    return true;
}
