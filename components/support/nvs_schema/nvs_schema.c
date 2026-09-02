#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs_schema.h"

#define TAG "nvs_schema"

/* Bounded so a corrupted partition cannot make the walk allocate without end.
   Well above the namespace count this project has ever held. */
#define NVS_SCHEMA_MAX_NAMESPACES 24

static const struct {
    const char *prefix;
    nvs_schema_scope_t scope;
} NVS_SCHEMA_PREFIXES[] = {
    { NVS_SCHEMA_PREFIX_SYSTEM, NVS_SCHEMA_SCOPE_SYSTEM },
    { NVS_SCHEMA_PREFIX_FEATURE, NVS_SCHEMA_SCOPE_FEATURE },
    { NVS_SCHEMA_PREFIX_APP, NVS_SCHEMA_SCOPE_APP },
};

nvs_schema_scope_t nvs_schema_scope_of(const char *ns)
{
    if (ns == NULL) {
        return NVS_SCHEMA_SCOPE_UNKNOWN;
    }

    for (size_t i = 0; i < (sizeof(NVS_SCHEMA_PREFIXES) / sizeof(NVS_SCHEMA_PREFIXES[0])); ++i) {
        const char *prefix = NVS_SCHEMA_PREFIXES[i].prefix;
        if (strncmp(ns, prefix, strlen(prefix)) == 0) {
            return NVS_SCHEMA_PREFIXES[i].scope;
        }
    }
    return NVS_SCHEMA_SCOPE_UNKNOWN;
}

const char *nvs_schema_scope_name(nvs_schema_scope_t scope)
{
    switch (scope) {
    case NVS_SCHEMA_SCOPE_SYSTEM:
        return "system";
    case NVS_SCHEMA_SCOPE_FEATURE:
        return "feature";
    case NVS_SCHEMA_SCOPE_APP:
        return "app";
    case NVS_SCHEMA_SCOPE_UNKNOWN:
    default:
        return "unknown";
    }
}

/*
 * Collects the distinct namespaces present in flash, with an entry count each.
 *
 * Erasing has to happen after the walk, never during it: nvs_erase_all() on a
 * namespace the iterator is still positioned in invalidates that iterator.
 */
static esp_err_t nvs_schema_collect_namespaces(nvs_schema_namespace_info_t *out,
                                               size_t max_count,
                                               size_t *out_count)
{
    nvs_iterator_t it = NULL;
    size_t count = 0;

    esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY, &it);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out_count = 0;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs entry find failed");

    while (err == ESP_OK && it != NULL) {
        nvs_entry_info_t info = { 0 };
        if (nvs_entry_info(it, &info) == ESP_OK) {
            size_t slot = count;
            for (size_t i = 0; i < count; ++i) {
                if (strncmp(out[i].name, info.namespace_name, NVS_NS_NAME_MAX_SIZE) == 0) {
                    slot = i;
                    break;
                }
            }
            if (slot == count) {
                if (count >= max_count) {
                    ESP_LOGW(TAG, "more than %u namespaces present; listing truncated", (unsigned)max_count);
                    break;
                }
                strlcpy(out[slot].name, info.namespace_name, sizeof(out[slot].name));
                out[slot].scope = nvs_schema_scope_of(out[slot].name);
                out[slot].entry_count = 0;
                ++count;
            }
            if (out[slot].entry_count < UINT16_MAX) {
                ++out[slot].entry_count;
            }
        }
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);

    *out_count = count;
    return ESP_OK;
}

esp_err_t nvs_schema_for_each_namespace(nvs_schema_namespace_cb_t cb, void *ctx)
{
    nvs_schema_namespace_info_t infos[NVS_SCHEMA_MAX_NAMESPACES] = { 0 };
    size_t count = 0;

    ESP_RETURN_ON_FALSE(cb != NULL, ESP_ERR_INVALID_ARG, TAG, "callback is null");
    ESP_RETURN_ON_ERROR(nvs_schema_collect_namespaces(infos, NVS_SCHEMA_MAX_NAMESPACES, &count),
                        TAG,
                        "collect namespaces failed");

    for (size_t i = 0; i < count; ++i) {
        if (!cb(&infos[i], ctx)) {
            break;
        }
    }
    return ESP_OK;
}

esp_err_t nvs_schema_erase_scope(nvs_schema_scope_t scope, size_t *erased_count)
{
    nvs_schema_namespace_info_t infos[NVS_SCHEMA_MAX_NAMESPACES] = { 0 };
    size_t count = 0;
    size_t erased = 0;

    /* Unprefixed namespaces include ESP-IDF's own (the Wi-Fi driver keeps
       "nvs.net80211" there). Wiping that bucket wholesale would take out
       driver state this project does not own. */
    ESP_RETURN_ON_FALSE(scope != NVS_SCHEMA_SCOPE_UNKNOWN,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "refusing to erase unclassified namespaces");

    ESP_RETURN_ON_ERROR(nvs_schema_collect_namespaces(infos, NVS_SCHEMA_MAX_NAMESPACES, &count),
                        TAG,
                        "collect namespaces failed");

    for (size_t i = 0; i < count; ++i) {
        const char *name = infos[i].name;
        nvs_handle_t handle = 0;

        if (infos[i].scope != scope) {
            continue;
        }
        if (nvs_open(name, NVS_READWRITE, &handle) != ESP_OK) {
            ESP_LOGW(TAG, "open failed while erasing namespace: %s", name);
            continue;
        }

        esp_err_t err = nvs_erase_all(handle);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "erase failed for namespace %s: %s", name, esp_err_to_name(err));
            continue;
        }

        ESP_LOGI(TAG, "erased namespace: %s (%s)", name, nvs_schema_scope_name(scope));
        ++erased;
    }

    if (erased_count != NULL) {
        *erased_count = erased;
    }
    return ESP_OK;
}
