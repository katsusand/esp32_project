#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "boot_id.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_boot_id[BOOT_ID_STRING_LEN + 1];
static bool s_ready;

const char *boot_id_get(void)
{
    portENTER_CRITICAL(&s_lock);
    bool ready = s_ready;
    portEXIT_CRITICAL(&s_lock);
    if (ready) {
        return s_boot_id;
    }

    uint8_t random[16];
    uint8_t mac[6] = { 0 };
    uint8_t mixed[16];
    char text[BOOT_ID_STRING_LEN + 1];

    esp_fill_random(random, sizeof(random));
    /* Without a MAC the zeroes only make the mix weaker, not wrong. */
    (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
    boot_id_mix(mixed, random, mac, (uint64_t)esp_timer_get_time());
    boot_id_format_uuid_v4(mixed, text);

    /* Two tasks may get here together; the first to finish decides, so every
       caller sees the same id. */
    portENTER_CRITICAL(&s_lock);
    if (!s_ready) {
        memcpy(s_boot_id, text, sizeof(s_boot_id));
        s_ready = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return s_boot_id;
}
