#include "esp_check.h"
#include "cyd_clock_app.h"
#include "system_boot.h"

void app_main(void)
{
    ESP_ERROR_CHECK(system_boot_start(cyd_clock_app_get_app()));
}
