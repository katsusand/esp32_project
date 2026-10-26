/*
 * Fixed settings items: which fixes are accepted, the last one winning, a page
 * of hidden items, and taps on fixed items. The "before app_shell_start()"
 * rule and the boot log live in system_settings_app.c and are checked on a
 * unit.
 */

#include <stdio.h>
#include "system_settings_fix.h"
#include "system_settings_fix_internal.h"

static int s_checks;
static int s_failures;

static void check(int ok, const char *what)
{
    ++s_checks;
    if (!ok) {
        ++s_failures;
        printf("FAIL: %s\n", what);
    } else {
        printf("ok: %s\n", what);
    }
}

static void test_set(void)
{
    system_settings_fix_reset();
    for (int i = 0; i < SYSTEM_SETTINGS_ITEM_COUNT; ++i) {
        if (system_settings_fix_mode((system_settings_item_t)i) != SYSTEM_SETTINGS_FIX_NONE ||
            system_settings_fix_reason((system_settings_item_t)i) != NULL) {
            check(0, "nothing is fixed until a product asks");
            return;
        }
    }
    check(1, "nothing is fixed until a product asks");

    check(system_settings_fix_set(SYSTEM_SETTINGS_ITEM_WIFI_IDLE, SYSTEM_SETTINGS_FIX_HIDDEN, NULL) == ESP_OK &&
              system_settings_fix_mode(SYSTEM_SETTINGS_ITEM_WIFI_IDLE) == SYSTEM_SETTINGS_FIX_HIDDEN,
          "Wi-Fi idle can be hidden");
    check(system_settings_fix_set(SYSTEM_SETTINGS_ITEM_WIFI_IDLE, SYSTEM_SETTINGS_FIX_READONLY, "接続したままにするため") == ESP_OK &&
              system_settings_fix_mode(SYSTEM_SETTINGS_ITEM_WIFI_IDLE) == SYSTEM_SETTINGS_FIX_READONLY &&
              system_settings_fix_reason(SYSTEM_SETTINGS_ITEM_WIFI_IDLE) != NULL,
          "the last call wins, with its reason");
    check(system_settings_fix_set(SYSTEM_SETTINGS_ITEM_WIFI_IDLE, SYSTEM_SETTINGS_FIX_NONE, "ignored") == ESP_OK &&
              system_settings_fix_mode(SYSTEM_SETTINGS_ITEM_WIFI_IDLE) == SYSTEM_SETTINGS_FIX_NONE &&
              system_settings_fix_reason(SYSTEM_SETTINGS_ITEM_WIFI_IDLE) == NULL,
          "NONE undoes a fix and drops the reason");

    int values_ok = 1;
    int actions_ok = 1;
    for (int i = 0; i < SYSTEM_SETTINGS_ITEM_COUNT; ++i) {
        system_settings_item_t item = (system_settings_item_t)i;
        esp_err_t ro = system_settings_fix_set(item, SYSTEM_SETTINGS_FIX_READONLY, NULL);
        esp_err_t hidden = system_settings_fix_set(item, SYSTEM_SETTINGS_FIX_HIDDEN, NULL);
        if (system_settings_item_has_value(item)) {
            values_ok &= ro == ESP_OK && hidden == ESP_OK;
        } else {
            actions_ok &= ro == ESP_ERR_NOT_SUPPORTED && hidden == ESP_OK &&
                          system_settings_fix_mode(item) == SYSTEM_SETTINGS_FIX_HIDDEN;
        }
        values_ok &= system_settings_item_name(item)[0] != '\0';
    }
    check(values_ok, "every item with a value can be read-only or hidden, and has a log name");
    check(actions_ok, "actions can only be hidden: a greyed-out button has nothing to read");

    check(system_settings_fix_set(SYSTEM_SETTINGS_ITEM_COUNT, SYSTEM_SETTINGS_FIX_HIDDEN, NULL) == ESP_ERR_INVALID_ARG &&
              system_settings_fix_set(SYSTEM_SETTINGS_ITEM_THEME, (system_settings_fix_mode_t)7, NULL) ==
                  ESP_ERR_INVALID_ARG,
          "unknown items and modes are refused");
    check(system_settings_fix_mode(SYSTEM_SETTINGS_ITEM_COUNT) == SYSTEM_SETTINGS_FIX_NONE &&
              system_settings_item_name(SYSTEM_SETTINGS_ITEM_COUNT)[0] == '\0',
          "an unknown item reads as not fixed");
}

static void test_pages_and_taps(void)
{
    static const system_settings_item_t nvs[] = {
        SYSTEM_SETTINGS_ITEM_CLEAR_TOUCH_CALIB,
        SYSTEM_SETTINGS_ITEM_CLEAR_APP_DATA,
        SYSTEM_SETTINGS_ITEM_CLEAR_NVS,
    };
    static const system_settings_action_item_t map[] = {
        { 0x10, SYSTEM_SETTINGS_ITEM_WIFI_IDLE },
        { 0x11, SYSTEM_SETTINGS_ITEM_WIFI_IDLE },
        { 0x20, SYSTEM_SETTINGS_ITEM_SYNC_INTERVAL },
    };

    system_settings_fix_reset();
    check(!system_settings_items_all_hidden(nvs, 3), "a page with nothing fixed stays");
    (void)system_settings_fix_set(SYSTEM_SETTINGS_ITEM_CLEAR_TOUCH_CALIB, SYSTEM_SETTINGS_FIX_HIDDEN, NULL);
    (void)system_settings_fix_set(SYSTEM_SETTINGS_ITEM_CLEAR_APP_DATA, SYSTEM_SETTINGS_FIX_HIDDEN, NULL);
    check(!system_settings_items_all_hidden(nvs, 3), "a page with one item left stays");
    (void)system_settings_fix_set(SYSTEM_SETTINGS_ITEM_CLEAR_NVS, SYSTEM_SETTINGS_FIX_HIDDEN, NULL);
    check(system_settings_items_all_hidden(nvs, 3), "a page with every item hidden goes");
    check(!system_settings_items_all_hidden(nvs, 0) && !system_settings_items_all_hidden(NULL, 3),
          "a page with no items listed is never dropped");

    system_settings_fix_reset();
    check(!system_settings_fixed_action(0x10, map, 3), "taps work while nothing is fixed");
    (void)system_settings_fix_set(SYSTEM_SETTINGS_ITEM_WIFI_IDLE, SYSTEM_SETTINGS_FIX_READONLY, NULL);
    check(system_settings_fixed_action(0x10, map, 3) && system_settings_fixed_action(0x11, map, 3),
          "both buttons of a read-only item are ignored");
    check(!system_settings_fixed_action(0x20, map, 3) && !system_settings_fixed_action(0x99, map, 3),
          "other items and unknown buttons still work");
    system_settings_fix_reset();
}

int main(void)
{
    printf("fixes\n");
    test_set();
    printf("pages and taps\n");
    test_pages_and_taps();
    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
