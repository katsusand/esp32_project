#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_assert.h"
#include "esp_err.h"
#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NVS namespaces are declared by the component that owns the data, not by a
 * central table here.
 *
 * Scope is carried in the namespace name as a prefix. That choice is what makes
 * the data manageable across an app swap: scope can be recovered by scanning
 * flash, including for namespaces whose owning component is no longer in the
 * build. A registry of live components could never report those, and they are
 * exactly the ones worth finding.
 *
 * English contract: pick the prefix by what should survive replacing the
 * foreground app.
 */
#define NVS_SCHEMA_PREFIX_SYSTEM  "sys_"  /* platform / framework. survives an app swap */
#define NVS_SCHEMA_PREFIX_FEATURE "ftr_"  /* reusable services. survives an app swap */
#define NVS_SCHEMA_PREFIX_APP     "app_"  /* foreground app private. goes with the app */

typedef enum {
    NVS_SCHEMA_SCOPE_SYSTEM = 0,
    NVS_SCHEMA_SCOPE_FEATURE,
    NVS_SCHEMA_SCOPE_APP,
    /*
     * No recognised prefix. This bucket is NOT just our own leftovers: ESP-IDF
     * keeps its own data here too, notably the Wi-Fi driver's "nvs.net80211".
     * Erasing it is therefore never safe to automate -- see
     * nvs_schema_erase_scope().
     */
    NVS_SCHEMA_SCOPE_UNKNOWN,
} nvs_schema_scope_t;

typedef struct {
    const char *ns;
    const char *key;
} nvs_key_descriptor_t;

/*
 * NVS truncates silently past NVS_NS_NAME_MAX_SIZE - 1 characters, which turns
 * a too-long namespace into a different namespace at runtime. Declare every
 * namespace literal through this so the build fails instead.
 */
#define NVS_SCHEMA_DECLARE_NS(symbol, ns_literal)                          \
    ESP_STATIC_ASSERT(sizeof(ns_literal) <= NVS_NS_NAME_MAX_SIZE,          \
                      "NVS namespace name is too long: " ns_literal);      \
    static const char symbol[] = ns_literal

static inline esp_err_t nvs_open_descriptor(const char *ns,
                                            nvs_open_mode_t open_mode,
                                            nvs_handle_t *out_handle)
{
    return nvs_open(ns, open_mode, out_handle);
}

nvs_schema_scope_t nvs_schema_scope_of(const char *ns);
const char *nvs_schema_scope_name(nvs_schema_scope_t scope);

typedef struct {
    char name[NVS_NS_NAME_MAX_SIZE];
    nvs_schema_scope_t scope;
    uint16_t entry_count;
} nvs_schema_namespace_info_t;

/*
 * Enumerates the namespaces actually present in flash.
 *
 * This is how orphaned data is found: a namespace the running firmware never
 * opens still shows up here, because the walk reads flash rather than asking
 * components what they own. `cb` returning false stops the walk.
 */
typedef bool (*nvs_schema_namespace_cb_t)(const nvs_schema_namespace_info_t *info, void *ctx);
esp_err_t nvs_schema_for_each_namespace(nvs_schema_namespace_cb_t cb, void *ctx);

/*
 * Erases every namespace in `scope`. Intended for swapping the foreground app:
 * NVS_SCHEMA_SCOPE_APP clears app-private data while system and feature
 * settings survive. `erased_count` may be NULL.
 *
 * English contract: NVS_SCHEMA_SCOPE_UNKNOWN is rejected with
 * ESP_ERR_INVALID_ARG. That bucket holds ESP-IDF's own namespaces, such as the
 * Wi-Fi driver's "nvs.net80211", alongside any stale data of ours. Sorting the
 * two apart is a human judgement, so this function refuses to guess.
 */
esp_err_t nvs_schema_erase_scope(nvs_schema_scope_t scope, size_t *erased_count);

#ifdef __cplusplus
}
#endif

