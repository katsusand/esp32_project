#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_health.h"

#define TAG "nvs_health"
#define NVS_HEALTH_SUMMARY_MAX_LEN 96

static portMUX_TYPE s_nvs_health_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_nvs_health_requires_initialize;
static size_t s_nvs_health_issue_count;
static char s_nvs_health_summary[NVS_HEALTH_SUMMARY_MAX_LEN + 1];

void nvs_health_reset(void)
{
    portENTER_CRITICAL(&s_nvs_health_lock);
    s_nvs_health_requires_initialize = false;
    s_nvs_health_issue_count = 0;
    s_nvs_health_summary[0] = '\0';
    portEXIT_CRITICAL(&s_nvs_health_lock);
}

void nvs_health_report_invalid(const nvs_key_descriptor_t *key, esp_err_t err, const char *reason)
{
    const char *ns_name = (key != NULL && key->ns != NULL && key->ns->name != NULL) ? key->ns->name : "?";
    const char *key_name = (key != NULL && key->key != NULL) ? key->key : "?";
    const char *detail = (reason != NULL && reason[0] != '\0') ? reason : esp_err_to_name(err);
    bool first_issue = false;

    portENTER_CRITICAL(&s_nvs_health_lock);
    first_issue = !s_nvs_health_requires_initialize;
    s_nvs_health_requires_initialize = true;
    ++s_nvs_health_issue_count;
    if (first_issue) {
        snprintf(s_nvs_health_summary,
                 sizeof(s_nvs_health_summary),
                 "%s/%s: %s",
                 ns_name,
                 key_name,
                 detail);
    }
    portEXIT_CRITICAL(&s_nvs_health_lock);

    ESP_LOGW(TAG,
             "initialize required due to invalid NVS: namespace=%s key=%s err=%s detail=%s",
             ns_name,
             key_name,
             esp_err_to_name(err),
             detail);
}

bool nvs_health_requires_initialize(void)
{
    portENTER_CRITICAL(&s_nvs_health_lock);
    bool requires_initialize = s_nvs_health_requires_initialize;
    portEXIT_CRITICAL(&s_nvs_health_lock);
    return requires_initialize;
}

size_t nvs_health_get_issue_count(void)
{
    portENTER_CRITICAL(&s_nvs_health_lock);
    size_t issue_count = s_nvs_health_issue_count;
    portEXIT_CRITICAL(&s_nvs_health_lock);
    return issue_count;
}

const char *nvs_health_get_summary(void)
{
    return s_nvs_health_summary;
}
