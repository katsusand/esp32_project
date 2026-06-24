#include "esp_check.h"
#include "cyd_clock_composition.h"

void app_main(void)
{
    ESP_ERROR_CHECK(cyd_clock_composition_start());
}
