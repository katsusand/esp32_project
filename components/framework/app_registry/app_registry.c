#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app_registry.h"

#define TAG "app_registry"

/*
 * Entries are borrowed pointers, matching how app_shell borrows
 * app_shell_app_t. Registration happens during composition startup on a single
 * task, before the shell task runs, so no lock is required here.
 */
static const app_registry_entry_t *s_entries[CONFIG_APP_REGISTRY_MAX_ENTRIES];
static size_t s_entry_count;

esp_err_t app_registry_register(const app_registry_entry_t *entry)
{
    ESP_RETURN_ON_FALSE(entry != NULL, ESP_ERR_INVALID_ARG, TAG, "entry is null");
    ESP_RETURN_ON_FALSE(entry->id != NULL, ESP_ERR_INVALID_ARG, TAG, "entry id is null");
    ESP_RETURN_ON_FALSE(entry->title != NULL, ESP_ERR_INVALID_ARG, TAG, "entry title is null");
    ESP_RETURN_ON_FALSE(entry->app != NULL, ESP_ERR_INVALID_ARG, TAG, "entry app is null");
    ESP_RETURN_ON_FALSE(app_registry_find(entry->id) == NULL,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "app id already registered: %s",
                        entry->id);
    ESP_RETURN_ON_FALSE(s_entry_count < CONFIG_APP_REGISTRY_MAX_ENTRIES,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "app registry is full (%d entries)",
                        CONFIG_APP_REGISTRY_MAX_ENTRIES);

    s_entries[s_entry_count++] = entry;
    ESP_LOGI(TAG, "registered app: id=%s title=%s", entry->id, entry->title);
    return ESP_OK;
}

size_t app_registry_count(void)
{
    return s_entry_count;
}

const app_registry_entry_t *app_registry_at(size_t index)
{
    if (index >= s_entry_count) {
        return NULL;
    }
    return s_entries[index];
}

size_t app_registry_settings_count(void)
{
    size_t count = 0;

    for (size_t i = 0; i < s_entry_count; ++i) {
        if (s_entries[i] != NULL && s_entries[i]->settings_app != NULL) {
            ++count;
        }
    }
    return count;
}

const app_registry_entry_t *app_registry_settings_at(size_t index)
{
    size_t seen = 0;

    for (size_t i = 0; i < s_entry_count; ++i) {
        if (s_entries[i] == NULL || s_entries[i]->settings_app == NULL) {
            continue;
        }
        if (seen == index) {
            return s_entries[i];
        }
        ++seen;
    }
    return NULL;
}

const app_registry_entry_t *app_registry_find(const char *id)
{
    if (id == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < s_entry_count; ++i) {
        if (s_entries[i] != NULL && strcmp(s_entries[i]->id, id) == 0) {
            return s_entries[i];
        }
    }
    return NULL;
}
