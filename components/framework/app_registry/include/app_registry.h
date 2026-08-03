#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include <stddef.h>
#include "esp_err.h"
#include "app_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *id;    /* stable identifier, unique per device */
    const char *title; /* label shown to the user */
    const app_shell_app_t *app;
    /*
     * Optional per-app settings screen, reached from the settings app.
     *
     * English contract: a settings screen is owned by its app, not registered
     * on its own. Attaching it here means it cannot outlive or exist without
     * the app, and it never appears in the launcher as a peer of real apps.
     */
    const app_shell_app_t *settings_app;
} app_registry_entry_t;

/*
 * Registers an app so generic UI can reach it without a compile-time reference.
 *
 * Why this exists: navigation used to be direct symbol references, so adding an
 * app meant editing the apps that link to it, and only a single app could ever
 * extend the settings screen. Registration inverts that: an app announces
 * itself, and the shell-side UI enumerates whatever is present.
 *
 * English contract: `entry` is not copied. It must point at storage that
 * outlives the process, which for this codebase means a `static const`. The
 * same `id` cannot be registered twice; the second attempt returns
 * ESP_ERR_INVALID_STATE so a double-registration is a visible error rather than
 * a duplicated launcher row.
 */
esp_err_t app_registry_register(const app_registry_entry_t *entry);

/* All registered apps. This is what a launcher lists. */
size_t app_registry_count(void);
const app_registry_entry_t *app_registry_at(size_t index);
const app_registry_entry_t *app_registry_find(const char *id);

/*
 * Only the apps that carry a settings screen, densely indexed.
 *
 * English contract: callers that render a list and then map a tap back to an
 * entry must use the same indexing for both. Filtering with app_registry_at()
 * on the caller side would desynchronise those indices, so the filtered view
 * is provided here instead.
 */
size_t app_registry_settings_count(void);
const app_registry_entry_t *app_registry_settings_at(size_t index);

#ifdef __cplusplus
}
#endif

#endif
