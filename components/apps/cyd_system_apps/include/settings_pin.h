#ifndef SETTINGS_PIN_H
#define SETTINGS_PIN_H

/*
 * Settings PIN: a PIN in front of settings and everything settings opens.
 *
 * Off by default. While off, nothing changes. While on, entering any guarded
 * app shows the PIN screen first, whoever asks for the switch and however the
 * device booted: settings (the clock's settings button, the boot touch
 * shortcut), Wi-Fi setup, every app settings screen registered with
 * app_registry, and any app that asks for it with settings_pin_guard_app().
 * The PIN stays entered until the user leaves those apps or the idle timeout
 * passes.
 *
 * The check is an app_shell enter guard (app_shell_set_enter_guard()), so a
 * product only has to call settings_pin_init() once before app_shell_start().
 * See docs/settings_pin.md.
 *
 * English contract: the PIN is stored as a salted PBKDF2-HMAC-SHA256 hash in
 * NVS ("sys_pin"), never in clear and never logged. Wrong PINs in a row are
 * counted in NVS, so the wait they cause survives a power cycle. There is no
 * way to remove a forgotten PIN from the touch screen alone, except through a
 * recovery provider the product installs.
 */

#include <stdbool.h>
#include "esp_err.h"
#include "app_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Loads the PIN state and installs the enter guard. Call once after NVS is up
 * (system_boot_start()) and before app_shell_start(). Not fatal: a PIN record
 * that cannot be read counts as locked, is reported to nvs_health, and leaves
 * only Initialize NVS reachable.
 */
esp_err_t settings_pin_init(void);

bool settings_pin_is_enabled(void);

/* The PIN screen, for apps that need to recognise it as a from_app. */
const app_shell_app_t *settings_pin_app_get_app(void);

/*
 * Puts one more app behind the PIN, or takes it out again.
 *
 * For a screen outside settings that a product wants to keep from passers-by,
 * such as system info on a monitoring unit. Whether to guard it is the app's
 * own setting: the app stores its choice and calls this at registration and
 * whenever the user changes it. Nothing here is saved. It only has an effect
 * while the PIN is on.
 *
 * `title` is shown on the PIN screen in front of it (e.g. "システム情報"); it
 * is not copied, so pass a literal. A guarded app counts as inside settings:
 * moving between it and settings does not ask again.
 *
 * English contract: shell task (or before app_shell_start()). At most
 * SETTINGS_PIN_GUARDED_APPS_MAX apps; another returns ESP_ERR_NO_MEM.
 * Guarding an app twice only updates its title; unguarding one that is not
 * guarded is not an error.
 */
#define SETTINGS_PIN_GUARDED_APPS_MAX 4
esp_err_t settings_pin_guard_app(const app_shell_app_t *app, const char *title, bool guarded);

/*
 * A second way past the PIN, supplied by the product, such as a one-time code
 * issued by a management server.
 *
 * While one is installed and available() says yes, the PIN screen shows a
 * button with `label` in its header. Tapping it switches to `app` (entered
 * with the PIN screen as from_app). That app does its own checking, then:
 *
 *  - on success calls settings_pin_recovery_succeeded(). The PIN screen comes
 *    back asking for a new PIN twice; only when that is stored does the user
 *    get in. The old PIN stays valid until then, so there is never a moment
 *    where the device is open without a PIN.
 *  - on cancel returns with app_shell_return_to(from_app).
 *
 * English contract: `provider` is not copied and must outlive the process (a
 * static const). `label` is drawn in 16px bold in a 248px button; keep it to
 * 14 full-width characters, using characters the UI fonts carry. available()
 * may be NULL (always available); it runs on the shell task whenever the PIN
 * screen is drawn and must not block. Passing NULL removes the provider.
 */
typedef struct {
    const char *label;
    bool (*available)(void *ctx);
    const app_shell_app_t *app;
} settings_pin_recovery_provider_t;

esp_err_t settings_pin_set_recovery_provider(const settings_pin_recovery_provider_t *provider, void *ctx);

/*
 * Called by the provider's app when its check passed. Switches back to the PIN
 * screen, which then asks for a new PIN. Ignored (with a warning) unless the
 * PIN screen is waiting for a PIN.
 */
void settings_pin_recovery_succeeded(void);

#ifdef __cplusplus
}
#endif

#endif
