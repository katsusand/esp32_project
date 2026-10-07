/*
 * radio_manager failure reasons.
 *
 * The caller puts this text into its error log line, and that line is what is
 * read later off the SD card to find out why there was no Internet. So every
 * Wi-Fi state must map to the right failure, every failure must have its own
 * words, and the words must stay the same from one failure to the next (the
 * error log folds repeats only when the whole line is the same).
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "radio_manager_failure.h"

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) {
        printf("  FAIL %s\n", what);
        failures++;
    } else {
        printf("  ok   %s\n", what);
    }
}

static void check_text(const char *got, const char *want, const char *what)
{
    char line[160];
    snprintf(line, sizeof(line), "%s: \"%s\"", what, want);
    check(got != NULL && strcmp(got, want) == 0, line);
    if (got != NULL && strcmp(got, want) != 0) {
        printf("       got \"%s\"\n", got);
    }
}

/* Short, printable ASCII, no format characters, no digits (a count or a time
   would stop repeats from folding). */
static bool is_log_safe(const char *text)
{
    size_t len = strlen(text);
    if (len == 0 || len > 48) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 || c > 0x7e || c == '%' || (c >= '0' && c <= '9')) {
            return false;
        }
    }
    return true;
}

static void test_wifi_state(void)
{
    printf("Wi-Fi state -> failure (every state)\n");
    static const struct {
        wifi_connection_state_t state;
        radio_manager_failure_t want;
        const char *name;
    } cases[] = {
        { WIFI_CONNECTION_STATE_STOPPED, RADIO_MANAGER_FAILURE_WIFI_UNAVAILABLE, "STOPPED ends the wait: Wi-Fi not available" },
        { WIFI_CONNECTION_STATE_INIT, RADIO_MANAGER_FAILURE_NONE, "INIT keeps waiting" },
        { WIFI_CONNECTION_STATE_OFF, RADIO_MANAGER_FAILURE_NONE, "OFF keeps waiting" },
        { WIFI_CONNECTION_STATE_CONNECTING, RADIO_MANAGER_FAILURE_NONE, "CONNECTING keeps waiting" },
        { WIFI_CONNECTION_STATE_CONNECTED, RADIO_MANAGER_FAILURE_NONE, "CONNECTED is no failure" },
        { WIFI_CONNECTION_STATE_RECONNECTING, RADIO_MANAGER_FAILURE_NONE, "RECONNECTING keeps waiting" },
        { WIFI_CONNECTION_STATE_FAILED, RADIO_MANAGER_FAILURE_WIFI_CONNECT_FAILED, "FAILED is a connect failure" },
        { WIFI_CONNECTION_STATE_SETUP_REQUIRED, RADIO_MANAGER_FAILURE_WIFI_SETUP_REQUIRED, "SETUP_REQUIRED is setup required" },
        { WIFI_CONNECTION_STATE_SETUP_RUNNING, RADIO_MANAGER_FAILURE_NONE, "SETUP_RUNNING keeps waiting (the pause comes from wait_connected)" },
    };
    check(sizeof(cases) / sizeof(cases[0]) == (size_t)WIFI_CONNECTION_STATE_SETUP_RUNNING + 1,
          "the table covers every wifi_connection_state_t");
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        check(radio_manager_failure_from_wifi_state(cases[i].state) == cases[i].want, cases[i].name);
    }
}

static void test_wifi_reason_words(void)
{
    printf("\nWi-Fi failure reason words (every reason)\n");
    static const char *const want[] = {
        [ESP32_WIFI_STA_FAILURE_NONE] = "unknown",
        [ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE] = "no saved profile",
        [ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE] = "AP not found",
        [ESP32_WIFI_STA_FAILURE_AUTH] = "auth failed",
        [ESP32_WIFI_STA_FAILURE_TIMEOUT] = "timeout",
        [ESP32_WIFI_STA_FAILURE_CONNECT] = "connect failed",
    };
    check(sizeof(want) / sizeof(want[0]) == (size_t)ESP32_WIFI_STA_FAILURE_CONNECT + 1,
          "the table covers every esp32_wifi_sta_failure_reason_t");
    for (int r = 0; r <= ESP32_WIFI_STA_FAILURE_CONNECT; ++r) {
        check_text(wifi_connection_failure_reason_text((esp32_wifi_sta_failure_reason_t)r), want[r], "reason words");
    }
    check_text(wifi_connection_failure_reason_text((esp32_wifi_sta_failure_reason_t)200), "unknown",
               "an unknown reason value");
}

static void test_failure_texts(void)
{
    printf("\nfailure -> text (every failure)\n");
    static const char *const want[] = {
        [RADIO_MANAGER_FAILURE_NONE] = "no detail",
        [RADIO_MANAGER_FAILURE_INVALID_REQUEST] = "invalid request",
        [RADIO_MANAGER_FAILURE_NOT_STARTED] = "radio manager not started",
        [RADIO_MANAGER_FAILURE_NOT_SUPPORTED] = "capability not supported",
        [RADIO_MANAGER_FAILURE_DISABLED] = "Wi-Fi disabled in this build",
        [RADIO_MANAGER_FAILURE_WIFI_UNAVAILABLE] = "Wi-Fi not available",
        [RADIO_MANAGER_FAILURE_WIFI_SETUP_PAUSED] = "paused for Wi-Fi setup",
        [RADIO_MANAGER_FAILURE_WIFI_SETUP_REQUIRED] = "Wi-Fi setup required",
        [RADIO_MANAGER_FAILURE_WIFI_CONNECT_FAILED] = "Wi-Fi connect failed: unknown",
        [RADIO_MANAGER_FAILURE_QUEUE_FULL] = "timed out: request queue full",
        [RADIO_MANAGER_FAILURE_RADIO_BUSY] = "timed out: radio in use by another request",
        [RADIO_MANAGER_FAILURE_WIFI_CONNECT_TIMEOUT] = "timed out waiting for Wi-Fi to connect",
    };
    const size_t count = sizeof(want) / sizeof(want[0]);
    check(count == (size_t)RADIO_MANAGER_FAILURE_WIFI_CONNECT_TIMEOUT + 1,
          "the table covers every radio_manager_failure_t");

    bool all_safe = true;
    bool all_distinct = true;
    for (size_t f = 0; f < count; ++f) {
        const char *text = radio_manager_failure_text((radio_manager_failure_t)f, 0);
        check_text(text, want[f], "failure text");
        all_safe = all_safe && is_log_safe(text);
        /* The Wi-Fi reason belongs to WIFI_CONNECT_FAILED only. */
        if (f != RADIO_MANAGER_FAILURE_WIFI_CONNECT_FAILED) {
            all_distinct = all_distinct &&
                           strcmp(radio_manager_failure_text((radio_manager_failure_t)f, ESP32_WIFI_STA_FAILURE_AUTH),
                                  text) == 0;
        }
        for (size_t g = 0; g < f; ++g) {
            all_distinct = all_distinct && strcmp(radio_manager_failure_text((radio_manager_failure_t)g, 0), text) != 0;
        }
    }
    check(all_safe, "every text is short printable ASCII without '%' or digits");
    check(all_distinct, "every failure has its own text, and only WIFI_CONNECT_FAILED reads the Wi-Fi reason");
    check_text(radio_manager_failure_text((radio_manager_failure_t)99, 0), "unknown", "an unknown failure value");
}

static void test_connect_failed(void)
{
    printf("\nWIFI_CONNECT_FAILED x every Wi-Fi reason\n");
    static const char *const want[] = {
        [ESP32_WIFI_STA_FAILURE_NONE] = "Wi-Fi connect failed: unknown",
        [ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE] = "Wi-Fi connect failed: no saved profile",
        [ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE] = "Wi-Fi connect failed: AP not found",
        [ESP32_WIFI_STA_FAILURE_AUTH] = "Wi-Fi connect failed: auth failed",
        [ESP32_WIFI_STA_FAILURE_TIMEOUT] = "Wi-Fi connect failed: timeout",
        [ESP32_WIFI_STA_FAILURE_CONNECT] = "Wi-Fi connect failed: connect failed",
    };
    for (int r = 0; r <= ESP32_WIFI_STA_FAILURE_CONNECT; ++r) {
        const char *text = radio_manager_failure_text(RADIO_MANAGER_FAILURE_WIFI_CONNECT_FAILED, (uint8_t)r);
        check_text(text, want[r], "connect failed text");

        /* The words are wifi_connection's, not a copy that could drift. */
        char joined[96];
        snprintf(joined, sizeof(joined), "Wi-Fi connect failed: %s",
                 wifi_connection_failure_reason_text((esp32_wifi_sta_failure_reason_t)r));
        check(strcmp(text, joined) == 0, "  ... and its reason words are wifi_connection_failure_reason_text()");
        check(is_log_safe(text), "  ... short printable ASCII without '%' or digits");
    }
    check_text(radio_manager_failure_text(RADIO_MANAGER_FAILURE_WIFI_CONNECT_FAILED, 200),
               "Wi-Fi connect failed: unknown", "an unknown Wi-Fi reason value");
}

static void test_lease(void)
{
    printf("\nradio_manager_lease_failure_text()\n");
    check_text(radio_manager_lease_failure_text(NULL), "no detail", "a NULL lease");

    radio_manager_lease_t lease = { 0 };
    check_text(radio_manager_lease_failure_text(&lease), "no detail", "a lease the caller only zeroed");

    lease.failure = RADIO_MANAGER_FAILURE_WIFI_CONNECT_FAILED;
    lease.wifi_failure_reason = ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE;
    check_text(radio_manager_lease_failure_text(&lease), "Wi-Fi connect failed: AP not found",
               "a lease from a failed connect");

    lease.failure = RADIO_MANAGER_FAILURE_WIFI_SETUP_REQUIRED;
    lease.wifi_failure_reason = 0;
    check_text(radio_manager_lease_failure_text(&lease), "Wi-Fi setup required", "a lease from setup required");
}

int main(void)
{
    test_wifi_state();
    test_wifi_reason_words();
    test_failure_texts();
    test_connect_failed();
    test_lease();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
